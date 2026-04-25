import threading
from datetime import datetime, timedelta, timezone
from typing import Dict, Optional, Tuple


_lock = threading.RLock()
_pending_acks: Dict[Tuple[int, int, int], int] = {}
_pending_ack_details: Dict[Tuple[int, int, int], dict] = {}
_pending_responses_by_node: Dict[int, int] = {}
_pending_response_details_by_node: Dict[int, dict] = {}
_pending_broadcast_response: Optional[dict] = None
_node_radio: Dict[int, dict] = {}


def _iso_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _default_expires_at() -> str:
    return (datetime.now(timezone.utc) + timedelta(minutes=5)).isoformat().replace("+00:00", "Z")


def _is_expired(detail: Optional[dict]) -> bool:
    if not detail or not detail.get("expires_at"):
        return False
    try:
        expires_at = datetime.fromisoformat(detail["expires_at"].replace("Z", "+00:00"))
    except (TypeError, ValueError):
        return False
    return expires_at <= datetime.now(timezone.utc)


def register_pending_ack(message_id: int, node_id: int, msg_id: int, counter: int) -> None:
    key = (node_id & 0xFFFF, msg_id & 0xFFFF, counter & 0xFFFFFFFF)
    with _lock:
        _pending_acks[key] = int(message_id)
        _pending_ack_details[key] = {
            "message_id": int(message_id),
            "dst_id": node_id & 0xFFFF,
            "sent_at": _iso_now(),
            "retry_count": 0,
            "msg_id": msg_id & 0xFFFF,
            "counter": counter & 0xFFFFFFFF,
        }


def pop_pending_ack(node_id: int, msg_id: int, counter: int) -> Optional[int]:
    key = (node_id & 0xFFFF, msg_id & 0xFFFF, counter & 0xFFFFFFFF)
    with _lock:
        _pending_ack_details.pop(key, None)
        return _pending_acks.pop(key, None)


def drop_pending_ack(node_id: int, msg_id: int, counter: int) -> None:
    key = (node_id & 0xFFFF, msg_id & 0xFFFF, counter & 0xFFFFFFFF)
    with _lock:
        _pending_acks.pop(key, None)
        _pending_ack_details.pop(key, None)


def register_pending_response(
    message_id: int,
    node_id: int,
    question: Optional[str] = None,
    expires_at: Optional[str] = None,
) -> None:
    global _pending_broadcast_response

    node_key = node_id & 0xFFFF
    detail = {
        "message_id": int(message_id),
        "dst_id": node_key,
        "question": question,
        "expires_at": expires_at or _default_expires_at(),
        "created_at": _iso_now(),
    }
    with _lock:
        if node_key == 0xFFFF:
            _pending_broadcast_response = detail
        else:
            _pending_responses_by_node[node_key] = int(message_id)
            _pending_response_details_by_node[node_key] = detail


def pop_pending_response(node_id: int) -> Optional[int]:
    global _pending_broadcast_response

    node_key = node_id & 0xFFFF
    with _lock:
        message_id = _pending_responses_by_node.pop(node_key, None)
        if message_id is not None:
            _pending_response_details_by_node.pop(node_key, None)
            return message_id
        if _is_expired(_pending_broadcast_response):
            _pending_broadcast_response = None
        if _pending_broadcast_response is None:
            return None
        return int(_pending_broadcast_response["message_id"])


def has_pending_response(node_id: int) -> bool:
    node_key = node_id & 0xFFFF
    with _lock:
        detail = _pending_response_details_by_node.get(node_key)
        if _is_expired(detail):
            _pending_response_details_by_node.pop(node_key, None)
            _pending_responses_by_node.pop(node_key, None)
            detail = None
        return detail is not None or (
            _pending_broadcast_response is not None and not _is_expired(_pending_broadcast_response)
        )


def snapshot_pending() -> dict:
    global _pending_broadcast_response

    with _lock:
        for node_key, detail in list(_pending_response_details_by_node.items()):
            if _is_expired(detail):
                _pending_response_details_by_node.pop(node_key, None)
                _pending_responses_by_node.pop(node_key, None)
        if _is_expired(_pending_broadcast_response):
            _pending_broadcast_response = None
        pending_response = list(_pending_response_details_by_node.values())
        if _pending_broadcast_response is not None:
            pending_response.append(_pending_broadcast_response.copy())
        return {
            "pending_ack": [detail.copy() for detail in _pending_ack_details.values()],
            "pending_response": [detail.copy() for detail in pending_response],
        }


def update_node_radio(node_id: int, rssi: Optional[int], snr: Optional[float] = None) -> None:
    node_key = node_id & 0xFFFF
    with _lock:
        _node_radio[node_key] = {
            "rssi": rssi,
            "snr": snr,
            "last_rx_at": _iso_now(),
        }


def get_node_radio(node_id: int) -> dict:
    node_key = node_id & 0xFFFF
    with _lock:
        return _node_radio.get(node_key, {}).copy()
