"""Strict-runner controls use real unittest outcomes, without network/target work."""
import importlib.util
import contextlib
import io
from pathlib import Path
import unittest
from unittest.mock import patch

PATH = Path(__file__).with_name("run_nt98513_wifi_native.py")
spec = importlib.util.spec_from_file_location("native_runner", PATH)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

class RunnerControls(unittest.TestCase):
    def test_actual_suite_discovery(self):
        suites = runner.load_suites()
        self.assertEqual(runner.validate_suites(suites), 58)

    def test_missing_suite_rejected(self):
        suites = runner.load_suites()
        suites.pop("test_wifi_net_native.WifiNetNative")
        with self.assertRaises(ValueError):
            runner.validate_suites(suites)

    def test_missing_method_rejected(self):
        suites = runner.load_suites()
        key = "test_wifi_process_native.NativeProcesses"
        suites[key] = unittest.TestSuite(list(suites[key])[1:])
        with self.assertRaises(ValueError):
            runner.validate_suites(suites)

    def test_duplicate_method_cannot_hide_missing_method(self):
        suites = runner.load_suites()
        key = "test_wifi_process_native.NativeProcesses"
        cases = list(suites[key]); cases[0] = cases[1]
        suites[key] = unittest.TestSuite(cases)
        with self.assertRaises(ValueError):
            runner.validate_suites(suites)

    def result(self, outcome):
        class Fixture(unittest.TestCase):
            def runTest(self):
                if outcome == "skip": self.skipTest("negative control")
                if outcome == "error": raise RuntimeError("negative control")
                if outcome == "failure": self.fail("negative control")
        class Expected(unittest.TestCase):
            @unittest.expectedFailure
            def runTest(self): self.fail("negative control")
        class Unexpected(unittest.TestCase):
            @unittest.expectedFailure
            def runTest(self): pass
        first = Expected() if outcome == "expected" else Unexpected() if outcome == "unexpected" else Fixture()
        cases = [first] + [unittest.FunctionTestCase(lambda: None) for _ in range(57)]
        return unittest.TextTestRunner(stream=io.StringIO()).run(unittest.TestSuite(cases))

    def test_clean_58_results_accepted(self):
        self.assertTrue(runner.clean_result(self.result("pass")))

    def test_nonclean_results_rejected(self):
        for outcome in ("skip", "error", "failure", "expected", "unexpected"):
            with self.subTest(outcome=outcome):
                self.assertFalse(runner.clean_result(self.result(outcome)))

    def test_incomplete_execution_rejected(self):
        result = unittest.TextTestRunner(stream=io.StringIO()).run(unittest.TestSuite())
        self.assertFalse(runner.clean_result(result))

    def test_entrypoint_propagates_gate_failures(self):
        # Synthetic unittest inputs exercise the gate itself, not native code.
        # The unchanged native cases run separately through the real entrypoint.
        for outcome in ("pass", "skip", "error", "failure", "missing_suite", "missing_method"):
            with self.subTest(outcome=outcome):
                suites = {}
                first = True
                for name, methods in runner.EXPECTED.items():
                    cases = []
                    for method in methods:
                        def body(mode=outcome if first else "pass"):
                            if mode == "skip": raise unittest.SkipTest("entrypoint control")
                            if mode == "error": raise RuntimeError("entrypoint control")
                            if mode == "failure": raise AssertionError("entrypoint control")
                        case = unittest.FunctionTestCase(body)
                        case.id = lambda identity=name + ".test_" + method: identity
                        cases.append(case); first = False
                    suites[name] = unittest.TestSuite(cases)
                key = "test_wifi_process_native.NativeProcesses"
                if outcome == "missing_suite": suites.pop(key)
                if outcome == "missing_method": suites[key] = unittest.TestSuite(list(suites[key])[1:])
                with patch.object(runner, "check_prerequisites"), patch.object(runner, "load_suites", return_value=suites), contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(runner.main(), 0 if outcome == "pass" else 1)

    def test_wrong_prerequisites_rejected(self):
        for system, uid, enabled in (("Darwin", 0, "1"), ("Linux", 1000, "1"), ("Linux", 0, "")):
            with self.subTest(system=system, uid=uid, enabled=enabled):
                with patch.object(runner.platform, "system", return_value=system), patch.object(runner.os, "geteuid", return_value=uid), patch.dict(runner.os.environ, {"NT98513_WIFI_NATIVE_TESTS": enabled}, clear=True):
                    with self.assertRaises(ValueError): runner.check_prerequisites()

if __name__ == "__main__":
    unittest.main(verbosity=2)
