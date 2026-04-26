# INSTRUKCJA uruchomienia BEKO

Ta instrukcja jest uniwersalna: nie zakłada konkretnego katalogu użytkownika ani stałego adresu IP Raspberry Pi.

Projekt składa się z trzech części:

- `frontend` - panel WWW React/Vite,
- `Auth Service` - backend logowania z katalogu `backend/` na branchu frontendu,
- `Gateway API` - backend gatewaya LAVIET z katalogu `gateway/` na branchu gatewaya.

Na Raspberry Pi uruchamiasz trzy procesy:

- Gateway API: `http://<IP_RPI>:8000`,
- Auth Service: `http://<IP_RPI>:8001`,
- frontend: `http://<IP_RPI>:5173`.

Gateway API i Auth Service możesz uruchomić razem jedną komendą z repo frontendu.

Na komputerze użytkownika zwykle wystarczy otworzyć przeglądarkę i wejść na frontend.

## 1. Ustaw zmienne

Na Raspberry Pi ustaw zmienne dla swojej instalacji:

```bash
export RPI_IP="<ADRES_IP_RPI>"
export WORKDIR="$HOME/beko"
export REPO_URL="https://github.com/MikolajDrozdz/BEKO.git"
```

Przykład sprawdzenia adresu IP na Raspberry Pi:

```bash
hostname -I
```

W dalszych komendach używaj `RPI_IP` bez `http://`, np. `192.168.1.50`.

## 2. Zainstaluj wymagane narzędzia

Na Raspberry Pi:

```bash
sudo apt update
sudo apt install -y git curl python3 python3-venv python3-pip nodejs npm

python3 --version
node -v
npm -v
```

Jeśli Node.js z systemu jest zbyt stary, zainstaluj nowszą wersję Node.js, najlepiej `18` albo `20`.

## 3. Pobierz repo z Gita

Repo jest używane w dwóch branchach:

- branch `frontend` - panel WWW i Auth Service,
- branch `gateway` - Gateway API.

Najprostszy układ katalogów:

```bash
mkdir -p "$WORKDIR"
cd "$WORKDIR"

git clone -b frontend "$REPO_URL" frontend
git clone -b gateway "$REPO_URL" gateway-repo
```

Ustaw ścieżki do aplikacji:

```bash
export FRONTEND_DIR="$WORKDIR/frontend"
export AUTH_DIR="$FRONTEND_DIR/backend"
export GATEWAY_DIR="$WORKDIR/gateway-repo/gateway"
```

Jeśli masz inny adres repo albo inne nazwy branchy, zmień `REPO_URL`, `frontend` i `gateway` w komendach powyżej.

## 4. Uruchom Gateway API

Terminal 1 na Raspberry Pi:

```bash
cd "$GATEWAY_DIR"

python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt

python -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

Test z Raspberry Pi albo z komputera w tej samej sieci:

```bash
curl "http://$RPI_IP:8000/api/system/"
```

## 5. Uruchom Auth Service

Terminal 2 na Raspberry Pi:

```bash
cd "$AUTH_DIR"

python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt

python seed.py --username admin --password admin123
python -m uvicorn main:app --host 0.0.0.0 --port 8001
```

Test:

```bash
curl "http://$RPI_IP:8001/health"
```

Test logowania:

```bash
curl -X POST "http://$RPI_IP:8001/auth/login" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "username=admin&password=admin123"
```

Domyślne konto z seeda:

```text
login: admin
hasło: admin123
```

## 6. Uruchom Gateway API i Auth Service jedną komendą

Jeśli repo i zależności są już pobrane, najwygodniej uruchomić oba backendy jednym skryptem z katalogu frontendu:

```bash
cd "$FRONTEND_DIR"
npm run dev:backends
```

Ten skrypt:

- tworzy venv dla Auth Service i Gateway API, jeśli ich nie ma,
- instaluje zależności z `requirements.txt`, jeśli tworzy venv albo brakuje `uvicorn`,
- tworzy konto admina, jeśli jeszcze nie istnieje,
- uruchamia Gateway API na porcie `8000`,
- uruchamia Auth Service na porcie `8001`,
- zatrzymuje oba backendy po `Ctrl+C`.

Jeśli gateway masz w innym katalogu niż domyślny `"$WORKDIR/gateway-repo/gateway"`, podaj ścieżkę przy starcie:

```bash
cd "$FRONTEND_DIR"
GATEWAY_DIR="/sciezka/do/gateway" npm run dev:backends
```

Jeśli chcesz wymusić ponowną instalację zależności:

```bash
cd "$FRONTEND_DIR"
INSTALL_DEPS=1 npm run dev:backends
```

Jeśli chcesz całkowicie pominąć sprawdzanie instalacji zależności:

```bash
cd "$FRONTEND_DIR"
INSTALL_DEPS=0 npm run dev:backends
```

To jest odpowiednik ręcznego uruchomienia Terminala 1 i Terminala 2 z poprzednich sekcji.

## 7. Uruchom frontend

Terminal 3 na Raspberry Pi:

```bash
cd "$FRONTEND_DIR"

npm install
npm run dev -- --host 0.0.0.0
```

Na komputerze otwórz w przeglądarce:

```text
http://<ADRES_IP_RPI>:5173
```

Przykład z użyciem zmiennej w terminalu:

```bash
echo "http://$RPI_IP:5173"
```

Frontend domyślnie wylicza adresy API z hosta, z którego został otwarty:

```text
Gateway API: http://<ADRES_IP_RPI>:8000
Auth Service: http://<ADRES_IP_RPI>:8001
```

W głównym panelu po zalogowaniu adresy API może zmienić tylko użytkownik z rolą admin.

Na ekranie logowania jest też przycisk ustawień połączenia. Jest potrzebny wtedy, gdy frontend otwiera się poprawnie, ale wskazuje na zły adres Auth Service i nie da się jeszcze zalogować.

## 8. Szybki start ręcznie w trzech terminalach

Jeśli repo i zależności są już pobrane:

Terminal 1 - Gateway API:

```bash
export WORKDIR="$HOME/beko"
export GATEWAY_DIR="$WORKDIR/gateway-repo/gateway"

cd "$GATEWAY_DIR"
source venv/bin/activate
python -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

Terminal 2 - Auth Service:

```bash
export WORKDIR="$HOME/beko"
export AUTH_DIR="$WORKDIR/frontend/backend"

cd "$AUTH_DIR"
source .venv/bin/activate
python -m uvicorn main:app --host 0.0.0.0 --port 8001
```

Terminal 3 - Frontend:

```bash
export WORKDIR="$HOME/beko"
export FRONTEND_DIR="$WORKDIR/frontend"

cd "$FRONTEND_DIR"
npm run dev -- --host 0.0.0.0
```

## 9. Build produkcyjny frontendu

Sprawdzenie buildu:

```bash
cd "$FRONTEND_DIR"
npm run build
```

Podgląd buildu:

```bash
npm run preview -- --host 0.0.0.0
```

Domyślny adres preview:

```text
http://<ADRES_IP_RPI>:4173
```

## 10. Aktualizacja kodu

Frontend i Auth Service:

```bash
cd "$FRONTEND_DIR"
git pull
npm install
```

Gateway API:

```bash
cd "$WORKDIR/gateway-repo"
git pull
cd "$GATEWAY_DIR"
source venv/bin/activate
pip install -r requirements.txt
```

Po aktualizacji zrestartuj procesy w terminalach.

## 11. Najczęstsze problemy

### `externally-managed-environment`

Nie instaluj paczek do systemowego Pythona. Użyj środowiska venv:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

W Gateway API venv może nazywać się `venv`, a w Auth Service `.venv`.

### `uvicorn: command not found`

Uruchamiaj przez aktywne środowisko i `python -m`:

```bash
source .venv/bin/activate
python -m uvicorn main:app --host 0.0.0.0 --port 8001
```

Dla Gateway API:

```bash
source venv/bin/activate
python -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

### `Could not import module "main"`

Jesteś w złym katalogu.

Dla Auth Service katalog musi zawierać `main.py`:

```bash
cd "$AUTH_DIR"
ls main.py requirements.txt
```

Dla Gateway API katalog musi zawierać `app/main.py`:

```bash
cd "$GATEWAY_DIR"
ls app/main.py requirements.txt
```

### Frontend nie widzi gatewaya

Sprawdź, czy Gateway API działa:

```bash
curl "http://$RPI_IP:8000/api/system/"
```

Sprawdź, czy Auth Service działa:

```bash
curl "http://$RPI_IP:8001/health"
```

Sprawdź, czy procesy nasłuchują na portach:

```bash
ss -ltnp | grep -E ':8000|:8001|:5173'
```

### Nie działa logowanie

Utwórz albo odśwież konto admina:

```bash
cd "$AUTH_DIR"
source .venv/bin/activate
python seed.py --username admin --password admin123
```

Potem sprawdź logowanie:

```bash
curl -X POST "http://$RPI_IP:8001/auth/login" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "username=admin&password=admin123"
```

### Port jest zajęty

Sprawdź proces:

```bash
ss -ltnp | grep -E ':8000|:8001|:5173|:4173'
```

Zatrzymaj stary proces albo uruchom usługę na innym porcie.

## 12. Co uruchamiać na komputerze użytkownika

Na komputerze, z którego chcesz korzystać z dashboardu, nie musisz instalować Node.js ani Pythona, jeśli wszystko działa na Raspberry Pi.

Wystarczy przeglądarka:

```text
http://<ADRES_IP_RPI>:5173
```

Komputer i Raspberry Pi muszą być w tej samej sieci albo Raspberry Pi musi być dostępne przez VPN / routing.

## 13. Czego nie commitować

Nie dodawaj do Gita lokalnych plików i katalogów:

```text
node_modules/
dist/
.venv/
venv/
__pycache__/
*.pyc
*.db
.env
```

Jeśli przez pomyłkę zrobisz `git add .`, cofnij staging:

```bash
git reset
```

Potem dodawaj tylko potrzebne pliki:

```bash
git add README.md INSTRUKCJA.md
```
