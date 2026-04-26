# BEKO Security Architecture

Ten dokument opisuje, jak firmware BEKO chroni komunikację radiową, sekrety lokalne, TPM, AES/HMAC, EEPROM, liczniki anti-replay i dostęp lokalny przez menu.

Dokument jest pisany z perspektywy aktualnego kodu firmware STM32 w katalogu `Core/App`. Jeżeli backend gatewaya albo frontend RPi zmieni protokół, ten plik należy zaktualizować razem z kodem.

## 1. Skrót

Najważniejsze decyzje bezpieczeństwa:

- TPM jest lokalnym root-of-trust dla noda.
- Root seed urządzenia jest przechowywany w TPM NV index `0x01C10101`.
- Zapis root seeda do głównego indexu wymaga TPM Physical Presence (`PPWRITE`).
- TPM PP jest podłączony bezpośrednio do TPM, pin `7`, aktywny stanem `VDD`.
- TPM komunikuje się z MCU przez `I2C3`, `PC0=SCL`, `PC1=SDA`.
- `TPM_RESET#` jest na `PB0`, aktywny niskim stanem.
- `TPM_DAVINT#` / `PIRQ` jest na `PH0`, aktywny niskim stanem.
- Sekrety w EEPROM są szyfrowane kluczem wyprowadzonym z root seeda TPM.
- Payload radiowy jest szyfrowany `AES-CTR`.
- Integralność i autentyczność ramki zapewnia `HMAC-SHA256`.
- HMAC obejmuje nagłówek i payload, czyli także `src_id`, `dst_id`, `msg_id` i `counter`.
- Odbiornik weryfikuje HMAC przed odszyfrowaniem payloadu.
- Porównanie MAC jest stałoczasowe względem długości tagu.
- Backend crypto używa sprzętowego AES/HASH STM32, jeżeli self-test przejdzie.
- Jeżeli hardware crypto nie przejdzie self-testu, firmware używa software fallback.
- Klucze i bufory tymczasowe są zerowane po użyciu przez `laviet_secure_zero()`.
- Dostęp do menu chronią role lokalne i PIN operator/admin.
- Logi TPM TIS można włączyć przez `TPM_INIT_LOG`; domyślnie verbose TPM init jest schowany.
- Logowanie wrażliwych payloadów na UART jest kontrolowane przez `PAGER_CONFIG_UART_SENSITIVE_LOGS`.

W uproszczeniu:

```text
TPM NV root seed
    |
    +-- HMAC(seed, "SEC:NET:ROOT")   -> lokalny network key
    |
    +-- HMAC(seed, "SEC:EEPROM:KEY") -> klucz szyfrowania sekretów EEPROM

Pair code + local_id + peer_id
    |
    +-- peer link base key
          |
          +-- HMAC(base, "LV1K" + domain + 0x01) -> AES-CTR enc key
          |
          +-- HMAC(base, "LV1K" + domain + 0x02) -> HMAC-SHA256 key
```

## 2. Cele bezpieczeństwa

System chroni cztery główne obszary:

1. Poufność wiadomości radiowych.
2. Integralność i autentyczność ramek.
3. Trwałe sekrety noda: root seed, trusted devices, pair code, countery.
4. Lokalny dostęp operatora/administratora.

System nie zakłada, że radio jest zaufane. Kanał radiowy jest traktowany jako publiczny:

- każdy może podsłuchać,
- każdy może wysłać bajty,
- każdy może powtórzyć starą transmisję,
- każdy może próbować floodować,
- pakiety mogą zginąć albo przyjść po czasie.

Dlatego bezpieczeństwo nie opiera się na LoRa/FSK/OOK jako takich. Radio jest tylko transportem. Ochrona jest w warstwie aplikacyjnej `LAVIET_FRAME_V1`.

## 3. Model atakującego

Zakładany atakujący może:

- słuchać kanału radiowego,
- wysyłać własne pakiety SX1276/RFM95,
- powtarzać stare ramki,
- próbować zgadnąć `src_id`,
- próbować podszyć się pod gateway albo node,
- odczytać zewnętrzny EEPROM po fizycznym dostępie do płytki,
- obserwować UART, jeżeli ma dostęp serwisowy,
- zresetować zasilanie w niekorzystnym momencie.

Zakładany atakujący nie powinien móc:

- odczytać sekretów z TPM bez obejścia zabezpieczeń TPM,
- zapisać nowego root seeda bez fizycznej obecności TPM PP,
- wygenerować poprawnego HMAC bez właściwego klucza,
- zmodyfikować zaszyfrowanej wiadomości tak, żeby przeszła HMAC,
- udawać sparowanego peer-a po zakończonym pairingu bez pair code.

Ważne: jeżeli atakujący ma pełny dostęp debug/SWD do działającego MCU i może czytać RAM, może próbować odczytywać klucze w czasie użycia. Ten projekt ogranicza czas życia sekretów w RAM, ale nie zastępuje pełnej ochrony debug portu, secure boot ani TrustZone policy.

## 4. Granice bezpieczeństwa

System ma kilka granic zaufania:

| Granica | Co jest zaufane | Co jest niezaufane |
|---|---|---|
| MCU firmware | kod firmware, jeśli flash/debug są kontrolowane | radio RX, payloady, zewnętrzny świat |
| TPM | root seed i operacje NV/PP | I2C jako transport, dopóki komenda nie przejdzie TPM |
| EEPROM | nośnik danych | zawartość EEPROM bez weryfikacji CRC i deszyfrowania |
| Radio | nic samo z siebie | każda ramka przed HMAC |
| Menu lokalne | świadoma interakcja użytkownika | przypadkowe naciśnięcia i nieuprawniony operator |

Najważniejsza zasada: żadna ramka radiowa nie powinna dostać się do logiki aplikacji jako zaufana, dopóki:

1. parser nie potwierdzi formatu,
2. HMAC nie pasuje,
3. polityka adresacji i typu nie jest poprawna,
4. counter/replay policy nie jest spełniona,
5. payload, jeżeli zaszyfrowany, nie zostanie odszyfrowany poprawnym kluczem.

## 5. Komponenty implementacji

Główne pliki:

| Plik | Rola |
|---|---|
| `Core/App/security_main.c` | TPM bootstrap, root seed, storage key, trusted devices, runtime security config |
| `Core/App/st33ktpm2x_lib/st33ktpm2x.c` | driver ST33KTPM2X/TIS over I2C, komendy TPM2 |
| `Core/App/laviet_crypto.c` | AES-CTR, HMAC-SHA256, hardware backend, fallback software, self-test |
| `Core/App/laviet_frame.c` | parser, serializer, walidacja `LAVIET_FRAME_V1`, input do HMAC |
| `Core/App/radio_main.c` | TX/RX ramek, ACK, pairing, countery, szyfrowanie i MAC |
| `Core/App/i2c_mem_store_lib/i2c_mem_store.c` | EEPROM storage, CRC, secret slots, XTEA-CTR dla małych rekordów |
| `Core/App/menu_main.c` | role lokalne, PIN operator/admin, dostęp do menu Security |
| `Core/App/pager_config.h` | flagi produkcyjne/logowania |

## 6. TPM jako root-of-trust

TPM jest używany po to, żeby główny sekret urządzenia nie był stałą w firmware ani zwykłym bajtem w EEPROM.

Obecny TPM:

- model/rodzina: ST33KTPM2X,
- adres I2C 7-bit: `0x2E`,
- magistrala: `I2C3`,
- linie: `PC0=SCL`, `PC1=SDA`,
- reset: `TPM_RESET#` na `PB0`, aktywny `GND`,
- interrupt/data available: `TPM_DAVINT#` na `PH0`, aktywny `GND`,
- physical presence: TPM pin `7`, aktywny `VDD`, podłączony do TPM, nie do GPIO MCU.

### 6.1. Dlaczego TPM jest bezpieczniejszy niż EEPROM

EEPROM jest tylko pamięcią. Jeśli ktoś fizycznie odczyta EEPROM, dostaje zapisane bajty.

TPM jest układem bezpieczeństwa:

- ma własne komendy i polityki autoryzacji,
- może wymagać platform auth albo physical presence,
- nie udostępnia NV write bez spełnienia atrybutów indexu,
- wykonuje `TPM2_GetRandom`,
- separuje trwały sekret od zwykłej pamięci aplikacji,
- pozwala przechowywać root seed poza firmware.

W tym projekcie TPM nie jest używany jako “magiczne szyfrowanie wszystkiego”. TPM jest korzeniem hierarchii kluczy. To oznacza, że z niego pochodzi materiał, który potem zasila klucze robocze.

## 7. TPM init

Podczas startu `security_main` wykonuje:

1. Konfigurację drivera TPM przez `st33ktpm2x_default_cfg(&cfg, &hi2c3)`.
2. Hard reset TPM przez `TPM_RESET#`.
3. Opcjonalny odczyt `DAVINT#`.
4. Skan/probe I2C3, jeśli `TPM_INIT_LOG` jest aktywny.
5. Odczyt identyfikacji TPM (`DID_VID`, `RID`).
6. `TPM2_Startup(ST_CLEAR)`.
7. `TPM2_SelfTest(full_test=false)`.
8. Ustawienie `s_tpm_ready=true`, jeśli transport i podstawowe komendy działają.

Przykładowe logi poprawnego startu:

```text
SEC: TPM ready DIDVID=0x0003104A RID=0x01 self_test_tpm_rc=0x0000090A
SEC: TPM NV read idx=0x01C10101 rc=0(OK) tpm_rc=0x00000000 len=8 nonzero=1
SEC: root seed loaded from TPM
SEC: MEM secret key derived from TPM root seed
```

`self_test_tpm_rc=0x0000090A` oznacza odpowiedź TPM na self-test. Firmware traktuje transport TPM jako gotowy, jeżeli komendy krytyczne, np. NV read, działają poprawnie. To dlatego root seed może zostać odczytany mimo diagnostycznego kodu z self-testu.

### 7.1. Verbose logi TPM

Pełne logi TIS/FIFO są ukryte za `TPM_INIT_LOG`.

Domyślnie nie są wypisywane:

```text
TPM TIS transceive start ...
TPM TIS request locality ...
TPM TIS data_avail ...
SEC: TPM reg8 DIDVID ...
```

Dlaczego to jest dobre:

- normalny UART nie jest zalewany danymi,
- mniej ryzyka przypadkowego wypisania informacji serwisowej,
- debug magistrali można włączyć tylko świadomie podczas diagnostyki.

## 8. Root seed

Root seed jest głównym trwałym sekretem noda.

Parametry:

| Parametr | Wartość |
|---|---|
| Rozmiar | `8 B` (`SECURITY_KEY_SEED_BYTES`) |
| Główny TPM NV index | `0x01C10101` |
| Legacy TPM NV index | `0x01C10100` |
| Atrybuty głównego indexu | `PPWRITE`, `OWNERREAD`, `NO_DA` |
| Zapis | wymaga TPM Physical Presence |
| Odczyt | owner read |

### 8.1. Dlaczego seed nie jest w firmware

Gdyby klucz był wpisany jako stała w kodzie:

- każdy egzemplarz miałby ten sam sekret,
- wyciek firmware ujawniłby sekret,
- nie byłoby sensownej rotacji,
- nie dałoby się związać sekretu z konkretną płytką.

W obecnym modelu firmware zna tylko algorytm:

```text
root seed -> HMAC labels -> klucze robocze
```

Sam root seed jest ładowany z TPM podczas startu.

### 8.2. Lifecycle root seeda

Przy starcie firmware wykonuje:

1. Próbuje odczytać `0x01C10101`.
2. Jeśli główny index nie ma danych, próbuje legacy index `0x01C10100`.
3. Jeśli legacy seed istnieje, firmware próbuje migrować go do PP-protected index.
4. Jeśli nie ma żadnego seeda, firmware generuje nowy seed.
5. Jeśli TPM jest gotowy, nowy seed jest zapisywany do TPM NV.
6. Zapis do `0x01C10101` wymaga aktywnego TPM PP.
7. Seed jest używany do wyprowadzenia kluczy.
8. Bufory tymczasowe z seedem są zerowane.

Przykład:

```text
SEC: TPM NV read idx=0x01C10101 rc=0(OK) tpm_rc=0x00000000 len=8 nonzero=1
SEC: root seed loaded from TPM
SEC: MEM secret key derived from TPM root seed
```

Przykład, gdy zapis wymaga fizycznej obecności:

```text
SEC: TPM NV write PP idx=0x01C10101 rc=7(ETPM_RC) tpm_rc=...
SEC: root seed write requires TPM PP button
```

### 8.3. Physical Presence

Physical Presence (`PP`) jest ważne, bo chroni operacje, które zmieniają root seeda.

W obecnym hardware:

- przycisk PP jest podłączony do TPM pin `7`,
- aktywny poziom to `VDD`,
- STM32 nie odczytuje PP jako GPIO,
- TPM sam egzekwuje warunek dla operacji `PPWRITE`.

To oznacza:

- firmware nie może “udawać” fizycznej obecności przez samo ustawienie GPIO,
- zdalny atak radiowy nie powinien móc nadpisać root seeda,
- operator musi fizycznie nacisnąć/aktywować PP podczas rotacji wymagającej zapisu.

### 8.4. Ograniczenie: seed ma 8 bajtów

Aktualny root seed ma `8 B`, czyli 64 bity.

To jest istotne ograniczenie. Architektura hierarchii kluczy jest poprawna, ale dla długoterminowego bezpieczeństwa produkcyjnego lepiej zwiększyć seed do `16 B` albo `32 B`.

Dlaczego mimo tego obecny model jest lepszy niż stały klucz:

- seed nie jest stałą firmware,
- seed może być inny per urządzenie,
- seed jest w TPM,
- zapis wymaga PP,
- klucze robocze są separowane labelami HMAC.

Co poprawić docelowo:

```text
SECURITY_KEY_SEED_BYTES: 8 -> 16 albo 32
TPM NV data_size:        8 -> 16 albo 32
test migracji EEPROM i TPM NV
```

## 9. Entropia i generowanie seeda

Firmware używa dwóch źródeł entropii:

1. Preferowane: `TPM2_GetRandom`.
2. Fallback: STM32 HAL RNG (`HAL_RNG_GenerateRandomNumber`).

Przepływ:

```text
security_get_entropy_bytes()
    if TPM ready:
        TPM2_GetRandom(len)
        if OK: return
        else: log, disable TPM ready for entropy path

    HAL_RNG_GenerateRandomNumber()
```

Dlaczego to jest bezpieczne:

- TPM RNG jest niezależny od MCU,
- HAL RNG daje fallback, gdy TPM random nie jest dostępny,
- seed nie jest deterministyczny,
- brak RNG powoduje błąd init/rotacji, a nie użycie stałego seeda.

## 10. Hierarchia kluczy

Firmware nie używa jednego klucza do wszystkiego.

Z root seeda powstają oddzielne domeny:

| Domena | Funkcja | Label |
|---|---|---|
| Network/root key | lokalny klucz sieciowy noda | `SEC:NET:ROOT` |
| EEPROM secret key | szyfrowanie secret slots EEPROM | `SEC:EEPROM:KEY` |

Implementacyjnie:

```text
network_key = HMAC-SHA256(seed, "SEC:NET:ROOT")[0..15]
store_key   = HMAC-SHA256(seed, "SEC:EEPROM:KEY")[0..15]
```

Dlaczego labelowanie jest bezpieczne:

- ten sam root seed nie jest używany bezpośrednio jako klucz AES,
- różne zastosowania dostają różne klucze,
- wyciek jednego klucza roboczego nie musi ujawniać drugiego,
- label utrudnia przypadkowe reuse materiału między protokołami.

Przykład:

```text
seed = 8 losowych bajtów z TPM

HMAC(seed, "SEC:NET:ROOT")
  -> digest 32 B
  -> pierwsze 16 B jako network_key

HMAC(seed, "SEC:EEPROM:KEY")
  -> digest 32 B
  -> pierwsze 16 B jako EEPROM secret key
```

## 11. Klucze ramek radiowych

Ramki radiowe nie używają bezpośrednio root seeda TPM. Używają kluczy ramek wyprowadzanych z trybu relacji peer.

Są dwa główne tryby:

| Tryb | Kiedy | Materiał bazowy |
|---|---|---|
| `SECURITY_FRAME_KEY_MODE_PAIR_V1_32` | zwykła komunikacja sparowany peer <-> node | pair code / trusted entry |
| `SECURITY_FRAME_KEY_MODE_SHARED` | pairing, error, broadcast/control fallback | stały shared frame root key |

### 11.1. Pair link key

Dla sparowanego urządzenia firmware pobiera zapisany `pair code` z trusted storage i wyprowadza z niego klucz relacji:

```text
lo = min(local_node_id, peer_node_id)
hi = max(local_node_id, peer_node_id)
base_key = HMAC-SHA256(pair_code, "SEC:PAIR:V1" || lo || hi)[0..15]
```

Relacja jest zależna od:

- lokalnego `node_id`,
- `peer_id`,
- pair code,
- trybu derivation.

Następnie powstają dwa oddzielne klucze:

```text
info = "LV1K" || domain_id || 0x00 || purpose

purpose=0x01 -> AES-CTR enc_key 16 B
purpose=0x02 -> HMAC-SHA256 hmac_key 32 B
```

`domain_id`:

- dla broadcastu: `0xFFFF`,
- dla unicastu: mniejszy z `local_id` i `peer_id`.

Dlaczego to jest bezpieczne:

- AES i HMAC nie używają tego samego klucza,
- klucze zależą od pary urządzeń,
- ten sam pair code nie daje identycznych kluczy dla każdej relacji, jeśli identyfikatory są inne,
- HMAC jako KDF daje separację domen.

### 11.2. Shared mode

`SECURITY_FRAME_KEY_MODE_SHARED` używa stałego root key:

```text
"LAVIET_SHARED_V1"
```

Ten tryb jest używany dla ramek, które muszą istnieć przed pełnym zaufaniem albo poza relacją pair:

- pairing,
- część ramek control/error,
- broadcast.

To nie daje tej samej siły co pair-specific key. Shared mode należy traktować jako tryb bootstrap/compatibility, a nie docelową ochronę poufnych danych użytkownika.

Bezpieczna zasada:

- zwykłe wiadomości i odpowiedzi powinny używać pair-derived keys,
- broadcast powinien nie przenosić sekretów,
- pairing powinien mieć lokalne potwierdzenie użytkownika,
- docelowo pairing powinien dostać challenge-response lub TPM policy.

## 12. Ramka `LAVIET_FRAME_V1`

Struktura:

```text
ver_type    1 B
flags       1 B
src_id      2 B
dst_id      2 B
msg_id      2 B
counter     4 B
payload_len 1 B
payload     0..16 B
mac_tag     32 B
```

Rozmiar:

```text
min = 45 B
max = 61 B
```

Adresacja:

| Adres | Znaczenie |
|---|---|
| `0x0000` | nieważny |
| `0x0001` | gateway |
| `0x0002..0xFFFE` | node |
| `0xFFFF` | broadcast |

Typy:

| Typ | Znaczenie |
|---|---|
| `DATA` | zwykła wiadomość |
| `ACK` | potwierdzenie dostarczenia |
| `RESP` | odpowiedź |
| `PAIR_REQ` | żądanie parowania |
| `PAIR_RESP` | odpowiedź parowania |
| `CFG` | konfiguracja |
| `COUNTER_SYNC` | synchronizacja countera |
| `KEY_ROTATE` | rotacja klucza |
| `ERROR` | błąd/protokół |

Flagi:

| Flaga | Znaczenie |
|---|---|
| `ENCRYPTED` | payload jest szyfrowany AES-CTR |
| `ACK_REQUIRED` | odbiorca ma odesłać ACK |
| `IS_ACK` | ramka jest ACK |
| `PAIRING` | ramka należy do pairingu |
| `CONFIG_ACCESS` | ramka konfiguracyjna |
| `BROADCAST` | dst to broadcast |
| `COUNTER_OVERRIDE` | specjalna zmiana countera |
| `KEY_UPDATE` | rotacja klucza |

## 13. Walidacja ramek

`laviet_frame_validate_plain()` odrzuca:

- złą wersję protokołu,
- nieznany typ,
- `src_id=0`,
- `src_id=0xFFFF`,
- `dst_id=0`,
- `msg_id=0`,
- payload dłuższy niż 16 B,
- broadcast bez flagi `BROADCAST`,
- broadcast z `ACK_REQUIRED`,
- niepoprawne kombinacje flag dla typu.

Przykład:

```text
DATA + IS_ACK -> błąd flag
ACK + payload_len != 6 -> błąd payload
COUNTER_SYNC bez COUNTER_OVERRIDE -> błąd flag
BROADCAST + ACK_REQUIRED -> błąd flag
```

Dlaczego to jest bezpieczne:

- tanie błędy są odrzucane przed kosztowną logiką,
- niepoprawne typy nie trafiają do handlerów,
- broadcast nie może wymusić ACK,
- `src_id` i `dst_id` mają ścisłą semantykę,
- payload ma twardy limit 16 B.

## 14. HMAC-SHA256

Każda ramka ma `mac_tag` 32 B.

MAC liczony jest po:

```text
ver_type ||
flags ||
src_id ||
dst_id ||
msg_id ||
counter ||
payload_len ||
payload
```

`mac_tag` nie jest częścią inputu do HMAC.

Dlaczego to jest bezpieczne:

- zmiana payloadu zmienia HMAC,
- zmiana adresu nadawcy zmienia HMAC,
- zmiana adresu odbiorcy zmienia HMAC,
- zmiana `msg_id` zmienia HMAC,
- zmiana `counter` zmienia HMAC,
- zmiana flag, np. dopisanie `ENCRYPTED`, zmienia HMAC.

Atakujący może wysłać losową ramkę, ale bez `hmac_key` nie powinien być w stanie policzyć poprawnego `mac_tag`.

### 14.1. Kolejność RX

W RX kolejność jest krytyczna:

```text
decode bytes -> validate plain -> derive keys -> verify HMAC -> decrypt -> handle
```

Payload jest odszyfrowywany dopiero po HMAC.

Dlaczego:

- AES-CTR jest strumieniowy i sam nie daje integralności,
- odszyfrowywanie losowych danych nie powinno sterować logiką,
- HMAC chroni ciphertext i nagłówek.

### 14.2. Stałoczasowe porównanie MAC

`laviet_mac_equal()` porównuje wszystkie 32 bajty i akumuluje różnice:

```c
diff |= a[i] ^ b[i];
```

Nie kończy po pierwszej różnicy.

Dlaczego:

- ogranicza leak timingowy mówiący, ile bajtów MAC było poprawnych,
- jest prostą i właściwą praktyką przy tagach MAC.

## 15. AES-CTR

Payload jest szyfrowany AES-CTR, gdy flaga `ENCRYPTED` jest ustawiona.

Właściwości AES-CTR:

- nie wymaga paddingu,
- działa dla payloadów 1..16 B,
- ten sam algorytm szyfruje i deszyfruje,
- ciphertext ma tę samą długość co plaintext,
- wymaga unikalnego counter block dla danego klucza.

### 15.1. Counter block AES-CTR

Counter block ma 16 B:

```text
0..2   "LV1"
3      0x00
4..5   src_id
6..7   dst_id
8..9   msg_id
10..13 frame.counter
14..15 block_index
```

Dla obecnego payloadu max 16 B `block_index` zwykle wynosi `0`.

Przykład:

```text
src_id  = 0x1234
dst_id  = 0x0001
msg_id  = 0x4567
counter = 0x89ABCDEF
block   = 0

counter block:
4C 56 31 00 12 34 00 01 45 67 89 AB CD EF 00 00
```

Dlaczego to jest bezpieczne:

- nonce/counter zależy od nadawcy, odbiorcy, msg_id i countera,
- powtórzenie tego samego counter block przy tym samym kluczu jest utrudnione przez monotoniczny counter i msg_id,
- HMAC obejmuje te same pola, więc atakujący nie może zmienić nonce bez uszkodzenia MAC.

### 15.2. AES-CTR nie jest autentykacją

AES-CTR sam nie chroni przed modyfikacją bitów.

Przykład problemu bez HMAC:

```text
plaintext  = "OK"
ciphertext = AES_CTR(plaintext)
atakujący zmienia bit ciphertextu
odbiorca dostaje zmieniony plaintext
```

W BEKO ten atak nie powinien przejść, bo HMAC jest liczony po zaszyfrowanym payloadzie i nagłówku. Zmieniony ciphertext da inny HMAC.

Dlatego bezpieczeństwo wiadomości to:

```text
Poufność:       AES-CTR
Integralność:   HMAC-SHA256
Autentyczność:  HMAC-SHA256 z kluczem relacji
Anti-replay:    counter policy
```

## 16. Hardware crypto STM32

`laviet_crypto_init()` sprawdza, czy:

- `hcryp.Instance == AES`,
- AES HAL jest `READY`,
- HASH HAL jest `READY`.

Potem wykonuje self-test:

1. HMAC software.
2. HMAC hardware.
3. Porównanie HMAC.
4. AES-CTR software dla 0 B.
5. AES-CTR hardware dla 0 B.
6. AES-CTR software dla 1 B.
7. AES-CTR hardware dla 1 B.
8. Porównanie 1 B.
9. AES-CTR software dla 16 B.
10. AES-CTR hardware dla 16 B.
11. Porównanie 16 B.
12. Roundtrip encrypt/decrypt.

Poprawny log:

```text
CRYPTO: AES pre_self_test inst=0x420c0000 state=1(READY) err=0x00000000 ...
CRYPTO: HASH pre_self_test state=1(READY) err=0x00000000 ...
CRYPTO: hardware backend ready
```

Jeżeli test nie przejdzie:

```text
CRYPTO: self-test mismatch stage=aes_1B_compare index=0 expected=0xAE actual=0x41 len=1
CRYPTO: hardware self-test failed, using software fallback
```

Firmware wtedy nadal działa, ale używa software fallback.

### 16.1. Dlaczego self-test jest ważny

AES/HASH HAL może być `READY`, ale wynik może być zły, np. przez:

- błędny `DataType`,
- złą endianowość key/IV/input,
- złą długość jednostki danych,
- błąd konfiguracji `.ioc`.

Self-test porównuje hardware z implementacją software i dopiero wtedy włącza `s_crypto_hw_ready`.

### 16.2. Pakowanie big-endian do AES

STM32 AES przyjmuje key/IV/data jako `uint32_t *`.

Firmware jawnie pakuje bajty protokołu do słów big-endian:

```text
bytes[0..3] -> word = b0<<24 | b1<<16 | b2<<8 | b3
```

Po AES wynik jest rozpakowywany z powrotem do bajtów.

Dlaczego:

- ramka LAVIET jest big-endian,
- AES test vector software działa na kolejności bajtowej protokołu,
- rzutowanie `uint8_t[16]` na `uint32_t *` dawało odwrócenie bajtów na little-endian MCU,
- jawne pakowanie usuwa niejednoznaczność.

## 17. Software fallback

Software fallback istnieje jako zabezpieczenie dostępności.

Jeżeli hardware AES/HASH:

- nie jest gotowy,
- nie przejdzie self-testu,
- zwróci błąd HAL,
- nie da się zablokować mutexa,

to firmware używa implementacji software.

Dlaczego to jest bezpieczne:

- system nie przechodzi na “brak crypto”,
- nadal liczy HMAC,
- nadal szyfruje AES-CTR,
- hardware path jest tylko akceleratorem, nie jedynym źródłem poprawności.

Ograniczenie:

- software AES/HMAC działa w RAM MCU i może być wolniejszy,
- przy pełnym modelu produkcyjnym trzeba też chronić debug/SWD i pamięć.

## 18. EEPROM secret storage

Zewnętrzny EEPROM jest traktowany jako niezaufany nośnik.

Przechowywane tam dane:

- metadata store,
- trusted devices,
- pair code,
- gateway counters,
- opcjonalne logi,
- runtime config.

Na M24C01-R pamięć ma tylko 128 B, więc obecnie:

```text
secret_area_bytes = 72 B
secret slot size  = 24 B
secret slots      = 3
log slots         = 0
```

Przy tym układzie slot `2` jest zarezerwowany na gateway counters, więc trwałe
trusted devices mają pojemność:

```text
trusted slot 0 = gateway
trusted slot 1 = dodatkowy node
```

Firmware nie powinien akceptować trusted device jako zapisanego, jeżeli wpis nie
został rzeczywiście utrwalony. Zapis trusted wykonuje write -> read-back ->
compare; dopiero po poprawnej weryfikacji wpis trafia do RAM jako zaufany.

Log:

```text
SEC: MEM store ready log_slots=0 secret_slots=3
SEC: MEM log disabled, storage reserved for trusted devices
```

### 18.1. Szyfrowanie secret slots

`i2c_mem_store` używa XTEA-CTR dla secret slots.

Klucz:

```text
store_key = HMAC(root_seed, "SEC:EEPROM:KEY")[0..15]
```

Slot zawiera:

```text
magic      2 B
length     1 B
slot_id    1 B
nonce      4 B
ciphertext 14 B
crc16      2 B
```

Dlaczego to jest bezpieczniejsze niż plaintext EEPROM:

- odczyt EEPROM nie ujawnia pair code bez store key,
- store key nie jest w EEPROM,
- store key pochodzi z TPM root seed,
- każdy zapis dostaje nonce z `secret_counter`,
- CRC wykrywa przypadkowe uszkodzenia i proste manipulacje formatu.

Ważne ograniczenie:

- CRC nie jest MAC.
- Integralność kryptograficzna EEPROM secret slots nie jest tak mocna jak HMAC.
- Docelowo warto dodać HMAC/AEAD dla rekordów EEPROM albo rozszerzyć format.

## 19. Trusted devices

Trusted entry zawiera:

```text
node_id
code_len
pair code
is_master
in_use
```

Slot `0` jest traktowany jako gateway/master slot.

Po sparowaniu:

1. pair code jest zapisany w trusted storage,
2. trusted storage trafia do encrypted secret slot EEPROM,
3. późniejsze ramki unicast używają pair-derived keys,
4. RX od nieznanego źródła jest odrzucany dla `DATA` i `RESP`.

Wyjątek: gateway (`src_id=0x0001`) może wysłać `DATA/RESP` jako broadcast
(`dst_id=0xFFFF`) po poprawnym HMAC w domenie broadcast/shared. Taka ramka nie
wymaga wpisu w trusted slotach, ale nie dostaje ACK i nie powinna przenosić
sekretów. Unicast `DATA/RESP` nadal wymaga sparowanego źródła.

Przykład RX spoza sieci:

```text
RADIO RX outside-network drop src=0x1234 dst=0x77CD type=DATA
```

Dlaczego to jest bezpieczne:

- samo zgadnięcie `src_id` nie wystarczy,
- trzeba mieć pair code, żeby wyprowadzić poprawny HMAC key,
- nawet po HMAC dodatkowo sprawdzana jest lista trusted dla zwykłych wiadomości.

## 20. Pairing

Pairing ma dwa warianty:

- zwykły pairing node-device,
- network pairing z gatewayem.

Ogólny przepływ:

```text
1. Lokalnie uruchamiasz tryb parowania.
2. Node otwiera okno czasowe.
3. Druga strona wysyła PAIR_REQ albo PAIR_RESP.
4. Firmware parsuje pair payload.
5. Użytkownik lokalnie akceptuje.
6. Pair code trafia do trusted storage.
7. Kolejne ramki używają pair-derived keys.
```

Dlaczego pairing nie jest tylko radiowy:

- wymaga aktywnego lokalnego okna,
- wymaga lokalnej akceptacji,
- dla gatewaya slot może być zablokowany po zapisaniu,
- RX spoza okna parowania jest ignorowany.

Ograniczenie:

- shared pairing key jest statyczny,
- pair code ma ograniczoną długość,
- docelowo pairing powinien dostać challenge-response i/lub TPM policy session.

## 21. ACK

ACK jest osobną ramką `LAVIET_TYPE_ACK`.

Payload ACK:

```text
acked_msg_id  2 B
acked_counter 4 B
```

Nadawca akceptuje ACK tylko, jeśli pasują:

- `src_id`,
- `msg_id`,
- `counter`.

Tu `msg_id` i `counter` oznaczają pola z payloadu ACK, czyli
`acked_msg_id` i `acked_counter`. Zewnętrzne pole `counter` samej ramki ACK jest
osobnym licznikiem TX nadawcy ACK i nie musi być równe `acked_counter`.

Przykład:

```text
RADIO ACK wait dst=0x0001 msg=0x0042 counter=123 timeout=6000 ms
RADIO RX OK type=ACK src=0x0001 dst=0x77CD msg=0x2F5E counter=456 flags=0x04 len=6
RADIO RX ACK src=0x0001 ack_msg=0x0042 ack_counter=123
RADIO ACK delivered src=0x0001 msg=0x0042 counter=123 retries=0
```

Dlaczego to jest bezpieczne:

- przypadkowy ACK nie zamyka pending TX,
- opóźniony ACK jest rozpoznawany jako late/duplicate,
- ACK nie jest generowany dla broadcastu,
- ACK nie jest szyfrowany, ale jest MACowany jak każda ramka.

## 22. Counter i anti-replay

Ramka ma 32-bitowy `counter`.

Firmware przechowuje:

```text
s_gateway_rx_counter
s_gateway_tx_counter
```

Gateway/backend powinien przechowywać analogiczne, rozdzielone wartości:

```text
gateway_tx_counter          ostatni counter użyty przez gateway w ramce do noda
node_rx_counter[node_id]    ostatni counter zaakceptowany od danego noda
```

`gateway_tx_counter` musi być trwały i monotoniczny. Backend nie powinien
zerować go przy restarcie procesu. Dla każdej ramki gateway -> node:

```text
counter = gateway_tx_counter + 1
persist gateway_tx_counter = counter
zbuduj nonce/AES/HMAC z tym counterem
wyślij ramkę
```

Dla ACK wysyłanego przez gateway zewnętrzny `frame.counter` też pochodzi z
`gateway_tx_counter`, natomiast payload ACK musi zawierać `msg_id` i `counter`
oryginalnej ramki noda:

```text
ack_payload = acked_msg_id || acked_counter
```

Przy odbiorze ACK od noda backend musi dopasowywać potwierdzenie po payloadzie
ACK (`acked_msg_id`, `acked_counter`), a nie po zewnętrznym `frame.counter`
ramki ACK.

W EEPROM slot counter ma:

```text
magic   0xC7
version 1
rx      4 B
tx      4 B
```

Dlaczego counter jest potrzebny:

- AES-CTR wymaga unikalnego counter block,
- HMAC chroni counter przed modyfikacją,
- odbiornik może wykryć stare ramki,
- ACK może być dopasowany do konkretnej transmisji.

Przykład replay:

```text
last gateway counter = 100
przychodzi ramka DATA z counter = 95
odbiornik wykrywa replay/stary counter
```

W obecnym kodzie istnieje diagnostyczna ścieżka `RADIO RX REPLAY bypass`, co jest zaznaczone w `todo.md` jako zadanie do przywrócenia finalnej polityki anti-replay. Docelowo ramki z `counter <= last_counter` powinny być odrzucane po poprawnym HMAC, przed wykonaniem akcji aplikacyjnej.

Bezpieczna docelowa polityka:

```text
if src == gateway and type != COUNTER_SYNC:
    if counter <= stored_rx_counter:
        drop as replay
    else:
        process
        persist new counter
```

## 23. `COUNTER_SYNC`

`COUNTER_SYNC` jest specjalną ramką gateway -> node.

Wymagania:

- `src_id == 0x0001`,
- typ `COUNTER_SYNC`,
- flaga `COUNTER_OVERRIDE`,
- payload 4 B,
- poprawny HMAC,
- payload zaszyfrowany.

Użycie:

```text
payload = new_counter_be32
```

Jeżeli backend utracił trwały `gateway_tx_counter` albo node zapamiętał wyższy
licznik niż backend, nie trzeba restartować firmware noda. Backend powinien
wysłać kontrolną ramkę `COUNTER_SYNC`:

```text
ver_type = 0x17                         # v1 + COUNTER_SYNC
flags    = 0x41                         # ENCRYPTED | COUNTER_OVERRIDE
flags    = 0x43                         # ENCRYPTED | ACK_REQUIRED | COUNTER_OVERRIDE, gdy backend chce ACK
src_id   = 0x0001
dst_id   = node_id
payload  = AES_CTR(new_counter_be32)
mac      = HMAC(header || encrypted_payload)
```

Po wysłaniu `COUNTER_SYNC` backend ustawia swój `gateway_tx_counter` na co
najmniej `new_counter`, a następna zwykła ramka gateway -> node musi mieć
`counter > new_counter`. `new_counter` nie powinien być mniejszy od wartości,
którą node mógł już zapamiętać; w praktyce wybiera się aktualny trwały licznik
backendu albo większą wartość z zapasem.

Dlaczego to jest bezpieczne:

- tylko gateway może wysłać poprawnie uwierzytelnioną synchronizację,
- counter sync nie jest zwykłą wiadomością,
- flaga `COUNTER_OVERRIDE` odróżnia operację od zwykłego DATA,
- HMAC chroni nową wartość countera.

Ryzyko:

- jeżeli gateway zostanie przejęty, może zsynchronizować counter.
- to jest akceptowany model: gateway jest centralnym zaufanym węzłem topologii gwiazdy.

## 24. Broadcast

Broadcast ma adres `0xFFFF`.

Zasady:

- broadcast nie wymaga ACK,
- broadcast nie może mieć `ACK_REQUIRED`,
- node nie wysyła zwykłych wiadomości na broadcast,
- broadcast może być używany przez gateway,
- gateway broadcast `DATA/RESP` jest akceptowany po poprawnym HMAC shared/broadcast nawet przed trusted-list,
- broadcast nie powinien przenosić sekretów.

Gateway z branchu `gateway` dla `coded=true` broadcast używa `broadcast_group_key`,
nie samego `LAVIET_SHARED_V1`. Node musi najpierw odebrać unicastowe
`KEY_ROTATE` od gatewaya:

```text
B7 01 epoch_be32 fragment_index fragment_count fragment[8]
B7 01 epoch_be32 fragment_index fragment_count fragment[8]
B7 02 epoch_be32 00 00 00 00 00 00 00 00 00 00
```

Po aktywacji node wyprowadza:

```text
broadcast_base_key = HMAC_SHA256(
    key  = broadcast_group_key[16],
    data = "LAVIET:BCAST:GROUP:V1" || epoch_be32 || 0xFFFF
)[0..15]

broadcast_aes_key  = HMAC_SHA256(broadcast_base_key, "LV1K" || 0xFFFF || 00 || 01)[0..15]
broadcast_hmac_key = HMAC_SHA256(broadcast_base_key, "LV1K" || 0xFFFF || 00 || 02)
```

Dla zaszyfrowanego broadcastu RX próbuje najpierw aktywny group key, a potem
fallback `SHARED`. Jeśli `KEY_ROTATE` nie dotarł albo nie został aktywowany,
`flags=0x21` (`ENCRYPTED | BROADCAST`) zakończy się `HMAC drop`.

Dlaczego broadcast jest słabszy:

- wiele urządzeń odbiera tę samą ramkę,
- nie ma per-node ACK,
- w praktyce używa shared/broadcast key domain,
- nie daje takiego samego potwierdzenia dostarczenia jak unicast.

Bezpieczne użycie:

```text
broadcast: krótkie komunikaty, wezwania, prośby o odpowiedź
unicast: dane wrażliwe, konfiguracja, ACK, odpowiedzi
```

## 25. Lokalny PIN i role

Menu ma role:

- operator,
- admin.

Domyślne PIN-y w kodzie:

```text
operator: 0000
admin:    9999
```

PIN:

- ma 4 cyfry,
- może być włączony/wyłączony,
- może zostać zmieniony w `Security -> PIN settings`,
- unlock trwa `60000 ms`,
- admin unlock odblokowuje też poziom operatora.

Dlaczego PIN jest użyteczny:

- ogranicza przypadkowe albo nieuprawnione wejście w menu,
- oddziela normalny tryb użytkownika od konfiguracji,
- wymaga świadomej lokalnej interakcji.

Ograniczenie:

- PIN w obecnym kodzie jest runtime/static, nie jest jeszcze trwale zapisany jako HMAC/TPM-protected secret.
- PIN nie jest głównym sekretem kryptograficznym.
- PIN nie zastępuje TPM PP.

Docelowo PIN powinien być zapisany jako:

```text
pin_hash = HMAC(TPM-derived local auth key, salt || pin)
```

## 26. Logi UART

Logi są potrzebne do uruchamiania embedded, ale mogą ujawniać za dużo.

Bezpieczne zasady:

- nie logować root seeda,
- nie logować pełnych kluczy,
- nie logować pair code w trybie produkcyjnym,
- debug HMAC/payload trzymać za flagą produkcyjną,
- TPM verbose logi tylko z `TPM_INIT_LOG`.

Aktualne mechanizmy:

- `TPM_INIT_LOG` ukrywa szczegółowe logi TPM TIS,
- `PAGER_CONFIG_UART_SENSITIVE_LOGS` kontroluje wrażliwe logi UART,
- startup pokazuje statusy, ale nie powinien drukować sekretów.

Przykład dobrego startupu:

```text
APP: FreeRTOS bootstrap
CRYPTO: hardware backend ready
SEC: TPM ready DIDVID=0x0003104A RID=0x01 self_test_tpm_rc=0x0000090A
SEC: TPM NV read idx=0x01C10101 rc=0(OK) tpm_rc=0x00000000 len=8 nonzero=1
SEC: root seed loaded from TPM
SEC: MEM secret key derived from TPM root seed
SEC: task ready
MENU: task ready
RADIO: init OK laviet_node=0x77CD
```

## 27. Dlaczego ta architektura jest bezpieczna

Bezpieczeństwo wynika z warstw, a nie z jednego mechanizmu.

### 27.1. Atak: podsłuch radiowy

Atakujący słyszy ciphertext.

Ochrona:

- payload jest szyfrowany AES-CTR,
- klucz AES jest pair-derived,
- counter block zawiera pola ramki,
- HMAC chroni nagłówek i ciphertext.

Efekt:

- podsłuch nie daje plaintextu,
- modyfikacja ciphertextu psuje HMAC.

### 27.2. Atak: zmiana payloadu

Atakujący zmienia bajt payloadu.

Ochrona:

- HMAC obejmuje payload,
- porównanie MAC wykrywa zmianę.

Efekt:

```text
RADIO RX HMAC drop src=...
```

### 27.3. Atak: zmiana adresu docelowego

Atakujący próbuje zmienić `dst_id`.

Ochrona:

- `dst_id` jest w HMAC input,
- zmiana `dst_id` psuje HMAC,
- parser sprawdza poprawność adresacji.

Efekt:

- ramka zostaje odrzucona.

### 27.4. Atak: fałszywy ACK

Atakujący wysyła ACK.

Ochrona:

- ACK też jest ramką z HMAC,
- ACK payload zawiera `acked_msg_id` i `acked_counter`,
- tracker wymaga zgodności `src_id`, `msg_id`, `counter`.

Efekt:

- losowy ACK nie zamyka transmisji.

### 27.5. Atak: odczyt EEPROM

Atakujący odczytuje zewnętrzny EEPROM.

Ochrona:

- secret slots są szyfrowane,
- klucz EEPROM pochodzi z TPM root seed,
- root seed nie jest w EEPROM,
- rekordy mają CRC.

Efekt:

- attacker widzi ciphertext i metadata, nie pair code wprost.

### 27.6. Atak: zdalna rotacja root seeda

Atakujący próbuje zdalnie wymusić zmianę root seeda.

Ochrona:

- zapis do TPM NV index `0x01C10101` wymaga `PPWRITE`,
- PP jest egzekwowane przez TPM,
- PP jest fizyczne, pin TPM `7`, aktywny `VDD`,
- STM32 nie może tego zastąpić zwykłą zmienną.

Efekt:

- bez fizycznej obecności zapis seeda powinien się nie udać.

### 27.7. Atak: błąd hardware AES/HASH

Problem:

- HAL może być `READY`, ale wynik AES może być błędny przez endianowość.

Ochrona:

- self-test porównuje hardware i software,
- niezgodność wyłącza hardware backend,
- fallback nadal wykonuje crypto.

Efekt:

- system nie używa cichego, błędnego AES.

### 27.8. Atak: przypadkowe wejście w menu admin

Ochrona:

- strony menu mają wymagany poziom auth,
- PIN operator/admin,
- unlock wygasa po czasie,
- użytkownik bez PIN może czytać/odpowiadać, ale nie konfigurować.

Efekt:

- konfiguracja nie jest dostępna z normalnego ekranu pracy.

## 28. Ograniczenia obecnego wdrożenia

To są punkty, których nie wolno ukrywać:

1. Root seed ma obecnie 8 B. Dla produkcji lepiej zwiększyć do 16/32 B.
2. Shared mode używa stałego firmware key i jest słabszy niż pair-specific mode.
3. Secret slots EEPROM używają CRC, nie kryptograficznego MAC.
4. Finalna polityka replay-drop jest oznaczona w TODO jako praca do domknięcia.
5. PIN nie jest jeszcze trwale zapisany jako TPM/HMAC-protected secret.
6. Brakuje pełnego secure boot / debug lock / TrustZone policy.
7. Pairing powinien docelowo dostać challenge-response albo TPM policy session.
8. `KEY_ROTATE` jako pełny radiowy protokół rotacji nie jest jeszcze domknięty.
9. Brakuje per-source rate limiting i abuse scoring dla flood/replay.
10. Broadcast ma niższy poziom pewności niż unicast.

Te ograniczenia nie unieważniają architektury. One pokazują, gdzie są granice obecnego poziomu bezpieczeństwa.

## 29. Zalecane utwardzenia

Najważniejsze następne kroki:

1. Zwiększyć `SECURITY_KEY_SEED_BYTES` do 16 albo 32.
2. Dodać HMAC/AEAD dla EEPROM secret slots.
3. Przywrócić finalny drop dla replay ramek gatewaya.
4. Dodać test replay attack.
5. Dodać test odtwarzania sekretów po reboocie.
6. Dodać test migracji `0x01C10100 -> 0x01C10101`.
7. Przenieść PIN do trwałego TPM-derived hash.
8. Dodać rate limiting per `src_id`.
9. Dodać abuse counters: bad HMAC, replay, malformed frame.
10. Domknąć `KEY_ROTATE` jako protokół radiowy.
11. Dodać policy/session TPM dla pairingu lub operacji admin.
12. Wyłączyć/maskować wrażliwe UART logi w buildzie produkcyjnym.
13. Zablokować debug/SWD w urządzeniach produkcyjnych.

## 30. Przykłady

### 30.1. Przykład: budowa zaszyfrowanej ramki DATA

Założenia:

```text
src_id  = 0x77CD
dst_id  = 0x0001
msg_id  = 0x0042
counter = 0x00000010
payload = "STS:OK"
```

Kroki:

```text
1. frame.payload = "STS:OK"
2. frame.flags = ACK_REQUIRED | ENCRYPTED
3. enc_key, hmac_key = derive_pair_keys(0x77CD, 0x0001, pair_code)
4. AES-CTR encrypt payload
5. HMAC(header || encrypted_payload)
6. encode frame
7. send raw bytes
8. wait for ACK(msg_id=0x0042, counter=0x00000010)
```

### 30.2. Przykład: odbiór zaszyfrowanej ramki

```text
1. radio RX bytes
2. laviet_frame_decode()
3. derive expected HMAC key
4. calculate HMAC over header || payload
5. constant-time compare with mac_tag
6. if HMAC OK: AES-CTR decrypt payload
7. if dst_id == local or broadcast: handle
8. if ACK_REQUIRED and unicast: send ACK
9. persist latest counter
```

### 30.3. Przykład: odrzucenie zmienionej ramki

```text
Atakujący zmienia dst_id:

original dst_id = 0x77CD
tampered dst_id = 0x1234

HMAC input zmienił się, ale mac_tag został stary.
Odbiornik liczy expected MAC i dostaje inną wartość.

Wynik:
RADIO RX HMAC drop src=...
```

### 30.4. Przykład: rotacja root seeda

```text
1. Admin wchodzi do Security -> Keys.
2. Firmware prosi o aktywne TPM PP.
3. Użytkownik trzyma przycisk PP.
4. Firmware generuje seed z TPM RNG albo HAL RNG.
5. Firmware zapisuje seed do NV index 0x01C10101.
6. TPM egzekwuje PPWRITE.
7. Firmware wyprowadza network_key i EEPROM key.
8. EEPROM secret store dostaje nowy klucz.
```

Bez PP:

```text
SEC: root seed write requires TPM PP button
```

### 30.5. Przykład: hardware crypto self-test

Poprawny:

```text
CRYPTO: AES pre_self_test inst=0x420c0000 state=1(READY) err=0x00000000
CRYPTO: HASH pre_self_test state=1(READY) err=0x00000000
CRYPTO: hardware backend ready
```

Błędna endianowość AES:

```text
CRYPTO: self-test mismatch stage=aes_1B_compare index=0 expected=0xAE actual=0x41 len=1
CRYPTO: hardware self-test failed, using software fallback
```

Po poprawce pakowania big-endian:

```text
CRYPTO: hardware backend ready
```

### 30.6. Przykład: EEPROM secret slot

Plain trusted payload:

```text
id_len   = 4
code_len = 8
id       = node_id as 4 B
code     = pair code 8 B
```

Po zapisie:

```text
magic      = C0 DE
len        = 14
slot       = 0
nonce      = monotonic secret_counter
ciphertext = XTEA-CTR(plain, store_key, nonce)
crc16      = CRC rekordu
```

Atakujący z EEPROM widzi ciphertext, nie pair code.

## 31. Checklist dla testów

Po zmianie security trzeba wykonać:

- boot z TPM podłączonym,
- boot bez aktywnego `TPM_INIT_LOG`,
- boot z `TPM_INIT_LOG`, jeśli diagnozujemy I2C,
- potwierdzenie `CRYPTO: hardware backend ready`,
- odczyt root seed z `0x01C10101`,
- reboot i ponowny odczyt tego samego stanu trusted/counter,
- re-pair gateway,
- `DATA -> ACK` gateway -> node,
- `RESP -> ACK` node -> gateway,
- broadcast RX bez ACK,
- próba ramki spoza trusted network,
- próba starego countera,
- próba złego HMAC,
- rotacja root seeda z PP,
- rotacja root seeda bez PP, oczekiwany błąd,
- test zaniku zasilania podczas zapisu EEPROM.

## 32. Najkrótsza odpowiedź “dlaczego to jest bezpieczne”

Bo sekret główny nie jest w firmware ani w EEPROM, tylko w TPM; zapis sekretu wymaga fizycznej obecności; klucze robocze są wyprowadzane z separacją domen; każda ramka jest uwierzytelniana HMAC po nagłówku i payloadzie; payload jest szyfrowany AES-CTR z nonce/counter zależnym od ramki; błędny hardware crypto jest wykrywany self-testem; EEPROM trzyma sekrety zaszyfrowane kluczem wyprowadzonym z TPM; a lokalne operacje konfiguracyjne są chronione PIN-em i menu role.

Nie jest to jednak koniec prac. Największe produkcyjne utwardzenia to: seed 16/32 B, replay-drop bez bypassu diagnostycznego, MAC/AEAD dla EEPROM secret slots, TPM policy dla pairingu/admin i trwały TPM-derived PIN hash.
