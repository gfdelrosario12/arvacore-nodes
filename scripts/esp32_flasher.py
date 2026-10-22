#!/usr/bin/env python3
"""
esp32_flasher.py - ArvaNode Production ESP32 Auto-Flasher & Device Verifier
ArvaCore Smart Irrigation Ecosystem (2026)

Integrates dynamic node provisioning with Arduino CLI / esptool build & flash pipelines.
Detects USB serial devices, validates node profiles, injects build-time configurations,
flashes firmware, verifies serial boot banners, and produces provisioning reports.
"""

import argparse
import glob
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

# Import provisioner from same directory
SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

try:
    from node_provisioner import NodeProvisioner, MAX_PROFILES
except ImportError:
    NodeProvisioner = None
    MAX_PROFILES = 10

REPO_ROOT = SCRIPT_DIR.parent
sys.path.insert(0, str(REPO_ROOT))

try:
    from arvanode_core.serial_monitor import SerialMonitor, discover_esp32_ports
except ImportError:
    SerialMonitor = None
    discover_esp32_ports = None
SKETCH_DIR = REPO_ROOT / "sketch_oct11a"
SKETCH_FILE = SKETCH_DIR / "sketch_oct11a.ino"
CONFIG_HEADER = SKETCH_DIR / "node_config.h"

DEFAULT_FQBN = "esp32:esp32:esp32"
DEFAULT_BAUD = 921600
MONITOR_BAUD = 115200

# Known ESP32 USB-to-UART bridge signatures
ESP32_USB_DRIVERS = ["cp210x", "ch341", "ch340", "ftdi_sio", "cdc_acm"]


# ─────────────────────────────────────────────────────────────────────────────
# DEVICE DETECTION ENGINE
# ─────────────────────────────────────────────────────────────────────────────

def detect_serial_devices():
    """
    Scans Linux /sys/class/tty and /dev for USB serial converters without
    requiring external third-party dependencies (works out of the box).
    """
    devices = []
    
    # Check /dev/serial/by-id if present
    by_id_map = {}
    by_id_dir = Path("/dev/serial/by-id")
    if by_id_dir.exists():
        for link in by_id_dir.iterdir():
            try:
                target = link.resolve()
                by_id_map[str(target)] = link.name
            except Exception:
                pass

    # Check candidates in /dev
    candidate_patterns = ["/dev/ttyUSB*", "/dev/ttyACM*"]
    candidate_paths = []
    for pat in candidate_patterns:
        candidate_paths.extend(glob.glob(pat))

    for dev_path in sorted(candidate_paths):
        device_name = Path(dev_path).name
        sys_path = Path(f"/sys/class/tty/{device_name}/device")

        description = by_id_map.get(dev_path, "USB Serial Device")
        driver_name = "unknown"
        is_esp32_compatible = True

        if sys_path.exists():
            # Check driver
            driver_link = sys_path / "driver"
            if driver_link.exists():
                try:
                    driver_name = driver_link.resolve().name
                except Exception:
                    pass

        # Identify known chips
        chip_guess = "Unknown ESP32/UART"
        if "cp210" in driver_name.lower() or "cp210" in description.lower():
            chip_guess = "CP2102/CP2104 USB-UART (ESP32 Dev Module standard)"
        elif "ch34" in driver_name.lower() or "ch34" in description.lower():
            chip_guess = "CH340/CH341 USB-UART (ESP32 NodeMCU standard)"
        elif "ftdi" in driver_name.lower() or "ftdi" in description.lower():
            chip_guess = "FTDI USB-UART"
        elif "acm" in device_name.lower():
            chip_guess = "Native USB CDC / ESP32-S2/S3"

        devices.append({
            "port": dev_path,
            "description": description,
            "driver": driver_name,
            "chip_type": chip_guess,
            "compatible": is_esp32_compatible,
            "readable": os.access(dev_path, os.R_OK),
            "writable": os.access(dev_path, os.W_OK),
        })

    return devices


def print_bootloader_troubleshooting():
    print("""
--------------------------------------------------------------------------------
ESP32 BOOTLOADER MANUAL ENTRY PROCEDURE:
If automatic upload fails with 'A fatal error occurred: Failed to connect to ESP32':
  1. Locate the two buttons on the ESP32 board: 'BOOT' (GPIO 0) and 'EN'/'RST'.
  2. Press and HOLD the 'BOOT' button.
  3. While holding 'BOOT', TAP the 'EN' button once.
  4. Release the 'BOOT' button when the flasher displays 'Connecting........_____'.
  5. After flashing succeeds, press 'EN' once to reboot into normal execution mode.
--------------------------------------------------------------------------------
""")


# ─────────────────────────────────────────────────────────────────────────────
# FLASHER CORE ENGINE
# ─────────────────────────────────────────────────────────────────────────────

class Esp32Flasher:
    def __init__(self, provisioner=None, fqbn=DEFAULT_FQBN):
        self.prov = provisioner or NodeProvisioner()
        self.fqbn = fqbn

    def list_devices(self):
        return detect_serial_devices()

    def build_firmware(self, node_id, dry_run=False, export_header=True):
        """
        Validates profile, injects build header, and compiles firmware via arduino-cli.
        """
        profile = self.prov.get_profile(node_id)
        if not profile:
            raise FileNotFoundError(f"Provisioning profile '{node_id}' does not exist.")

        # Ensure valid profile
        if profile.get("sensor_config", {}).get("telemetry_interval_ms") != 2000:
            raise ValueError(f"Profile '{node_id}' violates invariant: telemetry interval must be 3000ms.")

        if export_header:
            header_path = self.prov.export_header(node_id, CONFIG_HEADER)
            print(f"[PRE-BUILD] Injected build configuration -> {header_path}")

        build_cmd = [
            "arduino-cli", "compile",
            "--fqbn", self.fqbn,
            str(SKETCH_DIR)
        ]

        if dry_run:
            print(f"[DRY-RUN] Planned compilation command:")
            print(f"  $ {' '.join(build_cmd)}")
            return {
                "success": True,
                "node_id": node_id,
                "command": " ".join(build_cmd),
                "dry_run": True,
                "message": "Compilation simulated successfully."
            }

        print(f"[BUILD] Compiling firmware for '{node_id}' (FQBN: {self.fqbn})...")
        try:
            env = dict(os.environ)


            proc = subprocess.run(
                build_cmd,
                capture_output=True,
                text=True,
                check=False,
                env=env
            )
            if proc.returncode != 0:
                print(f"[BUILD ERROR]\n{proc.stderr}\n{proc.stdout}", file=sys.stderr)
                return {
                    "success": False,
                    "node_id": node_id,
                    "returncode": proc.returncode,
                    "error": proc.stderr or proc.stdout
                }

            print(f"[BUILD SUCCESS] Firmware compiled cleanly for {node_id}.")
            return {
                "success": True,
                "node_id": node_id,
                "returncode": 0,
                "output": proc.stdout
            }
        except FileNotFoundError:
            return {
                "success": False,
                "node_id": node_id,
                "error": "arduino-cli executable not found in PATH."
            }

    def flash_device(self, node_id, port=None, baud=DEFAULT_BAUD, dry_run=False):
        """
        Builds and flashes the specified profile onto the connected ESP32.
        """
        profile = self.prov.get_profile(node_id)
        if not profile:
            raise FileNotFoundError(f"Provisioning profile '{node_id}' does not exist.")

        # Check port
        devices = self.list_devices()
        if not port:
            if not devices:
                if not dry_run:
                    raise FileNotFoundError(
                        "No connected ESP32 serial devices detected.\n"
                        "Connect your ESP32 board over USB, check dmesg, or use --dry-run."
                    )
                port = "/dev/ttyUSB0 (Simulated)"
            else:
                port = devices[0]["port"]
        elif not dry_run and not os.path.exists(port):
            raise FileNotFoundError(f"Specified serial port '{port}' not found or device disconnected.")


        print(f"[TARGET] Logical ID:  {node_id} ({profile.get('display_name')})")
        print(f"[TARGET] Serial Port: {port}")
        print(f"[TARGET] Baud Rate:   {baud}")

        # Step 1: Build
        build_res = self.build_firmware(node_id, dry_run=dry_run)
        if not build_res["success"]:
            return build_res

        flash_cmd = [
            "arduino-cli", "upload",
            "-p", port,
            "--fqbn", self.fqbn,
            str(SKETCH_DIR)
        ]

        if dry_run:
            print(f"[DRY-RUN] Planned flashing command:")
            print(f"  $ {' '.join(flash_cmd)}")
            return {
                "success": True,
                "node_id": node_id,
                "port": port,
                "dry_run": True,
                "message": "Flashing simulated successfully."
            }

        # Step 2: Upload
        print(f"[FLASH] Uploading firmware to {port}...")
        try:
            env = dict(os.environ)
            if "HOME" not in env or not os.access(env["HOME"], os.W_OK):
                env["HOME"] = "/tmp"

            proc = subprocess.run(
                flash_cmd,
                capture_output=True,
                text=True,
                check=False,
                env=env
            )
            if proc.returncode != 0:
                print(f"[FLASH ERROR]\n{proc.stderr}\n{proc.stdout}", file=sys.stderr)
                print_bootloader_troubleshooting()
                return {
                    "success": False,
                    "node_id": node_id,
                    "port": port,
                    "returncode": proc.returncode,
                    "error": proc.stderr or proc.stdout
                }

            # Update profile status
            self.prov.update_status(node_id, "FLASHED", port=port)
            print(f"[FLASH SUCCESS] Successfully flashed {node_id} on {port}!")
            return {
                "success": True,
                "node_id": node_id,
                "port": port,
                "returncode": 0
            }
        except Exception as e:
            return {
                "success": False,
                "node_id": node_id,
                "port": port,
                "error": str(e)
            }

    def verify_boot(self, port, expected_node_id=None, timeout=5, dry_run=False):
        """
        Reconnects to serial output and listens for [BOOT] Node ID banner.
        """
        if dry_run:
            sim_node_id = expected_node_id or "ARVANODE-001"
            print(f"[DRY-RUN] Simulating serial verification on {port}...")
            print(f"  [BOOT] Node ID: {sim_node_id}")
            print(f"  [BOOT] MAC: 24:0A:C4:00:11:22")
            print(f"  [BOOT] Backend: https://api-aquaflow.gladwindr.me/api/v1")
            return {
                "success": True,
                "verified_node_id": sim_node_id,
                "simulated": True,
                "message": "Serial boot verification simulated successfully."
            }

        if not os.path.exists(port):
            return {
                "success": False,
                "error": f"Serial port {port} does not exist."
            }

        print(f"[VERIFY] Listening to serial boot logs on {port} for {timeout} seconds...")
        # Check permissions
        if not os.access(port, os.R_OK):
            return {
                "success": False,
                "error": f"Permission denied accessing {port}. Run 'sudo usermod -a -G dialout $USER'."
            }

        captured = []
        verified_id = None
        verified_mac = None

        try:
            if SerialMonitor is not None:
                with SerialMonitor(port=port, baudrate=MONITOR_BAUD) as mon:
                    mon.reset_esp32()
                    start_time = time.time()
                    while time.time() - start_time < timeout:
                        lines = mon.read_lines(timeout_sec=0.25)
                        for line in lines:
                            captured.append(line.strip())
                            if "[BOOT] Node ID:" in line:
                                match = re.search(r"\[BOOT\] Node ID:\s*([A-Za-z0-9_\-]+)", line)
                                if match:
                                    verified_id = match.group(1).strip()
                            if "[BOOT] MAC:" in line:
                                match = re.search(r"\[BOOT\] MAC:\s*([0-9A-Fa-f:]+)", line)
                                if match:
                                    verified_mac = match.group(1).strip()
                        if verified_id and (not expected_node_id or verified_id == expected_node_id):
                            break
            else:
                start_time = time.time()
                subprocess.run(["stty", "-F", port, str(MONITOR_BAUD), "raw", "-echo"], check=False)
                with open(port, "r", errors="replace") as ser:
                    while time.time() - start_time < timeout:
                        line = ser.readline()
                        if line:
                            captured.append(line.strip())
                            if "[BOOT] Node ID:" in line:
                                match = re.search(r"\[BOOT\] Node ID:\s*([A-Za-z0-9_\-]+)", line)
                                if match:
                                    verified_id = match.group(1).strip()
                            if "[BOOT] MAC:" in line:
                                match = re.search(r"\[BOOT\] MAC:\s*([0-9A-Fa-f:]+)", line)
                                if match:
                                    verified_mac = match.group(1).strip()
                            if verified_id and (not expected_node_id or verified_id == expected_node_id):
                                break
        except Exception as e:
            return {
                "success": False,
                "error": f"Serial reading error: {e}"
            }

        if verified_id:
            if expected_node_id and verified_id != expected_node_id:
                return {
                    "success": False,
                    "verified_node_id": verified_id,
                    "expected_node_id": expected_node_id,
                    "error": f"Node ID mismatch! Detected '{verified_id}', expected '{expected_node_id}'."
                }
            print(f"[VERIFIED] Hardware confirmed! Node ID: {verified_id}, MAC: {verified_mac or 'N/A'}")
            return {
                "success": True,
                "verified_node_id": verified_id,
                "verified_mac": verified_mac,
                "logs": captured
            }
        else:
            return {
                "success": False,
                "error": f"No boot banner detected within {timeout}s.",
                "logs": captured
            }

    def open_monitor(self, port=None, baud=MONITOR_BAUD, show_timestamps=True, colorize=True, auto_reset=True):
        """
        Launches the dedicated standalone serial monitor without requiring Arduino.
        """
        if SerialMonitor is None:
            raise RuntimeError("SerialMonitor could not be loaded from arvanode_core.")
        mon = SerialMonitor(port=port, baudrate=baud)
        mon.run_console(
            show_timestamps=show_timestamps,
            colorize=colorize,
            auto_reset_on_start=auto_reset
        )

    def generate_report(self):
        profiles = self.prov.list_profiles()
        devices = self.list_devices()
        return {
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "total_profiles": len(profiles),
            "max_allowed": MAX_PROFILES,
            "connected_devices": devices,
            "profiles": profiles
        }


# ─────────────────────────────────────────────────────────────────────────────
# CLI INTERFACE
# ─────────────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="ArvaNode Production ESP32 Auto-Flasher & Verification Tool"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    # devices
    subparsers.add_parser("devices", help="Detect and list connected ESP32 serial devices")

    # build
    bld_p = subparsers.add_parser("build", help="Build firmware for a specified node profile")
    bld_p.add_argument("--id", required=True, help="Logical Node ID (e.g. ARVANODE-001)")
    bld_p.add_argument("--dry-run", action="store_true", help="Simulate compilation command")

    # flash
    fls_p = subparsers.add_parser("flash", help="Build and flash firmware onto connected ESP32")
    fls_p.add_argument("--id", required=True, help="Logical Node ID (e.g. ARVANODE-001)")
    fls_p.add_argument("--port", help="Serial port (e.g. /dev/ttyUSB0)")
    fls_p.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"Upload baud rate (default: {DEFAULT_BAUD})")
    fls_p.add_argument("--monitor", action="store_true", help="Launch dedicated serial monitor immediately after flashing")
    fls_p.add_argument("--dry-run", action="store_true", help="Simulate flashing command")

    # provision
    prv_p = subparsers.add_parser("provision", help="Generate profile (if needed), build, and flash")
    prv_p.add_argument("--id", required=True, help="Logical Node ID (e.g. ARVANODE-001)")
    prv_p.add_argument("--name", help="Display name (optional)")
    prv_p.add_argument("--port", help="Serial port (e.g. /dev/ttyUSB0)")
    prv_p.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"Upload baud rate (default: {DEFAULT_BAUD})")
    prv_p.add_argument("--monitor", action="store_true", help="Launch dedicated serial monitor immediately after flashing")
    prv_p.add_argument("--dry-run", action="store_true", help="Simulate provisioning and flashing")

    # monitor
    mon_p = subparsers.add_parser("monitor", help="Launch dedicated standalone serial monitor (no Arduino needed)")
    mon_p.add_argument("--port", help="Serial port (e.g. /dev/ttyUSB0). Auto-detects if omitted.")
    mon_p.add_argument("--baud", type=int, default=MONITOR_BAUD, help=f"Baud rate (default: {MONITOR_BAUD})")
    mon_p.add_argument("--no-timestamps", action="store_true", help="Disable timestamp prefixes")
    mon_p.add_argument("--no-color", action="store_true", help="Disable ANSI colorized output")
    mon_p.add_argument("--no-reset", action="store_true", help="Do not pulse EN reset line upon opening")

    # verify
    vrf_p = subparsers.add_parser("verify", help="Verify device boot banner on serial port")
    vrf_p.add_argument("--port", required=True, help="Serial port to listen on")
    vrf_p.add_argument("--expected-id", help="Expected Logical Node ID")
    vrf_p.add_argument("--timeout", type=int, default=5, help="Listening timeout in seconds")
    vrf_p.add_argument("--dry-run", action="store_true", help="Simulate serial verification")

    # report
    rpt_p = subparsers.add_parser("report", help="Generate a comprehensive provisioning report")
    rpt_p.add_argument("--json", action="store_true", help="Output raw JSON format")

    args = parser.parse_args()
    flasher = Esp32Flasher()

    if args.command == "devices":
        devices = flasher.list_devices()
        print("=" * 80)
        print("DETECTED ESP32 / USB SERIAL DEVICES")
        print("=" * 80)
        if not devices:
            print("No USB serial devices currently detected.")
            print("Check that your ESP32 board is plugged in via a data-capable USB cable.")
            return 0
        for d in devices:
            perm_str = "R/W OK" if (d["readable"] and d["writable"]) else "PERMISSION ISSUE"
            print(f"Port:        {d['port']}")
            print(f"Description: {d['description']}")
            print(f"Chip Type:   {d['chip_type']}")
            print(f"Permissions: {perm_str}")
            print("-" * 80)
        return 0

    elif args.command == "build":
        res = flasher.build_firmware(args.id, dry_run=args.dry_run)
        return 0 if res["success"] else 1

    elif args.command == "flash":
        res = flasher.flash_device(args.id, port=args.port, baud=args.baud, dry_run=args.dry_run)
        if res["success"] and getattr(args, "monitor", False) and not args.dry_run:
            target_port = res.get("port") or args.port
            print(f"\n[POST-FLASH] Launching dedicated serial monitor on {target_port}...")
            flasher.open_monitor(port=target_port)
        return 0 if res["success"] else 1

    elif args.command == "provision":
        # Check if profile exists, generate if absent
        profile = flasher.prov.get_profile(args.id)
        if not profile:
            print(f"[PROVISION] Profile '{args.id}' not found. Generating new profile...")
            profile = flasher.prov.generate_profile(node_id=args.id, display_name=args.name, dry_run=args.dry_run)
            print(f"[PROVISION] Generated {args.id} ({profile['display_name']}).")

        res = flasher.flash_device(args.id, port=args.port, baud=args.baud, dry_run=args.dry_run)
        if res["success"] and getattr(args, "monitor", False) and not args.dry_run:
            target_port = res.get("port") or args.port
            print(f"\n[POST-FLASH] Launching dedicated serial monitor on {target_port}...")
            flasher.open_monitor(port=target_port)
        return 0 if res["success"] else 1

    elif args.command == "monitor":
        flasher.open_monitor(
            port=args.port,
            baud=args.baud,
            show_timestamps=not args.no_timestamps,
            colorize=not args.no_color,
            auto_reset=not args.no_reset
        )
        return 0

    elif args.command == "verify":
        res = flasher.verify_boot(args.port, expected_node_id=args.expected_id, timeout=args.timeout, dry_run=args.dry_run)
        return 0 if res["success"] else 1

    elif args.command == "report":
        rep = flasher.generate_report()
        if args.json:
            print(json.dumps(rep, indent=2))
            return 0
        print("=" * 80)
        print("ARVACORE NODE PROVISIONING & FLASH STATUS REPORT")
        print(f"Generated: {rep['timestamp']}")
        print(f"Active Profiles: {rep['total_profiles']} / {rep['max_allowed']} maximum")
        print("=" * 80)
        print(f"{'LOGICAL ID':<16} {'DISPLAY NAME':<22} {'STATUS':<14} {'LAST FLASHED'}")
        print("-" * 80)
        for p in rep["profiles"]:
            flashed_info = p.get("metadata", {}).get("last_flashed_at")
            if flashed_info:
                flashed_info = flashed_info[:19] + f" ({p.get('metadata', {}).get('last_flashed_port') or '?'})"
            else:
                flashed_info = "Never"
            print(f"{p.get('logical_node_id', ''):<16} "
                  f"{p.get('display_name', '')[:20]:<22} "
                  f"{p.get('status', 'GENERATED'):<14} "
                  f"{flashed_info}")
        print("=" * 80)
        print(f"Connected Serial Devices: {len(rep['connected_devices'])}")
        for d in rep["connected_devices"]:
            print(f"  • {d['port']}: {d['chip_type']} ({d['description']})")
        return 0

    return 0


if __name__ == "__main__":
    main()
