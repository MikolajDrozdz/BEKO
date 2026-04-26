# BEKO Pager Gateway Panel

Frontend dla systemu BEKO/LAVIET Gateway. Aplikacja jest panelem webowym do obsługi Raspberry Pi Gateway, nodów radiowych, komunikacji LoRa, logów, metryk systemowych, użytkowników oraz symulowanego panelu restauracyjnego.

## Stack

- React 18
- TypeScript
- Vite
- Tailwind CSS
- TanStack Query
- React Router
- Radix UI
- lucide-react

## Główne moduły

### Dashboard

Widok ogólny gatewaya:

- status gatewaya,
- liczba nodów,
- statystyki wiadomości,
- pending ACK / pending response,
- telemetry systemowe z wyborem zakresu czasu,
- message traffic z wyborem zakresu czasu,
- radio counters,
- log levels,
- RSSI nodów,
- ostatnie logi.

### Pairing

Obsługa parowania nodów z gatewayem.

### Nodes

Lista nodów i informacje o stanie urządzeń:

- adres noda,
- status sparowania,
- aktywność,
- liczniki,
- RSSI/SNR,
- bateria,
- firmware,
- pending response.

### Messenger

Panel komunikacji z nodami:

- wysyłanie wiadomości tekstowej jako `payload_hex`,
- walidacja ASCII,
- limit payloadu 16 bajtów po zakodowaniu UTF-8,
- broadcast `0xFFFF`,
- adresy nodów `0x0002..0xFFFE`,
- filtrowanie rozmów po broadcast i konkretnych adresach,
- lokalne nazwy adresów,
- przełącznik `coded`,
- przełącznik `ACK`,
- statusy wiadomości,
- szybka odpowiedź YES / OK / NO dla wiadomości wymagających odpowiedzi.

### Restaurant

Symulowany panel restauracyjny:

- kilkanaście miejsc/stolików,
- przypisanie noda do miejsca,
- przy przypisaniu wysyłana jest do noda nazwa/numer miejsca,
- akcja `READY`,
- `READY` jest wysyłane jako wiadomość kodowana z ACK,
- widoczne stany procesu: wysyłanie, oczekiwanie na ACK, dostarczone, błąd,
- metryki: miejsca, wolne, przypisane, w trakcie, dostarczone,
- możliwość usunięcia przypisanego noda.

### Logs

Podgląd logów gatewaya:

- filtrowanie po poziomie,
- wyszukiwanie,
- auto refresh,
- obsługa `/api/logs/summary`.

### System

Informacje systemowe, status gatewaya i akcje serwisowe.

### Users

Panel administracyjny użytkowników. Dostępny tylko dla admina.

## Backend API

Frontend korzysta z dwóch usług:

Backend gatewaya i Auth Service można uruchomić razem z katalogu frontendu:

```bash
npm run dev:backends
```

### Gateway API

Domyślnie:

```text
http://<ADRES_IP_RPI>:8000
```

Najważniejsze endpointy:

- `GET /api/system/`
- `GET /api/system/gateway`
- `GET /api/radio/status`
- `GET /api/messages/history`
- `POST /api/messages/send`
- `GET /api/messages/pending`
- `GET /api/messages/stats?range=24h`
- `GET /api/nodes/`
- `GET /api/logs/?limit=100`
- `GET /api/logs/summary`

### Auth Service

Domyślnie:

```text
http://<ADRES_IP_RPI>:8001
```

Endpointy:

- `POST /auth/login`
- `GET /auth/me`
- `GET /users/`
- `POST /users/`
- `PUT /users/{id}`
- `DELETE /users/{id}`

## Konfiguracja adresów API

Frontend domyślnie wylicza adresy API z hosta, z którego otwarto stronę:

- frontend: `http://<ADRES_IP_RPI>:5173`
- Gateway API: `http://<ADRES_IP_RPI>:8000`
- Auth Service: `http://<ADRES_IP_RPI>:8001`

Na ekranie logowania można zmienić adresy API przed logowaniem, jeśli frontend wskazuje na zły Auth Service. Po zalogowaniu ustawienia połączenia w głównym panelu są dostępne tylko dla admina.

## Uprawnienia

Użytkownicy mają role:

- `admin`
- `user`

Admin ma dostęp do wszystkiego. Zwykły użytkownik ma dostęp tylko do modułów przypisanych w `permissions`.

Dostępne capabilities:

- `dashboard`
- `pairing`
- `nodes`
- `messages`
- `logs`
- `system`
- `users`

## Wysyłka wiadomości

Endpoint:

```text
POST /api/messages/send
```

Body:

```json
{
  "dst_id": 2,
  "payload_hex": "7265616479",
  "coded": true,
  "ack_required": true
}
```

Znaczenie pól:

- `dst_id` - adres docelowy 16-bit,
- `payload_hex` - payload zakodowany hex,
- `coded` - czy szyfrować wiadomość,
- `ack_required` - czy wymagać ACK dla unicastu.

Broadcast:

```text
0xFFFF / 65535
```

Adres `0x0000` jest nieważny.

## Skrypty frontendowe

Instalacja:

```bash
npm install
```

Tryb developerski:

```bash
npm run dev -- --host 0.0.0.0
```

Build:

```bash
npm run build
```

Preview buildu:

```bash
npm run preview -- --host 0.0.0.0
```

## Pliki lokalne

Nie commitować:

- `node_modules/`
- `dist/`
- `backend/.venv/`
- `__pycache__/`
- `*.pyc`
- lokalnych baz testowych, jeśli nie są celowo wersjonowane.
