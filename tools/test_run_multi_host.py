#!/usr/bin/env python3
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from run_multi_host import command_for, parse_compare, parse_config


class MultiHostTests(unittest.TestCase):
    def write_config(self, text: str) -> Path:
        handle = tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False)
        with handle:
            handle.write(text)
        self.addCleanup(Path(handle.name).unlink, missing_ok=True)
        return Path(handle.name)

    def test_builds_commands_for_both_roles(self) -> None:
        path = self.write_config(
            "3 enp3s0 rx0 192.168.13.2 192.168.13.1 19003 tx-ns -\n"
        )
        channel = parse_config(path)[0]

        sender = command_for(channel, "sender", Path("/opt/cmb/sender"), 1000, True)
        self.assertEqual(sender[:4], ["ip", "netns", "exec", "tx-ns"])
        self.assertIn("--bind-host", sender)
        self.assertEqual(sender[sender.index("--bind-host") + 1], "192.168.13.2")
        self.assertIn("192.168.13.1", sender)
        self.assertIn("19003", sender)
        self.assertIn("--module-id", sender)

        receiver = command_for(channel, "receiver", Path("/opt/cmb/receiver"), 1000, False)
        self.assertEqual(receiver[0], "/opt/cmb/receiver")
        self.assertEqual(receiver[1:4], ["19003", "1000", "192.168.13.1"])
        self.assertEqual(receiver[4:8], ["--module-id", "3", "--rate-hz", "200"])

    def test_rate_and_source_are_passed_to_sender(self) -> None:
        path = self.write_config("4 a b 192.168.1.2 192.168.1.1 9004 - -\n")
        channel = parse_config(path)[0]
        sender = command_for(channel, "sender", Path("/opt/cmb/sender"), 10, False, False, 240, Path("/data/src"))
        self.assertEqual(sender[sender.index("--rate-hz") + 1], "240")
        self.assertEqual(sender[sender.index("--input") + 1], "/data/src/source-m004.bin")
        receiver = command_for(channel, "receiver", Path("/opt/cmb/receiver"), 10, False, False, 240, Path("/data/src"))
        self.assertNotIn("--input", receiver)
        self.assertEqual(receiver[receiver.index("--rate-hz") + 1], "240")

    def test_parse_compare_reads_summary(self) -> None:
        text = (
            "MISSING frame_id=5 (expected s.bin:5)\n"
            "frame_bytes=13684\n"
            "expected_frames=10 invalid=0 out_of_order=0 trailing_bytes=0\n"
            "actual_frames=9 invalid=1 out_of_order=0 trailing_bytes=0\n"
            "identical=8 mismatched=1 missing=1 unexpected=0\n"
            "compared_bits=985248 bit_errors=3 ber=3.045e-06 frame_loss_rate=1.000e-01\n"
            "result=DIFFERENT\n"
        )
        values = parse_compare(text)
        self.assertEqual(values["expected_frames"], "10")
        self.assertEqual(values["actual_frames"], "9")
        self.assertEqual(values["actual_invalid"], "1")
        self.assertEqual(values["missing"], "1")
        self.assertEqual(values["bit_errors"], "3")
        self.assertEqual(values["result"], "DIFFERENT")

    def test_sender_can_save_sent_frames(self) -> None:
        path = self.write_config("1 a b 192.168.1.2 192.168.1.1 9001 - -\n")
        channel = parse_config(path)[0]
        sender = command_for(channel, "sender", Path("/opt/cmb/sender"), 10, False, True)
        self.assertEqual(sender[-2:], ["--save-sent", "captures/sent"])
        receiver = command_for(channel, "receiver", Path("/opt/cmb/receiver"), 10, False, True)
        self.assertNotIn("--save-sent", receiver)

    def test_rejects_duplicate_ports(self) -> None:
        path = self.write_config(
            "0 a b 192.168.1.2 192.168.1.1 9000 - -\n"
            "1 c d 192.168.2.2 192.168.2.1 9000 - -\n"
        )
        with self.assertRaisesRegex(ValueError, "duplicate port"):
            parse_config(path)

    def test_rejects_invalid_ipv4_address(self) -> None:
        path = self.write_config("0 a b not-an-ip 192.168.1.1 9000 - -\n")
        with self.assertRaisesRegex(ValueError, "invalid tx_ip"):
            parse_config(path)


if __name__ == "__main__":
    unittest.main()
