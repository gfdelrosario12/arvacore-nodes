#!/usr/bin/env python3
"""
node_provisioner.py - ArvaNode Dynamic Identity & Provisioning Profile Generator
ArvaCore Smart Irrigation Ecosystem (2026)

Manages up to 10 unique ESP32 node provisioning profiles (ARVANODE-001 to ARVANODE-010).
Generates cryptographically secure credentials, exports build headers, validates schemas,
and tracks device lifecycle states without checking secrets into version control.
"""

import argparse
import copy
import json
import os
import re
import secrets
import sys
from datetime import datetime, timezone
from pathlib import Path

# ─────────────────────────────────────────────────────────────────────────────
# CONFIGURATION & CONSTANTS
# ─────────────────────────────────────────────────────────────────────────────

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
PROVISIONING_DIR = REPO_ROOT / "provisioning"
PROFILES_DIR = PROVISIONING_DIR / "profiles"
TEMPLATE_FILE = PROVISIONING_DIR / "profile_template.json"
DEFAULT_SKETCH_HEADER = REPO_ROOT / "sketch_oct11a" / "node_config.h"

MAX_PROFILES = 10

DEFAULT_PROFILES = [
    {"id": "ARVANODE-001", "name": "ArvaNode Field 01"},
    {"id": "ARVANODE-002", "name": "ArvaNode Field 02"},
    {"id": "ARVANODE-003", "name": "ArvaNode Field 03"},
    {"id": "ARVANODE-004", "name": "ArvaNode Field 04"},
    {"id": "ARVANODE-005", "name": "ArvaNode Field 05"},
    {"id": "ARVANODE-006", "name": "ArvaNode Field 06"},
    {"id": "ARVANODE-007", "name": "ArvaNode Field 07"},
    {"id": "ARVANODE-008", "name": "ArvaNode Field 08"},
    {"id": "ARVANODE-009", "name": "ArvaNode Field 09"},
    {"id": "ARVANODE-010", "name": "ArvaNode Field 10"},
]

VALID_STATES = [
    "GENERATED",
    "PROVISIONED",
    "FLASHED",
    "REGISTERED",
    "PENDING_PAIRING",
    "PAIRED",
    "REVOKED",
]

# ─────────────────────────────────────────────────────────────────────────────
# PROVISIONER CORE LOGIC
# ─────────────────────────────────────────────────────────────────────────────

class NodeProvisioner:
    def __init__(self, profiles_dir=None, template_file=None):
        self.profiles_dir = Path(profiles_dir or PROFILES_DIR)
        self.template_file = Path(template_file or TEMPLATE_FILE)
        self.profiles_dir.mkdir(parents=True, exist_ok=True)

    def _get_template(self):
        if self.template_file.exists():
            try:
                with open(self.template_file, "r", encoding="utf-8") as f:
                    return json.load(f)
            except Exception:
                pass
        # Fallback built-in template
        return {
            "logical_node_id": "ARVANODE-001",
            "display_name": "ArvaNode Field 01",
            "profile_version": 1,
            "hardware_mac": "",
            "hardware_model": "ESP32-WROOM-32",
            "hardware_revision": "rev1",
            "firmware_version": "1.2.0",
            "build_id": "std-20261018",
            "device_key": "",
            "status": "GENERATED",
            "network": {
                "primary_ssid": "ZTE_2.4G_5gKub",
                "primary_password": "H0meDLR$2025",
                "fallback_ssid": "ZTE_2.4G_5gKub",
                "fallback_password": "H0meDLR$2025",
                "wifi_ssid": "ZTE_2.4G_5gKub",
                "wifi_password": "H0meDLR$2025",
                "networks": [
                    {"ssid": "ZTE_2.4G_5gKub", "password": "H0meDLR$2025", "priority": 1},
                    {"ssid": "ZTE_2.4G_5gKub", "password": "H0meDLR$2025", "priority": 2}
                ],
                "backend_base_url": "https://api-aquaflow.gladwindr.me/api/v1",
                "gateway_key": "ed630e681ef735f36954469b48f832f210286246ca95fff62e62086cab74e8a9"
            },

            "sensor_config": {
                "datum_offset_cm": 20.0,
                "min_distance_cm": 2.0,
                "max_distance_cm": 450.0,
                "soil_air_value": 3200,
                "soil_water_value": 1500,
                "telemetry_interval_ms": 2000
            },
            "pinout": {
                "soil_moisture_analog": 34,
                "ultrasonic_trig": 5,
                "ultrasonic_echo": 18,
                "activation_btn": 19,
                "pairing_btn": 23,
                "pairing_led": 25,
                "activation_led": 26
            },
            "metadata": {
                "created_at": datetime.now(timezone.utc).isoformat(),
                "updated_at": datetime.now(timezone.utc).isoformat(),
                "last_flashed_at": None,
                "last_flashed_port": None
            }
        }

    def list_profiles(self):
        """Returns all existing profiles sorted by logical ID."""
        profiles = []
        for file in sorted(self.profiles_dir.glob("*.json")):
            if file.name.startswith("."):
                continue
            try:
                with open(file, "r", encoding="utf-8") as f:
                    data = json.load(f)
                    profiles.append(data)
            except Exception as e:
                profiles.append({
                    "logical_node_id": file.stem,
                    "error": f"Corrupt profile JSON: {e}"
                })
        return profiles

    def get_profile(self, node_id):
        file = self.profiles_dir / f"{node_id}.json"
        if not file.exists():
            return None
        with open(file, "r", encoding="utf-8") as f:
            return json.load(f)

    def generate_profile(self, node_id=None, display_name=None, dry_run=False, force=False, hardware_mac=None):
        existing = {p.get("logical_node_id"): p for p in self.list_profiles() if "logical_node_id" in p}

        # Check maximum capacity constraint
        if len(existing) >= MAX_PROFILES and (node_id not in existing or not force):
            raise ValueError(f"Maximum limit of {MAX_PROFILES} active node profiles reached.")

        # Determine logical ID
        if not node_id:
            # Pick first available from DEFAULT_PROFILES
            for item in DEFAULT_PROFILES:
                if item["id"] not in existing:
                    node_id = item["id"]
                    if not display_name:
                        display_name = item["name"]
                    break
            if not node_id:
                raise ValueError("All 10 default logical identities are already provisioned.")

        # Validate node ID format
        if not re.match(r"^ARVANODE-\d{3}$", node_id) and not re.match(r"^[A-Z0-9_\-]+$", node_id):
            raise ValueError(f"Invalid node ID format: '{node_id}'. Expected format e.g. 'ARVANODE-001'")

        # Duplicate check
        if node_id in existing and not force:
            raise ValueError(f"Profile '{node_id}' already exists. Use --force to replace.")

        # Fallback display name
        if not display_name:
            for item in DEFAULT_PROFILES:
                if item["id"] == node_id:
                    display_name = item["name"]
                    break
            if not display_name:
                display_name = f"ArvaNode {node_id}"

        now = datetime.now(timezone.utc).isoformat()
        template = self._get_template()
        profile = copy.deepcopy(template)

        profile["logical_node_id"] = node_id
        profile["display_name"] = display_name
        profile["hardware_mac"] = hardware_mac or ""
        profile["device_key"] = f"dev_sec_{secrets.token_hex(28)}"
        profile["build_id"] = f"bld-{datetime.now(timezone.utc).strftime('%Y%m%d%H%M%S')}"
        profile["status"] = "GENERATED"
        profile["metadata"]["created_at"] = now
        profile["metadata"]["updated_at"] = now

        if dry_run:
            return profile

        out_path = self.profiles_dir / f"{node_id}.json"
        with open(out_path, "w", encoding="utf-8") as f:
            json.dump(profile, f, indent=2)

        # Restrict permissions to owner only (0600)
        try:
            os.chmod(out_path, 0o600)
        except Exception:
            pass

        return profile

    def update_status(self, node_id, new_status, hardware_mac=None, port=None):
        if new_status not in VALID_STATES:
            raise ValueError(f"Invalid status '{new_status}'. Allowed: {', '.join(VALID_STATES)}")
        profile = self.get_profile(node_id)
        if not profile:
            raise FileNotFoundError(f"Profile '{node_id}' not found.")

        profile["status"] = new_status
        profile["metadata"]["updated_at"] = datetime.now(timezone.utc).isoformat()
        if hardware_mac:
            profile["hardware_mac"] = hardware_mac
        if port:
            profile["metadata"]["last_flashed_port"] = port
            profile["metadata"]["last_flashed_at"] = datetime.now(timezone.utc).isoformat()

        out_path = self.profiles_dir / f"{node_id}.json"
        with open(out_path, "w", encoding="utf-8") as f:
            json.dump(profile, f, indent=2)
        try:
            os.chmod(out_path, 0o600)
        except Exception:
            pass
        return profile

    def inspect_profile(self, node_id, redact=True):
        profile = self.get_profile(node_id)
        if not profile:
            return None
        data = copy.deepcopy(profile)
        if redact:
            key = data.get("device_key", "")
            if key:
                data["device_key"] = key[:8] + "****************"
            if "network" in data and "wifi_password" in data["network"]:
                data["network"]["wifi_password"] = "********"
            if "network" in data and "gateway_key" in data["network"]:
                gk = data["network"]["gateway_key"]
                data["network"]["gateway_key"] = gk[:8] + "..." if len(gk) > 12 else "********"
        return data

    def validate_profiles(self):
        """Validates all profiles against schema rules. Returns (bool, list_of_errors)."""
        errors = []
        profiles = self.list_profiles()
        seen_ids = set()
        seen_macs = set()

        if len(profiles) > MAX_PROFILES:
            errors.append(f"Profile count ({len(profiles)}) exceeds maximum limit of {MAX_PROFILES}.")

        for p in profiles:
            if "error" in p:
                errors.append(f"Corrupt profile '{p.get('logical_node_id')}': {p['error']}")
                continue

            node_id = p.get("logical_node_id")
            if not node_id:
                errors.append("Profile missing required 'logical_node_id'.")
                continue

            if node_id in seen_ids:
                errors.append(f"Duplicate logical_node_id detected: '{node_id}'.")
            seen_ids.add(node_id)

            status = p.get("status")
            if status not in VALID_STATES:
                errors.append(f"Profile '{node_id}' has invalid status '{status}'.")

            mac = p.get("hardware_mac", "").strip().upper()
            if mac and mac != "":
                if not re.match(r"^([0-9A-F]{2}[:-]){5}([0-9A-F]{2})$", mac):
                    errors.append(f"Profile '{node_id}' has malformed hardware MAC address: '{mac}'.")
                elif mac in seen_macs:
                    errors.append(f"Hardware MAC address collision: '{mac}' claimed by multiple profiles.")
                else:
                    seen_macs.add(mac)

            sensor = p.get("sensor_config", {})
            interval = sensor.get("telemetry_interval_ms", 0)
            if interval != 2000:
                errors.append(f"Profile '{node_id}' telemetry_interval_ms is {interval}; must be strictly 2000ms.")

            pinout = p.get("pinout", {})
            required_pins = {
                "soil_moisture_analog": 34,
                "ultrasonic_trig": 5,
                "ultrasonic_echo": 18,
                "activation_btn": 19,
                "pairing_btn": 23,
                "pairing_led": 25,
                "activation_led": 26,
            }
            for pin_name, expected_val in required_pins.items():
                if pinout.get(pin_name) != expected_val:
                    errors.append(f"Profile '{node_id}' pin '{pin_name}' is {pinout.get(pin_name)}; expected {expected_val}.")

        return (len(errors) == 0, errors)

    def export_header(self, node_id, output_path=None):
        profile = self.get_profile(node_id)
        if not profile:
            raise FileNotFoundError(f"Profile '{node_id}' not found.")

        out_path = Path(output_path or DEFAULT_SKETCH_HEADER)
        out_path.parent.mkdir(parents=True, exist_ok=True)

        network = profile.get("network", {})
        sensor = profile.get("sensor_config", {})
        pinout = profile.get("pinout", {})

        header_content = f"""// ============================================================================
// AUTO-GENERATED ARVANODE CONFIGURATION HEADER
// Generated: {datetime.now(timezone.utc).isoformat()}
// Profile: {profile.get('logical_node_id')} ({profile.get('display_name')})
// DO NOT EDIT DIRECTLY - MANAGED BY node_provisioner.py
// ============================================================================

#ifndef ARVA_NODE_CONFIG_H
#define ARVA_NODE_CONFIG_H

#define ARVA_CONFIG_INJECTED 1

// Node Identity
#define NODE_LOGICAL_ID "{profile.get('logical_node_id')}"
#define NODE_DISPLAY_NAME "{profile.get('display_name')}"
#define NODE_FIRMWARE_VERSION "{profile.get('firmware_version', '1.2.0')}"
#define NODE_BUILD_ID "{profile.get('build_id', 'std')}"
#define NODE_DEVICE_KEY "{profile.get('device_key', '')}"
#define NODE_HARDWARE_MAC "{profile.get('hardware_mac', '')}"

// Network Configuration (Primary & Fallback Priority)
#define NODE_PRIMARY_WIFI_SSID "{network.get('primary_ssid', network.get('wifi_ssid', 'ArvaLink-2'))}"
#define NODE_PRIMARY_WIFI_PASS "{network.get('primary_password', network.get('wifi_password', 'ARVaLINK2_2026'))}"
#define NODE_FALLBACK_WIFI_SSID "{network.get('fallback_ssid', 'ArvaLink-1')}"
#define NODE_FALLBACK_WIFI_PASS "{network.get('fallback_password', 'ARVaLINK1_2026')}"
#define NODE_WIFI_SSID "{network.get('primary_ssid', network.get('wifi_ssid', 'ArvaLink-2'))}"
#define NODE_WIFI_PASS "{network.get('primary_password', network.get('wifi_password', 'ARVaLINK2_2026'))}"
#define NODE_BACKEND_URL "{network.get('backend_base_url', 'https://api-aquaflow.gladwindr.me/api/v1')}"
#define NODE_GATEWAY_KEY "{network.get('gateway_key', '')}"


// Sensor Geometry & Timing
#define NODE_DATUM_OFFSET_CM {sensor.get('datum_offset_cm', 20.0):.2f}f
#define NODE_MIN_DISTANCE_CM {sensor.get('min_distance_cm', 2.0):.2f}f
#define NODE_MAX_DISTANCE_CM {sensor.get('max_distance_cm', 450.0):.2f}f
#define NODE_SOIL_AIR_VALUE {sensor.get('soil_air_value', 3200)}
#define NODE_SOIL_WATER_VALUE {sensor.get('soil_water_value', 1500)}
#define NODE_TELEMETRY_INTERVAL_MS {sensor.get('telemetry_interval_ms', 2000)}UL

// Hardware Pinout
#define PIN_SOIL_ANALOG {pinout.get('soil_moisture_analog', 34)}
#define PIN_ULTRASONIC_TRIG {pinout.get('ultrasonic_trig', 5)}
#define PIN_ULTRASONIC_ECHO {pinout.get('ultrasonic_echo', 18)}
#define PIN_BTN_ACTIVATE {pinout.get('activation_btn', 19)}
#define PIN_BTN_PAIRING {pinout.get('pairing_btn', 23)}
#define PIN_LED_PAIRING {pinout.get('pairing_led', 25)}
#define PIN_LED_ACTIVATE {pinout.get('activation_led', 26)}

#endif // ARVA_NODE_CONFIG_H
"""
        with open(out_path, "w", encoding="utf-8") as f:
            f.write(header_content)

        return out_path


# ─────────────────────────────────────────────────────────────────────────────
# CLI COMMANDS
# ─────────────────────────────────────────────────────────────────────────────

def cmd_list(args, prov):
    profiles = prov.list_profiles()
    print("=" * 80)
    print(f"ARVACORE PROVISIONING PROFILES (Active: {len(profiles)} / {MAX_PROFILES} max)")
    print("=" * 80)
    if not profiles:
        print("No provisioning profiles found. Run 'generate' to create one.")
        return 0

    header = f"{'LOGICAL ID':<16} {'DISPLAY NAME':<22} {'STATUS':<14} {'HARDWARE MAC':<18} {'UPDATED'}"
    print(header)
    print("-" * 80)
    for p in profiles:
        if "error" in p:
            print(f"{p.get('logical_node_id', 'UNKNOWN'):<16} ERROR: {p['error']}")
            continue
        print(f"{p.get('logical_node_id', ''):<16} "
              f"{p.get('display_name', '')[:20]:<22} "
              f"{p.get('status', 'GENERATED'):<14} "
              f"{(p.get('hardware_mac') or 'PENDING_BIND'):<18} "
              f"{p.get('metadata', {}).get('updated_at', '')[:19]}")
    print("=" * 80)
    return 0


def cmd_generate(args, prov):
    try:
        profile = prov.generate_profile(
            node_id=args.id,
            display_name=args.name,
            dry_run=args.dry_run,
            force=args.force,
            hardware_mac=args.mac
        )
        mode_str = "[DRY-RUN] Would generate" if args.dry_run else "Successfully generated"
        print(f"[SUCCESS] {mode_str} profile:")
        print(f"  • Logical ID:    {profile['logical_node_id']}")
        print(f"  • Display Name:  {profile['display_name']}")
        print(f"  • Status:        {profile['status']}")
        print(f"  • Build ID:      {profile['build_id']}")
        print(f"  • Credential:    {profile['device_key'][:8]}**************** (Redacted)")
        if not args.dry_run:
            print(f"  • Saved File:    {prov.profiles_dir / (profile['logical_node_id'] + '.json')} (mode 0600)")
        return 0
    except Exception as e:
        print(f"[ERROR] Failed to generate profile: {e}", file=sys.stderr)
        return 1


def cmd_inspect(args, prov):
    profile = prov.inspect_profile(args.id, redact=not args.show_secrets)
    if not profile:
        print(f"[ERROR] Profile '{args.id}' not found.", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(profile, indent=2))
        return 0

    print("=" * 60)
    print(f"NODE PROVISIONING PROFILE: {profile['logical_node_id']}")
    print("=" * 60)
    print(f"Display Name:     {profile.get('display_name')}")
    print(f"Status:           {profile.get('status')}")
    print(f"Hardware MAC:     {profile.get('hardware_mac') or '(Unbound / Discovered at flash)'}")
    print(f"Hardware Model:   {profile.get('hardware_model')}")
    print(f"Firmware Version: {profile.get('firmware_version')}")
    print(f"Build Identifier: {profile.get('build_id')}")
    print(f"Device Secret:    {profile.get('device_key')}")
    print("\nNetwork:")
    print(f"  SSID:           {profile.get('network', {}).get('wifi_ssid')}")
    print(f"  Backend URL:    {profile.get('network', {}).get('backend_base_url')}")
    print(f"  Gateway Key:    {profile.get('network', {}).get('gateway_key')}")
    print("\nSensor Settings:")
    print(f"  Datum Offset:   {profile.get('sensor_config', {}).get('datum_offset_cm')} cm")
    print(f"  Interval:       {profile.get('sensor_config', {}).get('telemetry_interval_ms')} ms (3.0s)")
    print("\nMetadata:")
    print(f"  Created:        {profile.get('metadata', {}).get('created_at')}")
    print(f"  Last Flashed:   {profile.get('metadata', {}).get('last_flashed_at') or 'Never'}")
    print(f"  Flashed Port:   {profile.get('metadata', {}).get('last_flashed_port') or 'None'}")
    print("=" * 60)
    return 0


def cmd_validate(args, prov):
    is_valid, errors = prov.validate_profiles()
    print("=" * 60)
    print("PROVISIONING CONFIGURATION VALIDATION")
    print("=" * 60)
    if is_valid:
        count = len(prov.list_profiles())
        print(f"[PASS] All {count} active profiles passed validation.")
        return 0
    else:
        print(f"[FAIL] Found {len(errors)} validation error(s):")
        for err in errors:
            print(f"  ✖ {err}")
        return 1


def cmd_export_header(args, prov):
    try:
        out_path = prov.export_header(args.id, args.output)
        print(f"[SUCCESS] Exported build header for '{args.id}' -> {out_path}")
        return 0
    except Exception as e:
        print(f"[ERROR] Failed to export header: {e}", file=sys.stderr)
        return 1


def main():
    parser = argparse.ArgumentParser(
        description="ArvaNode Dynamic Provisioning & Identity Manager (Max 10 Nodes)"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    # list
    subparsers.add_parser("list", help="List all configured provisioning profiles")

    # generate
    gen_p = subparsers.add_parser("generate", help="Generate a new node provisioning profile")
    gen_p.add_argument("--id", help="Logical Node ID (e.g. ARVANODE-004)")
    gen_p.add_argument("--name", help="Display name (e.g. 'ArvaNode Field 04')")
    gen_p.add_argument("--mac", help="Known hardware MAC address (optional)")
    gen_p.add_argument("--force", action="store_true", help="Overwrite existing profile if present")
    gen_p.add_argument("--dry-run", action="store_true", help="Simulate without writing files")

    # inspect
    ins_p = subparsers.add_parser("inspect", help="Inspect profile details")
    ins_p.add_argument("id", help="Logical Node ID to inspect")
    ins_p.add_argument("--json", action="store_true", help="Output raw JSON")
    ins_p.add_argument("--show-secrets", action="store_true", help="Do not redact secret tokens")

    # validate
    subparsers.add_parser("validate", help="Validate all provisioning profiles against invariants")

    # export-header
    hdr_p = subparsers.add_parser("export-header", help="Generate C/C++ config header for firmware compilation")
    hdr_p.add_argument("id", help="Logical Node ID")
    hdr_p.add_argument("-o", "--output", help="Output path for node_config.h")

    args = parser.parse_args()
    prov = NodeProvisioner()

    if args.command == "list":
        sys.exit(cmd_list(args, prov))
    elif args.command == "generate":
        sys.exit(cmd_generate(args, prov))
    elif args.command == "inspect":
        sys.exit(cmd_inspect(args, prov))
    elif args.command == "validate":
        sys.exit(cmd_validate(args, prov))
    elif args.command == "export-header":
        sys.exit(cmd_export_header(args, prov))
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()

