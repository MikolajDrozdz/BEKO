import asyncio
from fastapi import FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware
from .models.database import engine, Base
from .api.endpoints import messages, nodes, system, logs, pairing
from .services.lora_hardware import lora_device
from .services.laviet_frame import LavietFrameBuilder, LavietType
from .services.pairing import pairing_manager

# Start database models
Base.metadata.create_all(bind=engine)

app = FastAPI(title="LAVIET Gateway API", version="1.0.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

@app.middleware("http")
async def log_request_body(request: Request, call_next):
    body_bytes = await request.body()
    try:
        body_str = body_bytes.decode('utf-8')
        if body_str:
            print(f"[API REQUEST] {request.method} {request.url.path} - Body: {body_str}")
    except Exception:
        print(f"[API REQUEST] {request.method} {request.url.path} - Body: <binary data, len={len(body_bytes)}>")

    async def receive():
        return {"type": "http.request", "body": body_bytes}
    request._receive = receive

    response = await call_next(request)
    return response

app.include_router(messages.router, prefix="/api/messages", tags=["messages"])
app.include_router(nodes.router, prefix="/api/nodes", tags=["nodes"])
app.include_router(system.router, prefix="/api/system", tags=["system"])
app.include_router(logs.router, prefix="/api/logs", tags=["logs"])
app.include_router(pairing.router, prefix="/api/pairing", tags=["pairing"])

# Konfiguracja callbacka wysyłającego dla pairing managera
pairing_manager.set_send_callback(lora_device.send_frame)

# Tło odbierania wiadomości radiowych (nasłuch LoRa)
async def lora_listener_task():
    print("[BACKGROUND TASK] Rozpoczęto asynchroniczny nasłuch LoRa (LAVIET)")

    def on_receive(frame_bytes: bytes, rssi_dbm: int = 0):
        print(f"[LoRa DEBUG] Rozmiar otrzymanej ramki: {len(frame_bytes)} B, RSSI: {rssi_dbm}")
        try:
            # Weryfikujemy wstepnie format
            parsed = LavietFrameBuilder.parse_frame(frame_bytes)
            print(f"[LoRa] Zdekodowano nową ramkę: typ={parsed.type}, src={hex(parsed.src_id)}")

            if parsed.type == LavietType.PAIR_RESP:
                # Odcięcie paddingu narzucanego przez hardware (korzystamy z wyliczonej długości)
                raw_header_payload = frame_bytes[:13 + parsed.payload_len]
                pairing_manager.on_pair_resp(parsed, raw_header_payload)

            elif parsed.type == LavietType.DATA:
                print(f"[LoRa] Otrzymano DATA od {hex(parsed.src_id)} - TODO: Deszyfracja i propagacja")
            
            elif parsed.type == LavietType.ACK:
                print(f"[LoRa] Oczekiwano ACK od {hex(parsed.src_id)} - Otrzymano poprawnie!")
                
        except Exception as e:
            print(f"[LoRa] Błąd dekodowania przypchodzącej ramki: {e}")

    lora_device.attach_receive_interrupt(on_receive)

    while True:
        await asyncio.sleep(1)

@app.on_event("startup")
async def on_startup():
    print("Inicjalizacja modułu LoRa (SPI)")
    lora_device.initialize()
    asyncio.create_task(lora_listener_task())

@app.get("/")
def read_root():
    return {"message": "LAVIET Gateway działa!"}
