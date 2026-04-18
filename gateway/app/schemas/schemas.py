from datetime import datetime
from typing import List, Optional

from pydantic import BaseModel, Field


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
    dst_id: int = Field(
        ...,
        description="Identyfikator docelowego urzadzenia. Podaj 65535 dla broadcastu.",
    )
    payload_hex: str = Field(
        ...,
        description="Ciąg heksadecymalny reprezentujacy payload, np. '68656c6c6f' dla 'hello'.",
    )


class MessageCreate(MessageBase):
    coded: bool = Field(
        default=False,
        description="False = plaintext unicast/broadcast, True = AES-CTR dla unicastu.",
    )
    key_mode: Optional[str] = Field(
        default=None,
        description="Opcjonalny tryb klucza dla szyfrowanego unicastu: pair32, pair16, shared.",
    )

    model_config = {
        "json_schema_extra": {
            "examples": [
                {
                    "dst_id": 65535,
                    "payload_hex": "48656c6c6f",
                    "coded": False,
                    "key_mode": None,
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
