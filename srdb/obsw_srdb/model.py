"""
model.py — Pydantic models for the openobsw SRDB.

Each class maps directly to a YAML entity. Validation is performed
at instantiation time by Pydantic — invalid data raises immediately.
"""

from __future__ import annotations

from enum import Enum
from typing import List, Optional

from pydantic import BaseModel, field_validator, model_validator


# ---------------------------------------------------------------------------
# Enumerations
# ---------------------------------------------------------------------------

class ParameterType(str, Enum):
    UINT8  = "uint8"
    UINT16 = "uint16"
    UINT32 = "uint32"
    INT8   = "int8"
    INT16  = "int16"
    INT32  = "int32"
    FLOAT32 = "float32"


class ConversionType(str, Enum):
    LINEAR = "linear"


class Severity(str, Enum):
    INFO   = "INFO"
    LOW    = "LOW"
    MEDIUM = "MEDIUM"
    HIGH   = "HIGH"

    @property
    def pus_subservice(self) -> int:
        """Maps to TM(5,x) subservice number."""
        return {"INFO": 1, "LOW": 2, "MEDIUM": 3, "HIGH": 4}[self.value]


# ---------------------------------------------------------------------------
# Parameters
# ---------------------------------------------------------------------------

class ParameterConversion(BaseModel):
    type:   ConversionType
    slope:  Optional[float] = None
    offset: Optional[float] = None

    @model_validator(mode="after")
    def check_linear_fields(self) -> "ParameterConversion":
        if self.type == ConversionType.LINEAR:
            if self.slope is None or self.offset is None:
                raise ValueError("Linear conversion requires 'slope' and 'offset'")
        return self


class ParameterLimits(BaseModel):
    soft_low:  Optional[float] = None
    soft_high: Optional[float] = None
    hard_low:  Optional[float] = None
    hard_high: Optional[float] = None

    @model_validator(mode="after")
    def check_ordering(self) -> "ParameterLimits":
        if self.hard_low is not None and self.soft_low is not None:
            if self.hard_low > self.soft_low:
                raise ValueError("hard_low must be <= soft_low")
        if self.soft_high is not None and self.hard_high is not None:
            if self.soft_high > self.hard_high:
                raise ValueError("soft_high must be <= hard_high")
        return self


class Parameter(BaseModel):
    id:          int
    name:        str
    description: str
    type:        ParameterType
    unit:        Optional[str] = None
    conversion:  Optional[ParameterConversion] = None
    limits:      Optional[ParameterLimits] = None

    @field_validator("id")
    @classmethod
    def id_in_range(cls, v: int) -> int:
        if not (0x0000 <= v <= 0xFFFF):
            raise ValueError(f"Parameter ID 0x{v:04X} out of 16-bit range")
        return v

    @field_validator("name")
    @classmethod
    def name_is_snake_case(cls, v: str) -> str:
        if not v.replace("_", "").isalnum():
            raise ValueError(f"Parameter name '{v}' must be snake_case alphanumeric")
        return v


# ---------------------------------------------------------------------------
# Telecommands
# ---------------------------------------------------------------------------

class TCParameter(BaseModel):
    name:        str
    type:        ParameterType
    description: Optional[str] = None


class Telecommand(BaseModel):
    name:        str
    description: str
    apid:        int
    service:     int
    subservice:  int
    parameters:  List[TCParameter] = []

    @field_validator("apid")
    @classmethod
    def apid_in_range(cls, v: int) -> int:
        if not (0x000 <= v <= 0x7FE):
            raise ValueError(f"APID 0x{v:03X} out of 11-bit range (0x000–0x7FE)")
        return v

    @field_validator("service", "subservice")
    @classmethod
    def svc_in_range(cls, v: int) -> int:
        if not (0 <= v <= 255):
            raise ValueError(f"Service/subservice {v} out of uint8 range")
        return v


# ---------------------------------------------------------------------------
# HK sets
# ---------------------------------------------------------------------------

class HKSet(BaseModel):
    id:                     int
    name:                   str
    description:            str
    parameters:             List[str]   # parameter names — validated by loader
    default_interval_ticks: int

    @field_validator("id")
    @classmethod
    def id_in_range(cls, v: int) -> int:
        if not (1 <= v <= 255):
            raise ValueError(f"HK set ID {v} must be in 1–255")
        return v

    @field_validator("default_interval_ticks")
    @classmethod
    def interval_non_negative(cls, v: int) -> int:
        if v < 0:
            raise ValueError("default_interval_ticks must be >= 0")
        return v


# ---------------------------------------------------------------------------
# Events
# ---------------------------------------------------------------------------

class EventAuxData(BaseModel):
    name:        str
    type:        ParameterType
    description: Optional[str] = None


class Event(BaseModel):
    id:             int
    name:           str
    severity:       Severity
    description:    str
    safe_trigger:   bool = False
    auxiliary_data: List[EventAuxData] = []

    @field_validator("id")
    @classmethod
    def id_in_range(cls, v: int) -> int:
        if not (0x0000 <= v <= 0xFFFF):
            raise ValueError(f"Event ID 0x{v:04X} out of 16-bit range")
        return v

    @model_validator(mode="after")
    def safe_trigger_only_for_high(self) -> "Event":
        if self.safe_trigger and self.severity != Severity.HIGH:
            raise ValueError(
                f"Event '{self.name}': safe_trigger=true requires severity=HIGH"
            )
        return self


# ---------------------------------------------------------------------------
# Spacecraft
# ---------------------------------------------------------------------------

class Spacecraft(BaseModel):
    name:         str
    description:  Optional[str] = None
    scid:         int
    apid_default: int

    @field_validator("scid")
    @classmethod
    def scid_in_range(cls, v: int) -> int:
        if not (0x000 <= v <= 0x3FF):
            raise ValueError(f"SCID 0x{v:03X} out of 10-bit range")
        return v


# ---------------------------------------------------------------------------
# Top-level SRDB
# ---------------------------------------------------------------------------

class SRDB(BaseModel):
    spacecraft:   Spacecraft
    parameters:   List[Parameter]
    telecommands: List[Telecommand]
    hk_sets:      List[HKSet]
    events:       List[Event]

    def parameter_by_name(self, name: str) -> Optional[Parameter]:
        return next((p for p in self.parameters if p.name == name), None)

    def parameter_by_id(self, pid: int) -> Optional[Parameter]:
        return next((p for p in self.parameters if p.id == pid), None)

    def event_by_name(self, name: str) -> Optional[Event]:
        return next((e for e in self.events if e.name == name), None)

    def event_by_id(self, eid: int) -> Optional[Event]:
        return next((e for e in self.events if e.id == eid), None)

    def hk_set_by_id(self, sid: int) -> Optional[HKSet]:
        return next((s for s in self.hk_sets if s.id == sid), None)

    def safe_trigger_event_ids(self) -> List[int]:
        """Return list of event IDs that trigger safe mode transition."""
        return [e.id for e in self.events if e.safe_trigger]