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
| TOF | In progress ⚒️ | |
| RADIO | NO ❌ | |
| MIC | NO ❌ | |
| Speaker | NO ❌| Trzeba dodać głośnik |
| ACC | NO ❌ | Trzeba podłączyć |
| TMP | NO ❌ | |
| BUTTON | NO ❌ | |
| I2C MEM | NO ❌ | |
| HALL SENSOR | NO ❌ | |
| LIGHT SENSOR| NO ❌ | |
| OLED DISP | NO ❌ | |
| SERVO | NO ❌ | |
| LED DISP | NO ❌ | |
| LED ARRAY | NO ❌ | |
