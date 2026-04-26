from datetime import datetime
from typing import List, Optional

from pydantic import BaseModel, Field


class NodeBase(BaseModel):
    node_id: int


class NodeCreate(NodeBase):
    pass


class NodeResponse(NodeBase):
    id: int
    name: Optional[str] = None
    is_paired: bool
    online: bool = False
    counter: int
    network_mode: bool
    network_ttl: int
    last_seen: datetime
    rssi: Optional[int] = None
    snr: Optional[float] = None
    battery_percent: Optional[int] = None
    firmware: Optional[str] = None
    tx_counter: int = 0
    rx_counter: int = 0
    messages_sent: int = 0
    messages_delivered: int = 0
    messages_failed: int = 0
    pending_response: bool = False

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
    ack_required: bool = Field(
        default=True,
        description="True = ustaw flage ACK_REQUIRED dla unicastu; broadcast zawsze ignoruje ACK.",
    )

    model_config = {
        "json_schema_extra": {
            "examples": [
                {
                    "dst_id": 65535,
                    "payload_hex": "48656c6c6f",
                    "coded": False,
                    "ack_required": False,
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
