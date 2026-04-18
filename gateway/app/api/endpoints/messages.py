from typing import List

from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from ...core.laviet_crypto import (
    LAVIET_SHARED_V1,
    derive_unicast_base_key,
    get_aes_key,
    get_hmac_key,
    laviet_aes_ctr_crypt,
    laviet_generate_mac,
)
from ...models import models
from ...models.database import get_db
from ...schemas import schemas
from ...services import message_tracker
from ...services.laviet_frame import (
    LAVIET_BROADCAST_ID,
    LAVIET_FLAG_ACK_REQUIRED,
    LAVIET_FLAG_BROADCAST,
    LAVIET_FLAG_ENCRYPTED,
    LAVIET_GATEWAY_ID,
    LavietFrame,
    LavietFrameBuilder,
    LavietType,
)
from ...services.lora_hardware import lora_device
from ...services.pairing import pairing_manager

router = APIRouter()


def _debug_hex(label: str, data: bytes | None) -> None:
    if data is None:
        print(f"{label}<none>")
        return
    print(f"{label}{data.hex()}")


@router.post("/send", response_model=schemas.MessageResponse)
def send_message(msg: schemas.MessageCreate, db: Session = Depends(get_db)):
    try:
        payload_bytes = bytes.fromhex(msg.payload_hex)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail="Nieprawidlowy payload_hex") from exc

    if not payload_bytes:
        raise HTTPException(status_code=400, detail="Pusty payload")
    if len(payload_bytes) > 16:
        raise HTTPException(status_code=400, detail="Payload przekracza 16 B")
    if msg.dst_id == 0:
        raise HTTPException(status_code=400, detail="Nieprawidlowy dst_id=0")

    dst_id_16 = msg.dst_id & 0xFFFF
    if msg.dst_id in (0xFFFFFFFF, 4294967295):
        dst_id_16 = LAVIET_BROADCAST_ID

    node = None
    paired_code = None
    paired_code_source = "none"
    if dst_id_16 != LAVIET_BROADCAST_ID:
        node = db.query(models.Node).filter(models.Node.node_id == dst_id_16).first()
        if node is None:
            raise HTTPException(status_code=404, detail="Node not found or not paired")

        runtime_code = pairing_manager.get_paired_code(dst_id_16)
        if runtime_code:
            paired_code = runtime_code
            paired_code_source = "runtime"
        else:
            paired_code = node.paired_code
            paired_code_source = "db"
        if not paired_code:
            raise HTTPException(status_code=409, detail="Node is not paired yet")
        if node.paired_code != paired_code:
            node.paired_code = bytes(paired_code)
            node.is_paired = True
            db.commit()
            db.refresh(node)

    db_msg = models.Message(
        dst_id=dst_id_16,
        src_id=LAVIET_GATEWAY_ID,
        payload_hex=msg.payload_hex,
        status="pending",
    )
    db.add(db_msg)
    db.commit()
    db.refresh(db_msg)

    if dst_id_16 == LAVIET_BROADCAST_ID:
        domain_id = LAVIET_BROADCAST_ID
        aes_key = get_aes_key(LAVIET_SHARED_V1, domain_id)
        hmac_key = get_hmac_key(LAVIET_SHARED_V1, domain_id)
        counter = 0
    else:
        if not isinstance(paired_code, bytes):
            paired_code = bytes(paired_code)
        base_key = derive_unicast_base_key(LAVIET_GATEWAY_ID, dst_id_16, paired_code)
        domain_id = min(LAVIET_GATEWAY_ID, dst_id_16)
        aes_key = get_aes_key(base_key, domain_id)
        hmac_key = get_hmac_key(base_key, domain_id)
        _debug_hex(f"[TX HMAC DBG] paired_code[{paired_code_source}]=", paired_code)
        _debug_hex("[TX HMAC DBG] pair_base_key=", base_key)
        print(f"[TX HMAC DBG] domain_id=0x{domain_id:04X}")
        _debug_hex("[TX HMAC DBG] aes_key=", aes_key)
        _debug_hex("[TX HMAC DBG] hmac_key=", hmac_key)
        node.counter += 1
        db.commit()
        db.refresh(node)
        counter = node.counter

    msg_id = db_msg.id & 0xFFFF
    flags = 0
    if dst_id_16 == LAVIET_BROADCAST_ID:
        flags |= LAVIET_FLAG_BROADCAST
        cipher_payload = payload_bytes
    else:
        flags |= LAVIET_FLAG_ACK_REQUIRED
        if msg.coded:
            flags |= LAVIET_FLAG_ENCRYPTED
            cipher_payload = laviet_aes_ctr_crypt(
                payload_bytes,
                aes_key,
                LAVIET_GATEWAY_ID,
                dst_id_16,
                msg_id,
                counter,
            )
        else:
            cipher_payload = payload_bytes

    net_frame = LavietFrame(
        type=LavietType.DATA,
        flags=flags,
        src_id=LAVIET_GATEWAY_ID,
        dst_id=dst_id_16,
        msg_id=msg_id,
        counter=counter,
        payload_len=len(cipher_payload),
        payload=cipher_payload,
    )

    raw_frame_no_mac = LavietFrameBuilder.build_frame(net_frame)
    net_frame.mac_tag = laviet_generate_mac(hmac_key, raw_frame_no_mac, b"")
    _debug_hex("[TX HMAC DBG] mac_input=", raw_frame_no_mac)
    _debug_hex("[TX HMAC DBG] mac_tag=", net_frame.mac_tag)
    final_frame = LavietFrameBuilder.build_frame(net_frame)

    print(
        f"[TX] LAVIET src={hex(LAVIET_GATEWAY_ID)} dst={hex(dst_id_16)} "
        f"msg_id=0x{msg_id:04X} counter={counter} coded={msg.coded} "
        f"key_mode={'pair32' if dst_id_16 != LAVIET_BROADCAST_ID else 'shared-broadcast'} "
        f"payload={payload_bytes.hex()}"
    )
    tx_ok = bool(lora_device.send_frame(final_frame))
    if not tx_ok:
        db_msg.status = "failed"
        db.commit()
        db.refresh(db_msg)
        raise HTTPException(status_code=503, detail="Radio TX failed")

    db_msg.status = "sent"
    db.commit()
    db.refresh(db_msg)

    if dst_id_16 != LAVIET_BROADCAST_ID and (flags & LAVIET_FLAG_ACK_REQUIRED):
        message_tracker.register_pending_ack(db_msg.id, dst_id_16, msg_id, counter)

    return db_msg


@router.get("/history", response_model=List[schemas.MessageResponse])
def get_history(db: Session = Depends(get_db)):
    msgs = db.query(models.Message).order_by(models.Message.created_at.desc()).all()
    return msgs
