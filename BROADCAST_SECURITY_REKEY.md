# Bezpieczny szyfrowany broadcast i rotacja kluczy

Ten dokument opisuje docelowy sposob wdrozenia kodowanego broadcastu oraz
rotacji kluczy per-peer po okreslonej liczbie ramek z uzyciem Diffiego-Hellmana.

Status: etap V1 wdrozony w backendzie i `pager-rtos`.

Wdrozone teraz:

- backend ma `POST /api/gateway/broadcast-key/rotate`,
- gateway generuje losowy `broadcast_group_key[16]`,
- klucz jest przechowywany w DB jako zaszyfrowany blob z HMAC,
- instalacja do noda idzie unicastem przez sparowany link jako 2 zaszyfrowane
  ramki `KEY_ROTATE` po 8 B fragmentu oraz ramka aktywacji,
- node zapisuje aktywny klucz w RAM i probuje utrwalic go w TPM NV jako
  rozproszone rekordy: fragment 0, fragment 1 i osobny tag kontrolny,
- `coded=true` dla `dst_id=0xFFFF` uzywa aktywnego klucza grupowego zamiast
  stalego `LAVIET_SHARED_V1`.

Pelny per-peer re-key przez Diffiego-Hellmana pozostaje etapem V2, bo obecny
`LAVIET_MAX_PAYLOAD = 16` nie miesci 32-bajtowego public key X25519 w jednej
ramce kontrolnej.

## 1. Cel

Chcemy uzyskac:

- poufnosc payloadu broadcast przed osobami spoza grupy,
- integralnosc i autentycznosc ramki przez HMAC,
- brak ponownego uzycia nonce AES-CTR po restarcie backendu,
- mozliwosc wycofania noda z grupy przez rotacje klucza broadcast,
- per-peer re-key po limicie ramek lub czasie,
- odporna procedure commit/rollback, zeby node i gateway nie rozjechaly kluczy.

Nie da sie uzyskac pelnej poufnosci "per odbiorca" w jednej ramce broadcast.
Kazdy node posiadajacy aktualny klucz grupy moze odczytac kazdy zaszyfrowany
broadcast dla tej grupy. Jesli jeden node zostanie przejety, klucz grupy jest
skompromitowany i trzeba wygenerowac nowy klucz bez tego noda.

## 2. Wymagania bazowe

Przed wlaczeniem produkcyjnego broadcast crypto musza byc spelnione warunki:

1. Backend trzyma trwaly `gateway_tx_counter` w DB i rezerwuje go przed
   zbudowaniem AES nonce oraz HMAC.
2. Node zapisuje ostatni zaakceptowany counter gatewaya i odrzuca replay.
3. HMAC jest weryfikowany przed decrypt.
4. AES-CTR nonce sklada sie z pol ramki:

```text
"LV1\0" || src_id || dst_id || msg_id || counter || ctr16
```

5. Dla broadcastu nie wolno ustawiac `ACK_REQUIRED`. Odpowiedzi uzytkownika
   wracaja osobnymi ramkami unicast `RESP`.
6. Backend i firmware nie loguja kluczy, sekretow DH, group key ani plaintextu,
   jezeli urzadzenie dziala produkcyjnie.

## 3. Aktualny tryb kompatybilnosci

Obecny protokol V1 ma:

- `LAVIET_MAX_PAYLOAD = 16`,
- brak pola `key_id` / `epoch` w naglowku,
- wspolny bootstrap key `LAVIET_SHARED_V1`,
- HMAC-SHA256 32 B,
- AES-CTR.

W tym trybie `coded=true` dla broadcastu moze dzialac tak:

```text
type      = DATA
flags     = BROADCAST | ENCRYPTED
src_id    = 0x0001
dst_id    = 0xFFFF
counter   = next gateway_tx_counter
payload   = AES_CTR(plaintext, broadcast_aes_key, src, dst, msg_id, counter)
mac       = HMAC_SHA256(broadcast_hmac_key, header || encrypted_payload)
```

To jest zgodne z mechanika ramki, ale nie jest wystarczajace produkcyjnie,
jesli `broadcast_aes_key` i `broadcast_hmac_key` sa stale i znane wszystkim
instalacjom. Produkcyjnie trzeba wprowadzic losowy klucz grupy.

## 4. Produkcyjny model broadcast group key

Kazda grupa broadcast powinna miec wlasny losowy sekret:

```text
broadcast_group_key[16] = CSPRNG()   # etap V1, limit payloadu 16 B
broadcast_epoch         = uint32, monotonically increasing
group_id                = uint16 albo uint32
```

Z `broadcast_group_key` wyprowadzamy klucze robocze:

```text
broadcast_base_key = HMAC_SHA256(
    key  = broadcast_group_key,
    data = "LAVIET:BCAST:GROUP:V1" || be32(broadcast_epoch) || be16(group_id)
)[:16]

broadcast_aes_key  = HMAC_SHA256(broadcast_base_key, "LV1K" || be16(group_id) || 0x00 || 0x01)[:16]
broadcast_hmac_key = HMAC_SHA256(broadcast_base_key, "LV1K" || be16(group_id) || 0x00 || 0x02)
```

Zasady:

- `broadcast_group_key` nigdy nie idzie przez radio jako plaintext.
- Gateway generuje group key lokalnie z CSPRNG.
- Group key jest rozsylany do nodow tylko unicastem, po juz sparowanym linku
  per-peer.
- Node trzyma maksymalnie trzy sloty: `current`, `pending`, `previous`.
- Gateway trzyma stan dystrybucji klucza per node: `not_sent`, `sent`,
  `acked`, `activated`, `failed`.

## 5. Provisioning klucza broadcast

Po sparowaniu noda:

1. Gateway upewnia sie, ze unicast per-peer jest aktywny i ma poprawne liczniki.
2. Gateway wysyla do noda kontrolna ramke `GROUP_KEY_INSTALL`.
3. Node zapisuje klucz w slocie `pending`.
4. Node odsyla standardowy ACK z payloadem:

```text
acked_msg_id || acked_counter
```

5. Po ACK gateway oznacza node jako gotowy do aktywacji.
6. Gateway wysyla `GROUP_KEY_ACTIVATE(epoch)`.
7. Node przenosi `pending -> current`, a stary `current -> previous`.

Obecny etap V1 uzywa `broadcast_group_key[16]`, zeby zmiescic instalacje w
dwóch ramkach `KEY_ROTATE`:

```text
payload KEY_ROTATE install:
  magic      = 0xB7
  op         = 0x01
  epoch      = be32
  frag_idx   = 0..1
  frag_count = 2
  fragment   = 8 B

payload KEY_ROTATE activate:
  magic      = 0xB7
  op         = 0x02
  epoch      = be32
  reserved   = 10 B zero
```

Ramki `KEY_ROTATE` sa szyfrowane i MACowane kluczem per-peer. W V2, jesli
chcemy przejsc na `broadcast_group_key[32]`, trzeba wybrac jedna z dwoch opcji:

1. Preferowane: protokol V2 dla ramek kontrolnych z wiekszym payloadem, np.
   `LAVIET_MAX_CONTROL_PAYLOAD >= 64`.
2. Kompatybilne z V1: fragmentacja kontrolna, np. 4 ramki po 8 B danych plus
   naglowek fragmentu.

Fragmentacja musi byc szyfrowana i HMACowana per fragment. Node sklada klucz
dopiero po odebraniu wszystkich fragmentow i poprawnym sprawdzeniu MAC.

## 6. TX szyfrowanego broadcastu

Algorytm backendu dla kazdej ramki broadcast:

```text
counter = reserve_gateway_tx_counter()
msg_id  = next non-zero msg id

load current broadcast_group_key and broadcast_epoch
derive broadcast_aes_key and broadcast_hmac_key

flags = BROADCAST
if coded:
    flags |= ENCRYPTED
    payload = AES_CTR(plaintext, broadcast_aes_key, 0x0001, 0xFFFF, msg_id, counter)
else:
    payload = plaintext

mac = HMAC_SHA256(broadcast_hmac_key, header || payload)
send frame
```

Zasady:

- `ACK_REQUIRED` musi byc wyzerowane dla `dst_id=0xFFFF`.
- Jezeli `coded=true`, HMAC musi byc liczony po zaszyfrowanym payloadzie.
- Jezeli `coded=false`, HMAC nadal musi obejmowac jawny payload.
- Po restarcie backend nie moze cofnac `gateway_tx_counter`.
- Node nie moze aktualizowac `gateway_rx_counter` dla ramki, ktora nie przeszla
  HMAC.

## 7. RX szyfrowanego broadcastu na nodzie

Node:

1. Rozpoznaje broadcast po `dst_id == 0xFFFF` i fladze `BROADCAST`.
2. Sprawdza, ze `ACK_REQUIRED == 0`.
3. Dla `ENCRYPTED` probuje HMAC kluczem `current`.
4. Jezeli HMAC nie pasuje, w okresie przejsciowym probuje `previous`.
5. Dopiero po dopasowaniu HMAC robi AES-CTR decrypt tym samym slotem klucza.
6. Sprawdza replay counter gatewaya.
7. Przekazuje plaintext do aplikacji.

Brak pola `key_id` w V1 oznacza, ze w oknie rotacji node musi sprobowac
`current` i `previous`. W V2 nalezy dodac `key_id` albo `key_epoch` do naglowka,
zeby uniknac probowania wielu kluczy.

## 8. Rotacja group key

Rotacja group key jest potrzebna:

- cyklicznie, np. co 7 dni,
- po liczbie ramek broadcast, np. `BCAST_REKEY_FRAMES = 5000`,
- po usunieciu noda z grupy,
- po podejrzeniu wycieku klucza,
- po reinstalacji albo wymianie noda.

Procedura:

1. Gateway generuje nowy `broadcast_group_key` i `broadcast_epoch + 1`.
2. Gateway wybiera liste nodow, ktore maja zostac w grupie.
3. Dla kazdego noda gateway sprawdza, czy per-peer key nie jest blisko limitu.
   Jezeli jest, najpierw robi per-peer re-key.
4. Gateway wysyla `GROUP_KEY_INSTALL` unicastem do kazdego noda.
5. Gateway czeka na ACK od wymaganej listy nodow.
6. Jezeli celem jest usuniecie noda, aktywacje wysyla unicastem tylko do
   pozostalych nodow. Nie uzywa broadcast activate po starym kluczu.
7. Po aktywacji gateway nadaje broadcast juz z nowym epoch.
8. Node trzyma poprzedni klucz przez krotkie okno grace, np. 100 ramek albo
   10 minut, potem go kasuje.

## 9. Per-peer re-key przez Diffiego-Hellmana

Rekomendowany wariant DH to X25519:

- staly rozmiar public key: 32 B,
- szybki na MCU,
- mniej ryzyk konfiguracyjnych niz klasyczny DH modulo p,
- powszechne biblioteki i test vectors.

Sam Diffie-Hellman nie uwierzytelnia strony. Handshake musi byc
uwierzytelniony starym per-peer HMAC key albo dlugoterminowym kluczem podpisu.
Bez tego atak MITM jest trywialny.

### 9.1. Kiedy robic per-peer re-key

Gateway trzyma per node:

```text
pair_epoch
frames_tx_since_rekey
frames_rx_since_rekey
last_rekey_at
rekey_state
```

Re-key startuje, gdy zachodzi ktorykolwiek warunek:

```text
frames_tx_since_rekey >= PAIR_REKEY_FRAMES
frames_rx_since_rekey >= PAIR_REKEY_FRAMES
now - last_rekey_at >= PAIR_REKEY_SECONDS
admin forced rekey
group key rotation requires fresh peer key
```

Praktyczne wartosci poczatkowe:

```text
PAIR_REKEY_FRAMES  = 10000
PAIR_REKEY_SECONDS = 604800  # 7 dni
REKEY_START_AT     = 0.8 * PAIR_REKEY_FRAMES
```

Nie nalezy czekac do samego limitu. Gateway powinien startowac re-key przy
80% limitu, zeby miec czas na retry.

### 9.2. Handshake

Minimalny handshake:

```text
Gateway -> Node: REKEY_INIT
  old_pair_epoch
  new_pair_epoch
  alg = X25519_HKDF_SHA256
  gateway_eph_pub[32]
  gateway_nonce[16]
  threshold_next
  auth = HMAC(old_hmac_key, transcript)

Node -> Gateway: REKEY_RESP
  old_pair_epoch
  new_pair_epoch
  node_eph_pub[32]
  node_nonce[16]
  auth = HMAC(old_hmac_key, transcript)

Gateway -> Node: REKEY_COMMIT
  new_pair_epoch
  confirm = HMAC(new_hmac_key, "commit" || transcript_hash)

Node -> Gateway: ACK
  acked_msg_id || acked_counter
```

Ze wzgledu na 16 B payloadu obecnego V1, `REKEY_INIT` i `REKEY_RESP` wymagaja
albo ramek kontrolnych V2, albo fragmentacji. X25519 public key ma 32 B, wiec
nie da sie go bezpiecznie zmiescic w jednej obecnej ramce.

### 9.3. Derivacja nowego per-peer key

Po wymianie public key:

```text
dh_secret = X25519(local_eph_private, peer_eph_public)

salt = old_pair_base_key ||
       be16(gateway_id) ||
       be16(node_id) ||
       gateway_nonce ||
       node_nonce

info = "LAVIET:PAIR-REKEY:V1" || be32(new_pair_epoch)

new_pair_base_key = HKDF_SHA256(
    ikm  = dh_secret,
    salt = salt,
    info = info,
    len  = 32
)
```

Dla kompatybilnosci z aktualnym kodem, ktory historycznie uzywal 16 B
`pair_base_key`, mozna tymczasowo uzywac:

```text
pair_base_key_v1 = new_pair_base_key[:16]
```

Docelowo lepiej trzymac 32 B base key i z niego wyprowadzac AES/HMAC:

```text
aes_key  = HMAC_SHA256(new_pair_base_key, "LV1K" || domain_id || 0x00 || 0x01)[:16]
hmac_key = HMAC_SHA256(new_pair_base_key, "LV1K" || domain_id || 0x00 || 0x02)
```

### 9.4. Commit i rollback

Gateway i node musza trzymac sloty:

```text
current_pair_key
pending_pair_key
previous_pair_key
```

Zasady:

- `pending_pair_key` nie zastapuje `current_pair_key` przed `REKEY_COMMIT`.
- `REKEY_COMMIT` musi byc potwierdzony zwyklym ACK.
- Po commit: `current -> previous`, `pending -> current`.
- `previous` jest akceptowany tylko przez krotkie okno grace.
- Po bledzie handshake node kasuje `pending` i zostaje przy `current`.
- Gateway retry wykonuje pod starym `current_pair_key`.
- Ephemeral private key X25519 trzeba wyzerowac z RAM po zakonczeniu handshake.

Brak `key_epoch` w zwyklej ramce V1 oznacza, ze odbiornik w oknie grace powinien
sprawdzic HMAC najpierw `current`, potem `previous`. V2 powinien dodac jawny
`key_epoch` albo krotki `key_id`.

## 10. Zmiany wymagane w backendzie

Backend powinien dostac osobny `key_manager`:

```text
gateway/app/services/key_manager.py
```

Minimalne tabele:

```text
gateway_state
  key
  value

peer_keys
  node_id
  pair_epoch
  current_key_enc
  previous_key_enc
  pending_key_enc
  frames_tx_since_rekey
  frames_rx_since_rekey
  last_rekey_at
  rekey_state

broadcast_groups
  group_id
  current_epoch
  current_key_enc
  previous_epoch
  previous_key_enc
  pending_epoch
  pending_key_enc
  rotate_after_frames
  frames_since_rotate

broadcast_group_members
  group_id
  node_id
  state
  last_install_msg_id
  last_install_counter
```

Klucze w DB nie powinny lezec jako plaintext. Backend powinien miec master key
z zewnatrz:

```text
LAVIET_MASTER_KEY_FILE=/etc/laviet/master.key
```

Plik:

- owner: `root:beko` albo `beko:beko`,
- prawa: `0640` albo `0600`,
- poza repozytorium,
- nie logowany,
- backupowany przez bezpieczny mechanizm.

Lepsze opcje na produkcji:

- TPM/HSM,
- systemowy keyring,
- zaszyfrowany dysk,
- secret manager w deployment pipeline.

Endpointy administracyjne:

```text
POST /api/security/peers/{node_id}/rekey
POST /api/security/broadcast/groups/{group_id}/rotate
POST /api/security/broadcast/groups/{group_id}/members/{node_id}
DELETE /api/security/broadcast/groups/{group_id}/members/{node_id}
GET /api/security/keys/status
```

Endpointy musza byc chronione autoryzacja administracyjna. Nie wystawiac ich
bez kontroli dostepu w sieci lokalnej.

## 11. Zmiany wymagane w firmware

Firmware musi:

- obslugiwac `current`, `pending`, `previous` dla peer key i group key,
- umiec weryfikowac HMAC przez aktualny slot, a w grace takze poprzedni slot,
- zapisywac klucze i countery trwale,
- kasowac `pending` po timeout albo bledzie handshake,
- kasowac ephemeral private key z RAM po X25519,
- uzywac CSPRNG do nonce i ephemeral private key,
- miec tryb fragmentacji albo ramki kontrolne V2,
- nie logowac sekretow,
- wlaczyc readout protection flash na produkcyjnych STM32.

Na STM32 samo "bezpieczne przechowywanie" w zwyklej flash nie jest pelnym
secure storage. Minimum produkcyjne:

- RDP Level 1, a dla finalnego produktu rozwazyc Level 2,
- brak debug/JTAG/SWD w produkcji,
- brak logowania kluczy przez UART,
- unikalne sekrety per instalacja,
- procedura kasowania kluczy przy factory reset.

## 12. Migracja

Proponowana kolejnosc wdrozenia:

1. Utrwalony `gateway_tx_counter` i replay protection.
2. `coded=true` dla broadcastu w trybie kompatybilnym.
3. DB i storage dla `broadcast_group_key`.
4. Unicast `GROUP_KEY_INSTALL` i `GROUP_KEY_ACTIVATE`.
5. Rotacja group key po czasie/liczbie broadcastow.
6. X25519 per-peer re-key.
7. Fragmentacja albo ramki kontrolne V2.
8. Wycofanie produkcyjnego uzycia `LAVIET_SHARED_V1` dla broadcastu.

W trakcie migracji trzeba utrzymac flagi kompatybilnosci:

```text
LAVIET_ALLOW_SHARED_BROADCAST=true/false
LAVIET_REQUIRE_GROUP_BROADCAST=true/false
LAVIET_ALLOW_PREVIOUS_KEY_GRACE=true/false
```

Docelowo:

```text
LAVIET_ALLOW_SHARED_BROADCAST=false
LAVIET_REQUIRE_GROUP_BROADCAST=true
```

## 13. Testy akceptacyjne

Minimalny zestaw testow:

1. Broadcast `coded=true` nie zawiera plaintextu w `payload`.
2. HMAC jest liczony po `header || encrypted_payload`.
3. Node odrzuca broadcast z `ACK_REQUIRED`.
4. Node odrzuca replay z tym samym lub nizszym `counter`.
5. Node akceptuje ramke po restarcie backendu, jezeli counter wzrosl.
6. Node probuje `previous` key tylko w oknie grace.
7. Po rotacji group key usuniety node nie dekoduje nowych broadcastow.
8. Per-peer re-key nie aktywuje nowego klucza bez `REKEY_COMMIT`.
9. Zerwany re-key zostawia obie strony na starym kluczu.
10. ACK po re-key jest dopasowywany po payloadzie `acked_msg_id, acked_counter`.

## 14. Najwazniejsze zakazy

- Nie uzywac jednego stalego klucza broadcast we wszystkich instalacjach.
- Nie wysylac group key broadcastem.
- Nie robic DH bez uwierzytelnienia HMAC/podpisem.
- Nie resetowac `gateway_tx_counter`.
- Nie reuse'owac ephemeral private key X25519.
- Nie aktywowac nowego klucza bez fazy commit.
- Nie logowac kluczy ani sekretow DH.
- Nie traktowac szyfrowanego broadcastu jako tajnego przed nodami nalezacymi do
  tej samej grupy.
