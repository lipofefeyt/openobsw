"""
hardware.py — Hardware profile loader for openobsw SRDB.

Loads equipment hardware profiles from srdb/data/hardware/*.yaml.
Profiles parameterise simulation models — max speeds, noise levels,
bus interfaces — without changing model code.

Usage:
    from obsw_srdb.hardware import HardwareProfile, load_profile

    profile = load_profile("rw_sinclair_rw003", data_dir="srdb/data/hardware")
    max_speed = profile.params["max_speed_rpm"]
"""
from __future__ import annotations

from pathlib import Path
from typing import Any, Dict, Optional, Union

import yaml
from pydantic import BaseModel, field_validator


class HardwareProfile(BaseModel):
    """A hardware equipment profile loaded from YAML."""
    hardware_id:  str
    type:         str
    description:  str
    vendor:       str = "generic"
    params:       Dict[str, Any] = {}

    @field_validator("type")
    @classmethod
    def type_is_known(cls, v: str) -> str:
        known = {
            "reaction_wheel", "magnetorquer", "magnetometer",
            "gyroscope", "star_tracker", "css", "thruster",
            "gps", "thermal_node", "pcdu", "battery",
        }
        if v not in known:
            raise ValueError(
                f"Unknown hardware type '{v}'. Known: {sorted(known)}"
            )
        return v

    def get(self, key: str, default: Any = None) -> Any:
        """Get a parameter value with optional default."""
        return self.params.get(key, default)

    def require(self, key: str) -> Any:
        """Get a required parameter — raises KeyError if missing."""
        if key not in self.params:
            raise KeyError(
                f"Required parameter '{key}' missing from "
                f"hardware profile '{self.hardware_id}'"
            )
        return self.params[key]


def load_profile(
    hardware_id: str,
    data_dir: Union[str, Path] = "srdb/data/hardware",
) -> HardwareProfile:
    """
    Load a hardware profile by ID from data_dir.

    Args:
        hardware_id: Profile identifier (e.g. 'rw_sinclair_rw003')
        data_dir:    Directory containing hardware YAML profiles

    Returns:
        HardwareProfile instance

    Raises:
        FileNotFoundError: If profile YAML not found
        ValidationError:   If profile YAML is invalid
    """
    path = Path(data_dir) / f"{hardware_id}.yaml"
    if not path.exists():
        available = [p.stem for p in Path(data_dir).glob("*.yaml")]
        raise FileNotFoundError(
            f"Hardware profile '{hardware_id}' not found at {path}. "
            f"Available: {sorted(available)}"
        )
    with open(path) as f:
        data = yaml.safe_load(f)
    return HardwareProfile(**data)


def list_profiles(
    data_dir: Union[str, Path] = "srdb/data/hardware",
    equipment_type: Optional[str] = None,
) -> list[HardwareProfile]:
    """
    List all hardware profiles in data_dir.

    Args:
        data_dir:       Directory containing hardware YAML profiles
        equipment_type: Filter by type (e.g. 'reaction_wheel')
    """
    profiles = []
    for path in sorted(Path(data_dir).glob("*.yaml")):
        with open(path) as f:
            data = yaml.safe_load(f)
        profile = HardwareProfile(**data)
        if equipment_type is None or profile.type == equipment_type:
            profiles.append(profile)
    return profiles
