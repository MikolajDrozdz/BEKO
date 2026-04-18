"""
Pairing flow for the LAVIET gateway.
"""

import os
import threading
import time
from typing import Callable, Dict, Optional

from ..core.laviet_crypto import LAVIET_SHARED_V1, get_hmac_key, laviet_generate_mac, laviet_mac_equal
from ..models import models
from ..models.database import SessionLocal
from .laviet_frame import (
    LAVIET_BROADCAST_ID,
    LAVIET_FLAG_BROADCAST,
    LAVIET_FLAG_PAIRING,
    LAVIET_GATEWAY_ID,
    LavietFrame,
    LavietFrameBuilder,
    LavietType,
)

PAIRING_WINDOW_SECONDS = 300.0


class PairingManager:
    def __init__(self):
        self._lock = threading.RLock()
        self._pending_reqs: Dict[int, dict] = {}
        self._paired_nodes: Dict[int, bytes] = {}
        self._send_frame_cb: Optional[Callable[[bytes], bool]] = None

    def set_send_callback(self, cb: Callable[[bytes], bool]) -> None:
        self._send_frame_cb = cb

    def _prune_pending_locked(self, now: Optional[float] = None) -> None:
        now = time.monotonic() if now is None else now
        expired = [
            target_id
            for target_id, request in self._pending_reqs.items()
            if (now - request["opened_at"]) > PAIRING_WINDOW_SECONDS
        ]
        for target_id in expired:
            del self._pending_reqs[target_id]

    def _match_pending_request_locked(self, node_id: int, now: float) -> Optional[int]:
        self._prune_pending_locked(now)

        if node_id in self._pending_reqs:
            return node_id
        if LAVIET_BROADCAST_ID in self._pending_reqs:
            return LAVIET_BROADCAST_ID
        return None

    @staticmethod
    def _persist_paired_node(node_id: int, code: bytes) -> None:
        db = SessionLocal()
        try:
            node = db.query(models.Node).filter(models.Node.node_id == node_id).first()
            if not node:
                node = models.Node(
                    node_id=node_id,
                    is_paired=True,
                    counter=0,
                    paired_code=code,
                    network_mode=False,
                    network_ttl=3,
                )
                db.add(node)
            else:
                node.is_paired = True
                node.paired_code = code
            db.commit()
        finally:
            db.close()

    def start_pairing(self, target_node_id: int = LAVIET_BROADCAST_ID) -> bool:
        msg_id = int(time.time() % 65535)
        counter = int(time.time()) & 0xFFFFFFFF
        raw_payload = os.urandom(8)

        domain_id = (
            LAVIET_BROADCAST_ID
            if target_node_id == LAVIET_BROADCAST_ID
            else min(LAVIET_GATEWAY_ID, target_node_id)
        )
        hmac_key = get_hmac_key(LAVIET_SHARED_V1, domain_id)

        flags = LAVIET_FLAG_PAIRING
        if target_node_id == LAVIET_BROADCAST_ID:
            flags |= LAVIET_FLAG_BROADCAST

        frame = LavietFrame(
            type=LavietType.PAIR_REQ,
            flags=flags,
            src_id=LAVIET_GATEWAY_ID,
            dst_id=target_node_id,
            msg_id=msg_id,
            counter=counter,
            payload_len=len(raw_payload),
            payload=raw_payload,
        )

        raw_frame = LavietFrameBuilder.build_frame(frame)
        frame.mac_tag = laviet_generate_mac(hmac_key, raw_frame, b"")
        final_bytes = LavietFrameBuilder.build_frame(frame)

        tx_ok = True
        if self._send_frame_cb:
            tx_ok = bool(self._send_frame_cb(final_bytes))

        if not tx_ok:
            print(f"[PAIRING] TX failed for PAIR_REQ to {hex(target_node_id)}")
            return False

        with self._lock:
            self._prune_pending_locked()
            self._pending_reqs[target_node_id] = {
                "opened_at": time.monotonic(),
                "msg_id": msg_id,
                "counter": counter,
            }
            print(f"[PAIRING] Sent PAIR_REQ to {hex(target_node_id)}")

        return True

    def on_pair_resp(self, frame: LavietFrame, raw_bytes_no_mac: bytes) -> bool:
        from ..core.laviet_crypto import get_hmac_key

        if frame.type != LavietType.PAIR_RESP:
            return False

        now = time.monotonic()
        with self._lock:
            pending_key = self._match_pending_request_locked(frame.src_id, now)
            if pending_key is None:
                print(
                    f"[PAIRING] Rejected PAIR_RESP from {hex(frame.src_id)}: no active pairing window"
                )
                return False

        domain_id = min(LAVIET_GATEWAY_ID, frame.src_id)
        hmac_key = get_hmac_key(LAVIET_SHARED_V1, domain_id)
        calc_mac = laviet_generate_mac(hmac_key, raw_bytes_no_mac, b"")
        if not laviet_mac_equal(calc_mac, frame.mac_tag or b""):
            print(f"[PAIRING] Rejected PAIR_RESP from {hex(frame.src_id)}: invalid MAC")
            return False

        plain_payload = frame.payload
        if len(plain_payload) != 8:
            print("[PAIRING] Rejected PAIR_RESP (pairing code must be exactly 8 bytes)")
            return False

        code = plain_payload[:8]
        with self._lock:
            pending_key = self._match_pending_request_locked(frame.src_id, now)
            if pending_key is None:
                print(
                    f"[PAIRING] Rejected PAIR_RESP from {hex(frame.src_id)}: pairing window expired"
                )
                return False

            self._paired_nodes[frame.src_id] = code
            if pending_key != LAVIET_BROADCAST_ID:
                self._pending_reqs.pop(pending_key, None)

        self._persist_paired_node(frame.src_id, code)
        print(f"[PAIRING] Accepted PAIR_RESP from {hex(frame.src_id)}. Code={code.hex()}")
        return True

    def get_paired_code(self, node_id: int) -> Optional[bytes]:
        with self._lock:
            code = self._paired_nodes.get(node_id)
            return bytes(code) if code is not None else None

    def get_status(self) -> dict:
        with self._lock:
            self._prune_pending_locked()
            return {
                "paired_nodes_count": len(self._paired_nodes),
                "paired_nodes_list": [hex(k) for k in self._paired_nodes.keys()],
                "pending_targets": [hex(k) for k in self._pending_reqs.keys()],
            }


pairing_manager = PairingManager()
