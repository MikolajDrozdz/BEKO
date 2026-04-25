import platform
import socket

from fastapi import APIRouter, Depends, HTTPException, Query
import time
from sqlalchemy.orm import Session
from ...models.database import get_db
from ...services import system_metrics
from ...services.lora_hardware import lora_device
from ...services.laviet_frame import (
    LavietFrameBuilder, LavietFrame, LavietType, LAVIET_FRAME_VERSION,
    LAVIET_GATEWAY_ID, LAVIET_FLAG_COUNTER_OVERRIDE
)
from ...core.laviet_crypto import derive_unicast_base_key, get_aes_key, get_hmac_key, laviet_aes_ctr_crypt, laviet_generate_mac
from ...models import models

router = APIRouter()
GATEWAY_API_VERSION = "1.0.0"


def _gateway_info(radio_status: dict) -> dict:
    return {
        "online": radio_status["ready"],
        "gateway_id": LAVIET_GATEWAY_ID,
        "gateway_id_hex": hex(LAVIET_GATEWAY_ID),
        "hostname": socket.gethostname(),
        "platform": platform.platform(),
        "version": GATEWAY_API_VERSION,
        "frequency_hz": radio_status["frequency_hz"],
        "spreading_factor": radio_status["spreading_factor"],
        "tx_power": radio_status["tx_power"],
    }

@router.get("/", summary="Informacje o stanie Gatewaya (LAVIET)")
def get_system_status():
    """Zwraca podstawowe informacje o bramce LoRa."""
    radio_status = lora_device.get_status()
    gateway_info = _gateway_info(radio_status)
    return {
        "gateway_id": LAVIET_GATEWAY_ID,
        "gateway_id_hex": hex(LAVIET_GATEWAY_ID),
        "hostname": gateway_info["hostname"],
        "platform": gateway_info["platform"],
        "version": gateway_info["version"],
        "frequency_hz": gateway_info["frequency_hz"],
        "spreading_factor": gateway_info["spreading_factor"],
        "tx_power": gateway_info["tx_power"],
        "protocol_version": LAVIET_FRAME_VERSION,
        "status": "online" if lora_device.is_ready() else "radio_not_ready",
        "radio_ready": radio_status["ready"],
        "radio_driver": radio_status["driver"],
        "radio_error": radio_status["last_error"],
        "unicast_key_mode": "pair32",
        "gateway": gateway_info,
        "radio": radio_status,
    }


@router.get("/gateway", summary="Gateway info dla dashboardu")
def get_gateway_info():
    return _gateway_info(lora_device.get_status())


@router.get("/metrics", summary="Aktualne metryki systemu")
def get_current_metrics():
    return system_metrics.collect_metrics()


@router.get("/metrics/history", summary="Historia metryk systemu")
def get_metrics_history(
    range_: str = Query("1h", alias="range"),
    step: str = Query("10s"),
):
    range_seconds = system_metrics.parse_duration(range_, 3600)
    step_seconds = system_metrics.parse_duration(step, 10)
    return system_metrics.get_history(range_seconds, step_seconds)


def _send_system_frame(node_id: int, type_id: int, flags: int, db: Session):
    node = db.query(models.Node).filter(models.Node.node_id == node_id).first()
    if not node or not node.paired_code:
        raise HTTPException(status_code=400, detail="Węzeł nie jest sparowany.")

    code = node.paired_code
    if isinstance(code, str):
        code = code.encode('ascii')

    base_key = derive_unicast_base_key(LAVIET_GATEWAY_ID, node_id, code)
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

    if not lora_device.send_frame(final_frame):
        raise HTTPException(status_code=503, detail=f"Radio TX failed: {lora_device.get_last_error()}")

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
