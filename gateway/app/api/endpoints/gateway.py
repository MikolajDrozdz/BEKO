from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, Field
from sqlalchemy.orm import Session

from ...models.database import get_db
from ...services.laviet_frame import LAVIET_FLAG_COUNTER_OVERRIDE, LavietType
from ...services.broadcast_security import (
    install_broadcast_group_key,
    resolve_paired_node_ids,
    rotate_group_key,
)
from .system import _send_system_frame

router = APIRouter()


class GatewayCounterSyncRequest(BaseModel):
    node_id: int = Field(..., ge=1, le=0xFFFF)
    counter: int = Field(..., ge=1, le=0xFFFFFFFF)
    ack_required: bool = True


class BroadcastKeyRotateRequest(BaseModel):
    node_ids: list[int] | None = None
    ack_required: bool = True


@router.post("/counter-sync", summary="Wymuszenie counter sync gateway -> node")
def counter_sync(request: GatewayCounterSyncRequest, db: Session = Depends(get_db)):
    result = _send_system_frame(
        request.node_id,
        LavietType.COUNTER_SYNC,
        LAVIET_FLAG_COUNTER_OVERRIDE,
        db,
        counter_sync_value=request.counter,
        ack_required=request.ack_required,
    )
    return {
        "status": "counter_sync_sent",
        "requested_counter": request.counter,
        "ack_required": request.ack_required,
        **result,
    }


def _resolve_broadcast_key_nodes(db: Session, node_ids: list[int] | None) -> list[int]:
    try:
        return resolve_paired_node_ids(db, node_ids)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    except LookupError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc


@router.post("/broadcast-key/rotate", summary="Rotacja klucza grupowego dla szyfrowanego broadcastu")
def rotate_broadcast_group_key(request: BroadcastKeyRotateRequest, db: Session = Depends(get_db)):
    node_ids = _resolve_broadcast_key_nodes(db, request.node_ids)
    if not node_ids:
        raise HTTPException(status_code=404, detail="Brak sparowanych node'ow do instalacji broadcast group key")

    epoch, group_key = rotate_group_key(db)
    try:
        sent = install_broadcast_group_key(
            db,
            _send_system_frame,
            epoch,
            group_key,
            node_ids,
            ack_required=request.ack_required,
        )
    finally:
        group_key = b"\x00" * len(group_key)

    return {
        "status": "broadcast_group_key_rotation_sent",
        "epoch": epoch,
        "node_count": len(node_ids),
        "ack_required": request.ack_required,
        "nodes": sent,
    }
