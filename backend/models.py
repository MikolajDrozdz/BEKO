import json
from datetime import datetime
from sqlalchemy import Column, Integer, String, Boolean, DateTime, Text
from database import Base

ALL_CAPABILITIES = ["dashboard", "pairing", "nodes", "messages", "logs", "system"]


class User(Base):
    __tablename__ = "users"

    id = Column(Integer, primary_key=True, index=True)
    username = Column(String(50), unique=True, index=True, nullable=False)
    hashed_password = Column(String(255), nullable=False)
    role = Column(String(10), default="user", nullable=False)  # "admin" | "user"
    permissions_json = Column(Text, default="[]", nullable=False)
    is_active = Column(Boolean, default=True, nullable=False)
    created_at = Column(DateTime, default=datetime.utcnow, nullable=False)
    updated_at = Column(DateTime, default=datetime.utcnow, nullable=False)

    @property
    def permissions(self) -> list[str]:
        if self.role == "admin":
            return ALL_CAPABILITIES
        try:
            return json.loads(self.permissions_json or "[]")
        except Exception:
            return []

    @permissions.setter
    def permissions(self, value: list[str]) -> None:
        self.permissions_json = json.dumps(list(value))
