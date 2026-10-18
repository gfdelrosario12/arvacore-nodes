"""
arvanode_core.build_service - Firmware Build & Header Generation Service
ArvaCore Smart Irrigation Ecosystem (2026)

Compiles ESP32 firmware cleanly using stateless, controlled subprocess invocation
without retaining invalidated daemon state or shared mutable objects.
"""

import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Dict, Any, List

from .models import NodeConfig


@dataclass
class BuildResult:
    success: bool
    node_id: str
    command: List[str]
    returncode: int
    output: str
    error: str
    artifact_path: Optional[Path] = None
    dry_run: bool = False


class BuildService:
    """Handles C/C++ configuration header injection and firmware compilation."""

    def __init__(self, repo_root: Optional[Path] = None, fqbn: str = "esp32:esp32:esp32"):
        if repo_root is None:
            self.repo_root = Path(__file__).resolve().parent.parent
        else:
            self.repo_root = Path(repo_root)

        self.sketch_dir = self.repo_root / "sketch_oct11a"
        self.config_header = self.sketch_dir / "node_config.h"
        self.fqbn = fqbn

    def generate_header(self, config: NodeConfig, target_header: Optional[Path] = None) -> Path:
        """Injects build-time macros into node_config.h for the ESP32 preprocessor."""
        config.validate()
        dest = target_header or self.config_header
        dest.parent.mkdir(parents=True, exist_ok=True)

        content = f"""// ============================================================================
// AUTO-GENERATED ARVANODE CONFIGURATION HEADER
// Node ID: {config.logical_node_id} ({config.display_name})
// Firmware: {config.firmware_version} | Build: {config.build_id}
// ============================================================================

#ifndef ARVA_NODE_CONFIG_H
#define ARVA_NODE_CONFIG_H

#define ARVA_CONFIG_INJECTED 1

// Node Identity
#define NODE_LOGICAL_ID "{config.logical_node_id}"
#define NODE_DISPLAY_NAME "{config.display_name}"
#define NODE_FIRMWARE_VERSION "{config.firmware_version}"
#define NODE_BUILD_ID "{config.build_id}"
#define NODE_DEVICE_KEY "{config.device_key}"
#define NODE_HARDWARE_MAC "{config.hardware_mac}"

// Network Configuration (Primary & Fallback Priority)
#define NODE_PRIMARY_WIFI_SSID "{config.network.primary_ssid}"
#define NODE_PRIMARY_WIFI_PASS "{config.network.primary_password}"
#define NODE_FALLBACK_WIFI_SSID "{config.network.fallback_ssid}"
#define NODE_FALLBACK_WIFI_PASS "{config.network.fallback_password}"
#define NODE_WIFI_SSID "{config.network.primary_ssid}"
#define NODE_WIFI_PASS "{config.network.primary_password}"
#define NODE_BACKEND_URL "{config.network.backend_base_url}"
#define NODE_GATEWAY_KEY "{config.network.gateway_key}"

// Sensor Geometry & Timing
#define NODE_DATUM_OFFSET_CM {config.sensor.datum_offset_cm:.2f}f
#define NODE_MIN_DISTANCE_CM {config.sensor.min_distance_cm:.2f}f
#define NODE_MAX_DISTANCE_CM {config.sensor.max_distance_cm:.2f}f
#define NODE_SOIL_AIR_VALUE {config.sensor.soil_air_value}
#define NODE_SOIL_WATER_VALUE {config.sensor.soil_water_value}
#define NODE_TELEMETRY_INTERVAL_MS {config.sensor.telemetry_interval_ms}UL

// Hardware Pinout
#define PIN_SOIL_ANALOG {config.pinout.soil_moisture_analog}
#define PIN_ULTRASONIC_TRIG {config.pinout.ultrasonic_trig}
#define PIN_ULTRASONIC_ECHO {config.pinout.ultrasonic_echo}
#define PIN_BTN_ACTIVATE {config.pinout.activation_btn}
#define PIN_BTN_PAIRING {config.pinout.pairing_btn}
#define PIN_LED_PAIRING {config.pinout.pairing_led}
#define PIN_LED_ACTIVATE {config.pinout.activation_led}

#endif // ARVA_NODE_CONFIG_H
"""
        with open(dest, "w", encoding="utf-8") as f:
            f.write(content)

        return dest

    def compile(self, config: NodeConfig, build_dir: Optional[Path] = None, dry_run: bool = False) -> BuildResult:
        """Executes a clean, isolated compilation step."""
        config.validate()

        # Step 1: Generate header
        self.generate_header(config)

        # Step 2: Prepare compile command
        cmd = [
            "arduino-cli", "compile",
            "--fqbn", self.fqbn
        ]

        if build_dir:
            build_dir.mkdir(parents=True, exist_ok=True)
            cmd.extend(["--build-path", str(build_dir)])

        cmd.append(str(self.sketch_dir))

        if dry_run:
            return BuildResult(
                success=True,
                node_id=config.logical_node_id,
                command=cmd,
                returncode=0,
                output="[DRY-RUN] Compilation command prepared successfully.",
                error="",
                dry_run=True
            )

        # Step 3: Check toolchain
        arduino_bin = shutil.which("arduino-cli")
        if not arduino_bin:
            return BuildResult(
                success=False,
                node_id=config.logical_node_id,
                command=cmd,
                returncode=-1,
                output="",
                error="arduino-cli executable not found in system PATH.",
                dry_run=False
            )

        # Step 4: Run compilation in clean environment
        env = dict(os.environ)
        # Ensure HOME is preserved correctly so arduino-cli uses installed cores (~/.arduino15)
        # If user running under sandbox, fall back to /tmp if ~/.arduino15 is inaccessible
        if not os.access(os.path.expanduser("~/.arduino15"), os.W_OK) and not os.access(os.path.expanduser("~"), os.W_OK):
            # Sandbox or read-only container
            pass

        try:
            proc = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                check=False,
                env=env
            )

            # Check for binary output
            artifact = None
            if proc.returncode == 0:
                expected_bin = self.sketch_dir / f"{self.sketch_dir.name}.ino.bin"
                if expected_bin.exists():
                    artifact = expected_bin
                elif build_dir:
                    candidate = build_dir / f"{self.sketch_dir.name}.ino.bin"
                    if candidate.exists():
                        artifact = candidate

            return BuildResult(
                success=(proc.returncode == 0),
                node_id=config.logical_node_id,
                command=cmd,
                returncode=proc.returncode,
                output=proc.stdout or "",
                error=proc.stderr or ("" if proc.returncode == 0 else proc.stdout),
                artifact_path=artifact,
                dry_run=False
            )

        except Exception as ex:
            return BuildResult(
                success=False,
                node_id=config.logical_node_id,
                command=cmd,
                returncode=-1,
                output="",
                error=f"Unexpected error running compilation: {ex}",
                dry_run=False
            )
