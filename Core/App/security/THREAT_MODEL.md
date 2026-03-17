# Threat Model

## Assets

- Klucz sieciowy i klucze peer.
- Integralność ramek `USER`.
- Dostępność kanału radiowego i budżetu duty-cycle.
- Lista urządzeń zaufanych.
- EEPROM z konfiguracją security.

## Attacker goals

- Replay starych ramek, aby wywołać ponowny relay albo lokalną akcję.
- Flood broadcastami, żeby zużyć duty-cycle i baterię.
- Spam pairing/control traffic, żeby blokować UI i task radiowy.
- Wstrzykiwanie śmieciowych ramek o poprawnej strukturze, ale bez sensu logicznego.

## Current mitigations

- CRC ramki.
- `ttl` dla ograniczenia zasięgu relay.
- Auth/coding dla ruchu secure.
- Replay window per `src_id` oparty o `msg_id`.
- Ograniczone forwardowanie tylko dla ruchu `USER`.

## Remaining gaps

- Brak per-source token bucket.
- Brak globalnego limitu relay.
- Brak reputacji źródeł i czasowego banowania.
- Brak agregacji alertów security przy floodzie.
- Brak rozdziału priorytetów dla traffic local vs relay.
