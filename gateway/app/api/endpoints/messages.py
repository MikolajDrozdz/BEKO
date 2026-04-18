from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session
from typing import List
from ...models.database import get_db
from ...models import models
from ...schemas import schemas
from ...services.lora_hardware import lora_device
from ...services.laviet_frame import (
    LavietFrameBuilder, LavietFrame, LavietType,
    LAVIET_GATEWAY_ID, LAVIET_BROADCAST_ID, LAVIET_FLAG_ACK_REQUIRED,
    LAVIET_FLAG_BROADCAST
)
from ...core.laviet_crypto import LAVIET_SHARED_V1, get_aes_key, get_hmac_key, laviet_aes_ctr_crypt, laviet_generate_mac
from ...services.laviet_frame import LAVIET_FLAG_ENCRYPTED

router = APIRouter()

@router.post("/send", response_model=schemas.MessageResponse)
def send_message(msg: schemas.MessageCreate, db: Session = Depends(get_db)):
    """
    Wysyła wiadomość LoRa do węzła STM32.
    - `dst_id`: node_id docelowe (2 bajty, większe ID będą maskowane do 16 bitów) lub 65535 (0xFFFF) dla broadcast
    - `payload_hex`: treść 
    """
    try:
        payload_bytes = bytes.fromhex(msg.payload_hex)
    except ValueError:
        raise HTTPException(status_code=400, detail="Nieprawidłowy payload_hex")

    if not payload_bytes:
        raise HTTPException(status_code=400, detail="Pusty payload")

    if msg.dst_id == 0:
        raise HTTPException(status_code=400, detail="Nieprawidłowy dst_id=0")

    db_msg = models.Message(
        dst_id=msg.dst_id & 0xFFFF,
        src_id=LAVIET_GATEWAY_ID,
        payload_hex=msg.payload_hex,
        status="sent"
    )
    db.add(db_msg)
    db.commit()
    db.refresh(db_msg)

    # Według README.md, normalny ruch unicast (DATA) korzysta z wyliczonego klucza,
    # ale tu dla uproszczenia (aby zachowac zgodnosc zanim zaimplementujemy baze kodow)
    # używamy klucza z bazy lub domyślnego.
    node = db.query(models.Node).filter(models.Node.node_id == msg.dst_id).first()
    
    # Wybor klucza (zgodnie z "7.2 Ruch po sparowaniu")
    dst_id_16 = msg.dst_id & 0xFFFF
    
    # Obsluga broadcast ze starego frontendu (-1 lub 0xFFFFFFFF)
    if msg.dst_id == 0xFFFFFFFF or msg.dst_id == 4294967295:
        dst_id_16 = LAVIET_BROADCAST_ID

    if dst_id_16 == LAVIET_BROADCAST_ID or not node or not node.paired_code:
        # Fallback do LAVIET_SHARED_V1 dla broadcast/brak parowania
        domain_id = 0xFFFF if dst_id_16 == LAVIET_BROADCAST_ID else min(LAVIET_GATEWAY_ID, dst_id_16)
        aes_key = get_aes_key(LAVIET_SHARED_V1, domain_id)
        hmac_key = get_hmac_key(LAVIET_SHARED_V1, domain_id)
    else:
        # Wykorzystaj kod parowania wezla (unikalny)
        from ...core.laviet_crypto import security_peer_link_key_derive
        code = node.paired_code
        if not isinstance(code, bytes):
            code = bytes(code)
        base_key = security_peer_link_key_derive(LAVIET_GATEWAY_ID, dst_id_16, code)
        domain_id = min(LAVIET_GATEWAY_ID, dst_id_16)
        aes_key = get_aes_key(base_key, domain_id)
        hmac_key = get_hmac_key(base_key, domain_id)

    # Zwiększamy counter od razu, aby pierwsza wiadomość miała counter=1 (STM32 omija counter=0)
    if node:
        node.counter += 1
        db.commit()
        db.refresh(node)
        
    msg_id = db_msg.id & 0xFFFF
    counter = (node.counter if node else 0)

    # Flagi - dla unicastu do węzła zawsze wymagamy ACK i szyfrowania, dla broadcastu tylko flaga BROADCAST
    flags = 0
    if dst_id_16 == LAVIET_BROADCAST_ID:
        flags |= LAVIET_FLAG_BROADCAST
        cipher_payload = payload_bytes # Broadcast nie jest szyfrowany wg spec 7.1
    else:
        flags |= (LAVIET_FLAG_ACK_REQUIRED | LAVIET_FLAG_ENCRYPTED)
        cipher_payload = laviet_aes_ctr_crypt(payload_bytes, aes_key, LAVIET_GATEWAY_ID, dst_id_16, msg_id, counter)

    net_frame = LavietFrame(
        type=LavietType.DATA,
        flags=flags,
        src_id=LAVIET_GATEWAY_ID,
        dst_id=dst_id_16,
        msg_id=msg_id,
        counter=counter,
        payload_len=len(cipher_payload),
        payload=cipher_payload
    )
    
    raw_frame_no_mac = LavietFrameBuilder.build_frame(net_frame)
    net_frame.mac_tag = laviet_generate_mac(hmac_key, raw_frame_no_mac, b"")
    
    final_frame = LavietFrameBuilder.build_frame(net_frame)
    
    # Counter został już zwiększony i zapisany wcześniej

    print(f"[TX] LAVIET: src={hex(LAVIET_GATEWAY_ID)}, dst={hex(dst_id_16)}, msg_id={msg_id}, payload={payload_bytes.hex()}")
    lora_device.send_frame(final_frame)

    return db_msg

@router.get("/history", response_model=List[schemas.MessageResponse])
def get_history(db: Session = Depends(get_db)):
    msgs = db.query(models.Message).order_by(models.Message.created_at.desc()).all()
    return msgs
