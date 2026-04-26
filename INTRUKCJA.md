# INTRUKCJA

Ten plik opisuje, gdzie wejść, co zainstalować i jak uruchomić backend
gatewaya LAVIET/BEKO.

## 0. Pobranie programu

Program pobiera się z repozytorium Git projektu. W komendach niżej podmień
`<URL_REPOZYTORIUM>` na właściwy adres repo, np. z GitHuba/GitLaba.

Przykład pobrania do katalogu `/opt/beko`:

```bash
sudo mkdir -p /opt/beko
sudo chown "$USER":"$USER" /opt/beko
cd /opt/beko
git clone <URL_REPOZYTORIUM> BEKO
cd BEKO
```

Jeśli repozytorium zostało pobrane jako plik ZIP:

```bash
sudo mkdir -p /opt/beko
sudo chown "$USER":"$USER" /opt/beko
cd /opt/beko
unzip /sciezka/do/pobranego_pliku.zip
mv <katalog_po_rozpakowaniu> BEKO
cd BEKO
```

Po pobraniu sprawdź, czy widzisz katalog `gateway`:

```bash
ls -la
ls -la gateway
```

W instrukcji używam zmiennej `BEKO_HOME`. Ustaw ją na katalog, w którym masz
repozytorium `BEKO`.

Przykład dla instalacji produkcyjnej:

```bash
export BEKO_HOME=/opt/beko/BEKO
```

Przykład dla pracy lokalnej w aktualnie sklonowanym repo:

```bash
cd /sciezka/do/BEKO
export BEKO_HOME="$PWD"
```

Backend znajduje się w:

```bash
cd "$BEKO_HOME/gateway"
```

## 1. Pierwsza instalacja backendu

```bash
cd "$BEKO_HOME/gateway"
python3 -m venv venv
source venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
```

Na zwykłym komputerze bez Raspberry Pi część bibliotek GPIO/SPI może się nie
zainstalować albo nie działać poprawnie. Do pracy z prawdziwym radiem używaj
Raspberry Pi z włączonym SPI.

## 2. Włączenie SPI na Raspberry Pi

Jednorazowo:

```bash
sudo raspi-config
```

Wybierz:

```text
Interface Options -> SPI -> Enable
```

Po restarcie sprawdź:

```bash
ls -l /dev/spidev*
```

Jeśli SPI jest włączone, powinieneś zobaczyć np. `/dev/spidev0.0` lub
`/dev/spidev0.1`.

## 3. Konfiguracja radia

Domyślnie backend próbuje trybu `auto`: najpierw zewnętrzny `radio_handle.py`,
a jeśli go nie ma, wbudowany driver `builtin-sx1276`.

Typowa konfiguracja dla SX1276/RFM95:

```bash
cd "$BEKO_HOME/gateway"
source venv/bin/activate

export LAVIET_RADIO_DRIVER=auto
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

Jeśli chcesz wymusić wbudowany driver:

```bash
export LAVIET_RADIO_DRIVER=builtin
```

Jeśli chcesz wymusić zewnętrzny `radio_handle.py`:

```bash
export LAVIET_RADIO_DRIVER=external
```

Uwaga: obecny backend ma driver dla SX1276/RFM95. Nie jest to driver dla
SX1262/SX1762.

## 4. Uruchomienie backendu lokalnie

W terminalu:

```bash
cd "$BEKO_HOME/gateway"
source venv/bin/activate
uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

API będzie dostępne pod:

```text
http://localhost:8000
http://<IP_RASPBERRY_PI>:8000
```

Dokumentacja OpenAPI:

```text
http://localhost:8000/docs
```

## 5. Szybki test API

W drugim terminalu:

```bash
curl http://localhost:8000/
curl http://localhost:8000/api/system/
curl http://localhost:8000/api/system/gateway
curl http://localhost:8000/api/radio/status
curl http://localhost:8000/api/system/metrics
curl 'http://localhost:8000/api/system/metrics/history?range=1h&step=10s'
curl http://localhost:8000/api/nodes/
curl http://localhost:8000/api/messages/history
curl 'http://localhost:8000/api/messages/stats?range=24h'
curl http://localhost:8000/api/messages/pending
curl 'http://localhost:8000/api/logs/?limit=100'
curl http://localhost:8000/api/logs/summary
```

## 6. Wysłanie wiadomości

Payload musi być HEX-em. Przykład tekstu `OK?`:

```bash
python3 - <<'PY'
print("OK?".encode("utf-8").hex())
PY
```

Wysyłka broadcast:

```bash
curl -X POST http://localhost:8000/api/messages/send \
  -H 'Content-Type: application/json' \
  -d '{
    "dst_id": 65535,
    "payload_hex": "4f4b3f",
    "coded": false,
    "ack_required": false
  }'
```

Wysyłka broadcast z kodowaniem payloadu:

```bash
curl -X POST http://localhost:8000/api/messages/send \
  -H 'Content-Type: application/json' \
  -d '{
    "dst_id": 65535,
    "payload_hex": "4f4b3f",
    "coded": true,
    "ack_required": false
  }'
```

Uwaga: produkcyjny model bezpiecznego broadcastu wymaga osobnego klucza grupy
i rotacji kluczy. Projekt wdrozenia jest opisany w
[BROADCAST_SECURITY_REKEY.md](BROADCAST_SECURITY_REKEY.md).

Wysyłka do konkretnego noda, np. `0x1234` = `4660`:

```bash
curl -X POST http://localhost:8000/api/messages/send \
  -H 'Content-Type: application/json' \
  -d '{
    "dst_id": 4660,
    "payload_hex": "4f4b3f",
    "coded": false,
    "ack_required": true
  }'
```

Jeśli plaintext kończy się na `.`, `?` albo `!`, backend oznacza wiadomość jako
wymagającą odpowiedzi `YES` / `OK` / `NO`.

## 7. Pairing noda

Start parowania broadcast:

```bash
curl -X POST http://localhost:8000/api/pairing/start \
  -H 'Content-Type: application/json' \
  -d '{"target_node_id": 65535}'
```

Sprawdzenie statusu:

```bash
curl http://localhost:8000/api/pairing/status
```

## 8. Komendy systemowe dla noda

Synchronizacja licznika:

```bash
curl -X POST http://localhost:8000/api/system/nodes/4660/sync_counter
```

Rotacja kluczy:

```bash
curl -X POST http://localhost:8000/api/system/nodes/4660/rotate_keys
```

## 9. Uruchomienie jako usługa systemd

W przykładzie poniżej zakładam instalację w `/opt/beko/BEKO` i użytkownika
systemowego `beko`. Jeśli używasz innego katalogu albo użytkownika, podmień:

- `/opt/beko/BEKO`
- `User=beko`

Utwórz plik:

```bash
sudo nano /etc/systemd/system/laviet-gateway.service
```

Wklej:

```ini
[Unit]
Description=LAVIET Gateway Backend
After=network-online.target
Wants=network-online.target

[Service]
WorkingDirectory=/opt/beko/BEKO/gateway
Environment=LAVIET_RADIO_DRIVER=auto
Environment=LAVIET_SPI_BUS=0
Environment=LAVIET_SPI_CS=1
Environment=LAVIET_SPI_HZ=5000000
Environment=LAVIET_GPIO_RESET=25
Environment=LAVIET_GPIO_DIO0=22
Environment=LAVIET_LORA_FREQ_HZ=868500000
Environment=LAVIET_LORA_TX_POWER=17
Environment=LAVIET_LORA_SYNC_WORD=0x34
Environment=LAVIET_RX_POLL_MS=20
ExecStart=/opt/beko/BEKO/gateway/venv/bin/uvicorn app.main:app --host 0.0.0.0 --port 8000
Restart=always
RestartSec=3
User=beko

[Install]
WantedBy=multi-user.target
```

Włącz i uruchom:

```bash
sudo systemctl daemon-reload
sudo systemctl enable laviet-gateway
sudo systemctl start laviet-gateway
```

Logi usługi:

```bash
journalctl -u laviet-gateway -f
```

Restart:

```bash
sudo systemctl restart laviet-gateway
```

Zatrzymanie:

```bash
sudo systemctl stop laviet-gateway
```

## 10. Gdzie jest dokumentacja

- [README.md](README.md) - opis systemu
- [BROADCAST_SECURITY_REKEY.md](BROADCAST_SECURITY_REKEY.md) - szyfrowany broadcast i rotacja kluczy
- [gateway/README.md](gateway/README.md) - kontrakt gateway/node
- [gateway/FRAME_CRYPTO_SPEC.md](gateway/FRAME_CRYPTO_SPEC.md) - ramka i kryptografia
- [gateway/FRONTEND_API.md](gateway/FRONTEND_API.md) - API dla frontendu
- [gateway/README-BACKEND.md](gateway/README-BACKEND.md) - architektura backendu
- [gateway/RPI_SX1276_IMPLEMENTATION.md](gateway/RPI_SX1276_IMPLEMENTATION.md) - Raspberry Pi + radio
