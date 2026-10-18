"""
arvanode_core.repository - Preset & Profile Repository
ArvaCore Smart Irrigation Ecosystem (2026)

Manages persistent profile presets on disk. Profiles are treated as optional
saved presets rather than mandatory runtime bottlenecks.
"""

import json
import os
import secrets
from pathlib import Path
from typing import List, Optional, Dict, Any

from .models import NodeConfig, NetworkConfig, SensorConfig, PinoutConfig

MAX_PROFILES = 10

DEFAULT_PROFILES_METADATA = [
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


class ProfileRepository:
    """Manages file storage and retrieval of reusable node configuration presets."""

    def __init__(self, profiles_dir: Optional[Path] = None, storage_dir: Optional[Path] = None):
        target = profiles_dir or storage_dir
        if target is None:
            # Default to repo_root/provisioning/profiles
            repo_root = Path(__file__).resolve().parent.parent
            self.profiles_dir = repo_root / "provisioning" / "profiles"
        else:
            self.profiles_dir = Path(target)

        self.profiles_dir.mkdir(parents=True, exist_ok=True)


    def list_profiles(self) -> List[NodeConfig]:
        """Loads and returns all valid profile configurations found in profiles_dir."""
        profiles = []
        for json_file in sorted(self.profiles_dir.glob("*.json")):
            try:
                cfg = self.load_profile(json_file.stem)
                if cfg:
                    profiles.append(cfg)
            except Exception:
                # Log or skip corrupt files gracefully
                continue
        return profiles

    def load_profile(self, node_id: str) -> Optional[NodeConfig]:
        """Loads a single profile by logical node ID, validating and returning NodeConfig."""
        file_path = self.profiles_dir / f"{node_id}.json"
        if not file_path.exists():
            return None

        with open(file_path, "r", encoding="utf-8") as f:
            data = json.load(f)

        return NodeConfig.from_dict(data)

    def load(self, node_id: str) -> Optional[NodeConfig]:
        return self.load_profile(node_id)

    def exists(self, node_id: str) -> bool:
        return (self.profiles_dir / f"{node_id}.json").exists()

    def delete(self, node_id: str) -> bool:
        file_path = self.profiles_dir / f"{node_id}.json"
        if file_path.exists():
            file_path.unlink()
            return True
        return False

    def save_profile(self, config: NodeConfig) -> Path:
        """Saves a NodeConfig instance to disk as a JSON profile preset."""
        config.validate()

        file_path = self.profiles_dir / f"{config.logical_node_id}.json"
        raw_dict = config.to_dict()

        # Write safely
        temp_file = file_path.with_suffix(".tmp")
        with open(temp_file, "w", encoding="utf-8") as f:
            json.dump(raw_dict, f, indent=2)

        if os.name == "posix":
            try:
                os.chmod(temp_file, 0o600)
            except OSError:
                pass

        temp_file.replace(file_path)
        return file_path

    def save(self, config: NodeConfig) -> Path:
        return self.save_profile(config)


    def create_default_profile(self, node_id: str, display_name: Optional[str] = None) -> NodeConfig:
        """Creates and persists a new profile preset with generated cryptographically-secure device key."""
        existing = self.list_profiles()
        if len(existing) >= MAX_PROFILES and not (self.profiles_dir / f"{node_id}.json").exists():
            raise ValueError(f"Maximum limit of {MAX_PROFILES} active node profiles reached.")

        dev_key = f"dev_sec_{secrets.token_hex(28)}"
        name = display_name or f"ArvaNode {node_id.replace('ARVANODE-', '')}"

        config = NodeConfig(
            logical_node_id=node_id,
            display_name=name,
            device_key=dev_key,
            status="GENERATED",
            network=NetworkConfig(),
            sensor=SensorConfig(),
            pinout=PinoutConfig()
        )
        self.save_profile(config)
        return config

    def ensure_default_profiles(self) -> None:
        """Ensures the 10 baseline profiles exist on disk."""
        for meta in DEFAULT_PROFILES_METADATA:
            node_id = meta["id"]
            if not (self.profiles_dir / f"{node_id}.json").exists():
                self.create_default_profile(node_id, meta["name"])
