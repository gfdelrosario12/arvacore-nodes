#!/usr/bin/env python3
"""
test_provisioning_and_flasher.py - Comprehensive Unit & Mock Integration Tests
For ArvaNode Dynamic Provisioner and ESP32 Auto-Flasher Tooling
ArvaCore Smart Irrigation Ecosystem (2026)
"""

import json
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch, MagicMock

# Add scripts directory to module path
TEST_DIR = Path(__file__).resolve().parent
REPO_ROOT = TEST_DIR.parent
SCRIPTS_DIR = REPO_ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS_DIR))

from node_provisioner import NodeProvisioner, MAX_PROFILES, DEFAULT_PROFILES
from esp32_flasher import Esp32Flasher, detect_serial_devices


class TestNodeProvisioner(unittest.TestCase):
    def setUp(self):
        self.test_dir = tempfile.mkdtemp(prefix="arva_test_prov_")
        self.profiles_dir = Path(self.test_dir) / "profiles"
        self.prov = NodeProvisioner(profiles_dir=self.profiles_dir)

    def tearDown(self):
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def test_generate_single_profile(self):
        """Test generating next available default profile."""
        profile = self.prov.generate_profile()
        self.assertEqual(profile["logical_node_id"], "ARVANODE-001")
        self.assertEqual(profile["display_name"], "ArvaNode Field 01")
        self.assertEqual(profile["status"], "GENERATED")
        self.assertTrue(profile["device_key"].startswith("dev_sec_"))
        self.assertEqual(profile["sensor_config"]["telemetry_interval_ms"], 3000)

        # Confirm file on disk
        file_path = self.profiles_dir / "ARVANODE-001.json"
        self.assertTrue(file_path.exists())
        # Check permissions 0600 on POSIX
        if os.name == "posix":
            mode = oct(file_path.stat().st_mode & 0o777)
            self.assertEqual(mode, "0o600")

    def test_generate_all_10_default_profiles(self):
        """Test generating all 10 default profiles sequentially."""
        for expected in DEFAULT_PROFILES:
            p = self.prov.generate_profile()
            self.assertEqual(p["logical_node_id"], expected["id"])
            self.assertEqual(p["display_name"], expected["name"])

        profiles = self.prov.list_profiles()
        self.assertEqual(len(profiles), 10)

    def test_reject_11th_profile_limit(self):
        """Enforces strictly up to 10 unique node identities."""
        for _ in range(10):
            self.prov.generate_profile()

        with self.assertRaises(ValueError) as ctx:
            self.prov.generate_profile()
        self.assertIn("Maximum limit of 10 active node profiles reached", str(ctx.exception))

    def test_explicit_id_generation_and_duplicate_rejection(self):
        """Test explicit ID selection and rejection of duplicates without --force."""
        self.prov.generate_profile(node_id="ARVANODE-005")
        profile = self.prov.get_profile("ARVANODE-005")
        self.assertIsNotNone(profile)
        self.assertEqual(profile["logical_node_id"], "ARVANODE-005")

        # Duplicate rejection
        with self.assertRaises(ValueError) as ctx:
            self.prov.generate_profile(node_id="ARVANODE-005")
        self.assertIn("already exists", str(ctx.exception))

        # Successful overwrite with force=True
        p_updated = self.prov.generate_profile(node_id="ARVANODE-005", force=True, display_name="Updated Node 5")
        self.assertEqual(p_updated["display_name"], "Updated Node 5")

    def test_secret_redaction_in_inspect(self):
        """Verify secrets are redacted by default in inspection."""
        self.prov.generate_profile(node_id="ARVANODE-002")
        redacted = self.prov.inspect_profile("ARVANODE-002", redact=True)
        self.assertTrue("****" in redacted["device_key"])
        self.assertEqual(redacted["network"]["wifi_password"], "********")

        raw = self.prov.inspect_profile("ARVANODE-002", redact=False)
        self.assertFalse("****" in raw["device_key"])
        self.assertEqual(raw["network"]["wifi_password"], "hatdog123")

    def test_validation_rules(self):
        """Test schema and rule invariants validation."""
        self.prov.generate_profile(node_id="ARVANODE-001")
        is_valid, errors = self.prov.validate_profiles()
        self.assertTrue(is_valid)
        self.assertEqual(len(errors), 0)

        # Corrupt one profile with invalid interval
        corrupted_path = self.profiles_dir / "ARVANODE-001.json"
        with open(corrupted_path, "r") as f:
            data = json.load(f)
        data["sensor_config"]["telemetry_interval_ms"] = 5000  # violates 3000ms rule
        with open(corrupted_path, "w") as f:
            json.dump(data, f)

        is_valid, errors = self.prov.validate_profiles()
        self.assertFalse(is_valid)
        self.assertTrue(any("must be strictly 3000ms" in e for e in errors))

    def test_export_header(self):
        """Test export_header produces syntactically valid C/C++ definitions."""
        self.prov.generate_profile(node_id="ARVANODE-003")
        hdr_out = Path(self.test_dir) / "node_config.h"
        exported = self.prov.export_header("ARVANODE-003", hdr_out)
        self.assertTrue(exported.exists())

        content = exported.read_text()
        self.assertIn('#define ARVA_CONFIG_INJECTED 1', content)
        self.assertIn('#define NODE_LOGICAL_ID "ARVANODE-003"', content)
        self.assertIn('#define NODE_TELEMETRY_INTERVAL_MS 3000UL', content)
        self.assertIn('#define PIN_SOIL_ANALOG 34', content)
        self.assertIn('#define PIN_ULTRASONIC_TRIG 5', content)
        self.assertIn('#define PIN_ULTRASONIC_ECHO 18', content)

    def test_status_transitions(self):
        """Test lifecycle status transitions."""
        self.prov.generate_profile(node_id="ARVANODE-004")
        self.prov.update_status("ARVANODE-004", "PROVISIONED")
        p = self.prov.get_profile("ARVANODE-004")
        self.assertEqual(p["status"], "PROVISIONED")

        self.prov.update_status("ARVANODE-004", "FLASHED", hardware_mac="24:0A:C4:01:02:03", port="/dev/ttyUSB0")
        p = self.prov.get_profile("ARVANODE-004")
        self.assertEqual(p["status"], "FLASHED")
        self.assertEqual(p["hardware_mac"], "24:0A:C4:01:02:03")
        self.assertEqual(p["metadata"]["last_flashed_port"], "/dev/ttyUSB0")

        self.prov.update_status("ARVANODE-004", "REVOKED")
        p = self.prov.get_profile("ARVANODE-004")
        self.assertEqual(p["status"], "REVOKED")


class TestEsp32Flasher(unittest.TestCase):
    def setUp(self):
        self.test_dir = tempfile.mkdtemp(prefix="arva_test_flasher_")
        self.profiles_dir = Path(self.test_dir) / "profiles"
        self.prov = NodeProvisioner(profiles_dir=self.profiles_dir)
        self.flasher = Esp32Flasher(provisioner=self.prov)

    def tearDown(self):
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def test_devices_detection_non_crashing(self):
        """Device detection must return a list without crashing on any environment."""
        devs = self.flasher.list_devices()
        self.assertIsInstance(devs, list)

    def test_build_firmware_dry_run(self):
        """Dry-run compilation must return planned command without invoking compiler."""
        self.prov.generate_profile(node_id="ARVANODE-001")
        res = self.flasher.build_firmware("ARVANODE-001", dry_run=True, export_header=False)
        self.assertTrue(res["success"])
        self.assertTrue(res["dry_run"])
        self.assertIn("arduino-cli compile", res["command"])

    def test_flash_device_dry_run(self):
        """Dry-run flashing simulates flashing without touching hardware."""
        self.prov.generate_profile(node_id="ARVANODE-002")
        res = self.flasher.flash_device("ARVANODE-002", port="/dev/ttyUSB0", dry_run=True)
        self.assertTrue(res["success"])
        self.assertTrue(res["dry_run"])
        self.assertEqual(res["port"], "/dev/ttyUSB0")

    def test_flash_fails_when_device_missing_and_not_dry_run(self):
        """Flashing fails gracefully with clear error when port does not exist."""
        self.prov.generate_profile(node_id="ARVANODE-003")
        with self.assertRaises(FileNotFoundError) as ctx:
            self.flasher.flash_device("ARVANODE-003", port="/dev/nonexistent_port_12345", dry_run=False)
        self.assertTrue("not found" in str(ctx.exception) or "not detected" in str(ctx.exception))

    def test_verify_boot_dry_run(self):
        """Dry run serial boot verification correctly simulates boot banner."""
        res = self.flasher.verify_boot("/dev/ttyUSB0", expected_node_id="ARVANODE-001", dry_run=True)
        self.assertTrue(res["success"])
        self.assertEqual(res["verified_node_id"], "ARVANODE-001")

    def test_verify_boot_handles_missing_port(self):
        """Verify reports failure when serial port does not exist."""
        res = self.flasher.verify_boot("/dev/nonexistent_port_999", dry_run=False)
        self.assertFalse(res["success"])
        self.assertIn("does not exist", res["error"])

    def test_generate_report(self):
        """Test generating report across all profiles."""
        self.prov.generate_profile(node_id="ARVANODE-001")
        self.prov.generate_profile(node_id="ARVANODE-002")
        rep = self.flasher.generate_report()
        self.assertEqual(rep["total_profiles"], 2)
        self.assertEqual(rep["max_allowed"], 10)
        self.assertIn("profiles", rep)
        self.assertEqual(len(rep["profiles"]), 2)


if __name__ == "__main__":
    unittest.main()
