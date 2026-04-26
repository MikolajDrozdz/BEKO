# LAVIET Gateway Backend

Backend gatewaya działa w Pythonie na FastAPI i obsługuje sieć radiową
`LAVIET_FRAME_V1` przez moduł SX1276/RFM95 na Raspberry Pi.

Szczegółowe komendy uruchomieniowe są w [INTRUKCJA.md](../INTRUKCJA.md).

## 1. Stack

- FastAPI + Uvicorn
- SQLite + SQLAlchemy
- Pydantic
- PyCryptodome: AES-CTR i HMAC-SHA256
- `spidev` dla SPI
- `lgpio` albo `RPi.GPIO` dla GPIO
- wbudowany driver `builtin-sx1276`
- opcjonalny zewnętrzny `radio_handle.py`

## 2. Struktura

```text
gateway/app/
├── api/endpoints/
│   ├── logs.py          # logi i podsumowanie logów
│   ├── messages.py      # wysyłka, historia, statystyki, pending ACK/response
│   ├── nodes.py         # lista i usuwanie nodów
│   ├── pairing.py       # PAIR_REQ / status parowania
│   ├── radio.py         # szczegółowy status radia
│   └── system.py        # gateway info, metryki, komendy systemowe
├── core/
│   ├── laviet_crypto.py # AES-CTR, HMAC, klucze
│   └── security.py
├── models/
│   ├── database.py      # SQLite
│   └── models.py        # Node, Message, Log
├── schemas/
│   └── schemas.py       # modele Pydantic
├── services/
│   ├── laviet_frame.py      # format ramki
│   ├── lora_hardware.py     # SX1276/RFM95, SPI/GPIO, liczniki radia
│   ├── message_tracker.py   # pending ACK/response, RSSI/SNR per node
│   ├── pairing.py           # pairing manager
│   └── system_metrics.py    # próbki CPU/RAM/dysku/uptime
└── main.py
```

## 3. Najważniejsze endpointy

### System i radio

- `GET /api/system/`
- `GET /api/system/gateway`
- `GET /api/system/metrics`
- `GET /api/system/metrics/history?range=1h&step=10s`
- `GET /api/radio/status`
- `POST /api/system/nodes/{node_id}/sync_counter`
- `POST /api/system/nodes/{node_id}/rotate_keys`

### Wiadomości

- `POST /api/messages/send`
- `GET /api/messages/history`
- `GET /api/messages/stats?range=24h`
- `GET /api/messages/pending`

`POST /api/messages/send` przyjmuje:

```json
{
  "dst_id": 4660,
  "payload_hex": "48656c6c6f3f",
  "coded": false,
  "ack_required": true
}
```

`ack_required` działa tylko dla unicastu. Broadcast zawsze idzie bez
`ACK_REQUIRED`, żeby wiele nodów nie wysłało ACK jednocześnie.

Jeśli plaintext po `strip()` kończy się na `.`, `?` albo `!`, backend oznacza
wiadomość jako wymagającą odpowiedzi `YES` / `OK` / `NO`.

### Node’y

- `GET /api/nodes/`
- `DELETE /api/nodes/{node_id}`

Lista node’ów zwraca dane rozszerzone dla dashboardu, m.in. `online`, `rssi`,
`snr`, `tx_counter`, `rx_counter`, `messages_sent`, `messages_delivered`,
`messages_failed`, `pending_response`.

### Pairing

- `POST /api/pairing/start`
- `GET /api/pairing/status`

### Logi

- `GET /api/logs/?limit=100`
- `GET /api/logs/?level=ERROR`
- `GET /api/logs/?search=radio`
- `GET /api/logs/summary`

## 4. Radio

Wbudowany driver obsługuje SX1276/RFM95, nie SX1262/SX1762.

Domyślne parametry:

- częstotliwość: `868500000`
- bandwidth: `500`
- spreading factor: `7`
- coding rate: `4/5`
- TX power: `17`
- sync word: `0x34`

Konfiguracja przez zmienne środowiskowe:

```bash
export LAVIET_RADIO_DRIVER=auto      # auto | builtin | external
export LAVIET_SPI_BUS=0
export LAVIET_SPI_CS=1
export LAVIET_SPI_HZ=5000000
export LAVIET_GPIO_RESET=25
export LAVIET_GPIO_DIO0=22
export LAVIET_LORA_FREQ_HZ=868500000
export LAVIET_LORA_TX_POWER=17
export LAVIET_LORA_SYNC_WORD=0x34
export LAVIET_RX_POLL_MS=20
```

## 5. Statusy wiadomości

Backend używa m.in.:

- `pending`
- `sent`
- `sent_waiting_response`
- `delivered`
- `delivered_waiting_response`
- `answered`
- `received`
- `response`
- `failed`

## 6. Uwagi operacyjne

- Historia metryk systemowych jest trzymana w pamięci procesu.
- Pending ACK/response jest trzymane w pamięci procesu.
- `avg_ack_ms` i `avg_response_ms` w statystykach są obecnie `null`, bo baza
  nie zapisuje osobnych timestampów ACK/RESP.
- ACK wysyłany przez gateway do noda ma opóźnienie ok. `100 ms`, żeby node
  zdążył wrócić do nasłuchu.
