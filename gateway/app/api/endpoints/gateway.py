from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, Field
from sqlalchemy.orm import Session

from ...models import models
from ...models.database import get_db
from ...services.laviet_frame import LAVIET_FLAG_COUNTER_OVERRIDE, LavietType
from ...services.broadcast_security import (
    BROADCAST_CTRL_FRAGMENT_COUNT,
    build_activate_payload,
    build_install_payload,
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
    if node_ids:
        resolved = []
        for node_id in node_ids:
            node_id_16 = int(node_id) & 0xFFFF
            if node_id_16 == 0 or node_id_16 == 0xFFFF:
                raise HTTPException(status_code=400, detail=f"Nieprawidlowy node_id={node_id}")
            node = db.query(models.Node).filter(models.Node.node_id == node_id_16).first()
            if node is None or not node.paired_code:
                raise HTTPException(status_code=404, detail=f"Node {node_id_16} nie jest sparowany")
            resolved.append(node_id_16)
        return sorted(set(resolved))

    rows = (
        db.query(models.Node)
        .filter(models.Node.paired_code.isnot(None), models.Node.is_paired.is_(True))
        .order_by(models.Node.node_id.asc())
        .all()
    )
    return [int(row.node_id) & 0xFFFF for row in rows if int(row.node_id or 0) not in (0, 0xFFFF)]


@router.post("/broadcast-key/rotate", summary="Rotacja klucza grupowego dla szyfrowanego broadcastu")
def rotate_broadcast_group_key(request: BroadcastKeyRotateRequest, db: Session = Depends(get_db)):
    node_ids = _resolve_broadcast_key_nodes(db, request.node_ids)
    if not node_ids:
        raise HTTPException(status_code=404, detail="Brak sparowanych node'ow do instalacji broadcast group key")

    epoch, group_key = rotate_group_key(db)
    sent = []
    try:
        for node_id in node_ids:
            frames = []
            for fragment_index in range(BROADCAST_CTRL_FRAGMENT_COUNT):
                frames.append(
                    _send_system_frame(
                        node_id,
                        LavietType.KEY_ROTATE,
                        0,
                        db,
                        ack_required=request.ack_required,
                        payload_override=build_install_payload(epoch, fragment_index, group_key),
                    )
                )
            frames.append(
                _send_system_frame(
                    node_id,
                    LavietType.KEY_ROTATE,
                    0,
                    db,
                    ack_required=request.ack_required,
                    payload_override=build_activate_payload(epoch),
                )
            )
            sent.append({"node_id": node_id, "frames": frames})
    finally:
        group_key = b"\x00" * len(group_key)

    return {
        "status": "broadcast_group_key_rotation_sent",
        "epoch": epoch,
        "node_count": len(node_ids),
        "ack_required": request.ack_required,
        "nodes": sent,
    }
