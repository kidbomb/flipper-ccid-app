"""Shared pytest fixtures for the CCID host test suites.

Suites that define their own module-scoped `connection` fixture keep it; this
one only fills in for suites that don't, such as app_select_test.py.
"""

import pytest
from smartcard.System import readers
from smartcard.Exceptions import NoCardException, CardConnectionException


@pytest.fixture(scope="module")
def connection():
    """Create a connection to the smart card reader."""
    r = readers()
    if not r:
        pytest.skip("No smart card readers found")

    # Find the Flipper CCID reader
    reader = None
    for sc in r:
        if "USB Smart Card" in str(sc) or "Flipper" in str(sc):
            reader = sc
            break

    if reader is None:
        reader = r[0]  # Use first available reader

    try:
        conn = reader.createConnection()
        conn.connect()
        yield conn
        conn.disconnect()
    except (NoCardException, CardConnectionException) as e:
        pytest.skip(f"Could not connect to card: {e}")
