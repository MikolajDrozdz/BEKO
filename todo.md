Dodaj do tego projektu kontroler, coś na zasadzie menu na wyświetlaczu, są przyciski, które powinny być co 25 ms odpytywane o stan w zadaniu button_main.

Do menu zrób zadanie manu_main.

Na początku po uruchomieniu ma pokazać się inicjalizacja hello beko, która już jest. Potem ma zacząć działać w domyślej konfiguracji, czyli ma odbierać dane na podstawowych ustawieniach (takie jakie są w demo).

Jeżeli kliknę w przycisk jaki kolwiek ma pojawić się pager_menu w którym będą opcje takie jak wyślij wiadomość i kilka grup przykładowych wiadomości.

Dodaj security main do obsługi zadań związanych z domeną security.

Potem ma być menu, gdzie będzie kilka rzeczy do wyboru, takie jak:

1. Devices
	1. Add new device - to ma wchodzić w tryb na 1 minutę, który umożliwia połączenie się nowego urządzenia i potem akceptacja, czy chcemy się z nim połączyć.
	2. Delate device - usuwanie jakiegoś urządzenia
	3. Info device - podanie wiadomości o danych urządzeniach

2. Security
	1. Frequency hopping
	2. TMP
	3. Keys
	4. Coding

3. Hardware
	1. Dist measure - mierzy odległość za pomocą czujnika tof
		1. Measure
	2. Temperature - mierzy temperature
	3. Pressure - mierzy ciśnienie
	4. Led - zmienia działanie ledów

4. Modulation
  1. LoRa
    1. Freq
    2. Bandwith
    i inne zadania
  2. FSK
	1.. kilka opcji tak jak przy lora
  3. OKK
	1... kilka opcji tak jak przy lora

5. Info
	informacje o urządzeniu oprogramowniu i twórcach



Sieć tych modułów ma mieć możliwość przesyłania dalej wiadomości jeżeli będzie to wiadomość z tego systemu a nie będzie to widaomość do tego urządzenia.