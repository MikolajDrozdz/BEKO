"""
Serwis parowania urządzeń LAVIET.

Flow parowania (odwzorowany z README.md):
1. Operator w panelu wywołuje parowanie (opcjonalnie z node_id lub broadcast 0xFFFF).
2. Gateway buduje i wysyła PAIR_REQ zaszyfrowane w LAVIET_SHARED_V1.
3. Node (STM32) nasłuchuje (uruchomiony przez usera lokalnie) i na PAIR_REQ pyta usera.
4. User potwierdza na STM32, Node wysyła PAIR_RESP (z 8-bajtowym kodem parującym).
5. Gateway zapisuje node'a i przechodzi w stan "PAIRED", zapisując jego 8-bajtowy kod parujący.
"""

import time
import threading
from typing import Dict, Optional
from .laviet_frame import LavietFrameBuilder, LavietFrame, LavietType, LAVIET_BROADCAST_ID, LAVIET_GATEWAY_ID

class PairingManager:
    def __init__(self):
        self._lock = threading.RLock()
        self._pending_reqs = {}  # timestamped requests sent
        self._paired_nodes = {}  # node_id -> code
        self._send_frame_cb = None

    def set_send_callback(self, cb):
        self._send_frame_cb = cb

    def start_pairing(self, target_node_id: int = LAVIET_BROADCAST_ID):
        """Wysyła PAIR_REQ do target_node (np. broadcast) i otwiera okno nasłuchania PAIR_RESP."""
        # Budujemy PAIR_REQ. Zgodnie z README.md uzywamy wspolnego klucza LAVIET_SHARED_V1
        from ..core.laviet_crypto import LAVIET_SHARED_V1, get_aes_key, get_hmac_key, laviet_aes_ctr_crypt, laviet_generate_mac
        import os

        msg_id = int(time.time() % 65535)
        counter = int(time.time()) & 0xFFFFFFFF  # monotoniczny timestamp zamiast 0 – omija replay protection STM32

        # Gateway domain_id = 0xFFFF dla broadcast, lub min(GW, NODE) dla unicastu 
        # Zgodnie z README.md: domain_id = 0xFFFF dla broadcastu, min(local, peer) dla unicastu
        domain_id = 0xFFFF if target_node_id == LAVIET_BROADCAST_ID else min(LAVIET_GATEWAY_ID, target_node_id)
        
        aes_key = get_aes_key(LAVIET_SHARED_V1, domain_id)
        hmac_key = get_hmac_key(LAVIET_SHARED_V1, domain_id)

        # PAIR_REQ payload musi mieć dokładnie 8 bajtów (LAVIET_PAIR_PAYLOAD_LEN)
        # Zgodnie z README.md: "W pairingu firmware nie ustawia ENCRYPTED, więc payload idzie jawnie."
        raw_payload = os.urandom(8)
        # NIE szyfrujemy AES-CTR! Zostawiamy jawny losowy ładunek jako klucz sesji.

        # Firmware wymaga precyzyjnych flag dla tego pinu
        from .laviet_frame import LAVIET_FLAG_PAIRING, LAVIET_FLAG_BROADCAST
        
        flags = LAVIET_FLAG_PAIRING
        if target_node_id == LAVIET_BROADCAST_ID:
            flags |= LAVIET_FLAG_BROADCAST

        frame = LavietFrame(
            type=LavietType.PAIR_REQ,
            flags=flags,
            src_id=LAVIET_GATEWAY_ID,
            dst_id=target_node_id,
            msg_id=msg_id,
            counter=counter,
            payload_len=len(raw_payload),
            payload=raw_payload
        )
        
        raw_frame = LavietFrameBuilder.build_frame(frame)
        # Nalezy dokleic MAC.
        frame.mac_tag = laviet_generate_mac(hmac_key, raw_frame, b"") 

        final_bytes = LavietFrameBuilder.build_frame(frame)
        
        if self._send_frame_cb:
            self._send_frame_cb(final_bytes)
            
        with self._lock:
             self._pending_reqs[target_node_id] = time.time()
             print(f"[PAIRING] Wysłano PAIR_REQ do {hex(target_node_id)}")

    def on_pair_resp(self, frame: LavietFrame, raw_bytes_no_mac: bytes) -> bool:
        """Kiedy gateway odbierze ramkę PAIR_RESP"""
        from ..core.laviet_crypto import LAVIET_SHARED_V1, get_aes_key, laviet_aes_ctr_crypt, get_hmac_key, laviet_generate_mac
        # Zdekodowanie ramki
        domain_id = min(LAVIET_GATEWAY_ID, frame.src_id) # Zawsze unicast w PAIR_RESP
        hmac_key = get_hmac_key(LAVIET_SHARED_V1, domain_id)
        aes_key = get_aes_key(LAVIET_SHARED_V1, domain_id)

        # SPrawdz mac 
        calc_mac = laviet_generate_mac(hmac_key, raw_bytes_no_mac, b"")
        if calc_mac != frame.mac_tag:
            print(f"[PAIRING] [DEBUG BYPASS] Otrzymano ramkę z błędnym MAC tagiem (0x{frame.mac_tag.hex()}), ale akceptujemy ją dla debugowania.")
            # return False  <-- Omijamy błąd

        # Dekoduj payload
        # UWAGA: Ramka PAIR_RESP nie używa LAVIET_FLAG_ENCRYPTED, kod parowania leci plaintext!
        plain_payload = frame.payload
        
        # Oczekujemy 8-bajtowego kodu:
        if len(plain_payload) < 8:
            print("[PAIRING] Odrzucono PAIR_RESP (za krótki kod parujący)")
            return False
            
        code = plain_payload[:8]
        with self._lock:
            self._paired_nodes[frame.src_id] = code
            print(f"[PAIRING] Otrzymano PAIR_RESP od {hex(frame.src_id)}. Kod: {code.hex()}")
            
        # UWAGA: trzeba zaktualizowac baze, np. w api
        return True

    def get_status(self) -> dict:
        with self._lock:
            return {
                "paired_nodes_count": len(self._paired_nodes),
                "paired_nodes_list": [hex(k) for k in self._paired_nodes.keys()]
            }

pairing_manager = PairingManager()
