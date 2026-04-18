from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session
from pydantic import BaseModel, Field
from typing import Optional
from ...models.database import get_db
from ...models import models
from ...services.pairing import pairing_manager

router = APIRouter()

class PairingStartRequest(BaseModel):
    target_node_id: int = Field(default=65535, description="Identyfikator urządzenia, które ma zostać sparowane. Standardowo użyj 65535 żeby włączyć Broadcasting radiowy.")

    model_config = {
        "json_schema_extra": {
            "examples": [
                {
                    "target_node_id": 65535
                }
            ]
        }
    }

@router.post("/start", summary="Inicjacja parowania (wyślij PAIR_REQ)")
def start_pairing(req: PairingStartRequest):
    """
    Wysyła ramkę PAIR_REQ do stacji STM32. 
    STM32 usłyszawszy tę ramkę zapyta użytkownika na LCD czy akceptuje parowanie.
    """
    pairing_manager.start_pairing(target_node_id=req.target_node_id)
    return {
        "status": "started",
        "target_node_id": req.target_node_id
    }

@router.get("/status", summary="Stan sesji parowania (LAVIET)")
def get_pairing_status(db: Session = Depends(get_db)):
    """
    Zwraca węzły, które odpowiedziały własnym PAIR_RESP i zostały potwierdzone sprzętowo.
    """
    status = pairing_manager.get_status()
    # Zapisujemy do bazy nodes "auto-accepted" by logic received from background thread
    for src_id_hex in status.get("paired_nodes_list", []):
        node_id = int(src_id_hex, 16)
        code = pairing_manager._paired_nodes.get(node_id)
        if code:
            node = db.query(models.Node).filter(models.Node.node_id == node_id).first()
            if not node:
                node = models.Node(
                    node_id=node_id,
                    is_paired=True,
                    counter=0,
                    paired_code=code,
                    network_mode=False,
                    network_ttl=3
                )
                db.add(node)
            else:
                node.is_paired = True
                node.paired_code = code
            db.commit()
    return status
