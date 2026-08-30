"""obsw_srdb — openobsw Spacecraft Resource Database loader."""

from .composition import (
    SRDBComposer,
    SRDBCompositionError,
    SRDBContribution,
    SRDBContributionLoader,
)
from .loader import SRDBLoader, SRDBValidationError
from .model import (
    SRDB,
    Event,
    HKSet,
    Parameter,
    ParameterType,
    Severity,
    Spacecraft,
    Telecommand,
)

__all__ = [
    "SRDBLoader",
    "SRDBValidationError",
    "SRDBComposer",
    "SRDBCompositionError",
    "SRDBContribution",
    "SRDBContributionLoader",
    "SRDB",
    "Event",
    "HKSet",
    "Parameter",
    "ParameterType",
    "Severity",
    "Spacecraft",
    "Telecommand",
]
from .hardware import HardwareProfile, load_profile, list_profiles
