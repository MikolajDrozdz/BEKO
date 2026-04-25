from fastapi import APIRouter

from ...services.lora_hardware import lora_device


router = APIRouter()


@router.get("/status", summary="Szczegolowy status radia")
def get_radio_status():
    return lora_device.get_status()
