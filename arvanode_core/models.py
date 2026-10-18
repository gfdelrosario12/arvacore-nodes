"""
arvanode_core.models - Canonical Configuration & Provisioning Models
ArvaCore Smart Irrigation Ecosystem (2026)

Defines strongly-typed, immutable dataclasses for Node configuration, validation,
and normalization. This represents the single source of truth across the entire
ArvaNode provisioning pipeline.
"""

from dataclasses import dataclass, field, asdict
from datetime import datetime, timezone
import re
from typing import Optional, Dict, Any


class ConfigurationError(ValueError):
    """Raised when configuration values violate invariants or validation rules."""
    pass


NODE_ID_REGEX = re.compile(r"^ARVANODE-(00[1-9]|010|[0-9]{3,4})$")
MAC_REGEX = re.compile(r"^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$")



@dataclass(frozen=True)
class NetworkConfig:
    wifi_ssid: str = "Cream"
    wifi_password: str = "hatdog123"
    backend_base_url: str = "https://api-aquaflow.gladwindr.me/api/v1"
    gateway_key: str = "ed630e681ef735f36954469b48f832f210286246ca95fff62e62086cab74e8a9"

    def validate(self) -> None:
        if not self.wifi_ssid or not self.wifi_ssid.strip():
            raise ConfigurationError("WiFi SSID cannot be empty.")
        if not self.backend_base_url or not (
            self.backend_base_url.startswith("http://") or self.backend_base_url.startswith("https://")
        ):
            raise ConfigurationError(f"Invalid backend URL: '{self.backend_base_url}'. Must start with http:// or https://")


@dataclass(frozen=True)
class SensorConfig:
    datum_offset_cm: float = 20.0
    min_distance_cm: float = 2.0
    max_distance_cm: float = 450.0
    soil_air_value: int = 3200
    soil_water_value: int = 1500
    telemetry_interval_ms: int = 3000

    def validate(self) -> None:
        if self.min_distance_cm >= self.max_distance_cm:
            raise ConfigurationError(f"min_distance_cm ({self.min_distance_cm}) must be less than max_distance_cm ({self.max_distance_cm}).")
        if self.soil_water_value >= self.soil_air_value:
            raise ConfigurationError(f"soil_water_value ({self.soil_water_value}) must be less than soil_air_value ({self.soil_air_value}).")
        if self.telemetry_interval_ms != 3000:
            raise ConfigurationError(
                f"telemetry_interval_ms must be exactly 3000 ms per production system invariant (got {self.telemetry_interval_ms})."
            )


@dataclass(frozen=True)
class PinoutConfig:
    soil_moisture_analog: int = 34
    ultrasonic_trig: int = 5
    ultrasonic_echo: int = 18
    activation_btn: int = 19
    pairing_btn: int = 23
    pairing_led: int = 25
    activation_led: int = 26


ALLOWED_NODE_IDS = [f"ARVANODE-{i:03d}" for i in range(1, 11)]


@dataclass(frozen=True)
class NodeConfig:
    logical_node_id: str = "ARVANODE-001"
    display_name: str = "ArvaNode Field 01"
    hardware_model: str = "ESP32-WROOM-32"
    hardware_revision: str = "rev1"
    firmware_version: str = "1.2.0"
    build_id: str = "std-20261018"
    device_key: str = ""
    hardware_mac: str = ""
    status: str = "GENERATED"
    network: NetworkConfig = field(default_factory=NetworkConfig)
    sensor: SensorConfig = field(default_factory=SensorConfig)
    pinout: PinoutConfig = field(default_factory=PinoutConfig)

    def validate(self) -> None:
        if not self.logical_node_id or not self.logical_node_id.strip():
            raise ConfigurationError("logical_node_id cannot be empty.")
        if self.logical_node_id not in ALLOWED_NODE_IDS and not NODE_ID_REGEX.match(self.logical_node_id):
            raise ConfigurationError(f"Invalid logical_node_id: '{self.logical_node_id}'.")
        if self.logical_node_id not in ALLOWED_NODE_IDS:
            raise ConfigurationError(f"Node ID '{self.logical_node_id}' is outside allowed set {ALLOWED_NODE_IDS}.")
        if not self.display_name or not self.display_name.strip():
            raise ConfigurationError("display_name cannot be empty.")
        if self.hardware_mac and not MAC_REGEX.match(self.hardware_mac):
            raise ConfigurationError(f"Invalid hardware MAC format: '{self.hardware_mac}'.")
        self.network.validate()
        self.sensor.validate()


    def to_dict(self) -> Dict[str, Any]:
        return {
            "logical_node_id": self.logical_node_id,
            "display_name": self.display_name,
            "hardware_model": self.hardware_model,
            "hardware_revision": self.hardware_revision,
            "firmware_version": self.firmware_version,
            "build_id": self.build_id,
            "device_key": self.device_key,
            "hardware_mac": self.hardware_mac,
            "status": self.status,
            "network": asdict(self.network),
            "sensor_config": asdict(self.sensor),
            "pinout": asdict(self.pinout),
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "NodeConfig":
        net_raw = data.get("network", {})
        sensor_raw = data.get("sensor_config", {})
        pin_raw = data.get("pinout", {})

        network = NetworkConfig(
            wifi_ssid=net_raw.get("wifi_ssid", "Cream"),
            wifi_password=net_raw.get("wifi_password", "hatdog123"),
            backend_base_url=net_raw.get("backend_base_url", "https://api-aquaflow.gladwindr.me/api/v1"),
            gateway_key=net_raw.get("gateway_key", "")
        )

        sensor = SensorConfig(
            datum_offset_cm=float(sensor_raw.get("datum_offset_cm", 20.0)),
            min_distance_cm=float(sensor_raw.get("min_distance_cm", 2.0)),
            max_distance_cm=float(sensor_raw.get("max_distance_cm", 450.0)),
            soil_air_value=int(sensor_raw.get("soil_air_value", 3200)),
            soil_water_value=int(sensor_raw.get("soil_water_value", 1500)),
            telemetry_interval_ms=int(sensor_raw.get("telemetry_interval_ms", 3000))
        )

        pinout = PinoutConfig(
            soil_moisture_analog=int(pin_raw.get("soil_moisture_analog", 34)),
            ultrasonic_trig=int(pin_raw.get("ultrasonic_trig", 5)),
            ultrasonic_echo=int(pin_raw.get("ultrasonic_echo", 18)),
            activation_btn=int(pin_raw.get("activation_btn", 19)),
            pairing_btn=int(pin_raw.get("pairing_btn", 23)),
            pairing_led=int(pin_raw.get("pairing_led", 25)),
            activation_led=int(pin_raw.get("activation_led", 26))
        )

        cfg = cls(
            logical_node_id=data.get("logical_node_id", "ARVANODE-001"),
            display_name=data.get("display_name", "ArvaNode Field 01"),
            hardware_model=data.get("hardware_model", "ESP32-WROOM-32"),
            hardware_revision=data.get("hardware_revision", "rev1"),
            firmware_version=data.get("firmware_version", "1.2.0"),
            build_id=data.get("build_id", f"bld-{datetime.now(timezone.utc).strftime('%Y%m%d%H%M%S')}"),
            device_key=data.get("device_key", ""),
            hardware_mac=data.get("hardware_mac", ""),
            status=data.get("status", "GENERATED"),
            network=network,
            sensor=sensor,
            pinout=pinout
        )
        cfg.validate()
        return cfg

    @classmethod
    def from_default(cls, index: int = 1) -> "NodeConfig":
        """Creates a standard default NodeConfig by index (1 to 10)."""
        if index < 1 or index > 10:
            raise ConfigurationError(f"Default index must be between 1 and 10 (got {index}).")
        node_id = f"ARVANODE-{index:03d}"
        display_name = f"ArvaNode Field {index:02d}"
        return cls(
            logical_node_id=node_id,
            display_name=display_name,
            device_key=f"dev_sec_default_{index:02d}",
            network=NetworkConfig(),
            sensor=SensorConfig(),
            pinout=PinoutConfig()
        )
