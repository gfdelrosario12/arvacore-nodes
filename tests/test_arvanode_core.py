#!/usr/bin/env python3
"""
tests/test_arvanode_core.py - Comprehensive Unit & Integration Tests for arvanode_core
ArvaCore Smart Irrigation Ecosystem (2026)
"""

import os
import shutil
import tempfile
import unittest
from pathlib import Path

from arvanode_core.models import (
    NodeConfig,
    NetworkConfig,
    SensorConfig,
    PinoutConfig,
    ConfigurationError,
    ALLOWED_NODE_IDS
)
from arvanode_core.repository import ProfileRepository
from arvanode_core.build_service import BuildService, BuildResult
from arvanode_core.flash_service import FlashService, FlashResult
from arvanode_core.orchestrator import ProvisioningOrchestrator, ProvisioningResult


class TestArvaNodeModels(unittest.TestCase):
    """Tests canonical configuration models, validation, and serialization."""

    def test_default_config_validity(self):
        config = NodeConfig.from_default(1)
        self.assertEqual(config.logical_node_id, "ARVANODE-001")
        self.assertEqual(config.display_name, "ArvaNode Field 01")
        self.assertEqual(config.sensor.telemetry_interval_ms, 3000)
        # Validate passes without exception
        config.validate()

    def test_in_memory_modification_without_profile(self):
        """Verify editing configuration purely in-memory works seamlessly."""
        config = NodeConfig.from_default(2)
        # Create updated config with modified values
        updated_net = NetworkConfig(
            wifi_ssid="CustomFarmWiFi",
            wifi_password="SuperSecretPassword123",
            backend_base_url="https://api-aquaflow.gladwindr.me/api/v1",
            gateway_key="custom_gateway_key"
        )
        updated_sensor = SensorConfig(
            datum_offset_cm=20.0,
            min_distance_cm=2.0,
            max_distance_cm=450.0,
            soil_air_value=3100,
            soil_water_value=1500,
            telemetry_interval_ms=3000
        )
        custom_config = NodeConfig(
            logical_node_id=config.logical_node_id,
            display_name=config.display_name,
            network=updated_net,
            sensor=updated_sensor,
            pinout=config.pinout
        )
        custom_config.validate()

        payload = custom_config.to_dict()
        self.assertEqual(payload["network"]["wifi_ssid"], "CustomFarmWiFi")
        self.assertEqual(payload["sensor_config"]["soil_air_value"], 3100)

    def test_invalid_node_id_rejected(self):
        config = NodeConfig(logical_node_id="ARVANODE-999")
        with self.assertRaises(ConfigurationError):
            config.validate()

    def test_telemetry_interval_invariant(self):
        bad_sensor = SensorConfig(telemetry_interval_ms=1000)  # Must be 3000
        config = NodeConfig(sensor=bad_sensor)
        with self.assertRaises(ConfigurationError) as ctx:
            config.validate()
        self.assertIn("telemetry_interval_ms must be exactly 3000", str(ctx.exception))

    def test_invalid_soil_calibration(self):
        bad_sensor = SensorConfig(soil_air_value=1000, soil_water_value=2000)  # air must be strictly > water
        config = NodeConfig(sensor=bad_sensor)
        with self.assertRaises(ConfigurationError):
            config.validate()


class TestProfileRepository(unittest.TestCase):
    """Tests preset persistence and isolation."""

    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        self.repo = ProfileRepository(storage_dir=Path(self.temp_dir))

    def tearDown(self):
        shutil.rmtree(self.temp_dir)

    def test_save_and_load_profile(self):
        config = NodeConfig.from_default(3)
        updated_net = NetworkConfig(wifi_ssid="TestSSID")
        config = NodeConfig(
            logical_node_id=config.logical_node_id,
            display_name=config.display_name,
            network=updated_net,
            sensor=config.sensor,
            pinout=config.pinout
        )
        saved_path = self.repo.save(config)
        self.assertTrue(saved_path.exists())

        loaded = self.repo.load("ARVANODE-003")
        self.assertEqual(loaded.logical_node_id, "ARVANODE-003")
        self.assertEqual(loaded.network.wifi_ssid, "TestSSID")

    def test_list_profiles(self):
        self.assertEqual(len(self.repo.list_profiles()), 0)
        self.repo.save(NodeConfig.from_default(1))
        self.repo.save(NodeConfig.from_default(2))
        self.assertEqual(len(self.repo.list_profiles()), 2)

    def test_delete_profile(self):
        config = NodeConfig.from_default(1)
        self.repo.save(config)
        self.assertTrue(self.repo.exists("ARVANODE-001"))
        self.repo.delete("ARVANODE-001")
        self.assertFalse(self.repo.exists("ARVANODE-001"))


class TestBuildService(unittest.TestCase):
    """Tests header generation and isolated build invocation."""

    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        self.build_service = BuildService(repo_root=Path(self.temp_dir))

    def tearDown(self):
        shutil.rmtree(self.temp_dir)

    def test_generate_header(self):
        config = NodeConfig.from_default(1)
        header_file = Path(self.temp_dir) / "test_config.h"
        res_path = self.build_service.generate_header(config, target_header=header_file)
        self.assertTrue(res_path.exists())
        content = header_file.read_text()
        self.assertIn('#define NODE_LOGICAL_ID "ARVANODE-001"', content)
        self.assertIn("#define NODE_TELEMETRY_INTERVAL_MS 3000UL", content)
        self.assertIn("#define ARVA_CONFIG_INJECTED 1", content)

    def test_compile_dry_run(self):
        config = NodeConfig.from_default(1)
        result = self.build_service.compile(config, dry_run=True)
        self.assertTrue(result.success)
        self.assertTrue(result.dry_run)
        self.assertEqual(result.returncode, 0)
        self.assertIn("arduino-cli compile", " ".join(result.command))


class TestFlashService(unittest.TestCase):
    """Tests serial port detection and flash execution."""

    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        self.flash_service = FlashService()

    def tearDown(self):
        shutil.rmtree(self.temp_dir)

    def test_detect_devices_returns_list(self):
        devices = self.flash_service.discover_serial_ports()
        self.assertIsInstance(devices, list)

    def test_flash_dry_run(self):
        sketch_dir = Path(self.temp_dir) / "sketch"
        sketch_dir.mkdir()
        res = self.flash_service.flash(
            sketch_dir=sketch_dir,
            port="/dev/ttyUSB0",
            dry_run=True
        )
        self.assertTrue(res.success)
        self.assertTrue(res.dry_run)
        self.assertIn("arduino-cli upload", " ".join(res.command))

    def test_flash_nonexistent_port_fails(self):
        sketch_dir = Path(self.temp_dir) / "sketch"
        sketch_dir.mkdir()
        res = self.flash_service.flash(
            sketch_dir=sketch_dir,
            port="/dev/nonexistent_tty_arva_test",
            dry_run=False
        )
        self.assertFalse(res.success)
        self.assertIn("not found or board disconnected", res.error)


class TestProvisioningOrchestrator(unittest.TestCase):
    """Tests end-to-end orchestration pipeline and failure halting."""

    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        repo = ProfileRepository(storage_dir=Path(self.temp_dir) / "profiles")
        builder = BuildService(repo_root=Path(self.temp_dir))
        flasher = FlashService()
        self.orchestrator = ProvisioningOrchestrator(
            repository=repo,
            build_service=builder,
            flash_service=flasher
        )

    def tearDown(self):
        shutil.rmtree(self.temp_dir)

    def test_orchestrator_compile_only_dry_run(self):
        config = NodeConfig.from_default(1)
        result = self.orchestrator.run_compile_only(config, dry_run=True)
        self.assertTrue(result.success)
        self.assertEqual(result.stage, "COMPILATION")
        self.assertTrue(result.build_result.dry_run)

    def test_orchestrator_full_provision_dry_run(self):
        config = NodeConfig.from_default(1)
        result = self.orchestrator.run_full_provision(
            config=config,
            port="/dev/ttyUSB0",
            dry_run=True
        )
        self.assertTrue(result.success)
        self.assertEqual(result.stage, "COMPLETED")
        self.assertTrue(result.build_result.dry_run)
        self.assertTrue(result.flash_result.dry_run)

    def test_orchestrator_halts_on_validation_failure(self):
        bad_sensor = SensorConfig(telemetry_interval_ms=9999)  # Invalid!
        config = NodeConfig(sensor=bad_sensor)
        result = self.orchestrator.run_full_provision(
            config=config,
            port="/dev/ttyUSB0",
            dry_run=True
        )
        self.assertFalse(result.success)
        self.assertEqual(result.stage, "VALIDATION")
        self.assertIn("telemetry_interval_ms", result.message)
        self.assertIsNone(result.build_result)
        self.assertIsNone(result.flash_result)


if __name__ == "__main__":
    unittest.main()
