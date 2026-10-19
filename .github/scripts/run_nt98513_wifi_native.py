#!/usr/bin/env python3
"""Run the accepted native Wi-Fi suites; skipped or missing coverage fails CI."""
import importlib
import os
from pathlib import Path
import platform
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "experimental/nt98513/tests"
EXPECTED = {
    "test_wifi_process_native.NativeProcesses": """
        same_identity_disconnect_retires_generation
        failed_cleanup_stop_reports_failure status_identity_and_duplicate_events
        lease_sender_length_token_and_generation lease_ack_waits_for_bounded_network_work
        silent_lease_expiry supplicant_death control_socket_failure exec_failure
        term_resistant_dhcp_and_restart waitpid_interruption_preserves_ownership
        entropy_no_fallback_and_deadline owner_config_file_admission
        silent_acquisition_has_deadline_and_backoff
    """.split(),
    "test_wifi_net_native.WifiNetNative": """
        happy renew duplicate address_ack route_ack dns_rollback cleanup_ack foreign
        route_metric route_gateway route_protocol route_type route_scope route_family
        addr_local addr_prefix addr_scope addr_family addr_peer
        refuse_symlink refuse_mode refuse_parent refuse_hardlink refuse_fifo refuse_oversize
        conflict_content conflict_inode conflict_symlink absent ifindex_changed sequence
        no_socket invalid_lease interrupted_dump renew_error no_router tmp_collision
        foreign_same_interface binary_baseline refuse_parent_symlink
        close_clear_failure close_flag_failure truncated_dump normalized_dump
    """.split(),
}
TOTAL = 58


def check_prerequisites():
    if platform.system() != "Linux" or os.geteuid() != 0:
        raise ValueError("native Wi-Fi tests require Linux UID 0 inside the isolated container")
    if os.environ.get("NT98513_WIFI_NATIVE_TESTS") != "1":
        raise ValueError("NT98513_WIFI_NATIVE_TESTS=1 is required; skips are not coverage")
    if os.environ.get("WIFI_NET_TEST_SOURCE"):
        raise ValueError("WIFI_NET_TEST_SOURCE override is forbidden in the CI gate")


def load_suites():
    sys.path.insert(0, str(TESTS))
    suites = {}
    for qualified in EXPECTED:
        module_name, class_name = qualified.split(".")
        module = importlib.import_module(module_name)
        if Path(module.__file__).resolve() != TESTS / (module_name + ".py"):
            raise ValueError("test module was imported outside the checkout: " + module_name)
        cls = getattr(module, class_name)
        if not issubclass(cls, unittest.TestCase):
            raise ValueError("required suite is not a TestCase: " + qualified)
        suites[qualified] = unittest.defaultTestLoader.loadTestsFromTestCase(cls)
    return suites


def validate_suites(suites):
    if set(suites) != set(EXPECTED):
        raise ValueError("missing or unexpected native test suite")
    total = 0
    for name, methods in EXPECTED.items():
        expected = {name + ".test_" + method for method in methods}
        actual = [case.id() for case in suites[name]]
        if len(actual) != len(expected) or set(actual) != expected:
            raise ValueError("missing, duplicate or unexpected native test method in " + name)
        total += len(actual)
    if total != TOTAL:
        raise ValueError("expected exactly 14 process and 44 network tests")
    return total


def clean_result(result):
    return (result.testsRun == TOTAL and result.wasSuccessful() and
            not result.skipped and not result.errors and not result.failures and
            not result.expectedFailures and not result.unexpectedSuccesses)


def main():
    try:
        check_prerequisites()
        suites = load_suites()
        validate_suites(suites)
    except (ValueError, ImportError, AttributeError, TypeError) as error:
        print("Native Wi-Fi gate refused: " + str(error), file=sys.stderr)
        return 1
    result = unittest.TextTestRunner(verbosity=2).run(unittest.TestSuite(suites.values()))
    if not clean_result(result):
        print("Native Wi-Fi gate failed: all 58 required tests must execute cleanly without skips", file=sys.stderr)
        return 1
    print("Native Wi-Fi gate: 14 process + 44 network tests executed; zero skips or non-clean outcomes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
