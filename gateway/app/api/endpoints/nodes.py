from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session
from typing import List
from ...models.database import get_db
from ...models import models
from ...schemas import schemas

router = APIRouter()

@router.get("/", response_model=List[schemas.NodeResponse])
def get_nodes(db: Session = Depends(get_db)):
    nodes = db.query(models.Node).all()
    return nodes

@router.delete("/{node_id}")
def remove_node(node_id: int, db: Session = Depends(get_db)):
    node = db.query(models.Node).filter(models.Node.node_id == node_id).first()
    if not node:
        raise HTTPException(status_code=404, detail="Node not found")
    
    # Do zaprogramowania wyzwolenie wiadomosci radia o usunięciu węzła jeżeli potrzebne
    db.delete(node)
    db.commit()
    return {"status": "removed"}
