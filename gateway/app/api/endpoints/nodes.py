from datetime import datetime, timedelta
from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session
from typing import List
from ...models.database import get_db
from ...models import models
from ...schemas import schemas
from ...services import message_tracker
from ...services.laviet_frame import LAVIET_GATEWAY_ID

router = APIRouter()

@router.get("/", response_model=List[schemas.NodeResponse])
def get_nodes(db: Session = Depends(get_db)):
    nodes = db.query(models.Node).all()
    now = datetime.utcnow()
    response = []

    for node in nodes:
        node_id = node.node_id
        outbound_q = db.query(models.Message).filter(
            models.Message.src_id == LAVIET_GATEWAY_ID,
            models.Message.dst_id == node_id,
        )
        inbound_q = db.query(models.Message).filter(models.Message.src_id == node_id)
        delivered_count = outbound_q.filter(
            models.Message.status.in_(["delivered", "delivered_waiting_response", "answered"])
        ).count()
        failed_count = outbound_q.filter(models.Message.status == "failed").count()
        radio = message_tracker.get_node_radio(node_id)
        last_seen = node.last_seen
        online = False
        if last_seen is not None:
            last_seen_naive = last_seen.replace(tzinfo=None) if last_seen.tzinfo else last_seen
            online = now - last_seen_naive <= timedelta(minutes=5)

        response.append(
            {
                "id": node.id,
                "node_id": node_id,
                "name": None,
                "is_paired": node.is_paired,
                "online": online,
                "last_seen": node.last_seen,
                "rssi": radio.get("rssi"),
                "snr": radio.get("snr"),
                "battery_percent": None,
                "firmware": None,
                "counter": node.counter,
                "tx_counter": node.counter,
                "rx_counter": inbound_q.count(),
                "network_mode": node.network_mode,
                "network_ttl": node.network_ttl,
                "messages_sent": outbound_q.count(),
                "messages_delivered": delivered_count,
                "messages_failed": failed_count,
                "pending_response": message_tracker.has_pending_response(node_id),
            }
        )

    return response

@router.delete("/{node_id}")
def remove_node(node_id: int, db: Session = Depends(get_db)):
    node = db.query(models.Node).filter(models.Node.node_id == node_id).first()
    if not node:
        raise HTTPException(status_code=404, detail="Node not found")
    
    # Do zaprogramowania wyzwolenie wiadomosci radia o usunięciu węzła jeżeli potrzebne
    db.delete(node)
    db.commit()
    return {"status": "removed"}
