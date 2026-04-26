#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

WORKDIR="${WORKDIR:-$HOME/beko}"
FRONTEND_DIR="${FRONTEND_DIR:-$ROOT_DIR}"
AUTH_DIR="${AUTH_DIR:-$FRONTEND_DIR/backend}"
GATEWAY_DIR="${GATEWAY_DIR:-$WORKDIR/gateway-repo/gateway}"

AUTH_VENV="${AUTH_VENV:-$AUTH_DIR/.venv}"
GATEWAY_VENV="${GATEWAY_VENV:-$GATEWAY_DIR/venv}"

AUTH_HOST="${AUTH_HOST:-0.0.0.0}"
AUTH_PORT="${AUTH_PORT:-8001}"
GATEWAY_HOST="${GATEWAY_HOST:-0.0.0.0}"
GATEWAY_PORT="${GATEWAY_PORT:-8000}"

INSTALL_DEPS="${INSTALL_DEPS:-auto}"
SEED_AUTH="${SEED_AUTH:-1}"
ADMIN_USERNAME="${ADMIN_USERNAME:-admin}"
ADMIN_PASSWORD="${ADMIN_PASSWORD:-admin123}"

pids=()

log() {
  printf '[beko-backends] %s\n' "$*"
}

fail() {
  printf '[beko-backends] ERROR: %s\n' "$*" >&2
  exit 1
}

ensure_dir() {
  local dir="$1"
  local label="$2"
  [[ -d "$dir" ]] || fail "$label directory does not exist: $dir"
}

ensure_venv() {
  local dir="$1"
  local venv="$2"
  local label="$3"
  local created=0
  local should_install=0

  ensure_dir "$dir" "$label"
  [[ -f "$dir/requirements.txt" ]] || fail "$label requirements.txt not found in: $dir"

  if [[ ! -x "$venv/bin/python" ]]; then
    log "Creating $label venv: $venv"
    python3 -m venv "$venv"
    created=1
  fi

  if [[ "$INSTALL_DEPS" == "1" || "$created" == "1" ]]; then
    should_install=1
  elif [[ "$INSTALL_DEPS" == "auto" ]] && ! "$venv/bin/python" -c "import uvicorn" >/dev/null 2>&1; then
    should_install=1
  fi

  if [[ "$should_install" == "1" ]]; then
    log "Installing $label dependencies"
    "$venv/bin/python" -m pip install -r "$dir/requirements.txt"
  fi
}

cleanup() {
  if ((${#pids[@]})); then
    log "Stopping backends"
    for pid in "${pids[@]}"; do
      kill "$pid" 2>/dev/null || true
    done
    wait "${pids[@]}" 2>/dev/null || true
  fi
}

trap cleanup EXIT INT TERM

ensure_venv "$GATEWAY_DIR" "$GATEWAY_VENV" "Gateway API"
ensure_venv "$AUTH_DIR" "$AUTH_VENV" "Auth Service"

if [[ "$SEED_AUTH" == "1" && -f "$AUTH_DIR/seed.py" ]]; then
  log "Ensuring admin user exists"
  (
    cd "$AUTH_DIR"
    "$AUTH_VENV/bin/python" seed.py --username "$ADMIN_USERNAME" --password "$ADMIN_PASSWORD"
  )
fi

log "Starting Gateway API on ${GATEWAY_HOST}:${GATEWAY_PORT}"
(
  cd "$GATEWAY_DIR"
  exec "$GATEWAY_VENV/bin/python" -m uvicorn app.main:app --host "$GATEWAY_HOST" --port "$GATEWAY_PORT"
) &
pids+=("$!")

log "Starting Auth Service on ${AUTH_HOST}:${AUTH_PORT}"
(
  cd "$AUTH_DIR"
  exec "$AUTH_VENV/bin/python" -m uvicorn main:app --host "$AUTH_HOST" --port "$AUTH_PORT"
) &
pids+=("$!")

log "Both backends are running. Press Ctrl+C to stop."
wait -n "${pids[@]}"
