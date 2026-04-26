import unittest

from app.core.laviet_crypto import LAVIET_SHARED_V1, get_hmac_key, laviet_generate_mac
from app.services.laviet_frame import (
    LAVIET_BROADCAST_ID,
    LAVIET_FLAG_BROADCAST,
    LAVIET_FLAG_PAIRING,
    LAVIET_GATEWAY_ID,
    LavietFrame,
    LavietFrameBuilder,
    LavietType,
)


class LavietHmacCompatibilityTest(unittest.TestCase):
    def test_pair_req_broadcast_matches_node_reference(self):
        frame = LavietFrame(
            type=LavietType.PAIR_REQ,
            flags=LAVIET_FLAG_PAIRING | LAVIET_FLAG_BROADCAST,
            src_id=LAVIET_GATEWAY_ID,
            dst_id=LAVIET_BROADCAST_ID,
            msg_id=0x1001,
            counter=0x00000001,
            payload_len=8,
            payload=b"12345678",
        )

        hmac_key = get_hmac_key(LAVIET_SHARED_V1, LAVIET_BROADCAST_ID)
        mac_input = LavietFrameBuilder.build_mac_input(frame)
        mac_tag = laviet_generate_mac(hmac_key, mac_input, b"")

        self.assertEqual(
            hmac_key.hex(),
            "3d4dbff287a737a7be99f1d3d901eff2a8f795084603c4549bd220c609f133d6",
        )
        self.assertEqual(
            mac_input.hex(),
            "14280001ffff100100000001083132333435363738",
        )
        self.assertEqual(
            mac_tag.hex(),
            "27e0229407535660f6f0249eb9b3560c1568a27520266aa06f15c1667959ddf9",
        )

        frame.mac_tag = mac_tag
        self.assertEqual(LavietFrameBuilder.build_frame(frame), mac_input + mac_tag)

    def test_payload_len_mismatch_is_rejected(self):
        frame = LavietFrame(
            type=LavietType.DATA,
            flags=0,
            src_id=LAVIET_GATEWAY_ID,
            dst_id=0x1234,
            msg_id=1,
            counter=1,
            payload_len=2,
            payload=b"abc",
        )

        with self.assertRaises(ValueError):
            LavietFrameBuilder.build_mac_input(frame)


if __name__ == "__main__":
    unittest.main()
