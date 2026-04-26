import threading

from sqlalchemy.orm import Session

from ..models import models

GATEWAY_TX_COUNTER_KEY = "gateway_tx_counter"
MAX_COUNTER = 0xFFFFFFFF

_lock = threading.RLock()


def _get_or_create_counter_row(db: Session) -> models.GatewayState:
    row = (
        db.query(models.GatewayState)
        .filter(models.GatewayState.key == GATEWAY_TX_COUNTER_KEY)
        .first()
    )
    if row is None:
        row = models.GatewayState(key=GATEWAY_TX_COUNTER_KEY, value=0)
        db.add(row)
        db.flush()
    return row


def get_gateway_tx_counter(db: Session) -> int:
    with _lock:
        row = _get_or_create_counter_row(db)
        db.commit()
        return int(row.value or 0)


def reserve_gateway_tx_counter(db: Session) -> int:
    with _lock:
        row = _get_or_create_counter_row(db)
        current = int(row.value or 0)
        if current >= MAX_COUNTER:
            raise ValueError("gateway_tx_counter overflow")
        counter = current + 1
        row.value = counter
        db.commit()
        return counter


def reserve_gateway_tx_counter_for_sync(db: Session, new_counter: int) -> int:
    if new_counter < 1 or new_counter > MAX_COUNTER:
        raise ValueError("counter poza zakresem uint32")

    with _lock:
        row = _get_or_create_counter_row(db)
        current = int(row.value or 0)
        if new_counter <= current:
            raise ValueError(
                f"counter sync musi byc wiekszy niz gateway_tx_counter ({current})"
            )
        if current >= MAX_COUNTER:
            raise ValueError("gateway_tx_counter overflow")
        frame_counter = current + 1
        row.value = frame_counter
        db.commit()
        return frame_counter


def set_gateway_tx_counter_at_least(db: Session, counter: int) -> int:
    if counter < 0 or counter > MAX_COUNTER:
        raise ValueError("gateway_tx_counter poza zakresem uint32")

    with _lock:
        row = _get_or_create_counter_row(db)
        current = int(row.value or 0)
        if counter > current:
            row.value = counter
            db.commit()
            return counter
        db.commit()
        return current
