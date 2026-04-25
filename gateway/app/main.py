import asyncio
import struct
import time
from datetime import datetime

from fastapi import FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware

from .api.endpoints import logs, messages, nodes, pairing, radio, system
from .core.laviet_crypto import (
    LAVIET_SHARED_V1,
    derive_unicast_base_key,
    get_aes_key,
    get_hmac_key,
    laviet_aes_ctr_crypt,
    laviet_mac_equal,
    laviet_generate_mac,
)
from .models import models
from .models.database import Base, SessionLocal, engine
from .services import message_tracker
from .services import system_metrics
from .services.laviet_frame import (
    LAVIET_BROADCAST_ID,
    LAVIET_FLAG_ACK_REQUIRED,
    LAVIET_FLAG_BROADCAST,
    LAVIET_FLAG_ENCRYPTED,
    LAVIET_FLAG_IS_ACK,
    LAVIET_GATEWAY_ID,
    LavietFrame,
    LavietFrameBuilder,
    LavietType,
)
from .services.lora_hardware import lora_device
from .services.pairing import pairing_manager

Base.metadata.create_all(bind=engine)

app = FastAPI(title="LAVIET Gateway API", version="1.0.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.middleware("http")
async def log_request_body(request: Request, call_next):
    body_bytes = await request.body()
    try:
        body_str = body_bytes.decode("utf-8")
        if body_str:
            print(f"[API REQUEST] {request.method} {request.url.path} - Body: {body_str}")
    except Exception:
        print(
            f"[API REQUEST] {request.method} {request.url.path} - Body: <binary data, len={len(body_bytes)}>"
        )

    async def receive():
        return {"type": "http.request", "body": body_bytes}

    request._receive = receive
    response = await call_next(request)
    return response


app.include_router(messages.router, prefix="/api/messages", tags=["messages"])
app.include_router(nodes.router, prefix="/api/nodes", tags=["nodes"])
app.include_router(system.router, prefix="/api/system", tags=["system"])
app.include_router(radio.router, prefix="/api/radio", tags=["radio"])
app.include_router(logs.router, prefix="/api/logs", tags=["logs"])
app.include_router(pairing.router, prefix="/api/pairing", tags=["pairing"])

pairing_manager.set_send_callback(lora_device.send_frame)

VALID_USER_RESPONSES = {"YES", "OK", "NO"}
ACK_TX_DELAY_SECONDS = 0.100


def _debug_hex(label: str, data: bytes | None) -> None:
    if data is None:
        print(f"{label}<none>")
        return
    print(f"{label}{data.hex()}")


def _derive_node_keys(node_id: int, paired_code: bytes):
    if not isinstance(paired_code, bytes):
        paired_code = bytes(paired_code)
    base_key = derive_unicast_base_key(LAVIET_GATEWAY_ID, node_id, paired_code)
    domain_id = min(LAVIET_GATEWAY_ID, node_id)
    aes_key = get_aes_key(base_key, domain_id)
    hmac_key = get_hmac_key(base_key, domain_id)
    return aes_key, hmac_key


def _resolve_paired_code(node_id: int, node: models.Node | None) -> bytes | None:
    runtime_code = pairing_manager.get_paired_code(node_id)
    if runtime_code:
        return bytes(runtime_code)
    if node is not None and node.paired_code:
        return bytes(node.paired_code)
    return None


def _sync_node_paired_code(db, node_id: int, node: models.Node | None, paired_code: bytes | None):
    if not paired_code:
        return node

    if node is None:
        node = models.Node(
            node_id=node_id,
            is_paired=True,
            counter=0,
            paired_code=bytes(paired_code),
            network_mode=False,
            network_ttl=3,
        )
        db.add(node)
        db.commit()
        db.refresh(node)
        return node

    if node.paired_code != paired_code or not node.is_paired:
        node.paired_code = bytes(paired_code)
        node.is_paired = True
        db.commit()
        db.refresh(node)

    return node


def _derive_inbound_hmac_key(frame: LavietFrame, paired_code: bytes | None):
    if frame.type in (LavietType.PAIR_REQ, LavietType.PAIR_RESP, LavietType.ERROR):
        domain_id = (
            LAVIET_BROADCAST_ID
            if (frame.flags & LAVIET_FLAG_BROADCAST) or frame.dst_id == LAVIET_BROADCAST_ID
            else min(LAVIET_GATEWAY_ID, frame.src_id)
        )
        return get_hmac_key(LAVIET_SHARED_V1, domain_id)

    if (frame.flags & LAVIET_FLAG_BROADCAST) or frame.dst_id == LAVIET_BROADCAST_ID:
        return get_hmac_key(LAVIET_SHARED_V1, LAVIET_BROADCAST_ID)

    if not paired_code:
        return None

    _, hmac_key = _derive_node_keys(frame.src_id, paired_code)
    return hmac_key


def _verify_inbound_mac(frame: LavietFrame, paired_code: bytes | None, raw_header_payload: bytes) -> bool:
    hmac_key = _derive_inbound_hmac_key(frame, paired_code)
    if hmac_key is None:
        print(
            f"[LoRa HMAC] Missing key for src={hex(frame.src_id)} dst={hex(frame.dst_id)} "
            f"type={frame.type}"
        )
        return False

    if paired_code:
        base_key = derive_unicast_base_key(LAVIET_GATEWAY_ID, frame.src_id, paired_code)
        domain_id = min(LAVIET_GATEWAY_ID, frame.src_id)
        _debug_hex("[LoRa HMAC DBG] paired_code=", paired_code)
        _debug_hex("[LoRa HMAC DBG] pair_base_key=", base_key)
        print(f"[LoRa HMAC DBG] domain_id=0x{domain_id:04X}")
        _debug_hex("[LoRa HMAC DBG] hmac_key=", hmac_key)
    else:
        print("[LoRa HMAC DBG] using shared key path")

    _debug_hex("[LoRa HMAC DBG] mac_input=", raw_header_payload)
    expected_mac = laviet_generate_mac(hmac_key, raw_header_payload, b"")
    _debug_hex("[LoRa HMAC DBG] expected_mac=", expected_mac)
    _debug_hex("[LoRa HMAC DBG] received_mac=", frame.mac_tag or b"")
    if not laviet_mac_equal(expected_mac, frame.mac_tag or b""):
        print(
            f"[LoRa HMAC] Drop src={hex(frame.src_id)} dst={hex(frame.dst_id)} "
            f"msg=0x{frame.msg_id:04X}: invalid MAC"
        )
        return False

    return True


def _decode_inbound_payload(frame: LavietFrame, paired_code: bytes | None) -> bytes:
    payload = frame.payload
    if (frame.flags & LAVIET_FLAG_ENCRYPTED) == 0:
        return payload

    if paired_code:
        aes_key, _ = _derive_node_keys(frame.src_id, paired_code)
    else:
        domain_id = (
            LAVIET_BROADCAST_ID
            if frame.dst_id == LAVIET_BROADCAST_ID
            else min(LAVIET_GATEWAY_ID, frame.src_id)
        )
        aes_key = get_aes_key(LAVIET_SHARED_V1, domain_id)

    return laviet_aes_ctr_crypt(
        payload,
        aes_key,
        frame.src_id,
        frame.dst_id,
        frame.msg_id,
        frame.counter,
    )


def _send_ack_for_frame(db, frame: LavietFrame, node: models.Node | None, paired_code: bytes | None) -> None:
    if frame.dst_id != LAVIET_GATEWAY_ID:
        return
    if frame.dst_id == LAVIET_BROADCAST_ID or (frame.flags & LAVIET_FLAG_BROADCAST):
        return
    if (frame.flags & LAVIET_FLAG_ACK_REQUIRED) == 0:
        return
    if frame.type == LavietType.ACK:
        return
    if node is None or not paired_code:
        print(f"[LoRa ACK] Skip ACK for {hex(frame.src_id)}: node is not paired")
        return

    node.counter += 1
    db.commit()
    db.refresh(node)

    ack_payload = struct.pack(">HI", frame.msg_id & 0xFFFF, frame.counter & 0xFFFFFFFF)
    _, hmac_key = _derive_node_keys(frame.src_id, paired_code)
    ack_frame = LavietFrame(
        type=LavietType.ACK,
        flags=LAVIET_FLAG_IS_ACK,
        src_id=LAVIET_GATEWAY_ID,
        dst_id=frame.src_id,
        msg_id=int(time.time() * 1000) & 0xFFFF,
        counter=node.counter,
        payload_len=len(ack_payload),
        payload=ack_payload,
    )

    raw_frame = LavietFrameBuilder.build_frame(ack_frame)
    ack_frame.mac_tag = laviet_generate_mac(hmac_key, raw_frame, b"")
    final_frame = LavietFrameBuilder.build_frame(ack_frame)

    time.sleep(ACK_TX_DELAY_SECONDS)
    if lora_device.send_frame(final_frame):
        print(
            f"[LoRa ACK] Sent ACK to {hex(frame.src_id)} for msg=0x{frame.msg_id:04X} counter={frame.counter}"
        )
    else:
        print(
            f"[LoRa ACK] Failed to send ACK to {hex(frame.src_id)} for msg=0x{frame.msg_id:04X}"
        )


def _store_inbound_data(db, frame: LavietFrame, plain_payload: bytes, status: str = "received") -> models.Message:
    node = db.query(models.Node).filter(models.Node.node_id == frame.src_id).first()
    if node:
        node.last_seen = datetime.utcnow()

    db_msg = models.Message(
        dst_id=frame.dst_id,
        src_id=frame.src_id,
        payload_hex=plain_payload.hex(),
        status=status,
    )
    db.add(db_msg)
    db.commit()

    try:
        text = plain_payload.decode("utf-8")
    except UnicodeDecodeError:
        text = None

    log_label = "RESP" if status == "response" else "DATA"
    if text is not None:
        print(
            f"[LoRa {log_label}] src={hex(frame.src_id)} dst={hex(frame.dst_id)} "
            f"msg=0x{frame.msg_id:04X} counter={frame.counter} text={text!r}"
        )
    else:
        print(
            f"[LoRa {log_label}] src={hex(frame.src_id)} dst={hex(frame.dst_id)} "
            f"msg=0x{frame.msg_id:04X} counter={frame.counter} payload_hex={plain_payload.hex()}"
        )
    return db_msg


def _decode_user_response(payload: bytes) -> str | None:
    try:
        response = payload.decode("utf-8").strip().upper()
    except UnicodeDecodeError:
        return None

    if response in VALID_USER_RESPONSES:
        return response
    return None


def _handle_response_frame(db, frame: LavietFrame, plain_payload: bytes) -> None:
    db_msg = _store_inbound_data(db, frame, plain_payload, status="response")
    response = _decode_user_response(plain_payload)
    if response is None:
        print(
            f"[LoRa RESP] Stored unexpected response from {hex(frame.src_id)} "
            f"as message_id={db_msg.id}: payload_hex={plain_payload.hex()}"
        )
        return

    pending_message_id = message_tracker.pop_pending_response(frame.src_id)
    if pending_message_id is None:
        print(
            f"[LoRa RESP] Received {response} from {hex(frame.src_id)} "
            f"without pending question"
        )
        return

    pending_msg = db.query(models.Message).filter(models.Message.id == pending_message_id).first()
    if pending_msg is None:
        print(
            f"[LoRa RESP] Response {response} from {hex(frame.src_id)} matched missing "
            f"message_id={pending_message_id}"
        )
        return

    pending_msg.status = "answered"
    db.commit()
    print(
        f"[LoRa RESP] {response} from {hex(frame.src_id)} answered message_id={pending_message_id}"
    )


def _handle_ack_frame(db, frame: LavietFrame) -> None:
    if len(frame.payload) != 6:
        print(
            f"[LoRa ACK] Ignoring malformed ACK from {hex(frame.src_id)}: payload_len={len(frame.payload)}"
        )
        return

    acked_msg_id, acked_counter = struct.unpack(">HI", frame.payload)
    message_id = message_tracker.pop_pending_ack(frame.src_id, acked_msg_id, acked_counter)
    if message_id is None:
        print(
            f"[LoRa ACK] Unexpected ACK from {hex(frame.src_id)} for "
            f"msg=0x{acked_msg_id:04X} counter={acked_counter}"
        )
        return

    db_msg = db.query(models.Message).filter(models.Message.id == message_id).first()
    if db_msg is None:
        print(
            f"[LoRa ACK] ACK matched runtime tracker but message {message_id} is missing from DB"
        )
        return

    if db_msg.status == "answered":
        pass
    elif db_msg.status == "sent_waiting_response":
        db_msg.status = "delivered_waiting_response"
    else:
        db_msg.status = "delivered"
    db.commit()
    print(
        f"[LoRa ACK] Delivered message_id={message_id} from node={hex(frame.src_id)} "
        f"acked_msg=0x{acked_msg_id:04X} acked_counter={acked_counter}"
    )


async def lora_listener_task():
    print("[BACKGROUND TASK] Started async LoRa listener")

    def on_receive(frame_bytes: bytes, rssi_dbm: int = 0):
        print(f"[LoRa DEBUG] RX len={len(frame_bytes)} RSSI={rssi_dbm}")
        db = SessionLocal()
        try:
            parsed = LavietFrameBuilder.parse_frame(frame_bytes)
            radio_status = lora_device.get_status()
            message_tracker.update_node_radio(
                parsed.src_id,
                rssi_dbm,
                radio_status.get("last_snr"),
            )
            raw_header_payload = frame_bytes[: 13 + parsed.payload_len]
            print(
                f"[LoRa] Parsed frame type={parsed.type} src={hex(parsed.src_id)} "
                f"dst={hex(parsed.dst_id)} msg=0x{parsed.msg_id:04X} counter={parsed.counter}"
            )

            if parsed.type == LavietType.PAIR_RESP:
                pairing_manager.on_pair_resp(parsed, raw_header_payload)
                return

            node = db.query(models.Node).filter(models.Node.node_id == parsed.src_id).first()
            paired_code = _resolve_paired_code(parsed.src_id, node)
            node = _sync_node_paired_code(db, parsed.src_id, node, paired_code)
            if node:
                node.last_seen = datetime.utcnow()

            if not _verify_inbound_mac(parsed, paired_code, raw_header_payload):
                db.rollback()
                return

            if parsed.type == LavietType.DATA:
                plain_payload = _decode_inbound_payload(parsed, paired_code)
                _store_inbound_data(db, parsed, plain_payload)
            elif parsed.type == LavietType.RESP:
                plain_payload = _decode_inbound_payload(parsed, paired_code)
                _handle_response_frame(db, parsed, plain_payload)
            elif parsed.type == LavietType.ACK:
                _handle_ack_frame(db, parsed)
            else:
                print(f"[LoRa] Ignoring unsupported frame type={parsed.type}")
                db.commit()

            if parsed.type != LavietType.ACK:
                _send_ack_for_frame(db, parsed, node, paired_code)
        except Exception as e:
            print(f"[LoRa] RX decode error: {e}")
            db.rollback()
        finally:
            db.close()

    lora_device.attach_receive_interrupt(on_receive)

    while True:
        await asyncio.sleep(1)


@app.on_event("startup")
async def on_startup():
    print("Initializing LoRa module (SPI)")
    lora_device.initialize()
    system_metrics.collect_metrics()
    asyncio.create_task(system_metrics_sampler())
    asyncio.create_task(lora_listener_task())


async def system_metrics_sampler():
    while True:
        await asyncio.sleep(10)
        system_metrics.collect_metrics()


@app.get("/")
def read_root():
    return {"message": "LAVIET Gateway dziala!"}
