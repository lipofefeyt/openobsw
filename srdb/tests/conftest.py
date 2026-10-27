"""Shared pytest fixtures for SRDB tests."""

from pathlib import Path
import pytest
from obsw_srdb import SRDBLoader, SRDB

DATA_DIR = Path(__file__).parent.parent / "data"


@pytest.fixture(scope="session")
def srdb() -> SRDB:
    """Load the full SRDB once per test session."""
    return SRDBLoader.load(DATA_DIR)


@pytest.fixture(scope="session")
def data_dir() -> Path:
    return DATA_DIR

def pytest_configure(config: pytest.Config) -> None:
    config.addinivalue_line("markers", "requirement(id): SRS requirement verified by the test")
