from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field

from ...services.pairing import pairing_manager

router = APIRouter()


class PairingStartRequest(BaseModel):
    target_node_id: int = Field(
        default=65535,
        description="Target device ID. Use 65535 for broadcast pairing window.",
    )

    model_config = {
        "json_schema_extra": {
            "examples": [
                {
                    "target_node_id": 65535,
                }
            ]
        }
    }


@router.post("/start", summary="Start pairing by sending PAIR_REQ")
def start_pairing(req: PairingStartRequest):
    tx_ok = pairing_manager.start_pairing(target_node_id=req.target_node_id)
    if not tx_ok:
        raise HTTPException(status_code=503, detail="PAIR_REQ TX failed")

    return {
        "status": "started",
        "target_node_id": req.target_node_id,
    }


@router.get("/status", summary="Current pairing state")
def get_pairing_status():
    return pairing_manager.get_status()
