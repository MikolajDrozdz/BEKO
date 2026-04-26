# Raspberry Pi + SX1276/RFM95

Ten dokument opisuje wdrozenie gatewaya na Raspberry Pi z modulem SX1276/RFM95.
Aktualne repo ma juz backend FastAPI w `gateway/app` oraz wbudowany driver
`builtin-sx1276` w `gateway/app/services/lora_hardware.py`.

Uwaga: ten dokument dotyczy SX1276/RFM95. Nie opisuje SX1262/SX1762; te uklady
maja inny interfejs SPI i wymagalyby osobnego drivera.

## 1. Zalecana architektura

Rekomendowany podzial:

1. `radio-daemon`
2. `gateway-core`
3. `web-api`
4. `storage`

### 1.1. radio-daemon

Proces odpowiedzialny za:

- SPI,
- GPIO `DIO0`,
- reset ukladu,
- przechodzenie `RX <-> TX`,
- IRQ i bufor pakietow,
- rekonfiguracje LoRa/FSK/OOK.

To powinien byc pojedynczy proces z wlasna maszyna stanow.

### 1.2. gateway-core

Warstwa logiki protokolu:

- encode/decode `LAVIET_FRAME_V1`,
- HMAC/AES-CTR,
- pairing manager,
- trusted device registry,
- counter manager,
- ACK timeout i retry scheduler,
- logika `COUNTER_SYNC`.

### 1.3. web-api

Panel operatora/admina:

- wyslanie wiadomosci,
- uruchomienie pairingu,
- podglad `ACK`,
- podglad node'ow,
- reset licznika,
- logi.

Ta warstwa nie powinna dotykac SPI bezposrednio.

## 2. Zalecany stack

### Opcja preferowana

- `C++`
- `RadioLib`
- osobny serwis systemd
- lekki REST/WebSocket nad tym serwisem

Powod:

- mniejsze opoznienia,
- prostsza obsluga IRQ,
- latwiejsza zgodnosc z obecnym stylem `radio_main`,
- dobra kontrola retry i state machine.

### Opcja aktualnie zaimplementowana w tym repo

- Python/FastAPI jako panel, API i runtime gatewaya,
- wbudowany driver `builtin-sx1276` oparty o `spidev`,
- GPIO przez `lgpio` albo `RPi.GPIO`,
- opcjonalny zewnetrzny `radio_handle.py`, jesli istnieje w projekcie,
- runtime status radia pod `GET /api/radio/status`,
- metryki systemu pod `GET /api/system/metrics`.

Ta sciezka jest wygodna do testow i integracji panelu. Przy bardzo ciasnych
czasach RX/TX albo duzej liczbie nodow dalej warto rozwazyc osobny worker radiowy.

### Opcja alternatywna

- Python dla panelu i orkiestracji,
- osobny worker radiowy w C/C++,
- IPC przez Unix socket albo ZeroMQ.

Nie rekomenduje trzymania calej logiki radiowej w czystym Pythonie, jezeli ma obslugiwac:

- timeout ACK,
- szybkie `RX -> TX -> RX`,
- rownolegly panel www,
- duza ilosc logowania.

## 3. Dlaczego RadioLib

Na podstawie dokumentacji RadioLib wnioskuje, ze jest sensowna baza do wdrozenia na RPi, bo:

- dokumentacja obejmuje `SX127x`,
- dokumentacja obejmuje `PiHal`,
- `SX127x` ma `startReceive()` i `startTransmit()`.

To jest wniosek z dokumentacji biblioteki, nie z kodu tego repo.

## 4. Sprzet

Minimalny zestaw:

- Raspberry Pi 4 albo 5,
- modul SX1276 / RFM95 na 3.3 V,
- stabilne zasilanie,
- antena zgodna z pasmem,
- przewody krotkie, ekranowane gdy to mozliwe.

### 4.1. Polaczenia

Minimalne sygnaly:

- `MOSI`
- `MISO`
- `SCK`
- `NSS/CS`
- `RESET`
- `DIO0`
- `GND`
- `3V3`

Opcjonalnie:

- `DIO1`
- `DIO2`

### 4.2. Uwagi

- nie podawac 5 V na logike modulu,
- nie zasilac RFM95 z niestabilnego 3V3 przy dluzszych przewodach,
- dodac kondensatory blisko modulu,
- zadbac o poprawna mase RF i antene.

## 5. Konfiguracja Raspberry Pi

Zgodnie z oficjalna dokumentacja Raspberry Pi:

- wlaczyc SPI przez `dtparam=spi=on`,
- w razie potrzeby dobrac odpowiedni `dtoverlay`,
- zostawic radio pod jednym, stalym kontrolerem SPI.

Praktycznie:

1. wlaczyc SPI,
2. sprawdzic pojawienie sie `/dev/spidev*`,
3. przypisac `DIO0` i `RESET` do stalych GPIO,
4. uruchamiac serwis radiowy po starcie systemu.

## 6. Model runtime gatewaya

## 6.1. Stan noda

```text
UNKNOWN -> PAIRING_WAIT -> PAIRED -> ACTIVE
```

### `UNKNOWN`

- brak relacji zaufania,
- mozliwy tylko pairing bootstrap.

### `PAIRING_WAIT`

- gateway wyslal `PAIR_REQ`,
- czeka na `PAIR_RESP`.

### `PAIRED`

- gateway zapisuje kod parowania,
- wyprowadza klucze linku,
- resetuje lokalne retry.

### `ACTIVE`

- zwykla komunikacja `DATA/ACK`,
- `CFG`,
- `COUNTER_SYNC`,
- odczyt statusu.

## 6.2. Scheduler ACK

Dla kazdej wyslanej ramki unicast z `ACK_REQUIRED`:

1. zapisz `msg_id`,
2. zapisz `counter`,
3. uruchom timeout,
4. czekaj na `ACK`,
5. przy timeout retransmituj,
6. po limicie retry oznacz node jako `DEGRADED`.

## 7. Pairing z gatewayem

Rekomendowany flow wdrozeniowy:

1. Uzytkownik na nodzie wybiera `Pair with network`.
2. Node tylko nasluchuje przez 5 min.
3. Operator na panelu gatewaya wybiera `Start network pairing`.
4. Gateway wysyla `PAIR_REQ`.
5. Node pokazuje lokalna prosbe o akceptacje.
6. Node po akceptacji wysyla `PAIR_RESP`.
7. Gateway zapisuje trusted relation i aktywuje node.

Dokladny przyklad surowej ramki `PAIR_REQ / PAIR_RESP` jest opisany w `gateway_docs/README.md`.

Pelna spec ramki, kluczy, HMAC i AES-CTR jest opisana w `gateway_docs/FRAME_CRYPTO_SPEC.md`.

## 8. Rekomendacja implementacyjna dla pairingu

Najprostsza, zgodna z obecnym firmware:

- `PAIR_REQ` jako broadcast od gatewaya,
- `PAIR_RESP` jako unicast do gatewaya,
- po `PAIR_RESP` gateway zapisuje `node_id + code[8]`.

To jest najwygodniejszy model, gdy `node_id` nie jest znane przed parowaniem.

## 9. Moduly po stronie RPi

Aktualny podzial kodu w tym repo:

- `gateway/app/services/lora_hardware.py` - driver SX1276/RFM95, SPI/GPIO, liczniki RX/TX,
- `gateway/app/services/laviet_frame.py` - ramka `LAVIET_FRAME_V1`,
- `gateway/app/core/laviet_crypto.py` - AES-CTR, HMAC i wyprowadzanie kluczy,
- `gateway/app/services/pairing.py` - pairing `PAIR_REQ/PAIR_RESP`,
- `gateway/app/services/message_tracker.py` - pending ACK/response i ostatnie RSSI/SNR per node,
- `gateway/app/services/system_metrics.py` - historia metryk systemowych,
- `gateway/app/api/endpoints/` - API FastAPI dla panelu.

Minimalny podzial przy ewentualnym przepisaniu na C/C++:

- `sx1276_hal.*`
- `laviet_frame.*`
- `laviet_crypto.*`
- `gateway_pairing.*`
- `gateway_retry.*`
- `gateway_nodes.*`
- `gateway_web.*`

## 10. Testy

Najpierw:

1. test loop `RX/TX` z jednym node'em,
2. test `PAIR_REQ -> PAIR_RESP`,
3. test `DATA -> ACK`,
4. test retransmisji po braku `ACK`,
5. test restartu gatewaya z zachowaniem counterow,
6. test restartu node'a.

## 11. Ograniczenia jednego SX1276 na gatewayu

Przy jednym module SX1276/RFM95 gateway jest half-duplex:

- albo slucha,
- albo nadaje.

To oznacza:

- okna retry musza byc krotkie i deterministyczne,
- nie nalezy rozciagac TX,
- po kazdym TX trzeba szybko wracac do RX,
- panel webowy nie moze blokowac watku radiowego.

## 12. Co bym wdrozyl najpierw na RPi

1. Serwis radiowy w C++.
2. `PAIR_REQ/PAIR_RESP`.
3. `DATA/ACK`.
4. Retry i timeout.
5. Dopiero potem panel www.

## 13. Zrodla

- Semtech SX1276:
  https://www.semtech.com/products/wireless-rf/lora-transceivers/sx1276
- Raspberry Pi configuration i SPI:
  https://www.raspberrypi.com/documentation/computers/configuration.html
- RadioLib:
  https://jgromes.github.io/RadioLib/
