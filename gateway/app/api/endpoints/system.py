from fastapi import APIRouter, Depends, HTTPException
import time
from sqlalchemy.orm import Session
from ...models.database import get_db
from ...services.lora_hardware import lora_device
from ...services.laviet_frame import (
    LavietFrameBuilder, LavietFrame, LavietType, LAVIET_FRAME_VERSION,
    LAVIET_GATEWAY_ID, LAVIET_FLAG_COUNTER_OVERRIDE
)
from ...core.laviet_crypto import security_peer_link_key_derive, get_aes_key, get_hmac_key, laviet_aes_ctr_crypt, laviet_generate_mac
from ...models import models

router = APIRouter()

@router.get("/", summary="Informacje o stanie Gatewaya (LAVIET)")
def get_system_status():
    """Zwraca podstawowe informacje o bramce LoRa."""
    return {
        "gateway_id": LAVIET_GATEWAY_ID,
        "gateway_id_hex": hex(LAVIET_GATEWAY_ID),
        "protocol_version": LAVIET_FRAME_VERSION,
        "status": "online"
    }



def _send_system_frame(node_id: int, type_id: int, flags: int, db: Session):
    node = db.query(models.Node).filter(models.Node.node_id == node_id).first()
    if not node or not node.paired_code:
        raise HTTPException(status_code=400, detail="Węzeł nie jest sparowany.")

    code = node.paired_code
    if isinstance(code, str):
        code = code.encode('ascii')

    base_key = security_peer_link_key_derive(LAVIET_GATEWAY_ID, node_id, code)
    domain_id = min(LAVIET_GATEWAY_ID, node_id)
    aes_key = get_aes_key(base_key, domain_id)
    hmac_key = get_hmac_key(base_key, domain_id)

    msg_id = int(time.time() % 65535)
    counter = node.counter
    
    # 0 bajtów payloadu dla komend prostych
    payload = b""
    cipher_payload = laviet_aes_ctr_crypt(payload, aes_key, LAVIET_GATEWAY_ID, node_id, msg_id, counter)

    net_frame = LavietFrame(
        type=type_id,
        flags=flags,
        src_id=LAVIET_GATEWAY_ID,
        dst_id=node_id,
        msg_id=msg_id,
        counter=counter,
        payload_len=len(cipher_payload),
        payload=cipher_payload
    )
    
    raw_no_mac = LavietFrameBuilder.build_frame(net_frame)
    net_frame.mac_tag = laviet_generate_mac(hmac_key, raw_no_mac, b"")
    final_frame = LavietFrameBuilder.build_frame(net_frame)
    
    node.counter += 1
    db.commit()

    lora_device.send_frame(final_frame)

@router.post("/nodes/{node_id}/sync_counter", summary="Synchronizacja licznika węzła (ADMIN)")
def sync_counter(node_id: int, db: Session = Depends(get_db)):
    """Wysyła ramkę synchronizacji licznika (COUNTER_SYNC)."""
    _send_system_frame(node_id, LavietType.COUNTER_SYNC, LAVIET_FLAG_COUNTER_OVERRIDE, db)
    return {"status": f"sync requested for node {node_id}"}

@router.post("/nodes/{node_id}/rotate_keys", summary="Rotacja kluczy węzła (ADMIN)")
def rotate_keys(node_id: int, db: Session = Depends(get_db)):
    """Wysyła ramkę rotacji kluczy (KEY_ROTATE)."""
    _send_system_frame(node_id, LavietType.KEY_ROTATE, 0, db)
    return {"status": f"key rotation requested for node {node_id}"}
