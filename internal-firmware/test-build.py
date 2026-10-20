#!/usr/bin/env python3
"""Check private-setting staging without any camera, credentials or NOR access."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().with_name("stage-private-config.sh")


class PrivateStageTests(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="anyka-internal-test-"))
        self.dest = Path(tempfile.mkdtemp(prefix="anyka-internal-config-test-"))
        self.dest.rmdir()
        self.source = self.work / "source"
        (self.source / "ssh").mkdir(parents=True)
        (self.source / "sdk-rtsp").mkdir()
        fixtures = {"wpa_supplicant.conf": "network={ssid=fixture}\n",
                    "web-access.json": '{"fixture":true}\n', "web-ui.conf": "0\n",
                    "ssh/authorized_keys": "fixture-public-key\n",
                    "ssh/dropbear_ed25519_host_key": "fixture-host-key\n",
                    "sdk-rtsp/native-wdr.conf": "0\n", "sdk-rtsp/motion-enabled.conf": "1\n"}
        for name, value in fixtures.items(): (self.source / name).write_text(value)
        self.index = self.work / "SDK-INDEX"
        self.index.write_text("F SHA256SUMS\nF start.sh\nD www\nD www/cgi-bin\nF www/cgi-bin/isp.cgi\n")
        # Simulate the camera's tmpfs mount table on the PC. Every other
        # filesystem command uses the real host BusyBox and private fixtures.
        self.busybox = self.work / "busybox-fixture"
        real = shutil.which("busybox") or "/bin/busybox"
        self.busybox.write_text('#!/bin/sh\nif [ "$1" = awk ] && [ "${3:-}" = /proc/mounts ]; then\n'
                                ' [ ! -e "' + str(self.work / "disk-tmp") + '" ]; exit $?\nfi\n'
                                'exec "' + real + '" "$@"\n')
        self.busybox.chmod(0o700)

    def tearDown(self):
        shutil.rmtree(self.work)
        if self.dest.exists(): shutil.rmtree(self.dest)

    def invoke(self, dest=None):
        return subprocess.run(["sh", str(SCRIPT), str(self.source), str(dest or self.dest), str(self.index)],
                              env=dict(os.environ, BUSYBOX=str(self.busybox)),
                              capture_output=True, text=True)

    def test_stage_keeps_cgi_directories_real_and_secrets_private(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        root = self.dest / "openipc-persist"
        self.assertEqual((root / "web-access.json").stat().st_mode & 0o777, 0o600)
        self.assertFalse((root / "sdk-rtsp/www/cgi-bin").is_symlink())
        self.assertEqual(os.readlink(root / "sdk-rtsp/www/cgi-bin/isp.cgi"),
                         "/app/runtime/sdk-rtsp/www/cgi-bin/isp.cgi")
        self.assertEqual((root / "sdk-rtsp/native-wdr.conf").read_text(), "0\n")
        self.assertTrue((self.dest / "PRIVATE-STAGE-READY").is_file())

    def test_rejects_symlinked_key_without_ready_marker(self):
        p = self.source / "ssh/authorized_keys"
        p.unlink(); p.symlink_to("../web-access.json")
        self.assertNotEqual(self.invoke().returncode, 0)
        self.assertFalse((self.dest / "PRIVATE-STAGE-READY").exists())

    def test_rejects_escape_from_destination(self):
        self.index.write_text("D ../escape\n")
        self.assertNotEqual(self.invoke().returncode, 0)
        self.assertFalse((self.dest / "PRIVATE-STAGE-READY").exists())

    def test_rejects_non_ram_or_nested_destination(self):
        wrong = self.work / "not-ram-prefix"
        self.assertNotEqual(self.invoke(wrong).returncode, 0)
        self.assertFalse(wrong.exists())
        self.assertNotEqual(self.invoke(Path(str(self.dest) + "/nested")).returncode, 0)

    def test_unknown_setting_requires_review(self):
        (self.source / "sdk-rtsp/unknown.conf").write_text("fixture\n")
        self.assertNotEqual(self.invoke().returncode, 0)
        self.assertFalse((self.dest / "PRIVATE-STAGE-READY").exists())

    def test_requires_tmpfs_before_creating_anything(self):
        (self.work / "disk-tmp").touch()
        self.assertNotEqual(self.invoke().returncode, 0)
        self.assertFalse(self.dest.exists())


if __name__ == "__main__":
    unittest.main()
