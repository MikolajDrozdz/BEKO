# Threat Model

## Assets

- Klucz sieciowy i klucze peer.
- Integralność ramek `LAVIET_FRAME_V1`.
- Dostępność kanału radiowego i budżetu duty-cycle.
- Lista urządzeń zaufanych.
- EEPROM z konfiguracją security.

## Attacker goals

- Replay starych ramek, aby wywołać ponowną lokalną akcję lub fałszywy `ACK`.
- Flood broadcastami, żeby zużyć duty-cycle i baterię.
- Spam pairing/control traffic, żeby blokować UI i task radiowy.
- Wstrzykiwanie śmieciowych ramek o poprawnej strukturze, ale bez sensu logicznego.

## Current mitigations

- HMAC-SHA256 dla każdej ramki `LAVIET_FRAME_V1`.
- AES-CTR dla payloadu z flagą `ENCRYPTED`.
- Monotoniczny `counter` gateway-node.
- Brak relay mesh/TTL w bieżącej topologii gwiazdy.
- Broadcast tylko od gatewaya i bez `ACK_REQUIRED`.

## Remaining gaps

- Brak per-source token bucket.
- Brak pełnego trwałego modelu counterów dla wielu relacji.
- Brak reputacji źródeł i czasowego banowania.
- Brak agregacji alertów security przy floodzie.
- Brak rozdziału priorytetów dla ruchu lokalnego i administracyjnego.
