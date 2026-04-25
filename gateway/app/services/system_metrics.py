import os
import shutil
import threading
import time
from collections import deque
from datetime import datetime, timezone
from typing import Any, Deque, Dict, List


_STARTED_AT = time.time()
_HISTORY: Deque[Dict[str, Any]] = deque(maxlen=8640)
_LOCK = threading.RLock()
_LAST_CPU_TOTAL: int | None = None
_LAST_CPU_IDLE: int | None = None


def _utc_now() -> datetime:
    return datetime.now(timezone.utc)


def _iso_z(dt: datetime) -> str:
    return dt.isoformat().replace("+00:00", "Z")


def parse_duration(value: str, default_seconds: int) -> int:
    value = (value or "").strip().lower()
    if not value:
        return default_seconds

    units = {
        "s": 1,
        "m": 60,
        "h": 3600,
        "d": 86400,
    }
    unit = value[-1]
    if unit in units:
        number = value[:-1]
        try:
            return max(1, int(float(number) * units[unit]))
        except ValueError:
            return default_seconds

    try:
        return max(1, int(float(value)))
    except ValueError:
        return default_seconds


def _read_cpu_percent() -> float:
    global _LAST_CPU_IDLE, _LAST_CPU_TOTAL

    try:
        with open("/proc/stat", "r", encoding="utf-8") as f:
            parts = f.readline().split()
    except OSError:
        return 0.0

    if not parts or parts[0] != "cpu":
        return 0.0

    values = [int(part) for part in parts[1:]]
    idle = values[3] + (values[4] if len(values) > 4 else 0)
    total = sum(values)

    with _LOCK:
        if _LAST_CPU_TOTAL is None or _LAST_CPU_IDLE is None:
            _LAST_CPU_TOTAL = total
            _LAST_CPU_IDLE = idle
            return 0.0

        total_delta = total - _LAST_CPU_TOTAL
        idle_delta = idle - _LAST_CPU_IDLE
        _LAST_CPU_TOTAL = total
        _LAST_CPU_IDLE = idle

    if total_delta <= 0:
        return 0.0
    return round(max(0.0, min(100.0, 100.0 * (1.0 - idle_delta / total_delta))), 1)


def _read_cpu_temp() -> float | None:
    paths = (
        "/sys/class/thermal/thermal_zone0/temp",
        "/sys/class/hwmon/hwmon0/temp1_input",
    )
    for path in paths:
        try:
            raw = open(path, "r", encoding="utf-8").read().strip()
            return round(float(raw) / 1000.0, 1)
        except (OSError, ValueError):
            continue
    return None


def _read_memory() -> tuple[int, int, float]:
    mem_total = 0
    mem_available = 0
    try:
        with open("/proc/meminfo", "r", encoding="utf-8") as f:
            for line in f:
                key, value = line.split(":", 1)
                if key == "MemTotal":
                    mem_total = int(value.strip().split()[0]) * 1024
                elif key == "MemAvailable":
                    mem_available = int(value.strip().split()[0]) * 1024
    except OSError:
        return 0, 0, 0.0

    used = max(0, mem_total - mem_available)
    percent = round((used / mem_total) * 100.0, 1) if mem_total else 0.0
    return used, mem_total, percent


def collect_metrics() -> Dict[str, Any]:
    now = _utc_now()
    memory_used, memory_total, memory_percent = _read_memory()
    disk = shutil.disk_usage("/")

    metric = {
        "timestamp": _iso_z(now),
        "uptime": int(time.time() - _STARTED_AT),
        "cpu_percent": _read_cpu_percent(),
        "cpu_temp": _read_cpu_temp(),
        "load_avg": [round(value, 2) for value in os.getloadavg()],
        "memory_used": memory_used,
        "memory_total": memory_total,
        "memory_percent": memory_percent,
        "disk_used": disk.used,
        "disk_total": disk.total,
        "disk_percent": round((disk.used / disk.total) * 100.0, 1) if disk.total else 0.0,
        "_ts": now.timestamp(),
    }

    with _LOCK:
        _HISTORY.append(metric)

    return {key: value for key, value in metric.items() if not key.startswith("_")}


def get_history(range_seconds: int, step_seconds: int) -> List[Dict[str, Any]]:
    cutoff = time.time() - range_seconds
    step_seconds = max(1, step_seconds)
    buckets: Dict[int, Dict[str, Any]] = {}

    with _LOCK:
        samples = [sample.copy() for sample in _HISTORY if sample.get("_ts", 0) >= cutoff]

    for sample in samples:
        bucket = int(sample["_ts"] // step_seconds) * step_seconds
        buckets[bucket] = sample

    return [
        {key: value for key, value in buckets[key].items() if not key.startswith("_")}
        for key in sorted(buckets)
    ]
