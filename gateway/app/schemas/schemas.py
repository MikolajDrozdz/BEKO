from pydantic import BaseModel, Field
from typing import Optional, List
from datetime import datetime

class NodeBase(BaseModel):
    node_id: int

class NodeCreate(NodeBase):
    pass

class NodeResponse(NodeBase):
    id: int
    is_paired: bool
    counter: int
    network_mode: bool
    network_ttl: int
    last_seen: datetime

    class Config:
        from_attributes = True

class MessageBase(BaseModel):
    dst_id: int = Field(..., description="Identyfikator docelowy urządzenia. Podaj 65535 dla Broadcast (do wszystkich).")
    payload_hex: str = Field(..., description="Ciąg w formacie heksadecymalnym zrzutowany ze stringa np: '68656c6c6f' (hello).")

class MessageCreate(MessageBase):
    coded: bool = Field(default=False, description="Zignorowane w LAVIET. Pole dla wstecznej kompatybilności.") 

    model_config = {
        "json_schema_extra": {
            "examples": [
                {
                    "dst_id": 65535,
                    "payload_hex": "48656c6c6f",
                    "coded": False
                }
            ]
        }
    }


class MessageResponse(MessageBase):
    id: int
    src_id: int
    is_ack: bool
    status: str
    created_at: datetime

    class Config:
        from_attributes = True

class LogResponse(BaseModel):
    id: int
    level: str
    event: str
    created_at: datetime

    class Config:
        from_attributes = True
