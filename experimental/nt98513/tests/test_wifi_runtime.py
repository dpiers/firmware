"""Compile and execute production Wi-Fi policy with non-mutating boundaries."""
from pathlib import Path
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
SRC = HERE.parent / 'package/nt98513-wifi-control/src'

class WifiRuntime(unittest.TestCase):
    def test_actual_state_and_input_code(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'runtime'
            subprocess.run(['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L',
                '-D_DARWIN_C_SOURCE', '-Wall', '-Wextra', '-Werror', '-I', str(SRC),
                str(HERE / 'wifi_runtime.c'), str(SRC / 'state.c'),
                str(SRC / 'config.c'), str(SRC / 'lease.c'),
                '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

if __name__ == '__main__':
    unittest.main()
