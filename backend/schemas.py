from datetime import datetime
from typing import Optional
from pydantic import BaseModel, Field, field_validator

VALID_CAPABILITIES = {"dashboard", "pairing", "nodes", "messages", "logs", "system"}


class Token(BaseModel):
    access_token: str
    token_type: str = "bearer"
    user: "UserResponse"


class UserBase(BaseModel):
    username: str = Field(..., min_length=3, max_length=50, pattern=r"^[a-zA-Z0-9_.-]+$")
    role: str = Field(default="user", pattern="^(admin|user)$")
    permissions: list[str] = []
    is_active: bool = True

    @field_validator("permissions")
    @classmethod
    def validate_permissions(cls, v: list[str]) -> list[str]:
        invalid = set(v) - VALID_CAPABILITIES
        if invalid:
            raise ValueError(f"Invalid permissions: {', '.join(sorted(invalid))}")
        return list(dict.fromkeys(v))  # deduplicate preserving order


class UserCreate(UserBase):
    password: str = Field(..., min_length=6, max_length=128)


class UserUpdate(BaseModel):
    role: Optional[str] = Field(None, pattern="^(admin|user)$")
    permissions: Optional[list[str]] = None
    is_active: Optional[bool] = None
    password: Optional[str] = Field(None, min_length=6, max_length=128)

    @field_validator("permissions")
    @classmethod
    def validate_permissions(cls, v: Optional[list[str]]) -> Optional[list[str]]:
        if v is None:
            return v
        invalid = set(v) - VALID_CAPABILITIES
        if invalid:
            raise ValueError(f"Invalid permissions: {', '.join(sorted(invalid))}")
        return list(dict.fromkeys(v))


class UserResponse(UserBase):
    id: int
    created_at: datetime
    updated_at: datetime

    model_config = {"from_attributes": True}
