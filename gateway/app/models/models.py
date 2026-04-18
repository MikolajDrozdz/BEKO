from sqlalchemy import Boolean, Column, Integer, String, LargeBinary, DateTime
from sqlalchemy.sql import func
from .database import Base

class Node(Base):
    __tablename__ = "nodes"

    id = Column(Integer, primary_key=True, index=True)
    node_id = Column(Integer, unique=True, index=True)
    is_paired = Column(Boolean, default=False)
    counter = Column(Integer, default=0)
    shared_key = Column(LargeBinary, nullable=True)
    paired_code = Column(LargeBinary, nullable=True)  # 8-bajtowy kod parowania
    network_mode = Column(Boolean, default=False)      # Czy węzeł sieciowy (relay)
    network_ttl = Column(Integer, default=3)           # TTL propagacji w mesh
    last_seen = Column(DateTime(timezone=True), server_default=func.now(), onupdate=func.now())

class Message(Base):
    __tablename__ = "messages"

    id = Column(Integer, primary_key=True, index=True)
    dst_id = Column(Integer, index=True)
    src_id = Column(Integer, default=0)
    payload_hex = Column(String)
    is_ack = Column(Boolean, default=False)
    status = Column(String, default="pending")  # pending, sent, delivered, failed
    created_at = Column(DateTime(timezone=True), server_default=func.now())

class Log(Base):
    __tablename__ = "logs"

    id = Column(Integer, primary_key=True, index=True)
    level = Column(String, default="INFO")
    event = Column(String)
    created_at = Column(DateTime(timezone=True), server_default=func.now())
