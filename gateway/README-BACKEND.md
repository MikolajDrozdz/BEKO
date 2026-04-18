# LAVIET Gateway Backend

Dokumentacja oraz przewodnik dla backendu obsługującego sieć radiową LAVIET na platformie Raspberry Pi z modułem LoRa (np. RFM95 / SX1276). 

## 1. Architektura i Stack Technologiczny

Backend został zaprojektowany z myślą o urządzeniach wbudowanych, ale z wystawieniem nowoczesnego API w oparciu o środowisko Python:
* **Główny Framework API:** FastAPI
* **Serwer uvicorn:** Asynchroniczny serwer ASGI (`uvicorn[standard]`)
* **Baza danych:** SQLite z dostępem poprzez ORM **SQLAlchemy**
* **Walidacja danych:** Pydantic
* **Kryptografia:** PyCryptodome (AES-CTR 128-bit, HMAC-SHA256)
* **Obsługa sprzętu:** Zależności takie jak `spidev`, `RPi.GPIO`, `lgpio` obsługują komunikację po SPI z modułem LoRa-FSK.

## 2. Struktura Projektu

Aplikacja żyje w katalogu `gateway/app/`:
```text
gateway/app/
├── api/             # Definicja routerów FastAPI (endpoints)
│   ├── endpoints/
│   │   ├── messages.py   # API wysyłania/historii wiadomości
│   │   ├── nodes.py      # Zarządzanie sparowanymi węzłami
│   │   ├── pairing.py    # Endpointy wyzwalające PAIR_REQ
│   │   ├── logs.py       # Pobieranie logów z bazy
│   │   └── system.py     # Endpoint statusu rutera
├── core/            # Jądro logiki i bezpieczeństwa
│   ├── laviet_crypto.py  # AES-CTR, HMAC-SHA256, generowanie kluczy na podstawie domain_id
│   └── security.py       # Narzędzia dodatkowe
├── models/          # SQLAlchemy modele bazy SQLite
│   ├── database.py       
│   └── models.py         # Tabele: Node, Message, Log
├── schemas/         # Struktury Pydantic wejścia/wyjścia API
│   └── schemas.py
├── services/        # Logika biznesowa i sterowniki
│   ├── laviet_frame.py   # Dekoder i enkoder ramki na poziomie bajtów (LAVIET_FRAME_VERSION = 1)
│   ├── lora_hardware.py  # Sterowanie przerwaniami RX z pinu DIO0 oraz komunikacją SPI
│   └── pairing.py        # Stanowa logika procesu zapraszania do sieci (handshake PAIR_REQ/RESP)
└── main.py          # Punkt wejściowy FastAPI i przypięcie background tasku nasłuchiwania LoRa
```

## 3. Uruchamianie Systemu

Aby uruchomić backend lokalnie lub na produkcji (Raspberry Pi):

1. **Instalacja środowiska:**
   ```bash
   python3 -m venv venv
   source venv/bin/activate
   pip install -r requirements.txt
   ```
2. **Uruchomienie serwera dev:**
   ```bash
   # Będąc w katalogu gateway/
   uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
   ```
3. **Konfiguracja SPI:** System RPi musi mieć włączone SPI (`sudo raspi-config` -> Interface Options -> SPI).

---

## 4. Prompt do wygenerowania identycznego backendu z poziomu zera (LLM Prompt)

Jeśli kiedykolwiek utracisz kod tego backendu lub zechcesz przenieść go na inną technologię/przebudować z użyciem AI, skopiuj poniższy blok i użyj jako instrukcji kontekstowej dla modelu (np. ChatGPT / Claude / Gemini).

> **SYSTEM PROMPT / INSTRUCTION DLA LLM:**
> 
> "Zbuduj dla mnie architekturę backendową w Pythonie (FastAPI) dla bramki transmisyjnej LoRa. Nazwa kodowa protokołu to 'LAVIET'. System komunikuje się z bramką za pomocą SPI, przechowuje dane w SQLite i eksponuje API REST. 
> 
> Użyj tego opisu do wygenerowania dokładnej struktury plików podzielonej na warstwy: `api`, `core`, `models`, `schemas`, `services`.
> 
> **Wymagania bazy danych (SQLAlchemy):**
> 1. Tabela `Node`: `id`, `node_id` (Integer, unique), `is_paired` (Boolean), `counter` (Int), `shared_key` (LargeBinary), `paired_code` (LargeBinary), `network_mode` (Bool), `network_ttl` (Int), `last_seen` (DateTime).
> 2. Tabela `Message`: `id`, `dst_id` (Int), `src_id` (Int default 0), `payload_hex` (String), `is_ack` (Bool), `status` (String: pending, sent, delivered, failed).
> 3. Tabela `Log`: `id`, `level`, `event`.
> 
> **Wymagania kryptografii (`app/core/laviet_crypto.py` wg standardu STM32):**
> - Użyj `PyCryptodome`. Współdzielony klucz bazowy to `LAVIET_SHARED_V1` (16 bajtów ASCII).
> - Obliczanie subkluczy robionych przez HMAC-SHA256 nad `b'LV1K' + struct.pack('>H', domain_id) + b'\x00' + selector`. Dla AES selector to `1` (obcięty do 16 bajtów), dla HMAC selector to `2`.
> - Szyfrowanie to AES-CTR. Prefix dla `Crypto.Util.Counter` przed wejściem AES ma 14 bajtów: `b'LV1\x00' | src_id(2B) | dst_id(2B) | msg_id(2B) | counter(4B)`. `initial_value=0`.
> - Generowanie tagu uwierzytelniającego ramkę to HMAC-SHA256 nad połączonym: `header_bytes + cipher_payload_bytes`. Wynik ma 32 bajty.
> - Odtwórz klucz renegocjacyjny `security_peer_link_key_derive(local_id, peer_id, code)`: `label = b'SEC:PAIR:V1'`, połączone z posortowanymi `min(), max()` ID, puszczone przez `hmac.new` gdzie kluczem jest 8-bajtowy kod parujący. Zwraca 16 b AES Peer Link Key.
> 
> **Wymagania definicji ramki radiowej (`app/services/laviet_frame.py`):**
> - Limit payloadu: 16 bajtów. MAC tag: 32 bajty. Frame długość od minimum 45B do 61B. Gateway ID: `0x0001`, Broadcast: `0xFFFF`.
> - Format struct to: `>BB HHHI B` co odpowiada kolejno: (1) `ver_type`, (2) `flags`, (3) `src_id`, (4) `dst_id`, (5) `msg_id`, (6) `counter`, (7) `payload_len`.
> - `ver_type` pole to 4 bity wersji protokołu `1` i 4 bity typu (DATA=1, ACK=2, PAIR_REQ=4, PAIR_RESP=5).
> - Flagi to mapa bitowa: ENCRYPTED(1<<0), ACK_REQUIRED(1<<1), IS_ACK(1<<2), PAIRING(1<<3), BROADCAST(1<<5). Zaimplementuj dataclass dekodujące oraz enkodujące `LavietFrame(type, flags, src_id, dst_id, msg_id, counter, payload_len, payload, mac_tag)`. Rzuć `ValueError`, gdy payload jest > 16 albo mac się nie zgadza z długością reszty ramki w `parse_frame()`.
> 
> **Wymagania menedżera parowania (`app/services/pairing.py`):**
> - In-memory object `PairingManager`. 
> - Funkcja `start_pairing(target_node_id=0xFFFF)` wysyła nieszysfrowaną ramkę `PAIR_REQ` (flaga PAIRING i ewentualnie BROADCAST), jako wygenerowany 8-bajtowy jawny (urandom) kod. Mac nad ramką robi bazując na HMAC generowanym nad `domain_id` równym 0xFFFF lub docelowemu unicast_id.
> - Callbck `on_pair_resp(frame, raw_bytes)` odbiera od STM32 odpowiedź (type 5). Oblicza klucz hmac dla src_id noda z bazowego `LAVIET_SHARED_V1`, weryfikuje podany MAC_TAG, następnie bierze 8-bajtowy kod z payloadu i zapisuje w słowniku `_paired_nodes`.
> 
> **Główny ruter i background task (`app/main.py`):**
> - Dołącz routery API.
> - W zdarzeniu statup utwórz pętlę w backgroundzie `asyncio.create_task()` iterującą nasłuch LoRa z dummy/sprzętowym przerwaniem, które parsuje ramki i przekazuje PAIR_RESP do `pairing_manager.on_pair_resp()`.
> - Każdy przychodzący request do API loguj przez proste użycie middleware na porcie HTTP.
> 
> **Podział punktów z API (Endpointy, routery Pydantic):**
> - `GET /api/nodes` - zwraca wykaz nodes z bazy. 
> - `POST /api/pairing/start` - przyjmuje docelowy ID bazy albo inicjuje broadcast na network. Odpala `pairing_manager.start_pairing()`.
> - `GET /api/messages` i `POST /api/messages` do kolejkowania ramek heksadecymalnych payload_hex.
> - `GET /api/system/status` - rzuca informacje o LoRa (czy interfejs działa) z podstawowym licznikiem.
> 
> Wygeneruj kompletny i skompilowany projekt uwzględniając podział na pliki i foldery. Kod musi być ścisły co do Big-Endian `>HHHI B` na nagłówkach radiowych pycryptodome AES-CTR i algorytmie weryfikacji po mac."
