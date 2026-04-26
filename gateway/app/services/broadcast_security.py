import hashlib
import hmac
import os
import stat
import threading
from pathlib import Path

from sqlalchemy.orm import Session

from ..core.laviet_crypto import derive_broadcast_group_base_key, get_aes_key, get_hmac_key
from ..models import models
from .laviet_frame import LAVIET_BROADCAST_ID

DEFAULT_GROUP_ID = LAVIET_BROADCAST_ID
BROADCAST_GROUP_KEY_LEN = 16
BROADCAST_CTRL_MAGIC = 0xB7
BROADCAST_CTRL_VERSION = 1
BROADCAST_CTRL_INSTALL_FRAGMENT = 1
BROADCAST_CTRL_ACTIVATE = 2
BROADCAST_CTRL_FRAGMENT_LEN = 8
BROADCAST_CTRL_FRAGMENT_COUNT = 2

_lock = threading.RLock()


def _master_key_path() -> Path:
    return Path(os.getenv("LAVIET_MASTER_KEY_FILE", "gateway_master.key"))


def _load_or_create_master_key() -> bytes:
    raw_hex = os.getenv("LAVIET_MASTER_KEY_HEX")
    if raw_hex:
        key = bytes.fromhex(raw_hex)
        if len(key) != 32:
            raise ValueError("LAVIET_MASTER_KEY_HEX musi miec 32 bajty hex")
        return key

    path = _master_key_path()
    if path.exists():
        data = path.read_bytes().strip()
        key = bytes.fromhex(data.decode("ascii"))
        if len(key) != 32:
            raise ValueError(f"{path} nie zawiera 32-bajtowego master key")
        return key

    key = os.urandom(32)
    path.write_text(key.hex() + "\n")
    try:
        path.chmod(stat.S_IRUSR | stat.S_IWUSR)
    except OSError:
        pass
    return key


def _store_keys(master_key: bytes) -> tuple[bytes, bytes]:
    enc_key = hmac.new(master_key, b"LAVIET:DB:BCAST:AES:V1", hashlib.sha256).digest()[:16]
    mac_key = hmac.new(master_key, b"LAVIET:DB:BCAST:HMAC:V1", hashlib.sha256).digest()
    return enc_key, mac_key


def _xor_stream(data: bytes, enc_key: bytes, nonce: bytes) -> bytes:
    from Crypto.Cipher import AES
    from Crypto.Util import Counter

    ctr = Counter.new(32, prefix=nonce, initial_value=0)
    return AES.new(enc_key, AES.MODE_CTR, counter=ctr).encrypt(data)


def _encrypt_key(group_key: bytes) -> bytes:
    if len(group_key) != BROADCAST_GROUP_KEY_LEN:
        raise ValueError("broadcast group key musi miec 16 bajtow")
    master_key = _load_or_create_master_key()
    enc_key, mac_key = _store_keys(master_key)
    nonce = os.urandom(12)
    cipher = _xor_stream(group_key, enc_key, nonce)
    tag = hmac.new(mac_key, nonce + cipher, hashlib.sha256).digest()
    return nonce + tag + cipher


def _decrypt_key(blob: bytes) -> bytes:
    if len(blob) != 12 + 32 + BROADCAST_GROUP_KEY_LEN:
        raise ValueError("niepoprawny key_blob broadcast group")
    master_key = _load_or_create_master_key()
    enc_key, mac_key = _store_keys(master_key)
    nonce = blob[:12]
    tag = blob[12:44]
    cipher = blob[44:]
    expected = hmac.new(mac_key, nonce + cipher, hashlib.sha256).digest()
    if not hmac.compare_digest(expected, tag):
        raise ValueError("broadcast group key MAC mismatch")
    return _xor_stream(cipher, enc_key, nonce)


def get_active_group(db: Session, group_id: int = DEFAULT_GROUP_ID) -> tuple[int, bytes] | None:
    with _lock:
        row = (
            db.query(models.BroadcastGroupKey)
            .filter(
                models.BroadcastGroupKey.group_id == group_id,
                models.BroadcastGroupKey.active.is_(True),
            )
            .first()
        )
        if row is None:
            return None
        return int(row.epoch or 0), _decrypt_key(bytes(row.key_blob))


def get_active_broadcast_frame_keys(db: Session, group_id: int = DEFAULT_GROUP_ID) -> tuple[int, bytes, bytes]:
    group = get_active_group(db, group_id)
    if group is None:
        raise LookupError("Brak aktywnego broadcast group key. Najpierw wykonaj rotacje/install.")
    epoch, group_key = group
    base_key = derive_broadcast_group_base_key(group_key, epoch, group_id)
    aes_key = get_aes_key(base_key, group_id)
    hmac_key = get_hmac_key(base_key, group_id)
    return epoch, aes_key, hmac_key


def rotate_group_key(db: Session, group_id: int = DEFAULT_GROUP_ID) -> tuple[int, bytes]:
    with _lock:
        current = (
            db.query(models.BroadcastGroupKey)
            .filter(models.BroadcastGroupKey.group_id == group_id)
            .first()
        )
        next_epoch = 1 if current is None else int(current.epoch or 0) + 1
        group_key = os.urandom(BROADCAST_GROUP_KEY_LEN)
        blob = _encrypt_key(group_key)

        if current is None:
            current = models.BroadcastGroupKey(
                group_id=group_id,
                epoch=next_epoch,
                key_blob=blob,
                active=True,
            )
            db.add(current)
        else:
            current.epoch = next_epoch
            current.key_blob = blob
            current.active = True
        db.commit()
        return next_epoch, group_key


def build_install_payload(epoch: int, fragment_index: int, group_key: bytes) -> bytes:
    if len(group_key) != BROADCAST_GROUP_KEY_LEN:
        raise ValueError("broadcast group key musi miec 16 bajtow")
    if fragment_index < 0 or fragment_index >= BROADCAST_CTRL_FRAGMENT_COUNT:
        raise ValueError("niepoprawny fragment broadcast group key")

    start = fragment_index * BROADCAST_CTRL_FRAGMENT_LEN
    fragment = group_key[start:start + BROADCAST_CTRL_FRAGMENT_LEN]
    return bytes([
        BROADCAST_CTRL_MAGIC,
        BROADCAST_CTRL_INSTALL_FRAGMENT,
    ]) + int(epoch).to_bytes(4, "big") + bytes([
        fragment_index & 0xFF,
        BROADCAST_CTRL_FRAGMENT_COUNT,
    ]) + fragment


def build_activate_payload(epoch: int) -> bytes:
    return bytes([
        BROADCAST_CTRL_MAGIC,
        BROADCAST_CTRL_ACTIVATE,
    ]) + int(epoch).to_bytes(4, "big") + bytes(10)
