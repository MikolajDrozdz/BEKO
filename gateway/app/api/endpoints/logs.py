from fastapi import APIRouter, Depends, Query
from sqlalchemy.orm import Session
from typing import List
from ...models.database import get_db
from ...models import models
from ...schemas import schemas

router = APIRouter()

@router.get("/", response_model=List[schemas.LogResponse])
def get_logs(
    limit: int = Query(100, ge=1, le=1000),
    level: str | None = Query(None),
    search: str | None = Query(None),
    db: Session = Depends(get_db),
):
    query = db.query(models.Log)
    if level:
        query = query.filter(models.Log.level == level.upper())
    if search:
        query = query.filter(models.Log.event.ilike(f"%{search}%"))
    return query.order_by(models.Log.created_at.desc()).limit(limit).all()


@router.get("/summary")
def get_logs_summary(db: Session = Depends(get_db)):
    rows = db.query(models.Log.level).all()
    by_level = {}
    for row in rows:
        level = row[0] or "UNKNOWN"
        by_level[level] = by_level.get(level, 0) + 1

    latest = db.query(models.Log).order_by(models.Log.created_at.desc()).first()
    return {
        "total": len(rows),
        "by_level": by_level,
        "latest_at": latest.created_at if latest else None,
    }
