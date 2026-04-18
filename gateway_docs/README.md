# Gateway Docs

Ten folder opisuje kontrakt komunikacyjny miedzy gatewayem Raspberry Pi a firmware STM32 w tym repo.

## 1. Rola gatewaya

Gateway:

- wystawia siec radiowa dla node'ow,
- inicjuje parowanie sieciowe,
- utrzymuje stan zaufania dla kazdego node'a,
- wysyla `DATA`, `CFG`, `COUNTER_SYNC`, `KEY_ROTATE`,
- oczekuje `ACK` dla unicastu z flaga `ACK_REQUIRED`,
- prowadzi retry, timeout i logowanie zdarzen.

Node STM32:

- nie inicjuje parowania sieciowego,
- po wejsciu w `Pair with network` przechodzi tylko w nasluch,
- akceptuje `PAIR_REQ` tylko od `LAVIET_GATEWAY_ID=0x0001`,
- po lokalnym potwierdzeniu odsyla `PAIR_RESP`,
- po sparowaniu prowadzi normalna komunikacje unicast z gatewayem.

## 2. Adresacja

- `LAVIET_GATEWAY_ID = 0x0001`
- `LAVIET_BROADCAST_ID = 0xFFFF`
- `0x0000` jest niewazne
- node ma 16-bit `node_id` wyprowadzony z UID STM32

## 3. Format ramki

Kolejnosc pol na laczu:

`ver_type | flags | src_id | dst_id | msg_id | counter | payload_len | payload | mac_tag`

Stale:

- `LAVIET_FRAME_VERSION = 1`
- `LAVIET_MAX_PAYLOAD = 16`
- `LAVIET_MAC_TAG_LEN = 32`
- `LAVIET_FRAME_MIN_LEN = 45`
- `LAVIET_FRAME_MAX_LEN = 61`

Wszystkie pola wielobajtowe sa kodowane big-endian.

## 4. Typy ramek

- `DATA = 1`
- `ACK = 2`
- `RESP = 3`
- `PAIR_REQ = 4`
- `PAIR_RESP = 5`
- `CFG = 6`
- `COUNTER_SYNC = 7`
- `KEY_ROTATE = 8`
- `ERROR = 9`

## 5. Zachowanie pairingu sieciowego

### 5.1. Co robi node

Po wybraniu `Pair with network` node:

1. przechodzi na 5 min w tryb nasluchu,
2. nie wysyla zadnego `PAIR_REQ`,
3. czeka na `PAIR_REQ` od gatewaya,
4. po odebraniu pokazuje prosbe o lokalne potwierdzenie,
5. po akceptacji zapisuje relacje i wysyla `PAIR_RESP` do gatewaya.

### 5.2. Co musi zrobic gateway

Gateway inicjuje caly flow:

1. operator uruchamia pairing w panelu gatewaya,
2. gateway buduje `PAIR_REQ`,
3. gateway wysyla `PAIR_REQ` do noda,
4. node odsyla `PAIR_RESP`,
5. gateway zapisuje zaufanie i moze zaczac zwykla komunikacje unicast.

### 5.3. Adresowanie `PAIR_REQ`

Sa dwa poprawne tryby:

- `broadcast`, gdy gateway nie zna jeszcze `node_id`,
- `unicast`, gdy `node_id` jest juz znane z produkcji, etykiety lub innego kanalu.

Dla nieznanego noda rekomendowany jest `broadcast`.

### 5.4. Przykadowe ramki pairingu

Najczestsze bledy po stronie gatewaya:

- zly `flags`,
- zly `domain_id` przy wyprowadzaniu klucza HMAC,
- ustawienie `ACK_REQUIRED` dla `PAIR_REQ`,
- ustawienie `ENCRYPTED` dla `PAIR_REQ / PAIR_RESP`,
- payload inny niz dokladnie `8 B`,
- `src_id != 0x0001` przy network pairingu.

#### Przyklad A: `PAIR_REQ` broadcast od gatewaya

To jest najprostszy i rekomendowany bootstrap, gdy gateway nie zna jeszcze `node_id`.

Pola:

- `ver_type = 0x14` -> wersja `1`, typ `PAIR_REQ=4`
- `flags = 0x28` -> `PAIRING | BROADCAST`
- `src_id = 0x0001`
- `dst_id = 0xFFFF`
- `msg_id = 0x1001`
- `counter = 0x00000001`
- `payload_len = 0x08`
- `payload = "12345678"` -> `31 32 33 34 35 36 37 38`
- `mac_tag = HMAC-SHA256(...)`

W pairingu firmware nie ustawia `ENCRYPTED`, wiec payload idzie jawnie.

Klucz HMAC dla tego przykladu:

- `base_key = "LAVIET_SHARED_V1"` jako 16 B ASCII
- `domain_id = 0xFFFF` dla broadcastu
- `info = 4C 56 31 4B FF FF 00 02`
- `hmac_key = HMAC_SHA256(base_key, info)`

Wynik dla tego konkretnego przykladu:

```text
hmac_key =
3D 4D BF F2 87 A7 37 A7 BE 99 F1 D3 D9 01 EF F2
A8 F7 95 08 46 03 C4 54 9B D2 20 C6 09 F1 33 D6

mac_input =
14 28 00 01 FF FF 10 01 00 00 00 01 08 31 32 33 34 35 36 37 38

mac_tag =
27 E0 22 94 07 53 56 60 F6 F0 24 9E B9 B3 56 0C
15 68 A2 75 20 26 6A A0 6F 15 C1 66 79 59 DD F9

full_frame =
14 28 00 01 FF FF 10 01 00 00 00 01 08 31 32 33
34 35 36 37 38 27 E0 22 94 07 53 56 60 F6 F0 24
9E B9 B3 56 0C 15 68 A2 75 20 26 6A A0 6F 15 C1
66 79 59 DD F9
```

#### Przyklad B: `PAIR_RESP` unicast od node'a do gatewaya

Po lokalnej akceptacji node odsyla `PAIR_RESP` do gatewaya.
Payload to nadal ten sam 8-bajtowy kod parowania.

Pola:

- `ver_type = 0x15` -> wersja `1`, typ `PAIR_RESP=5`
- `flags = 0x08` -> tylko `PAIRING`
- `src_id = 0x1234`
- `dst_id = 0x0001`
- `msg_id = 0x0001`
- `counter = 0x00000001`
- `payload_len = 0x08`
- `payload = "12345678"` -> `31 32 33 34 35 36 37 38`

Tu bardzo latwo o blad:

- to nie jest broadcast,
- `domain_id` dla HMAC nie wynosi `0xFFFF`,
- dla unicastu jest `min(local_id, peer_id)`.

Dla `src_id=0x1234` i `dst_id=0x0001`:

- `domain_id = 0x0001`
- `info = 4C 56 31 4B 00 01 00 02`

Wynik dla tego konkretnego przykladu:

```text
hmac_key =
6F 05 6F B1 12 85 84 24 40 20 B4 19 59 51 09 A2
E5 A7 70 19 C6 54 DF 31 C8 AA 38 63 8E C4 85 A1

mac_input =
15 08 12 34 00 01 00 01 00 00 00 01 08 31 32 33 34 35 36 37 38

mac_tag =
4B E3 1E FB 7F 06 5F 82 35 B3 42 45 0E EE A8 D5
81 7F 84 8D 76 0C C9 95 A7 BF A3 B5 5C 69 54 1A

full_frame =
15 08 12 34 00 01 00 01 00 00 00 01 08 31 32 33
34 35 36 37 38 4B E3 1E FB 7F 06 5F 82 35 B3 42
45 0E EE A8 D5 81 7F 84 8D 76 0C C9 95 A7 BF A3
B5 5C 69 54 1A
```

#### Przyklad C: `PAIR_REQ` unicast od gatewaya do znanego `node_id`

Jesli gateway zna juz `node_id`, moze wyslac `PAIR_REQ` jako unicast.

Wtedy:

- `dst_id = node_id`, nie `0xFFFF`,
- `flags = 0x08`, bez `BROADCAST`,
- `domain_id = min(0x0001, node_id)`, zwykle `0x0001`,
- nadal bez `ACK_REQUIRED`,
- nadal bez `ENCRYPTED`.

To jest najczestszy powod odrzucenia ramek: gateway wysyla unicast, ale dalej liczy HMAC jak dla broadcastu.

## 6. Minimalne flow radiowe

### 6.1. Pairing

1. `GW -> BC/NODE : PAIR_REQ`
2. `NODE -> user : lokalna prosba o potwierdzenie`
3. `NODE -> GW : PAIR_RESP`
4. `GW : zapis trusted relation`

### 6.2. Zwykla wiadomosc

1. `GW -> NODE : DATA` z `ACK_REQUIRED`
2. `NODE : walidacja, decrypt, wyswietlenie`
3. `NODE -> GW : ACK`

### 6.3. Counter sync

1. `GW -> NODE : COUNTER_SYNC` z `COUNTER_OVERRIDE`
2. `NODE : aktualizacja licznika po HMAC`
3. `NODE -> GW : ACK`

## 7. Klucze i zgodnosc z obecnym firmware

Gateway musi odwzorowac aktualna logike firmware:

### 7.1. Ruch bootstrap / pairing

Do `PAIR_REQ`, `PAIR_RESP`, `ERROR` oraz broadcastu firmware uzywa wspolnego klucza bazowego:

`"LAVIET_SHARED_V1"` jako 16 bajtow ASCII.

Z tego klucza wyprowadzane sa:

- klucz AES-CTR,
- klucz HMAC-SHA256,

przez HMAC-SHA256 nad `LV1K || domain_id || 0x00 || selector`,
gdzie:

- `domain_id = 0xFFFF` dla broadcastu,
- `domain_id = min(local_id, peer_id)` dla unicastu,
- `selector = 1` dla AES,
- `selector = 2` dla HMAC.

### 7.2. Ruch po sparowaniu

Dla normalnego unicastu po sparowaniu firmware uzywa klucza linku wyprowadzanego z:

- `local_node_id`,
- `peer_node_id`,
- 8-bajtowego kodu pairingowego.

Algorytm zgodnosci jest obecnie zapisany w `Core/App/security_main.c`
funkcja `security_peer_link_key_derive(...)`.

Gateway musi odtworzyc ten sam algorytm 1:1, inaczej:

- `DATA`,
- `ACK`,
- `CFG`,
- `COUNTER_SYNC`,
- `KEY_ROTATE`

nie przejda walidacji HMAC po obu stronach.

## 8. ACK i timeout

Node odsyla `ACK` tylko dla poprawnej ramki unicast, ktora:

- jest skierowana do jego `node_id`,
- ma ustawione `ACK_REQUIRED`,
- nie jest sama `ACK`.

Payload `ACK` ma 6 bajtow:

`acked_msg_id(2) || acked_counter(4)`

Gateway powinien:

- utrzymywac timeout oczekiwania,
- wykonac retransmisje po timeout,
- mapowac `ACK` po `acked_msg_id + acked_counter`,
- odrzucac opoznione `ACK` po zamknieciu okna retry.

## 9. Rekomendowany stan po stronie gatewaya

Dla kazdego node'a gateway powinien przechowywac:

- `node_id`,
- `trusted`,
- `pair_code[8]`,
- `tx_counter`,
- `rx_counter`,
- timestamp ostatniego `ACK`,
- licznik retry,
- ostatni profil radiowy.

## 10. Co nie jest jeszcze w tym repo

W tym repo nie ma implementacji gatewaya.
Folder `gateway_docs` opisuje tylko kontrakt i rekomendowany kierunek.

## 11. Zrodla techniczne

- Semtech SX1276 product page i datasheet:
  https://www.semtech.com/products/wireless-rf/lora-transceivers/sx1276
- Raspberry Pi docs, wlaczenie SPI przez `dtparam=spi=on`:
  https://www.raspberrypi.com/documentation/computers/configuration.html
- RadioLib docs (`SX127x`, `PiHal`, `startReceive`, `startTransmit`):
  https://jgromes.github.io/RadioLib/
