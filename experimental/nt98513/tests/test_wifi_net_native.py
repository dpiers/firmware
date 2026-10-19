"""Production rtnetlink/resolver tests; native Linux root inside isolated Docker.

All networking syscalls are link-wrapped, including unexpected socket domains.
Run the suite in --network none --cap-drop ALL with source mounted read-only.
Set WIFI_NET_TEST_BUILD to preserve compiler commands, binaries and test output.
A non-Linux/non-root host skips this privileged-file-metadata test suite.
"""
from pathlib import Path
import hashlib
import json
import os
import platform
import shlex
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
SRC = Path(os.environ.get("WIFI_NET_TEST_SOURCE", HERE.parent / "package/nt98513-wifi-control/src"))
CASES = (
    "happy renew duplicate address_ack route_ack dns_rollback cleanup_ack foreign "
    "route_metric route_gateway route_protocol route_type route_scope route_family "
    "addr_local addr_prefix addr_scope addr_family addr_peer "
    "refuse_symlink refuse_mode refuse_parent refuse_hardlink refuse_fifo refuse_oversize "
    "conflict_content conflict_inode conflict_symlink absent ifindex_changed sequence "
    "no_socket invalid_lease interrupted_dump renew_error no_router tmp_collision "
    "foreign_same_interface binary_baseline refuse_parent_symlink "
    "close_clear_failure close_flag_failure truncated_dump normalized_dump"
).split()

@unittest.skipUnless(platform.system() == "Linux" and os.geteuid() == 0,
                     "requires isolated Linux root for production safe-path ownership checks")
class WifiNetNative(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = Path(os.environ["WIFI_NET_TEST_BUILD"]) if "WIFI_NET_TEST_BUILD" in os.environ else Path(tempfile.mkdtemp(prefix="wifi-net-build-"))
        cls.build.mkdir(parents=True, exist_ok=True)
        cls.binary = cls.build / "wifi_net_native"
        sources = [HERE / "wifi_net_native.c"] + [SRC / x for x in ("net.c", "resolver.c", "config.c", "lease.c")]
        wraps = ("socket", "bind", "setsockopt", "close", "if_nametoindex", "ioctl", "sendto", "recv")
        cmd = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-I", str(SRC)]
        cmd += [str(p) for p in sources] + ["-Wl,--wrap=" + w for w in wraps] + ["-o", str(cls.binary)]
        (cls.build / "command.txt").write_text(shlex.join(cmd) + "\n")
        inputs = sources + [SRC / "wifi.h", Path(__file__)]
        (cls.build / "sources.json").write_text(json.dumps({str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}, indent=2) + "\n")
        built = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (cls.build / "compiler.txt").write_text(subprocess.check_output(["cc", "--version"], text=True) + "\n" + built.stdout)
        if built.returncode:
            raise RuntimeError(built.stdout)

    def run_case(self, case):
        result = subprocess.run([str(self.binary), case], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10)
        (self.build / (case + ".txt")).write_text(result.stdout)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("PASS " + case, result.stdout)

for _case in CASES:
    setattr(WifiNetNative, "test_" + _case, lambda self, case=_case: self.run_case(case))

if __name__ == "__main__":
    unittest.main(verbosity=2)
