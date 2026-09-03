#!/usr/bin/env python3
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from run_multi_host import command_for, parse_config


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
        self.assertEqual(receiver[-2:], ["--module-id", "3"])

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
