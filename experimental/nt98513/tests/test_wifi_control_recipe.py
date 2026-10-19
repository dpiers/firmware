"""Exercise the real local-package SITE expansion at its Buildroot boundary."""
from pathlib import Path
import subprocess
import tempfile
import unittest

RECIPE = Path(__file__).resolve().parents[1] / "package/nt98513-wifi-control/nt98513-wifi-control.mk"

class ControlRecipe(unittest.TestCase):
    def test_local_site_has_no_trailing_slash(self):
        # Buildroot's PKGDIR ends in '/'; passing it through broke show-info.
        with tempfile.TemporaryDirectory(prefix="wifi-site-") as directory:
            makefile = Path(directory) / "Makefile"
            makefile.write_text(
                "include " + str(RECIPE) + "\n"
                "$(info SITE=$(NT98513_WIFI_CONTROL_SITE))\n"
                ".PHONY: check\ncheck:\n\t@:\n")
            for pkgdir in ("/fixture/nt98513-wifi-control/", "/fixture/nt98513-wifi-control"):
                with self.subTest(pkgdir=pkgdir):
                    result = subprocess.run(
                        ["make", "--silent", "-f", str(makefile), "check",
                         "NT98513_WIFI_CONTROL_PKGDIR=" + pkgdir],
                        capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout, "SITE=/fixture/nt98513-wifi-control\n")

if __name__ == "__main__":
    unittest.main()
