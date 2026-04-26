# LAVIET Frame And Crypto Spec

Ten dokument opisuje techniczny kontrakt radiowy dla gatewaya, ktory ma rozmawiac
z aktualnym firmware STM32 z tego repo.

To jest spec ramki i kryptografii po stronie nodu. Jezeli gateway jest innym
projektem niz `gateway/`, powinien odwzorowac to 1:1.

## 1. Zakres

Spec obejmuje:

- format ramki na laczu,
- kodowanie pol,
- znaczenie flag i typow,
- wyprowadzanie kluczy,
- szyfrowanie AES-CTR,
- liczenie HMAC-SHA256,
- kolejnosc operacji TX i RX,
- zachowanie pairingu,
- najczestsze bledy zgodnosci.

## 2. Stale protokolu

- `LAVIET_FRAME_VERSION = 1`
- `LAVIET_GATEWAY_ID = 0x0001`
- `LAVIET_BROADCAST_ID = 0xFFFF`
- `0x0000` jest niewazne jako `src_id` i `dst_id`; sluzy jako sentinel dla braku adresu / blednej inicjalizacji
- `0x0002..0xFFFE` to zakres node'ow, czyli 65 533 adresy
- `LAVIET_MAX_PAYLOAD = 16`
- `LAVIET_MAC_TAG_LEN = 32`
- `LAVIET_FRAME_HEADER_LEN = 13`
- `LAVIET_FRAME_MIN_LEN = 45`
- `LAVIET_FRAME_MAX_LEN = 61`

Wszystkie pola wielobajtowe sa big-endian.

## 3. Layout ramki

Kolejnosc bajtow na laczu:

```text
ver_type | flags | src_id | dst_id | msg_id | counter | payload_len | payload | mac_tag
```

Naglowek ma staly format:

```text
>BBHHHIB
```

czyli:

1. `ver_type`      1 B
2. `flags`         1 B
3. `src_id`        2 B
4. `dst_id`        2 B
5. `msg_id`        2 B
6. `counter`       4 B
7. `payload_len`   1 B

Potem:

- `payload`        `0..16 B`
- `mac_tag`        `32 B`

## 4. `ver_type`

`ver_type` sklada sie z:

- high nibble: wersja protokolu,
- low nibble: typ ramki.

Wzor:

```text
ver_type = ((version & 0x0F) << 4) | (type & 0x0F)
```

Przyklad:

- `DATA=1`  -> `0x11`
- `ACK=2`   -> `0x12`
- `PAIR_REQ=4`  -> `0x14`
- `PAIR_RESP=5` -> `0x15`

## 5. Typy ramek

- `DATA = 1`
- `ACK = 2`
- `RESP = 3`
- `PAIR_REQ = 4`
- `PAIR_RESP = 5`
- `CFG = 6`
- `COUNTER_SYNC = 7`
- `KEY_ROTATE = 8`
- `ERROR = 9`

## 6. Flagi

Mapa bitowa `flags`:

- `ENCRYPTED        = 1 << 0`
- `ACK_REQUIRED     = 1 << 1`
- `IS_ACK           = 1 << 2`
- `PAIRING          = 1 << 3`
- `CONFIG_ACCESS    = 1 << 4`
- `BROADCAST        = 1 << 5`
- `COUNTER_OVERRIDE = 1 << 6`
- `KEY_UPDATE       = 1 << 7`

## 7. Zasady walidacji typu i flag

### 7.1. `DATA`

- payload `1..16 B`
- unicast zwykle z `ACK_REQUIRED`
- broadcast bez `ACK_REQUIRED`
- jezeli plaintext po `strip()` konczy sie na `.`, `?` albo `!`, node pokazuje mozliwosc odpowiedzi `YES` / `OK` / `NO` takze dla broadcastu
- szyfrowanie:
  - unicast po sparowaniu: tak
  - broadcast: opcjonalnie, gdy ustawione jest `ENCRYPTED`; produkcyjny model klucza grupy i rotacji opisuje `../BROADCAST_SECURITY_REKEY.md`

### 7.2. `ACK`

- payload dokladnie `6 B`
- payload:
  - `acked_msg_id(2)`
  - `acked_counter(4)`
- musi miec `IS_ACK`
- nie moze miec `ENCRYPTED`
- nie moze miec `ACK_REQUIRED`

### 7.3. `RESP`

- payload `0..16 B`
- dla odpowiedzi uzytkownika payload to ASCII `YES`, `OK` albo `NO`
- `RESP` z odpowiedzia na broadcast jest wysylany jako unicast do gatewaya
- unicast moze miec `ACK_REQUIRED`
- po sparowaniu zwykle szyfrowany

### 7.4. `PAIR_REQ` / `PAIR_RESP`

- payload dokladnie `8 B`
- payload to jawny `code[8]`
- musza miec `PAIRING`
- nie moga miec `ENCRYPTED`
- nie moga miec `ACK_REQUIRED`

### 7.5. `CFG`

- payload `2..16 B`
- musi miec `CONFIG_ACCESS`
- szyfrowany

### 7.6. `COUNTER_SYNC`

- payload dokladnie `4 B`
- musi miec `COUNTER_OVERRIDE`
- szyfrowany
- node akceptuje tylko od `LAVIET_GATEWAY_ID`

### 7.7. `KEY_ROTATE`

- payload `1..16 B`
- musi miec `KEY_UPDATE`
- szyfrowany

### 7.8. `ERROR`

- payload dokladnie `8 B`
- obecnie traktowany jako ruch bootstrap/shared-key

## 8. Klucze - dwa poziomy

W protokole sa dwa poziomy kluczy:

1. `base_key`
2. z niego wyprowadzane:
   - `aes_key`
   - `hmac_key`

## 9. `base_key` dla ruchu bootstrap/shared

Do:

- `PAIR_REQ`
- `PAIR_RESP`
- `ERROR`
- broadcastu

uzywany jest wspolny klucz bazowy:

```text
base_key = b"LAVIET_SHARED_V1"
```

To jest 16 B ASCII.

## 10. `base_key` dla ruchu po sparowaniu

Dla normalnego unicastu po sparowaniu gateway i node musza wyprowadzic ten sam
klucz linku z kodu pairingowego.

### 10.1. Docelowy algorytm zgodny z aktualnym firmware

```text
pair_base_key = HMAC_SHA256(
    key  = code[8],
    data = b"SEC:PAIR:V1" ||
           be32(min(local_id, peer_id)) ||
           be32(max(local_id, peer_id))
)[:16]
```

To jest tryb, ktory w firmware odpowiada:

- `PAIR_V1_32`

### 10.2. Tryb kompatybilnosci

Aktualny node na RX od gatewaya probuje tez trybu:

```text
pair_base_key_v1_16 = HMAC_SHA256(
    key  = code[8],
    data = b"SEC:PAIR:V1" ||
           be16(min(local_id, peer_id)) ||
           be16(max(local_id, peer_id))
)[:16]
```

To jest tryb:

- `PAIR_V1_16`

Nowy gateway nie powinien go wybierac jako glowny wariant.
Nowy gateway powinien nadawac zgodnie z `PAIR_V1_32`.

### 10.3. Fallback kompatybilnosci

Aktualny node probuje przy odbiorze od gatewaya kolejno:

1. ostatnio zapamietany tryb gatewaya,
2. `PAIR_V1_32`,
3. `PAIR_V1_16`,
4. `SHARED`

To jest tylko mechanizm zgodnosci ze starszymi gatewayami.
Nie nalezy go traktowac jako docelowego projektu nowego gatewaya.

## 11. Wyprowadzanie `aes_key` i `hmac_key`

Z `base_key` wyprowadzane sa dwa subklucze przez HMAC-SHA256.

### 11.1. `domain_id`

```text
domain_id = 0xFFFF                dla broadcastu
domain_id = min(local_id, peer_id) dla unicastu
```

### 11.2. Wzor

```text
info = b"LV1K" || be16(domain_id) || b"\x00" || selector
```

gdzie:

- `selector = 0x01` dla AES
- `selector = 0x02` dla HMAC

Czyli:

```text
aes_key  = HMAC_SHA256(base_key, b"LV1K" || be16(domain_id) || b"\x00\x01")[:16]
hmac_key = HMAC_SHA256(base_key, b"LV1K" || be16(domain_id) || b"\x00\x02")
```

`hmac_key` ma 32 B.
`aes_key` ma 16 B.

## 12. AES-CTR

Szyfrowany jest tylko `payload`.
Naglowek i `mac_tag` nie sa szyfrowane.

### 12.1. Nonce / counter block

Firmware buduje 16-bajtowy blok:

```text
b"LV1\x00" ||
be16(src_id) ||
be16(dst_id) ||
be16(msg_id) ||
be32(counter) ||
be16(block_index)
```

czyli:

- `4 B`  domena `LV1\0`
- `2 B`  `src_id`
- `2 B`  `dst_id`
- `2 B`  `msg_id`
- `4 B`  `counter`
- `2 B`  numer bloku AES-CTR

W implementacji Python `PyCryptodome` rownowazne jest:

```text
prefix = b"LV1\x00" || be16(src_id) || be16(dst_id) || be16(msg_id) || be32(counter)
counter_len = 16 bit
initial_value = 0
big-endian
```

### 12.2. Co to znaczy praktycznie

Jezeli gateway policzy HMAC poprawnie, ale uzyje innego nonce do AES-CTR, node:

- przyjmie HMAC,
- ale plaintext po decrypt bedzie smieciem.

Najczestsze powody:

- inne `msg_id` w AES niz w headerze,
- inne `counter` w AES niz w headerze,
- zamienione `src_id` i `dst_id`,
- inny endian,
- zly licznik blokow AES.

## 13. HMAC

### 13.1. Wejscie do HMAC

HMAC jest liczony po:

```text
ver_type || flags || src_id || dst_id || msg_id || counter || payload_len || payload
```

To jest dokladnie:

```text
header_bytes + payload_bytes
```

### 13.2. Bardzo wazne

Jezeli `ENCRYPTED=1`, to `payload` uzyty do HMAC jest ciphertextem, nie plaintextem.

Czyli kolejnosc jest taka:

1. zbuduj naglowek,
2. jesli trzeba, zaszyfruj payload,
3. policz HMAC po `header + encrypted_payload`,
4. dopiero wtedy dolacz `mac_tag`.

### 13.3. Wzor

```text
mac_input = header_bytes || payload_bytes
mac_tag   = HMAC_SHA256(hmac_key, mac_input)
```

`mac_tag` ma zawsze 32 B.

## 14. Kolejnosc budowy ramki TX

Gateway powinien robic to dokladnie w tej kolejnosci:

1. wybierz `type`
2. wybierz `src_id`, `dst_id`
3. wybierz `msg_id`
4. wybierz `counter`
5. ustaw `flags`
6. ustaw `payload_len`
7. wpisz plaintext do `payload`
8. wyprowadz `base_key`
9. wyprowadz `aes_key`, `hmac_key`
10. jesli `ENCRYPTED=1`, zaszyfruj `payload`
11. policz `mac_tag` po `header + payload`
12. zakoduj wszystko jako:
    `header || payload || mac_tag`

## 15. Kolejnosc odbioru RX po stronie nodu

Node robi to tak:

1. dekoduje ramke bez odszyfrowywania
2. wykonuje tania walidacje formatu i flag
3. dobiera kandydatow klucza
4. liczy oczekiwany HMAC
5. porownuje `expected_mac` z `frame.mac_tag`
6. dopiero po poprawnym HMAC wykonuje AES-CTR decrypt
7. potem sprawdza typ, `dst_id`, payload i logike aplikacyjna

Czyli:

- zly HMAC = brak decrypt
- brak zgodnego klucza = brak decrypt

## 16. Jak gateway ma liczyc klucze dla poszczegolnych klas ruchu

### 16.1. `PAIR_REQ` broadcast

- `base_key = LAVIET_SHARED_V1`
- `domain_id = 0xFFFF`
- `ENCRYPTED = 0`
- `PAIRING = 1`
- `BROADCAST = 1`

### 16.2. `PAIR_REQ` unicast

- `base_key = LAVIET_SHARED_V1`
- `domain_id = min(0x0001, node_id)` zwykle `0x0001`
- `ENCRYPTED = 0`
- `PAIRING = 1`
- `BROADCAST = 0`

### 16.3. `PAIR_RESP`

- `base_key = LAVIET_SHARED_V1`
- `domain_id = min(node_id, 0x0001)` zwykle `0x0001`
- `ENCRYPTED = 0`
- `PAIRING = 1`

### 16.4. Zwykle `DATA/RESP/CFG/COUNTER_SYNC/KEY_ROTATE` po sparowaniu

- `base_key = pair_base_key` z kodu pairingowego
- `domain_id = min(0x0001, node_id)` zwykle `0x0001`
- `DATA` i `RESP` unicast zwykle z `ACK_REQUIRED`
- broadcast `DATA` w trybie V1/kompatybilnym uzywa `SHARED`; produkcyjnie powinien uzywac group key, ma flage `BROADCAST` i nie moze miec `ACK_REQUIRED`
- broadcast moze wymagac odpowiedzi uzytkownika, jesli plaintext konczy sie na `.`, `?` albo `!`; odpowiedz wraca jako unicast `RESP`

### 16.5. `ACK`

- po sparowaniu: uzywa tego samego trybu klucza co zwykly unicast
- `payload = acked_msg_id(2) || acked_counter(4)`
- `IS_ACK = 1`
- `ENCRYPTED = 0`

## 17. `msg_id` i `counter`

### 17.1. `msg_id`

- 16 bit
- musi byc dokladnie tym samym `msg_id`:
  - w naglowku,
  - w AES-CTR nonce,
  - w HMAC input

### 17.2. `counter`

- 32 bit
- musi byc dokladnie tym samym `counter`:
  - w naglowku,
  - w AES-CTR nonce,
  - w HMAC input

### 17.3. Uwaga o aktualnym firmware

Aktualny node nie blokuje juz twardo ramek z gatewaya tylko dlatego, ze
`counter <= last_counter`. Loguje to jako `REPLAY bypass`.

To jest mechanizm zgodnosci i diagnostyki, a nie docelowy model bezpieczenstwa.
Gateway nadal powinien prowadzic monotoniczny `counter`.

## 18. Pairing - co idzie po eterze

Podczas pairingu nie jest wysylany gotowy klucz HMAC ani gotowy klucz AES.

W payloadzie `PAIR_REQ` i `PAIR_RESP` idzie tylko:

```text
code[8]
```

Obie strony lokalnie wyprowadzaja z tego:

- `pair_base_key`
- potem `aes_key`
- potem `hmac_key`

## 19. ACK - co gateway musi zrobic

Dla unicastu z `ACK_REQUIRED` gateway powinien:

1. zapamietac `msg_id`
2. zapamietac `counter`
3. uruchomic timeout
4. po `ACK` sprawdzic:
   - `acked_msg_id`
   - `acked_counter`
5. uznac ramke za dostarczona tylko gdy oba pola pasuja

Aktualny backend gatewaya czeka ok. `100 ms` przed wyslaniem wlasnego `ACK`
do noda, zeby node zdazyl wrocic z TX/RX do nasluchu. To opoznienie nie jest
czescia formatu ramki, tylko zachowaniem runtime gatewaya.

## 20. Najczestsze bledy gatewaya

1. HMAC liczony po plaintext zamiast ciphertext
2. zly `domain_id`
3. zly `base_key`
4. wysylanie `PAIR_REQ` kluczem pair zamiast `LAVIET_SHARED_V1`
5. inne `msg_id` w AES niz w headerze
6. inne `counter` w AES niz w headerze
7. zly endian
8. `ACK_REQUIRED` ustawione dla broadcastu
9. `ENCRYPTED` ustawione dla `PAIR_REQ / PAIR_RESP / ACK`
10. payload > `16 B`
11. brak `PAIRING` przy `PAIR_REQ / PAIR_RESP`
12. uzycie `PAIR_V1_16` jako glowny tryb nowego gatewaya

## 21. Minimalny algorytm wysylki `DATA` po sparowaniu

Pseudokod:

```text
input:
  gateway_id = 0x0001
  node_id
  pair_code[8]
  payload[1..16]
  msg_id
  counter

pair_base_key = HMAC_SHA256(
  key  = pair_code,
  data = b"SEC:PAIR:V1" || be32(min(gateway_id, node_id)) || be32(max(gateway_id, node_id))
)[:16]

domain_id = min(gateway_id, node_id)

aes_key  = HMAC_SHA256(pair_base_key, b"LV1K" || be16(domain_id) || b"\x00\x01")[:16]
hmac_key = HMAC_SHA256(pair_base_key, b"LV1K" || be16(domain_id) || b"\x00\x02")

header = pack(
  ver_type=(1 << 4) | 1,
  flags=ENCRYPTED|ACK_REQUIRED,
  src_id=gateway_id,
  dst_id=node_id,
  msg_id=msg_id,
  counter=counter,
  payload_len=len(payload)
)

cipher_payload = AES_CTR(payload, aes_key, nonce_from(src_id, dst_id, msg_id, counter))
mac_tag = HMAC_SHA256(hmac_key, header || cipher_payload)
frame = header || cipher_payload || mac_tag
```

## 22. Co powinien robic nowy gateway

Nowy gateway, ktory ma byc zgodny z aktualnym node STM32, powinien:

- dla pairingu uzywac `SHARED`; dla broadcastu V1 dopuszcza `SHARED`, ale produkcyjnie uzyc group key z rotacja
- dla zwyklego unicastu po sparowaniu uzywac `PAIR_V1_32`
- liczyc HMAC po ciphertext
- szyfrowac tylko payload
- utrzymywac monotoniczny `counter`
- mapowac `ACK` po `acked_msg_id + acked_counter`

## 23. Co node zaakceptuje dzisiaj

Aktualny node na RX od gatewaya akceptuje:

- `PAIR_V1_32`
- `PAIR_V1_16`
- `SHARED`

ale tylko jako kompatybilnosc.

Jesli projektujesz nowy gateway od zera, wybieraj:

- `SHARED` dla bootstrap/pairing oraz tylko kompatybilnosciowego broadcastu
- `PAIR_V1_32` dla normalnego unicastu po sparowaniu
