# LAVIET Gateway API - Dokumentacja dla Frontendu

Ten dokument stanowi referencję API dla twórców frontendu integrujących się z bramką (Raspberry Pi Gateway) obsługującą sieć **LAVIET_FRAME_V1**. Cały routing i serwer działa w oparciu o framework **FastAPI**.

Domyślny adres serwera: `http://<IP_BRAMKI>:8000` (port i host zależy od usługi `uvicorn` w systemie bazowym).

## Uwagi Wstępne (Zgodność V1)
1. **Node ID (Adresacja)**: Poprzednio używano 32-bitowych identyfikatorów. Aktualnie cała adresacja radiowa jest zredukowana do 16 bitów, co oznacza, że backend automatycznie maskuje (`& 0xFFFF`) wszelkie wysyłane parametry. Unikaj jednak podawania starych, długich `node_id`. Broadcast (wysyłka do wszystkich) to teraz `65535` (`0xFFFF`). Zawsze używaj typu `integer`.
2. **Kryptografia**: Bezpieczeństwo i szyfrowanie (`HMAC`, `AES`) są całkowicie transparentne dla UI. Gateway pod spodem utrzymuje klucze weryfikacji. 

---

## 1. Moduł: Routing Wiadomości (Messages)

Najważniejszy komponent do codziennej pracy oraz rozsyłania poleceń do końcówek z STM32.

### Szablon Wysyłania
`POST /api/messages/send`
**Opis**: Kolejkuje i natychmiastowo wysyła w przestrzeń radiową zapytanie AES-CTR.

**Body (JSON)**:
```json
{
  "dst_id": 65535, 
  "payload_hex": "48656c6c6f"
}
```
- `dst_id` [int] - Adres docelowy (2-bajtowy hex oddany w int). Użyj `65535` dla Broadcastu.
- `payload_hex` [str] - Treść wiadomości zakodowana szesnastkowo. Zamiast operować na surowych charach wysyłasz HEX-string.

**Odpowiedź (200 OK)**:
Potwierdzenie bazy danych z unikalnym `msg_id`.

---

### Historia Wysłanych
`GET /api/messages/history`
**Opis**: Zwraca całą historię lokalną SQLite wysłanych wiadomości.

---

## 2. Moduł: Zarządzanie Parowaniem (Pairing)

W starym systemie BEKO to STM32 wysyłał zapytania (JOIN_REQ). **Teraz STM32 uśpiony czeka na inicjację przez Gateway FrontEnd.**

### Inicjowanie Parowania
`POST /api/pairing/start`
**Opis**: Rozsiewa w siatce radiowej w pełni zaszyfrowany sygnał `PAIR_REQ`, proszący wycelowanego node'a (lub wszystkich) o dołączenie. Po odebraniu, użytkownik fizyczny przy terminalu STM musi nacisnąć przycisk akceptacji.

**Body (JSON)**:
```json
{
  "target_node_id": 65535
}
```

---

### Sprawdzanie i Zatwierdzanie Bazy
`GET /api/pairing/status`
**Opis**: Używaj tej metody (np. pollując co 3 sekundy po zainicjowaniu parowania). Node, który poprawnie odpowiedział (`PAIR_RESP`), automatycznie wpadnie do wewnątrzpamięciowej kolekcji, a to API zatwierdzi jego konfigurację z siecią wgrywając ją trwale do bazy danych wezłów `Node`. To API w odróżnieniu od starych endpointów *accept/reject* podejmuje operację logowania do głównego zasilania samodzielnie.

**Odpowiedź (JSON)**:
```json
{
  "paired_nodes_count": 1,
  "paired_nodes_list": ["0x25a1"]
}
```

---

## 3. Moduł: Zarządzanie Węzłami (Nodes)

### Lista Sparowanych Urządzeń
`GET /api/nodes/`
**Opis**: Odczyt wszystkich zaklasyfikowanych przez gateway Node'ów, które wymieniły się tajnymi kodami relacyjnymi wpisanymi w pole `paired_code`. Zwraca historię counterów wymaganą do zapobiegania atakom typu *Replay*.

### Usuwanie Węzła
`DELETE /api/nodes/{node_id}`
**Opis**: Usuwa z sieci zaufania podany węzeł. Aby znowu go wspierać, gateway będzie musiał przejść przez pełny Pairing Flow.

---

## 4. Moduł: Tryb Administracyjny / Serwisowy (System)

Odpytania wspierające diagnostykę i stabilizację protokołu radiowego.

### Kondycja Bramy
`GET /api/system/`
**Opis**: Pobiera wewnętrzny adres bramy (`0x0001` - `LAVIET_GATEWAY_ID`), naświetlając wersję protokołu.

### Forcowanie Synchronizacji Licznika HMAC (Counter Override)
`POST /api/system/nodes/{node_id}/sync_counter`
**Opis**: Wyciąga `counter` przypięty do sesji SQLite i wysyła hardware'ową komendę na radio, która rozkazuje zrestartowanie/nadpisanie okna przesuwnego po stronie STM32. Używane w przypadku, gdy HMAC gubi ciągłość przez brak zasilania na węźle.

### Rotacja Kluczy Kryptograficznych (Key Rotation)
`POST /api/system/nodes/{node_id}/rotate_keys`
**Opis**: Wysyła bezpieczną ramkę `KEY_ROTATE` - żąda od urządzenia wygenerowania nowych relacyjnych seedów.

---

*Ten dokument stanowi ostateczną wersję dokumentacji interfejsów zgodną ze stanem pliku `gateway/app/main.py` na czas przejścia z BEKO na system środowiska LAVIET.*
