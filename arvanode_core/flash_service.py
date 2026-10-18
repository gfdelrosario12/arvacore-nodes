"""
arvanode_core.flash_service - Serial Discovery & Hardware Flash Service
ArvaCore Smart Irrigation Ecosystem (2026)

Handles USB serial discovery, port validation, and invoking the flashing utility
(arduino-cli / esptool) completely independent of compilation logic.
"""

import glob
import os
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import List, Dict, Any, Optional


@dataclass
class FlashResult:
    success: bool
    port: str
    baud: int
    command: List[str]
    returncode: int
    output: str
    error: str
    dry_run: bool = False


class FlashService:
    """Manages serial device discovery and ESP32 flashing operations."""

    def __init__(self, fqbn: str = "esp32:esp32:esp32", default_baud: int = 921600):
        self.fqbn = fqbn
        self.default_baud = default_baud

    def discover_serial_ports(self) -> List[Dict[str, Any]]:
        """Scans Linux /dev and /sys for active USB-to-UART bridge controllers."""
        devices = []
        by_id_dir = Path("/dev/serial/by-id")
        by_id_map = {}

        if by_id_dir.exists():
            for link in by_id_dir.iterdir():
                try:
                    target = link.resolve()
                    by_id_map[str(target)] = link.name
                except Exception:
                    pass

        patterns = ["/dev/ttyUSB*", "/dev/ttyACM*"]
        found_paths = []
        for pat in patterns:
            found_paths.extend(glob.glob(pat))

        for dev_path in sorted(found_paths):
            dev_name = Path(dev_path).name
            sys_path = Path(f"/sys/class/tty/{dev_name}/device")
            desc = by_id_map.get(dev_path, "USB Serial Device")
            driver_name = "unknown"

            if sys_path.exists():
                driver_link = sys_path / "driver"
                if driver_link.exists():
                    try:
                        driver_name = driver_link.resolve().name
                    except Exception:
                        pass

            chip_guess = "ESP32 UART Device"
            if "cp210" in driver_name.lower() or "cp210" in desc.lower():
                chip_guess = "CP2102/CP2104 USB-UART (ESP32 Standard)"
            elif "ch34" in driver_name.lower() or "ch34" in desc.lower():
                chip_guess = "CH340/CH341 USB-UART"
            elif "ftdi" in driver_name.lower():
                chip_guess = "FTDI USB-UART"

            devices.append({
                "port": dev_path,
                "description": desc,
                "driver": driver_name,
                "chip": chip_guess,
                "readable": os.access(dev_path, os.R_OK),
                "writable": os.access(dev_path, os.W_OK),
            })

        return devices

    def flash(
        self,
        sketch_dir: Path,
        port: str,
        baud: Optional[int] = None,
        dry_run: bool = False
    ) -> FlashResult:
        """Flashes precompiled or source firmware onto the target serial device."""
        baud_rate = baud or self.default_baud

        # Validate port
        if not dry_run:
            if not os.path.exists(port):
                return FlashResult(
                    success=False,
                    port=port,
                    baud=baud_rate,
                    command=[],
                    returncode=-1,
                    output="",
                    error=f"Serial port '{port}' not found or board disconnected.",
                    dry_run=False
                )

            if not os.access(port, os.W_OK):
                return FlashResult(
                    success=False,
                    port=port,
                    baud=baud_rate,
                    command=[],
                    returncode=-1,
                    output="",
                    error=f"Permission denied accessing '{port}'. Ensure your user is in the 'dialout' or 'uucp' group.",
                    dry_run=False
                )

        cmd = [
            "arduino-cli", "upload",
            "-p", port,
            "--fqbn", self.fqbn,
            str(sketch_dir)
        ]

        if dry_run:
            return FlashResult(
                success=True,
                port=port,
                baud=baud_rate,
                command=cmd,
                returncode=0,
                output="[DRY-RUN] Flashing command prepared successfully.",
                error="",
                dry_run=True
            )

        arduino_bin = shutil.which("arduino-cli")
        if not arduino_bin:
            return FlashResult(
                success=False,
                port=port,
                baud=baud_rate,
                command=cmd,
                returncode=-1,
                output="",
                error="arduino-cli executable not found in system PATH.",
                dry_run=False
            )

        try:
            proc = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                check=False
            )

            return FlashResult(
                success=(proc.returncode == 0),
                port=port,
                baud=baud_rate,
                command=cmd,
                returncode=proc.returncode,
                output=proc.stdout or "",
                error=proc.stderr or ("" if proc.returncode == 0 else proc.stdout),
                dry_run=False
            )

        except Exception as ex:
            return FlashResult(
                success=False,
                port=port,
                baud=baud_rate,
                command=cmd,
                returncode=-1,
                output="",
                error=f"Unexpected error running upload: {ex}",
                dry_run=False
            )
