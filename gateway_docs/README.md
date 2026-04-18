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

1. przechodzi na 60 s w tryb nasluchu,
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
