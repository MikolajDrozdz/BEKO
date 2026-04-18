import threading
from typing import Dict, Optional, Tuple


_lock = threading.RLock()
_pending_acks: Dict[Tuple[int, int, int], int] = {}


def register_pending_ack(message_id: int, node_id: int, msg_id: int, counter: int) -> None:
    key = (node_id & 0xFFFF, msg_id & 0xFFFF, counter & 0xFFFFFFFF)
    with _lock:
        _pending_acks[key] = int(message_id)


def pop_pending_ack(node_id: int, msg_id: int, counter: int) -> Optional[int]:
    key = (node_id & 0xFFFF, msg_id & 0xFFFF, counter & 0xFFFFFFFF)
    with _lock:
        return _pending_acks.pop(key, None)


def drop_pending_ack(node_id: int, msg_id: int, counter: int) -> None:
    key = (node_id & 0xFFFF, msg_id & 0xFFFF, counter & 0xFFFFFFFF)
    with _lock:
        _pending_acks.pop(key, None)
