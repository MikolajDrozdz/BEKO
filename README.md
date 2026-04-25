# BEKO Pager Network (`pager-rtos`)

## 1. Opis systemu

BEKO to bezprzewodowy system pagerowy pracujący w topologii **gwiazdy**.  
W systemie występuje:

- jeden węzeł centralny: **Raspberry Pi Gateway**,
- do **65 533 node’ów STM32** w przestrzeni adresowej protokołu,
- komunikacja radiowa przez obecny backend **SX1276/RFM95**.

Gateway wysyła wiadomości do wybranego node’a albo do całej sieci.  
Każda poprawnie odebrana wiadomość z gatewaya musi zostać potwierdzona przez node ramką `ACK`.

---

## 2. Adresacja

Adresacja w ramce `LAVIET_FRAME_V1` jest 16-bitowa:

- `0x0000` – wartość nieważna, zarezerwowana jako sentinel dla braku adresu / błędnej inicjalizacji,
- `0x0001` – Raspberry Pi Gateway,
- `0x0002..0xFFFE` – adresy node’ów,
- `0xFFFF` – broadcast.

W systemie może działać:

- **1 gateway**,
- **65 533 urządzenia końcowe** adresowane jako `0x0002..0xFFFE`.

---

## 3. Role użytkowników

W systemie przewidziane są cztery role:

### Administrator
Ma pełny dostęp do systemu.  
Może zmieniać konfigurację gatewaya i node’ów, zarządzać parowaniem, licznikami bezpieczeństwa, kluczami i ustawieniami krytycznymi.  
Dostęp wymaga podania odpowiedniego PIN-u administratora.

### Operator
Ma ograniczony dostęp administracyjny.  
Może wykonywać działania, które nie naruszają integralności całego systemu, np. wysyłać wiadomości, zmieniać wybrane ustawienia użytkowe i obsługiwać system operacyjnie.

### Użytkownik
Ma dostęp tylko do podstawowych funkcji node’a.  
Może odczytać wiadomość, wysłać odpowiedź przyciskami i korzystać z urządzenia w normalnym trybie pracy.

### Serwisant
Ma fizyczny dostęp do urządzenia.  
Może podłączyć się do node’a lub gatewaya w celu diagnostyki, odczytu logów, testów i czynności serwisowych.  
Dostęp serwisowy powinien być kontrolowany i logowany.

---

## 4. Ochrona ustawień lokalnych

Każdy node posiada lokalny tryb konfiguracji.  
Dostęp do ustawień wymaga podania **4-cyfrowego PIN-u**.

PIN:

- chroni lokalną konfigurację,
- może zostać zmieniony,
- nie może być przechowywany w postaci jawnej.

Powinien być przechowywany w postaci zabezpieczonej, np. jako wartość chroniona z użyciem HMAC lub materiału powiązanego z TPM.

PIN służy do kontroli dostępu, a nie jako główny sekret kryptograficzny systemu.

---

## 5. Ramka `LAVIET_FRAME_V1`

System wykorzystuje zwartą ramkę aplikacyjną `LAVIET_FRAME_V1`.

### Struktura ramki

- `ver_type` – 1 B
- `flags` – 1 B
- `src_id` – 2 B
- `dst_id` – 2 B
- `msg_id` – 2 B
- `counter` – 4 B
- `payload_len` – 1 B
- `payload` – 0–16 B
- `mac_tag` – 32 B

### Rozmiar ramki

Część stała zajmuje:

- 1 + 1 + 2 + 2 + 2 + 4 + 1 + 32 = **45 B**

Całkowity rozmiar ramki:

- minimum: **45 B**
- maksimum: **61 B**

Ramka mieści się w limicie 64 B i zostawia **3 B rezerwy**.

---

## 6. Znaczenie pól `ver_type` i `flags`

### `ver_type`
Pole 1-bajtowe:

- starsze 4 bity – wersja protokołu,
- młodsze 4 bity – typ wiadomości.

Przykładowe typy:

- `0x1` – `DATA`
- `0x2` – `ACK`
- `0x3` – `RESP`
- `0x4` – `PAIR_REQ`
- `0x5` – `PAIR_RESP`
- `0x6` – `CFG`
- `0x7` – `COUNTER_SYNC`
- `0x8` – `KEY_ROTATE`
- `0x9` – `ERROR`

### `flags`
Pole 1-bajtowe:

- bit 0 – `ENCRYPTED`
- bit 1 – `ACK_REQUIRED`
- bit 2 – `IS_ACK`
- bit 3 – `PAIRING`
- bit 4 – `CONFIG_ACCESS`
- bit 5 – `BROADCAST`
- bit 6 – `COUNTER_OVERRIDE`
- bit 7 – `KEY_UPDATE`

---

## 7. Zabezpieczenia

System chroni:

- poufność wiadomości,
- integralność ramek,
- autentyczność nadawcy,
- licznik anti-replay,
- konfigurację urządzenia,
- klucze kryptograficzne.

### HMAC-SHA256

Każda ramka zawiera `mac_tag` o długości **32 B**.  
Do uwierzytelniania i kontroli integralności używany jest **HMAC-SHA256**.

HMAC liczony jest po:

`ver_type || flags || src_id || dst_id || msg_id || counter || payload_len || payload`

Firmware inicjalizuje bloki **CRYP/HASH/RNG** STM32U545 i nie uruchamia ruchu secure, jeśli CRYP/HASH nie zgłoszą gotowości. `laviet_crypto` zachowuje portable ścieżkę obliczeń jako referencję zgodności formatu ramki; backend można dalej przepiąć na pełne wywołania HAL bez zmiany kontraktu `LAVIET_FRAME_V1`.

### Szyfrowanie wiadomości

Payload wiadomości szyfrowany jest w trybie:
- **AES-CTR**

Zalety:

- brak paddingu,
- ciphertext ma taką samą długość jak plaintext,
- dobrze pasuje do krótkiego payloadu 0–16 B.

Jeżeli flaga `ENCRYPTED` jest ustawiona, odbiornik musi:

1. zweryfikować HMAC,
2. sprawdzić licznik `counter`,
3. dopiero wtedy odszyfrować payload.

---

## 8. TPM i klucze

TPM jest głównym punktem zaufania w systemie.

Wszystkie operacje bezpieczeństwa, które mogą być wykonane w TPM i które są wspierane w przyjętej architekturze, powinny być wykonywane właśnie tam.

TPM jest używany do:

- przechowywania sekretu głównego urządzenia,
- ochrony kluczy,
- generowania danych losowych,
- wsparcia parowania,
- wsparcia rotacji kluczy,
- odpieczętowywania materiału kryptograficznego.

Klucze nie mogą być przechowywane w firmware w postaci jawnej.  
Są wyprowadzane lub ładowane bezpiecznie przy starcie, a następnie używane tylko tymczasowo.

---

## 9. Anti-replay i synchronizacja licznika

Każda ramka zawiera pole `counter` o długości 4 B.

Licznik:

- rośnie monotonicznie,
- służy do ochrony przed replay,
- jest sprawdzany po stronie odbiorcy.

Gateway może wysłać specjalną wiadomość `COUNTER_SYNC`, aby nadpisać licznik node’a.  
Do tego celu używana jest flaga `COUNTER_OVERRIDE`.

Taka operacja:

- jest dostępna tylko dla gatewaya,
- musi być zabezpieczona HMAC,
- powinna być logowana.

---

## 10. Parowanie i rotacja kluczy

### Parowanie

Parowanie odbywa się wyłącznie z gatewayem. Próg RSSI `-20 dBm` jest traktowany jako ostrzeżenie diagnostyczne do walidacji praktycznej, a nie jako twarda blokada w pierwszym wdrożeniu.

Przebieg:

1. Gateway wysyła `PAIR_REQ`.
2. Node przechodzi w tryb parowania.
3. Użytkownik lokalnie potwierdza operację.
4. TPM wspiera ustanowienie materiału kryptograficznego.
5. Zapisywana jest relacja zaufania.
6. Wyprowadzane są klucze robocze.

### Rotacja kluczy

System powinien wspierać rotację kluczy z użyciem **Diffie–Hellman**.

Rotacja może być uruchamiana:

- okresowo,
- po określonej liczbie wiadomości,
- na żądanie administratora,
- po incydencie bezpieczeństwa.

---

## 11. Logi UART i kontrola startu

Podczas uruchamiania urządzenia przez UART powinny być wypisywane informacje o inicjalizacji:

- MCU,
- TPM,
- SX1276/RFM95,
- wyświetlacza,
- przycisków,
- LED,
- EEPROM,
- materiału kluczowego,
- konfiguracji bezpieczeństwa,
- zgodności firmware.

Dodatkowo czasy inicjalizacji ważnych modułów powinny być mierzone.  
Jeżeli któryś element uruchamia się poza oczekiwanym zakresem czasowym, system powinien zgłosić anomalię na UART.

---

## 12. Główny graf systemu

```mermaid
graph TD
    A["Administrator"] --> WEB["Panel / interfejs systemu"]
    O["Operator"] --> WEB
    U["Użytkownik"] --> NODE["Node STM32"]
    A --> NODE
    S["Serwisant"] --> DEV["Dostęp fizyczny / UART / serwis"]

    WEB --> GW["Raspberry Pi Gateway"]

    GW --> N1["Node STM32 #1"]
    GW --> N2["Node STM32 #X"]
    GW --> N3["Node STM32 #65533"]

    N1 --> UI1["Wyświetlacz"]

    N2 --> UI2["Wyświetlacz"]

    N3 --> UI3["Wyświetlacz"]

    DEV --> GW
    DEV --> N2
````

---

## 13. Główne przepływy komunikacji

### Zwykła wiadomość z ACK

```mermaid
sequenceDiagram
    participant O as Operator / Administrator
    participant GW as Raspberry Pi Gateway
    participant N as Node STM32
    participant U as Użytkownik

    O->>GW: Wysłanie wiadomości
    GW->>GW: Budowa ramki
    GW->>GW: AES-CTR payload
    GW->>GW: HMAC-SHA256
    GW->>N: DATA
    N->>N: Weryfikacja HMAC i counter
    N->>N: Odszyfrowanie payloadu
    N->>U: Wyświetlenie wiadomości
    alt Wiadomość kończy się na ".", "?" albo "!"
        N->>U: Pokazanie opcji YES / OK / NO
        U->>N: Wybór odpowiedzi
        N->>GW: RESP
    end
    N->>GW: ACK
    GW->>O: Wynik operacji
```

### Broadcast z odpowiedzią użytkownika

Broadcast `DATA` nie ma `ACK_REQUIRED`, ale node nadal musi pokazać opcje `YES` / `OK` / `NO`, jeśli plaintext wiadomości po `strip()` kończy się na `.`, `?` albo `!`. Odpowiedź użytkownika jest odsyłana do gatewaya jako unicast `RESP` z payloadem ASCII `YES`, `OK` albo `NO`.

### Parowanie

```mermaid
sequenceDiagram
    participant A as Administrator
    participant GW as Raspberry Pi Gateway
    participant N as Node STM32
    participant TPM as TPM
    participant U as Użytkownik

    A->>GW: Uruchomienie parowania
    GW->>N: PAIR_REQ
    N->>U: Prośba o potwierdzenie
    U->>N: Potwierdzenie lokalne
    GW->>TPM: Operacje bezpieczeństwa
    N->>TPM: Operacje bezpieczeństwa
    GW->>N: Wymiana danych parowania
    N->>GW: PAIR_RESP
    GW->>A: Wynik operacji
```

### Synchronizacja licznika

```mermaid
sequenceDiagram
    participant A as Administrator
    participant GW as Raspberry Pi Gateway
    participant N as Node STM32

    A->>GW: Żądanie synchronizacji licznika
    GW->>N: COUNTER_SYNC
    N->>N: Weryfikacja HMAC
    N->>N: Aktualizacja counter
    N->>GW: ACK
    GW->>A: Wynik operacji
```

### Rotacja kluczy

```mermaid
sequenceDiagram
    participant A as Administrator
    participant GW as Raspberry Pi Gateway
    participant N as Node STM32
    participant TPM as TPM

    A->>GW: Uruchomienie rotacji kluczy
    GW->>N: KEY_ROTATE
    GW->>TPM: Operacje DH / materiał kluczowy
    N->>TPM: Operacje DH / materiał kluczowy
    GW->>N: Wymiana danych
    N->>GW: Odpowiedź
    N->>GW: ACK
    GW->>A: Wynik operacji
```
