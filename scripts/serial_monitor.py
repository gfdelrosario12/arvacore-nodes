#!/usr/bin/env python3
"""
serial_monitor.py - Standalone High-Performance ESP32 Serial Monitor
ArvaCore Smart Irrigation Ecosystem (2026)

Runs a dedicated serial monitor for ESP32 hardware without requiring Arduino IDE or Arduino CLI.
Supports native termios driver, automatic ESP32 port discovery, timestamps, and hardware reset.
"""

import argparse
import sys
from pathlib import Path

# Insert parent directory to allow importing arvanode_core
SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
sys.path.insert(0, str(REPO_ROOT))

try:
    from arvanode_core.serial_monitor import (
        SerialMonitor,
        discover_esp32_ports,
        SUPPORTED_BAUDS,
        DEFAULT_MONITOR_BAUD
    )
except ImportError:
    # Fallback to local import if called from other directories
    sys.path.insert(0, str(REPO_ROOT / "arvanode_core"))
    from serial_monitor import (
        SerialMonitor,
        discover_esp32_ports,
        SUPPORTED_BAUDS,
        DEFAULT_MONITOR_BAUD
    )


def main():
    parser = argparse.ArgumentParser(
        description="ArvaCore Dedicated Standalone ESP32 Serial Monitor (No Arduino required)"
    )
    parser.add_argument(
        "-p", "--port",
        help="Serial port (e.g. /dev/ttyUSB0, /dev/ttyACM0). Auto-detects if omitted."
    )
    parser.add_argument(
        "-b", "--baud",
        type=int,
        default=DEFAULT_MONITOR_BAUD,
        choices=SUPPORTED_BAUDS,
        help=f"Baud rate (default: {DEFAULT_MONITOR_BAUD})"
    )
    parser.add_argument(
        "--no-timestamps",
        action="store_true",
        help="Disable [HH:MM:SS] timestamp prefix on log lines"
    )
    parser.add_argument(
        "--no-color",
        action="store_true",
        help="Disable ANSI syntax color highlighting"
    )
    parser.add_argument(
        "--no-reset",
        action="store_true",
        help="Do not pulse hardware reset (EN) line on connect"
    )
    parser.add_argument(
        "--list-ports",
        action="store_true",
        help="List detected serial devices and exit"
    )

    args = parser.parse_args()

    if args.list_ports:
        devices = discover_esp32_ports()
        print("=" * 70)
        print("DETECTED ESP32 / USB SERIAL PORTS")
        print("=" * 70)
        if not devices:
            print("No USB serial devices currently detected.")
            return 0
        for d in devices:
            perm_str = "R/W OK" if (d["readable"] and d["writable"]) else "PERMISSION ISSUE"
            print(f"Port:        {d['port']}")
            print(f"Description: {d['description']}")
            print(f"Chip Type:   {d['chip_type']}")
            print(f"Permissions: {perm_str}")
            print("-" * 70)
        return 0

    port = args.port
    if not port:
        devices = discover_esp32_ports()
        if not devices:
            print("[ERROR] No USB serial ports detected. Connect your ESP32 board over USB.", file=sys.stderr)
            return 1
        port = devices[0]["port"]
        print(f"[AUTO-DETECT] Selected active serial device: {port} ({devices[0]['chip_type']})")

    try:
        mon = SerialMonitor(port=port, baudrate=args.baud)
        mon.run_console(
            show_timestamps=not args.no_timestamps,
            colorize=not args.no_color,
            auto_reset_on_start=not args.no_reset
        )
        return 0
    except Exception as ex:
        print(f"\n[ERROR] {ex}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

