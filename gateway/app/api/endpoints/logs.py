from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session
from typing import List
from ...models.database import get_db
from ...models import models
from ...schemas import schemas

router = APIRouter()

@router.get("/", response_model=List[schemas.LogResponse])
def get_logs(db: Session = Depends(get_db)):
    logs = db.query(models.Log).order_by(models.Log.created_at.desc()).limit(100).all()
    return logs
