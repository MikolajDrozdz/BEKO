/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
*/
var NAVTREE =
[
  [ "BEKO Pager Firmware", "index.html", [
    [ "Wprowadzenie", "index.html#intro", null ],
    [ "Punkty startowe", "index.html#start", null ],
    [ "Strony opisowe", "index.html#pages", null ],
    [ "Generowanie dokumentacji", "index.html#generation", null ],
    [ "Generowanie dokumentacji", "doxygen_build.html", [
      [ "Wymagania", "doxygen_build.html#doxygen_requirements", null ],
      [ "Komenda", "doxygen_build.html#doxygen_command", null ],
      [ "Zakres wejscia", "doxygen_build.html#doxygen_scope", null ],
      [ "Polityka plikow wynikowych", "doxygen_build.html#doxygen_output", null ]
    ] ],
    [ "Architektura firmware", "doxygen_architecture.html", [
      [ "Warstwy", "doxygen_architecture.html#arch_layers", null ],
      [ "Zadania RTOS", "doxygen_architecture.html#arch_tasks", null ],
      [ "Wspolna magistrala I2C", "doxygen_architecture.html#arch_shared_i2c", null ],
      [ "Runtime configuration", "doxygen_architecture.html#arch_runtime", null ]
    ] ],
    [ "Mapa modulow", "doxygen_modules.html", [
      [ "Core application", "doxygen_modules.html#module_app", null ],
      [ "Radio", "doxygen_modules.html#module_radio", null ],
      [ "Protocol and crypto", "doxygen_modules.html#module_protocol", null ],
      [ "Security and storage", "doxygen_modules.html#module_security", null ],
      [ "User interface", "doxygen_modules.html#module_ui", null ],
      [ "Sensors", "doxygen_modules.html#module_sensors", null ]
    ] ],
    [ "Bezpieczenstwo", "doxygen_security.html", [
      [ "LAVIET_FRAME_V1", "doxygen_security.html#security_frame", null ],
      [ "Kryptografia", "doxygen_security.html#security_crypto", null ],
      [ "TPM", "doxygen_security.html#security_tpm", null ],
      [ "Storage", "doxygen_security.html#security_storage", null ]
    ] ],
    [ "BEKO Pager Network (<span class=\"tt\">pager-rtos</span>)", "md_README.html", [
      [ "1. Opis systemu", "md_README.html#autotoc_md1", null ],
      [ "2. Adresacja", "md_README.html#autotoc_md3", null ],
      [ "3. Role użytkowników", "md_README.html#autotoc_md5", [
        [ "Administrator", "md_README.html#autotoc_md6", null ],
        [ "Operator", "md_README.html#autotoc_md7", null ],
        [ "Użytkownik", "md_README.html#autotoc_md8", null ],
        [ "Serwisant", "md_README.html#autotoc_md9", null ]
      ] ],
      [ "4. Ochrona ustawień lokalnych", "md_README.html#autotoc_md11", null ],
      [ "5. Ramka <span class=\"tt\">LAVIET_FRAME_V1</span>", "md_README.html#autotoc_md13", [
        [ "Struktura ramki", "md_README.html#autotoc_md14", null ],
        [ "Rozmiar ramki", "md_README.html#autotoc_md15", null ]
      ] ],
      [ "6. Znaczenie pól <span class=\"tt\">ver_type</span> i <span class=\"tt\">flags</span>", "md_README.html#autotoc_md17", [
        [ "<span class=\"tt\">ver_type</span>", "md_README.html#autotoc_md18", null ],
        [ "<span class=\"tt\">flags</span>", "md_README.html#autotoc_md19", null ]
      ] ],
      [ "7. Zabezpieczenia", "md_README.html#autotoc_md21", [
        [ "HMAC-SHA256", "md_README.html#autotoc_md22", null ],
        [ "Szyfrowanie wiadomości", "md_README.html#autotoc_md23", null ]
      ] ],
      [ "8. TPM i klucze", "md_README.html#autotoc_md25", null ],
      [ "9. Anti-replay i synchronizacja licznika", "md_README.html#autotoc_md27", null ],
      [ "10. Parowanie i rotacja kluczy", "md_README.html#autotoc_md29", [
        [ "Parowanie", "md_README.html#autotoc_md30", null ],
        [ "Rotacja kluczy", "md_README.html#autotoc_md31", null ]
      ] ],
      [ "11. Logi UART i kontrola startu", "md_README.html#autotoc_md33", null ],
      [ "12. Główny graf systemu", "md_README.html#autotoc_md35", null ],
      [ "13. Główne przepływy komunikacji", "md_README.html#autotoc_md37", [
        [ "Zwykła wiadomość z ACK", "md_README.html#autotoc_md38", null ],
        [ "Parowanie", "md_README.html#autotoc_md39", null ],
        [ "Synchronizacja licznika", "md_README.html#autotoc_md40", null ],
        [ "Rotacja kluczy", "md_README.html#autotoc_md41", null ]
      ] ],
      [ "14. Dokumentacja Doxygen", "md_README.html#autotoc_md43", null ]
    ] ],
    [ "BEKO Security Architecture", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html", [
      [ "1. Skrót", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md81", null ],
      [ "2. Cele bezpieczeństwa", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md82", null ],
      [ "3. Model atakującego", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md83", null ],
      [ "4. Granice bezpieczeństwa", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md84", null ],
      [ "5. Komponenty implementacji", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md85", null ],
      [ "6. TPM jako root-of-trust", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md86", [
        [ "6.1. Dlaczego TPM jest bezpieczniejszy niż EEPROM", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md87", null ]
      ] ],
      [ "7. TPM init", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md88", [
        [ "7.1. Verbose logi TPM", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md89", null ]
      ] ],
      [ "8. Root seed", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md90", [
        [ "8.1. Dlaczego seed nie jest w firmware", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md91", null ],
        [ "8.2. Lifecycle root seeda", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md92", null ],
        [ "8.3. Physical Presence", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md93", null ],
        [ "8.4. Ograniczenie: seed ma 8 bajtów", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md94", null ]
      ] ],
      [ "9. Entropia i generowanie seeda", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md95", null ],
      [ "10. Hierarchia kluczy", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md96", null ],
      [ "11. Klucze ramek radiowych", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md97", [
        [ "11.1. Pair link key", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md98", null ],
        [ "11.2. Shared mode", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md99", null ]
      ] ],
      [ "12. Ramka <span class=\"tt\">LAVIET_FRAME_V1</span>", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md100", null ],
      [ "13. Walidacja ramek", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md101", null ],
      [ "14. HMAC-SHA256", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md102", [
        [ "14.1. Kolejność RX", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md103", null ],
        [ "14.2. Stałoczasowe porównanie MAC", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md104", null ]
      ] ],
      [ "15. AES-CTR", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md105", [
        [ "15.1. Counter block AES-CTR", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md106", null ],
        [ "15.2. AES-CTR nie jest autentykacją", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md107", null ]
      ] ],
      [ "16. Hardware crypto STM32", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md108", [
        [ "16.1. Dlaczego self-test jest ważny", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md109", null ],
        [ "16.2. Pakowanie big-endian do AES", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md110", null ]
      ] ],
      [ "17. Software fallback", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md111", null ],
      [ "18. EEPROM secret storage", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md112", [
        [ "18.1. Szyfrowanie secret slots", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md113", null ]
      ] ],
      [ "19. Trusted devices", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md114", null ],
      [ "20. Pairing", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md115", null ],
      [ "21. ACK", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md116", null ],
      [ "22. Counter i anti-replay", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md117", null ],
      [ "23. <span class=\"tt\">COUNTER_SYNC</span>", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md118", null ],
      [ "24. Broadcast", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md119", null ],
      [ "25. Lokalny PIN i role", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md120", null ],
      [ "26. Logi UART", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md121", null ],
      [ "27. Dlaczego ta architektura jest bezpieczna", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md122", [
        [ "27.1. Atak: podsłuch radiowy", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md123", null ],
        [ "27.2. Atak: zmiana payloadu", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md124", null ],
        [ "27.3. Atak: zmiana adresu docelowego", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md125", null ],
        [ "27.4. Atak: fałszywy ACK", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md126", null ],
        [ "27.5. Atak: odczyt EEPROM", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md127", null ],
        [ "27.6. Atak: zdalna rotacja root seeda", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md128", null ],
        [ "27.7. Atak: błąd hardware AES/HASH", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md129", null ],
        [ "27.8. Atak: przypadkowe wejście w menu admin", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md130", null ]
      ] ],
      [ "28. Ograniczenia obecnego wdrożenia", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md131", null ],
      [ "29. Zalecane utwardzenia", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md132", null ],
      [ "30. Przykłady", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md133", [
        [ "30.1. Przykład: budowa zaszyfrowanej ramki DATA", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md134", null ],
        [ "30.2. Przykład: odbiór zaszyfrowanej ramki", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md135", null ],
        [ "30.3. Przykład: odrzucenie zmienionej ramki", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md136", null ],
        [ "30.4. Przykład: rotacja root seeda", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md137", null ],
        [ "30.5. Przykład: hardware crypto self-test", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md138", null ],
        [ "30.6. Przykład: EEPROM secret slot", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md139", null ]
      ] ],
      [ "31. Checklist dla testów", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md140", null ],
      [ "32. Najkrótsza odpowiedź “dlaczego to jest bezpieczne”", "md_Core_2App_2security_2SECURITY__ARCHITECTURE.html#autotoc_md141", null ]
    ] ],
    [ "STRIDE / CIA Analysis", "md_Core_2App_2security_2STRIDE__CIA__ANALYSIS.html", [
      [ "STRIDE", "md_Core_2App_2security_2STRIDE__CIA__ANALYSIS.html#autotoc_md143", null ],
      [ "CIA", "md_Core_2App_2security_2STRIDE__CIA__ANALYSIS.html#autotoc_md144", null ],
      [ "Najważniejsze priorytety", "md_Core_2App_2security_2STRIDE__CIA__ANALYSIS.html#autotoc_md145", null ]
    ] ],
    [ "Threat Model", "md_Core_2App_2security_2THREAT__MODEL.html", [
      [ "Assets", "md_Core_2App_2security_2THREAT__MODEL.html#autotoc_md147", null ],
      [ "Attacker goals", "md_Core_2App_2security_2THREAT__MODEL.html#autotoc_md148", null ],
      [ "Current mitigations", "md_Core_2App_2security_2THREAT__MODEL.html#autotoc_md149", null ],
      [ "Remaining gaps", "md_Core_2App_2security_2THREAT__MODEL.html#autotoc_md150", null ]
    ] ],
    [ "Security", "md_Core_2App_2security_2TODO.html", [
      [ "Pytania", "md_Core_2App_2security_2TODO.html#autotoc_md152", null ],
      [ "Obszary", "md_Core_2App_2security_2TODO.html#autotoc_md153", null ],
      [ "Security TODO", "md_Core_2App_2security_2TODO.html#autotoc_md154", [
        [ "DoS / Replay", "md_Core_2App_2security_2TODO.html#autotoc_md155", null ]
      ] ]
    ] ],
    [ "Struktury Danych", "annotated.html", [
      [ "Struktury danych", "annotated.html", "annotated_dup" ],
      [ "Indeks struktur danych", "classes.html", null ],
      [ "Pola danych", "functions.html", [
        [ "Wszystko", "functions.html", null ],
        [ "Zmienne", "functions_vars.html", null ]
      ] ]
    ] ],
    [ "Pliki", "files.html", [
      [ "Lista plików", "files.html", "files_dup" ],
      [ "Globalne", "globals.html", [
        [ "Wszystko", "globals.html", "globals_dup" ],
        [ "Funkcje", "globals_func.html", "globals_func" ],
        [ "Definicje typów", "globals_type.html", null ],
        [ "Wyliczenia", "globals_enum.html", null ],
        [ "Wartości wyliczeń", "globals_eval.html", null ],
        [ "Definicje", "globals_defs.html", null ]
      ] ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"annotated.html",
"laviet__frame_8c_source.html",
"md_README.html#autotoc_md5",
"radio__main_8h.html#a13f3ef3efed1bdfd60e6f3c8594790e2a1fefc52330e3cfd15873de1d4e352dae",
"security__main_8h.html#adab8fef5f7716a7e73c831dc6ddd18ae",
"structradio__main__fsk__cfg__t.html#add9b154833acdd78e7cd463f6b7912a6"
];

var SYNCONMSG = 'kliknij żeby wyłączyć pokazywanie otwartego elementu w drzewie zawartości';
var SYNCOFFMSG = 'kliknij żeby włączyć pokazywanie otwartego elementu w drzewie zawartości';
var LISTOFALLMEMBERS = 'Lista wszystkich składowych';