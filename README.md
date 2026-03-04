# BEKO podstawowy program z inicjalizacjami peryferii

## Struktura projektu

Pliki są podzielone 2 części, te do pisania aplikacji i te automatycznie tworzone przez IDE.

Aplikacja:
- /Core/App: **Tutaj pisze się program w stylu Arduino** (tj. inicjalizacja ~ app_init i pętra główna ~ app_main)

Automatycznie stworzone przez IDE:
- /Core/Src: defaultowa lokalizacja plików .c do projektu razem z *main.c*
- /Core/Inc: defaultowa lokalizacja plików .h do projektu razem z *main.h*

| Peryferium      | Status    | Uwagi |
|:----------|:---------:| ------:|
| LCD   | OK ✅ | |
| BMP280 | OK ✅ | Nie jest dostępne na wszystkich płytkach, potrzebna naprawa |
| TOF | OK ✅ | Uwaga! funckja blokująca na ok. 242 ms, może być zaimplenetowany w inny sposób. INT nie jest aktywny, jest zostawiony dla radia. |
| RADIO | OK ✅ | Jest gotowe do implementacji, jest napisane demo. Powstanie zawdzięczam Codex 5.3 |
| MIC | NO ❌ | |
| Speaker | NO ❌| Trzeba dodać głośnik |
| ACC | NO ❌ | Trzeba podłączyć |
| TMP | OK ✅ | |
| BUTTON | In progess ⚒️ | |
| I2C MEM |  In progess ⚒️ | |
| HALL SENSOR | NO ❌ | |
| LIGHT SENSOR| NO ❌ | |
| OLED DISP | NO ❌ | |
| SERVO | NO ❌ | |
| LED DISP | NO ❌ | |
| LED ARRAY | OK ✅  | |
