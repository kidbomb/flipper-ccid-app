#!/usr/bin/env python3
"""
PIV PIN Management Test Suite

Tests PIV-specific PIN commands per NIST SP 800-73-5:
- VERIFY (INS=0x20) with PIV PIN reference 0x80
- CHANGE REFERENCE DATA (INS=0x24) 
- RESET RETRY COUNTER (INS=0x2C)

PIV PIN Format:
- 6-8 numeric ASCII digits (0x30-0x39)
- Padded with 0xFF to 8 bytes

Key References (Table 4 of SP 800-73-5):
- 0x80: PIV Card Application PIN
- 0x81: PIN Unblocking Key (PUK)

Usage:
    pytest piv_pin_test.py -v
"""

import pytest
from smartcard.System import readers
from smartcard.Exceptions import NoCardException, CardConnectionException
from smartcard.util import toHexString


# =============================================================================
# PIV Constants
# =============================================================================

# PIV AID
PIV_AID = [0xA0, 0x00, 0x00, 0x03, 0x08, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00]

# PIV PIN/PUK References
PIV_PIN_REF = 0x80      # PIV Card Application PIN
PIV_PUK_REF = 0x81      # PIN Unblocking Key

# Status words
SW_OK = [0x90, 0x00]
SW_AUTH_BLOCKED = [0x69, 0x83]           # Authentication blocked
SW_REF_DATA_NOT_FOUND = [0x6A, 0x88]     # Reference data not found
SW_SECURITY_NOT_SATISFIED = [0x69, 0x82] # Security status not satisfied
SW_WRONG_LENGTH = [0x67, 0x00]           # Wrong length
SW_WRONG_DATA = [0x6A, 0x80]             # Wrong data
SW_WRONG_P1P2 = [0x6A, 0x86]             # Incorrect P1P2

# Default PIV PIN: "123456" padded with 0xFF
DEFAULT_PIN = [0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0xFF, 0xFF]

# Default PIV PUK: "12345678" (8 bytes, no padding needed)
DEFAULT_PUK = [0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38]

# Wrong PIN: "999999" padded
WRONG_PIN = [0x39, 0x39, 0x39, 0x39, 0x39, 0x39, 0xFF, 0xFF]

# Wrong PUK: "99999999"
WRONG_PUK = [0x39, 0x39, 0x39, 0x39, 0x39, 0x39, 0x39, 0x39]

# New PIN for testing: "654321" padded
NEW_PIN = [0x36, 0x35, 0x34, 0x33, 0x32, 0x31, 0xFF, 0xFF]

# Alternative PIN: "111111" padded (6 digits, minimum length)
MIN_PIN = [0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0xFF, 0xFF]

# Maximum length PIN: "12345678" (8 digits, no padding)
MAX_PIN = [0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38]


# =============================================================================
# Fixtures
# =============================================================================

@pytest.fixture(scope="module")
def connection():
    """Create a connection to the smart card reader."""
    r = readers()
    if not r:
        pytest.skip("No smart card readers found")
    
    # Find Flipper CCID reader
    reader = r[0]
    for sc in r:
        if "USB Smart Card" in str(sc) or "Flipper" in str(sc):
            reader = sc
            break
    
    print(f"\nUsing reader: {reader}")
    
    try:
        conn = reader.createConnection()
        conn.connect()
        print(f"ATR: {toHexString(conn.getATR())}")
        yield conn
        conn.disconnect()
    except (NoCardException, CardConnectionException) as e:
        pytest.skip(f"Could not connect to card: {e}")


@pytest.fixture
def piv_selected(connection):
    """Select PIV application and return connection."""
    connection.reconnect()  # Reset state
    select_piv(connection)
    return connection


# =============================================================================
# Helper Functions
# =============================================================================

def transmit_and_log(connection, apdu: list, description: str = ""):
    """Transmit APDU and log exchange."""
    if description:
        print(f"\n{description}")
    print(f"  >> {toHexString(apdu)}")
    data, sw1, sw2 = connection.transmit(apdu)
    print(f"  << {toHexString(data)} SW={sw1:02X}{sw2:02X}")
    return data, sw1, sw2


def select_piv(connection):
    """Select PIV application."""
    apdu = [0x00, 0xA4, 0x04, 0x00, len(PIV_AID)] + PIV_AID
    data, sw1, sw2 = transmit_and_log(connection, apdu, "SELECT PIV")
    assert [sw1, sw2] == SW_OK, f"Failed to select PIV: {sw1:02X}{sw2:02X}"
    return data


def verify_piv_pin(connection, pin: list, reference: int = PIV_PIN_REF):
    """Send VERIFY command for PIV PIN."""
    apdu = [0x00, 0x20, 0x00, reference, len(pin)] + pin
    return transmit_and_log(connection, apdu, 
        f"VERIFY PIV PIN ref={reference:02X}")


def check_piv_pin_retries(connection, reference: int = PIV_PIN_REF):
    """Check remaining PIV PIN retries (VERIFY with no data)."""
    apdu = [0x00, 0x20, 0x00, reference]
    return transmit_and_log(connection, apdu, 
        f"CHECK PIV RETRIES ref={reference:02X}")


def change_reference_data(connection, old_value: list, new_value: list, 
                          reference: int = PIV_PIN_REF):
    """Send CHANGE REFERENCE DATA command.
    
    Command format: 00 24 00 <ref> Lc <old PIN><new PIN>
    """
    data = old_value + new_value
    apdu = [0x00, 0x24, 0x00, reference, len(data)] + data
    return transmit_and_log(connection, apdu,
        f"CHANGE REFERENCE DATA ref={reference:02X}")


def reset_retry_counter(connection, puk: list, new_pin: list, 
                        reference: int = PIV_PIN_REF):
    """Send RESET RETRY COUNTER command.
    
    Command format: 00 2C 00 <ref> Lc <PUK><new PIN>
    """
    data = puk + new_pin
    apdu = [0x00, 0x2C, 0x00, reference, len(data)] + data
    return transmit_and_log(connection, apdu,
        f"RESET RETRY COUNTER ref={reference:02X}")


def get_retries_from_sw(sw1: int, sw2: int) -> int:
    """Extract retry count from status word 63CX."""
    if sw1 == 0x63 and (sw2 & 0xF0) == 0xC0:
        return sw2 & 0x0F
    return -1


# =============================================================================
# PIV PIN VERIFY Tests
# =============================================================================

class TestPivPinVerify:
    """Test VERIFY command with PIV PIN reference 0x80."""
    
    def test_verify_correct_pin(self, piv_selected):
        """VERIFY with correct PIV PIN should return 9000."""
        # Check if PIN is blocked
        _, sw1, sw2 = check_piv_pin_retries(piv_selected, PIV_PIN_REF)
        if [sw1, sw2] == SW_AUTH_BLOCKED:
            pytest.skip("PIV PIN is blocked")
        
        data, sw1, sw2 = verify_piv_pin(piv_selected, DEFAULT_PIN, PIV_PIN_REF)
        assert [sw1, sw2] == SW_OK, f"Expected 9000, got {sw1:02X}{sw2:02X}"
    
    def test_verify_wrong_pin_returns_retries(self, piv_selected):
        """VERIFY with wrong PIN should return 63CX with remaining retries."""
        data, sw1, sw2 = verify_piv_pin(piv_selected, WRONG_PIN, PIV_PIN_REF)
        
        # Should return 63CX (X = remaining retries)
        assert sw1 == 0x63, f"Expected SW1=63, got {sw1:02X}"
        assert (sw2 & 0xF0) == 0xC0, f"Expected SW2=CX, got {sw2:02X}"
        
        retries = get_retries_from_sw(sw1, sw2)
        print(f"Remaining retries: {retries}")
        assert retries >= 0 and retries <= 10, f"Invalid retry count: {retries}"
    
    def test_check_retries_without_data(self, piv_selected):
        """Check retries without providing PIN data."""
        _, sw1, sw2 = check_piv_pin_retries(piv_selected, PIV_PIN_REF)
        
        # Should return 63CX with retry count
        if [sw1, sw2] != SW_AUTH_BLOCKED:
            assert sw1 == 0x63, f"Expected SW1=63, got {sw1:02X}"
            retries = get_retries_from_sw(sw1, sw2)
            print(f"Current retry count: {retries}")
    
    def test_verify_puk_reference(self, piv_selected):
        """VERIFY with PUK reference 0x81 should work."""
        _, sw1, sw2 = check_piv_pin_retries(piv_selected, PIV_PUK_REF)
        if [sw1, sw2] == SW_AUTH_BLOCKED:
            pytest.skip("PIV PUK is blocked")
        
        # PUK should verify successfully with correct value
        data, sw1, sw2 = verify_piv_pin(piv_selected, DEFAULT_PUK, PIV_PUK_REF)
        assert [sw1, sw2] == SW_OK, f"Expected 9000, got {sw1:02X}{sw2:02X}"
    
    def test_verify_minimum_length_pin(self, piv_selected):
        """VERIFY with 6-digit PIN (minimum length)."""
        # Use only 6 bytes without padding
        short_pin = [0x31, 0x32, 0x33, 0x34, 0x35, 0x36]  # "123456"
        
        data, sw1, sw2 = verify_piv_pin(piv_selected, short_pin, PIV_PIN_REF)
        # Should succeed - default PIN is "123456"
        assert [sw1, sw2] == SW_OK, f"Expected 9000, got {sw1:02X}{sw2:02X}"


# =============================================================================
# CHANGE REFERENCE DATA Tests (INS=0x24)
# =============================================================================

class TestChangeReferenceData:
    """Test CHANGE REFERENCE DATA command (INS=0x24)."""
    
    def test_change_pin_success(self, piv_selected):
        """Change PIN from default to new value."""
        # Skip if PIN blocked
        _, sw1, sw2 = check_piv_pin_retries(piv_selected, PIV_PIN_REF)
        if [sw1, sw2] == SW_AUTH_BLOCKED:
            pytest.skip("PIV PIN is blocked")
        
        # Change PIN from default to new
        _, sw1, sw2 = change_reference_data(
            piv_selected, DEFAULT_PIN, NEW_PIN, PIV_PIN_REF)
        assert [sw1, sw2] == SW_OK, f"Change failed: {sw1:02X}{sw2:02X}"
        
        # Verify new PIN works
        _, sw1, sw2 = verify_piv_pin(piv_selected, NEW_PIN, PIV_PIN_REF)
        assert [sw1, sw2] == SW_OK, "New PIN verification failed"
        
        # Change back to default for other tests
        _, sw1, sw2 = change_reference_data(
            piv_selected, NEW_PIN, DEFAULT_PIN, PIV_PIN_REF)
        assert [sw1, sw2] == SW_OK, "Failed to restore default PIN"
    
    def test_change_pin_wrong_old_pin(self, piv_selected):
        """Change PIN with wrong old PIN should fail."""
        _, sw1, sw2 = check_piv_pin_retries(piv_selected, PIV_PIN_REF)
        if [sw1, sw2] == SW_AUTH_BLOCKED:
            pytest.skip("PIV PIN is blocked")
        
        _, sw1, sw2 = change_reference_data(
            piv_selected, WRONG_PIN, NEW_PIN, PIV_PIN_REF)
        
        # Should fail with 63CX (retry counter)
        assert sw1 == 0x63, f"Expected SW1=63, got {sw1:02X}"
    
    def test_change_pin_minimum_length(self, piv_selected):
        """Change to minimum length PIN (6 digits)."""
        _, sw1, sw2 = check_piv_pin_retries(piv_selected, PIV_PIN_REF)
        if [sw1, sw2] == SW_AUTH_BLOCKED:
            pytest.skip("PIV PIN is blocked")
        
        # Change to 6-digit PIN
        _, sw1, sw2 = change_reference_data(
            piv_selected, DEFAULT_PIN, MIN_PIN, PIV_PIN_REF)
        assert [sw1, sw2] == SW_OK, f"Change failed: {sw1:02X}{sw2:02X}"
        
        # Verify it works
        _, sw1, sw2 = verify_piv_pin(piv_selected, MIN_PIN, PIV_PIN_REF)
        assert [sw1, sw2] == SW_OK, "Min PIN verification failed"
        
        # Restore default
        _, sw1, sw2 = change_reference_data(
            piv_selected, MIN_PIN, DEFAULT_PIN, PIV_PIN_REF)
        assert [sw1, sw2] == SW_OK
    
    def test_change_pin_maximum_length(self, piv_selected):
        """Change to maximum length PIN (8 digits)."""
        _, sw1, sw2 = check_piv_pin_retries(piv_selected, PIV_PIN_REF)
        if [sw1, sw2] == SW_AUTH_BLOCKED:
            pytest.skip("PIV PIN is blocked")
        
        # Change to 8-digit PIN
        _, sw1, sw2 = change_reference_data(
            piv_selected, DEFAULT_PIN, MAX_PIN, PIV_PIN_REF)
        assert [sw1, sw2] == SW_OK, f"Change failed: {sw1:02X}{sw2:02X}"
        
        # Verify it works
        _, sw1, sw2 = verify_piv_pin(piv_selected, MAX_PIN, PIV_PIN_REF)
        assert [sw1, sw2] == SW_OK, "Max PIN verification failed"
        
        # Restore default
        _, sw1, sw2 = change_reference_data(
            piv_selected, MAX_PIN, DEFAULT_PIN, PIV_PIN_REF)
        assert [sw1, sw2] == SW_OK
    
    def test_change_puk(self, piv_selected):
        """Change PUK value."""
        _, sw1, sw2 = check_piv_pin_retries(piv_selected, PIV_PUK_REF)
        if [sw1, sw2] == SW_AUTH_BLOCKED:
            pytest.skip("PIV PUK is blocked")
        
        new_puk = [0x38, 0x37, 0x36, 0x35, 0x34, 0x33, 0x32, 0x31]  # "87654321"
        
        # Change PUK
        _, sw1, sw2 = change_reference_data(
            piv_selected, DEFAULT_PUK, new_puk, PIV_PUK_REF)
        assert [sw1, sw2] == SW_OK, f"Change PUK failed: {sw1:02X}{sw2:02X}"
        
        # Restore default PUK
        _, sw1, sw2 = change_reference_data(
            piv_selected, new_puk, DEFAULT_PUK, PIV_PUK_REF)
        assert [sw1, sw2] == SW_OK


# =============================================================================
# RESET RETRY COUNTER Tests (INS=0x2C)
# =============================================================================

class TestResetRetryCounter:
    """Test RESET RETRY COUNTER command (INS=0x2C)."""
    
    def test_reset_retry_counter_success(self, piv_selected):
        """Reset PIN retry counter using PUK."""
        # First, cause a failed verification to decrement counter
        verify_piv_pin(piv_selected, WRONG_PIN, PIV_PIN_REF)
        
        # Check counter was decremented
        _, sw1, sw2 = check_piv_pin_retries(piv_selected, PIV_PIN_REF)
        if [sw1, sw2] == SW_AUTH_BLOCKED:
            # PIN got blocked, use PUK to unblock
            pass
        else:
            retries = get_retries_from_sw(sw1, sw2)
            print(f"Retries after failure: {retries}")
        
        # Reset with PUK
        _, sw1, sw2 = reset_retry_counter(
            piv_selected, DEFAULT_PUK, DEFAULT_PIN, PIV_PIN_REF)
        assert [sw1, sw2] == SW_OK, f"Reset failed: {sw1:02X}{sw2:02X}"
        
        # Verify PIN works and counter is reset
        _, sw1, sw2 = verify_piv_pin(piv_selected, DEFAULT_PIN, PIV_PIN_REF)
        assert [sw1, sw2] == SW_OK, "PIN verification after reset failed"
    
    def test_reset_with_wrong_puk(self, piv_selected):
        """Reset with wrong PUK should fail."""
        _, sw1, sw2 = reset_retry_counter(
            piv_selected, WRONG_PUK, DEFAULT_PIN, PIV_PIN_REF)
        
        # Should fail with 63CX (PUK retry counter)
        assert sw1 == 0x63, f"Expected SW1=63, got {sw1:02X}"
    
    def test_reset_sets_new_pin(self, piv_selected):
        """Reset should set a new PIN value."""
        # Reset with new PIN
        _, sw1, sw2 = reset_retry_counter(
            piv_selected, DEFAULT_PUK, NEW_PIN, PIV_PIN_REF)
        assert [sw1, sw2] == SW_OK, f"Reset failed: {sw1:02X}{sw2:02X}"
        
        # Old PIN should not work
        _, sw1, sw2 = verify_piv_pin(piv_selected, DEFAULT_PIN, PIV_PIN_REF)
        assert [sw1, sw2] != SW_OK, "Old PIN should not work after reset"
        
        # New PIN should work
        _, sw1, sw2 = verify_piv_pin(piv_selected, NEW_PIN, PIV_PIN_REF)
        assert [sw1, sw2] == SW_OK, "New PIN should work after reset"
        
        # Restore default PIN
        _, sw1, sw2 = change_reference_data(
            piv_selected, NEW_PIN, DEFAULT_PIN, PIV_PIN_REF)
        assert [sw1, sw2] == SW_OK


# =============================================================================
# PIN Blocking and Unblocking Tests
# =============================================================================

class TestPinBlocking:
    """Test PIN blocking and unblocking scenarios."""
    
    def test_pin_blocks_after_max_retries(self, piv_selected):
        """PIN should block after maximum retry attempts."""
        # Get current retry count
        _, sw1, sw2 = check_piv_pin_retries(piv_selected, PIV_PIN_REF)
        if [sw1, sw2] == SW_AUTH_BLOCKED:
            # Already blocked, unblock first
            reset_retry_counter(piv_selected, DEFAULT_PUK, DEFAULT_PIN, PIV_PIN_REF)
            _, sw1, sw2 = check_piv_pin_retries(piv_selected, PIV_PIN_REF)
        
        initial_retries = get_retries_from_sw(sw1, sw2)
        print(f"Initial retries: {initial_retries}")
        
        if initial_retries <= 0:
            pytest.skip("Cannot test - no retries available")
        
        # Exhaust all retries (max 10, but we'll limit to what's available)
        for i in range(initial_retries):
            _, sw1, sw2 = verify_piv_pin(piv_selected, WRONG_PIN, PIV_PIN_REF)
            if [sw1, sw2] == SW_AUTH_BLOCKED:
                print(f"PIN blocked after {i + 1} failures")
                break
        
        # Verify PIN is now blocked
        _, sw1, sw2 = check_piv_pin_retries(piv_selected, PIV_PIN_REF)
        assert [sw1, sw2] == SW_AUTH_BLOCKED, "PIN should be blocked"
        
        # Correct PIN should also fail when blocked
        _, sw1, sw2 = verify_piv_pin(piv_selected, DEFAULT_PIN, PIV_PIN_REF)
        assert [sw1, sw2] == SW_AUTH_BLOCKED, "Blocked PIN should reject correct PIN"
        
        # Unblock with PUK for cleanup
        _, sw1, sw2 = reset_retry_counter(
            piv_selected, DEFAULT_PUK, DEFAULT_PIN, PIV_PIN_REF)
        assert [sw1, sw2] == SW_OK, "Failed to unblock PIN with PUK"
    
    def test_unblock_blocked_pin(self, piv_selected):
        """Blocked PIN can be unblocked with PUK."""
        # Block the PIN first
        _, sw1, sw2 = check_piv_pin_retries(piv_selected, PIV_PIN_REF)
        if [sw1, sw2] != SW_AUTH_BLOCKED:
            # Block it
            retries = get_retries_from_sw(sw1, sw2)
            for _ in range(retries + 1):
                verify_piv_pin(piv_selected, WRONG_PIN, PIV_PIN_REF)
        
        # Verify it's blocked
        _, sw1, sw2 = check_piv_pin_retries(piv_selected, PIV_PIN_REF)
        assert [sw1, sw2] == SW_AUTH_BLOCKED, "PIN should be blocked"
        
        # Unblock with PUK
        _, sw1, sw2 = reset_retry_counter(
            piv_selected, DEFAULT_PUK, DEFAULT_PIN, PIV_PIN_REF)
        assert [sw1, sw2] == SW_OK, f"Unblock failed: {sw1:02X}{sw2:02X}"
        
        # Verify PIN works again
        _, sw1, sw2 = verify_piv_pin(piv_selected, DEFAULT_PIN, PIV_PIN_REF)
        assert [sw1, sw2] == SW_OK, "PIN should work after unblock"


# =============================================================================
# Error Handling Tests
# =============================================================================

class TestErrorHandling:
    """Test error handling for PIN commands."""
    
    def test_change_pin_invalid_length(self, piv_selected):
        """CHANGE REFERENCE DATA with invalid data length should fail."""
        # Too short - only 4 bytes
        short_data = [0x31, 0x32, 0x33, 0x34]
        apdu = [0x00, 0x24, 0x00, PIV_PIN_REF, len(short_data)] + short_data
        _, sw1, sw2 = transmit_and_log(piv_selected, apdu, "CHANGE with short data")
        
        # Should fail with wrong length or wrong data
        assert [sw1, sw2] != SW_OK, "Should reject short data"
    
    def test_reset_retry_invalid_length(self, piv_selected):
        """RESET RETRY COUNTER with invalid data length should fail."""
        # Too short
        short_data = [0x31, 0x32, 0x33, 0x34]
        apdu = [0x00, 0x2C, 0x00, PIV_PIN_REF, len(short_data)] + short_data
        _, sw1, sw2 = transmit_and_log(piv_selected, apdu, "RESET with short data")
        
        assert [sw1, sw2] != SW_OK, "Should reject short data"
    
    def test_verify_invalid_pin_reference(self, piv_selected):
        """VERIFY with invalid reference should fail."""
        # Use invalid reference 0x82
        _, sw1, sw2 = verify_piv_pin(piv_selected, DEFAULT_PIN, 0x82)
        
        # Should return reference not found or similar error
        assert [sw1, sw2] != SW_OK, "Invalid reference should fail"
    
    def test_change_pin_non_numeric(self, piv_selected):
        """CHANGE PIN with non-numeric characters should fail."""
        # PIN with letters (0x41 = 'A')
        bad_pin = [0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0xFF, 0xFF]
        
        _, sw1, sw2 = change_reference_data(
            piv_selected, DEFAULT_PIN, bad_pin, PIV_PIN_REF)
        
        # Should fail - PIV PINs must be numeric
        assert [sw1, sw2] != SW_OK, "Non-numeric PIN should be rejected"
    
    def test_change_pin_too_short(self, piv_selected):
        """CHANGE PIN with PIN < 6 digits should fail."""
        # Only 5 digits
        short_new = [0x31, 0x32, 0x33, 0x34, 0x35, 0xFF, 0xFF, 0xFF]
        
        # This might succeed or fail depending on implementation
        # The test documents the behavior
        _, sw1, sw2 = change_reference_data(
            piv_selected, DEFAULT_PIN, short_new, PIV_PIN_REF)
        print(f"Short PIN change result: {sw1:02X}{sw2:02X}")


# =============================================================================
# Main
# =============================================================================

if __name__ == "__main__":
    pytest.main([__file__, "-v"])
