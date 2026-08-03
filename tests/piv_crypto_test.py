#!/usr/bin/env python3
"""
PIV GENERAL AUTHENTICATE Test Suite

Tests PIV cryptographic operations per NIST SP 800-73-5 Part 2:
- Internal Authenticate (card signs challenge)
- Key reference validation (0x9A, 0x9C, 0x9E)
- PIN protection for authentication keys
- ECC P-256 signature operations

GENERAL AUTHENTICATE Command Format:
    00 87 <algorithm> <key ref> Lc <7C auth template> Le

Authentication Template (7C):
    81: Challenge (data to sign)
    82: Response (signature)

Usage:
    pytest piv_crypto_test.py -v
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

# PIV Key References
PIV_KEY_PIV_AUTH = 0x9A      # PIV Authentication Key (requires PIN)
PIV_KEY_CARD_AUTH = 0x9E     # Card Authentication Key (no PIN)
PIV_KEY_DIGITAL_SIG = 0x9C   # Digital Signature Key (requires PIN always)
PIV_KEY_KEY_MGMT = 0x9D      # Key Management Key (not present in demo)

# PIV Algorithms
PIV_ALG_ECC_P256 = 0x11      # ECC P-256 (NIST P-256, secp256r1)
PIV_ALG_ECC_P384 = 0x14      # ECC P-384
PIV_ALG_RSA_2048 = 0x07      # RSA 2048

# Dynamic Authentication Template tags
TAG_DYNAMIC_AUTH = 0x7C
TAG_WITNESS = 0x80
TAG_CHALLENGE = 0x81
TAG_RESPONSE = 0x82

# Status words
SW_OK = [0x90, 0x00]
SW_SECURITY_NOT_SATISFIED = [0x69, 0x82]
SW_REF_DATA_NOT_FOUND = [0x6A, 0x88]
SW_WRONG_DATA = [0x6A, 0x80]
SW_WRONG_LENGTH = [0x67, 0x00]
SW_FUNC_NOT_SUPPORTED = [0x6A, 0x81]

# Default PIV PIN: "123456" padded with 0xFF
DEFAULT_PIN = [0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0xFF, 0xFF]


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


@pytest.fixture
def piv_with_pin(connection):
    """Select PIV and verify PIN."""
    connection.reconnect()
    select_piv(connection)
    verify_pin(connection, DEFAULT_PIN)
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
    print(f"  << {toHexString(data) if data else '(empty)'} SW={sw1:02X}{sw2:02X}")
    return data, sw1, sw2


def select_piv(connection):
    """Select PIV application."""
    apdu = [0x00, 0xA4, 0x04, 0x00, len(PIV_AID)] + PIV_AID
    data, sw1, sw2 = transmit_and_log(connection, apdu, "SELECT PIV")
    assert [sw1, sw2] == SW_OK, f"Failed to select PIV: {sw1:02X}{sw2:02X}"
    return data


def verify_pin(connection, pin: list):
    """Verify PIV PIN."""
    apdu = [0x00, 0x20, 0x00, 0x80, len(pin)] + pin
    data, sw1, sw2 = transmit_and_log(connection, apdu, "VERIFY PIN")
    return data, sw1, sw2


def build_tlv(tag: int, value: bytes) -> list:
    """Build a TLV structure."""
    result = [tag]
    if len(value) < 0x80:
        result.append(len(value))
    elif len(value) <= 0xFF:
        result.extend([0x81, len(value)])
    else:
        result.extend([0x82, (len(value) >> 8) & 0xFF, len(value) & 0xFF])
    result.extend(value)
    return result


def general_authenticate(connection, algorithm: int, key_ref: int, 
                         template_data: list, le: int = 0) -> tuple:
    """Send GENERAL AUTHENTICATE command.
    
    Command: 00 87 <alg> <key ref> Lc <7C template> Le
    """
    # Build outer 7C template
    auth_template = build_tlv(TAG_DYNAMIC_AUTH, bytes(template_data))
    
    apdu = [0x00, 0x87, algorithm, key_ref, len(auth_template)] + auth_template
    if le > 0:
        apdu.append(le)
    else:
        apdu.append(0x00)  # Le = 0 means max
    
    return transmit_and_log(connection, apdu, 
        f"GENERAL AUTHENTICATE alg={algorithm:02X} key={key_ref:02X}")


def internal_authenticate(connection, algorithm: int, key_ref: int, 
                          challenge: bytes) -> tuple:
    """Perform Internal Authenticate operation.
    
    Sends challenge in tag 81, expects signature in response tag 82.
    """
    # Build challenge TLV: 81 <len> <challenge>
    challenge_tlv = build_tlv(TAG_CHALLENGE, challenge)
    
    return general_authenticate(connection, algorithm, key_ref, challenge_tlv)


def parse_tlv(data: list) -> dict:
    """Parse TLV structures from data."""
    result = {}
    offset = 0
    
    while offset < len(data):
        if offset >= len(data):
            break
            
        tag = data[offset]
        offset += 1
        
        if offset >= len(data):
            break
            
        length = data[offset]
        offset += 1
        
        if length == 0x81:
            if offset >= len(data):
                break
            length = data[offset]
            offset += 1
        elif length == 0x82:
            if offset + 1 >= len(data):
                break
            length = (data[offset] << 8) | data[offset + 1]
            offset += 2
        
        if offset + length > len(data):
            break
            
        result[tag] = data[offset:offset + length]
        offset += length
    
    return result


# =============================================================================
# Internal Authenticate Tests
# =============================================================================

class TestInternalAuthenticate:
    """Test Internal Authenticate operations."""
    
    def test_card_auth_no_pin_required(self, piv_selected):
        """Card Authentication Key (0x9E) doesn't require PIN."""
        # Use a simple 32-byte challenge (SHA-256 sized)
        challenge = bytes([0x01, 0x02, 0x03, 0x04] * 8)
        
        data, sw1, sw2 = internal_authenticate(
            piv_selected, PIV_ALG_ECC_P256, PIV_KEY_CARD_AUTH, challenge)
        
        assert [sw1, sw2] == SW_OK, f"Expected 9000, got {sw1:02X}{sw2:02X}"
        
        # Response should contain 7C template with 82 (response)
        assert len(data) > 0, "Expected signature data"
        
        # Parse outer 7C template
        assert data[0] == TAG_DYNAMIC_AUTH, f"Expected 7C tag, got {data[0]:02X}"
        
        # Find inner 82 tag (response/signature)
        inner = parse_tlv(data)
        assert TAG_DYNAMIC_AUTH in inner, "Missing 7C template"
        
        response_data = inner[TAG_DYNAMIC_AUTH]
        inner2 = parse_tlv(response_data)
        assert TAG_RESPONSE in inner2, "Missing 82 (response) tag"
        
        signature = inner2[TAG_RESPONSE]
        print(f"Signature ({len(signature)} bytes): {toHexString(signature)}")
        
        # ECC P-256 signature should be 64 bytes (R || S)
        assert len(signature) == 64, f"Expected 64-byte signature, got {len(signature)}"
    
    def test_piv_auth_requires_pin(self, piv_selected):
        """PIV Authentication Key (0x9A) requires PIN."""
        challenge = bytes([0x11, 0x22, 0x33, 0x44] * 8)
        
        # Without PIN verification
        data, sw1, sw2 = internal_authenticate(
            piv_selected, PIV_ALG_ECC_P256, PIV_KEY_PIV_AUTH, challenge)
        
        # Should fail with security status not satisfied
        assert [sw1, sw2] == SW_SECURITY_NOT_SATISFIED, \
            f"Expected 6982, got {sw1:02X}{sw2:02X}"
    
    def test_piv_auth_with_pin(self, piv_with_pin):
        """PIV Authentication Key (0x9A) works after PIN verification."""
        challenge = bytes([0x11, 0x22, 0x33, 0x44] * 8)
        
        data, sw1, sw2 = internal_authenticate(
            piv_with_pin, PIV_ALG_ECC_P256, PIV_KEY_PIV_AUTH, challenge)
        
        assert [sw1, sw2] == SW_OK, f"Expected 9000, got {sw1:02X}{sw2:02X}"
        assert len(data) > 0, "Expected signature data"
    
    def test_digital_sig_requires_pin_always(self, piv_with_pin):
        """Digital Signature Key (0x9C) clears PIN after each use."""
        challenge = bytes([0xAA, 0xBB, 0xCC, 0xDD] * 8)
        
        # First signature should work
        data, sw1, sw2 = internal_authenticate(
            piv_with_pin, PIV_ALG_ECC_P256, PIV_KEY_DIGITAL_SIG, challenge)
        
        assert [sw1, sw2] == SW_OK, f"First sig expected 9000, got {sw1:02X}{sw2:02X}"
        
        # Second signature without re-verifying PIN should fail
        # (because 0x9C has pin_always = true)
        data, sw1, sw2 = internal_authenticate(
            piv_with_pin, PIV_ALG_ECC_P256, PIV_KEY_DIGITAL_SIG, challenge)
        
        assert [sw1, sw2] == SW_SECURITY_NOT_SATISFIED, \
            f"Second sig expected 6982, got {sw1:02X}{sw2:02X}"
    
    def test_different_challenges_different_signatures(self, piv_selected):
        """Same key with different challenges produces different signatures."""
        challenge1 = bytes([0x01] * 32)
        challenge2 = bytes([0x02] * 32)
        
        data1, sw1, sw2 = internal_authenticate(
            piv_selected, PIV_ALG_ECC_P256, PIV_KEY_CARD_AUTH, challenge1)
        assert [sw1, sw2] == SW_OK
        
        data2, sw1, sw2 = internal_authenticate(
            piv_selected, PIV_ALG_ECC_P256, PIV_KEY_CARD_AUTH, challenge2)
        assert [sw1, sw2] == SW_OK
        
        # Signatures should be different
        assert data1 != data2, "Different challenges should produce different signatures"


# =============================================================================
# Key Reference Tests
# =============================================================================

class TestKeyReferences:
    """Test key reference validation."""
    
    def test_nonexistent_key(self, piv_selected):
        """Reference to non-existent key should fail."""
        challenge = bytes([0xFF] * 32)
        
        # Key 0x9D (Key Management) is not present in demo
        data, sw1, sw2 = internal_authenticate(
            piv_selected, PIV_ALG_ECC_P256, PIV_KEY_KEY_MGMT, challenge)
        
        assert [sw1, sw2] == SW_REF_DATA_NOT_FOUND, \
            f"Expected 6A88, got {sw1:02X}{sw2:02X}"
    
    def test_invalid_key_reference(self, piv_selected):
        """Invalid key reference should fail."""
        challenge = bytes([0xFF] * 32)
        
        # Key 0x00 is not valid
        data, sw1, sw2 = internal_authenticate(
            piv_selected, PIV_ALG_ECC_P256, 0x00, challenge)
        
        assert [sw1, sw2] == SW_REF_DATA_NOT_FOUND, \
            f"Expected 6A88, got {sw1:02X}{sw2:02X}"


# =============================================================================
# Algorithm Tests
# =============================================================================

class TestAlgorithms:
    """Test algorithm validation."""
    
    def test_ecc_p256_supported(self, piv_selected):
        """ECC P-256 should be supported."""
        challenge = bytes([0x12, 0x34] * 16)
        
        data, sw1, sw2 = internal_authenticate(
            piv_selected, PIV_ALG_ECC_P256, PIV_KEY_CARD_AUTH, challenge)
        
        assert [sw1, sw2] == SW_OK, f"Expected 9000, got {sw1:02X}{sw2:02X}"
    
    def test_ecc_p384_not_supported(self, piv_selected):
        """ECC P-384 is not supported in demo implementation."""
        challenge = bytes([0x12, 0x34] * 16)
        
        data, sw1, sw2 = internal_authenticate(
            piv_selected, PIV_ALG_ECC_P384, PIV_KEY_CARD_AUTH, challenge)
        
        # P-384 not implemented, should fail
        assert [sw1, sw2] != SW_OK, "P-384 should not be supported"
    
    def test_rsa_not_supported(self, piv_selected):
        """RSA 2048 is not supported in demo implementation."""
        challenge = bytes([0x12, 0x34] * 16)
        
        data, sw1, sw2 = internal_authenticate(
            piv_selected, PIV_ALG_RSA_2048, PIV_KEY_CARD_AUTH, challenge)
        
        # RSA not implemented, should fail
        assert [sw1, sw2] != SW_OK, "RSA should not be supported"


# =============================================================================
# Error Handling Tests
# =============================================================================

class TestErrorHandling:
    """Test error handling for GENERAL AUTHENTICATE."""
    
    def test_empty_template(self, piv_selected):
        """Empty authentication template should fail."""
        # Send empty 7C template
        data, sw1, sw2 = general_authenticate(
            piv_selected, PIV_ALG_ECC_P256, PIV_KEY_CARD_AUTH, [])
        
        assert [sw1, sw2] != SW_OK, "Empty template should fail"
    
    def test_missing_7c_template(self, piv_selected):
        """Missing 7C template should fail."""
        # Send data without 7C wrapper - this is handled by the command builder
        # so we test with invalid inner data
        bad_data = [TAG_CHALLENGE, 0x04, 0x01, 0x02, 0x03, 0x04]  # 81 tag directly
        
        # Build manually without 7C wrapper
        apdu = [0x00, 0x87, PIV_ALG_ECC_P256, PIV_KEY_CARD_AUTH, 
                len(bad_data)] + bad_data + [0x00]
        
        data, sw1, sw2 = transmit_and_log(piv_selected, apdu, 
            "GENERAL AUTHENTICATE without 7C")
        
        assert [sw1, sw2] != SW_OK, "Missing 7C template should fail"
    
    def test_challenge_too_long(self, piv_selected):
        """Challenge longer than curve order should fail for ECC."""
        # P-256 can only sign data up to 32 bytes effectively
        long_challenge = bytes(range(256))  # 256 bytes
        
        data, sw1, sw2 = internal_authenticate(
            piv_selected, PIV_ALG_ECC_P256, PIV_KEY_CARD_AUTH, long_challenge)
        
        # Should fail with wrong data or wrong length
        assert [sw1, sw2] in [SW_WRONG_DATA, SW_WRONG_LENGTH], \
            f"Expected 6A80 or 6700 for long challenge, got {sw1:02X}{sw2:02X}"


# =============================================================================
# Signature Consistency Tests
# =============================================================================

class TestSignatureConsistency:
    """Test signature generation consistency."""
    
    def test_signature_format(self, piv_selected):
        """Verify signature format is R || S (64 bytes)."""
        challenge = bytes([0x42] * 32)
        
        data, sw1, sw2 = internal_authenticate(
            piv_selected, PIV_ALG_ECC_P256, PIV_KEY_CARD_AUTH, challenge)
        
        assert [sw1, sw2] == SW_OK
        
        # Parse the response
        outer = parse_tlv(data)
        inner_data = outer.get(TAG_DYNAMIC_AUTH, [])
        inner = parse_tlv(inner_data)
        signature = inner.get(TAG_RESPONSE, [])
        
        # Should be exactly 64 bytes (32 for R, 32 for S)
        assert len(signature) == 64, f"Signature should be 64 bytes, got {len(signature)}"
        
        # R and S should not be all zeros
        r = signature[:32]
        s = signature[32:]
        
        assert any(b != 0 for b in r), "R component should not be all zeros"
        assert any(b != 0 for b in s), "S component should not be all zeros"
    
    def test_randomized_signatures(self, piv_selected):
        """ECDSA signatures should be randomized (different each time)."""
        challenge = bytes([0x55] * 32)
        
        # Get two signatures for the same challenge
        data1, sw1, sw2 = internal_authenticate(
            piv_selected, PIV_ALG_ECC_P256, PIV_KEY_CARD_AUTH, challenge)
        assert [sw1, sw2] == SW_OK
        
        data2, sw1, sw2 = internal_authenticate(
            piv_selected, PIV_ALG_ECC_P256, PIV_KEY_CARD_AUTH, challenge)
        assert [sw1, sw2] == SW_OK
        
        # Due to ECDSA randomization, signatures should be different
        # (Both are valid signatures of the same data, but use different k values)
        assert data1 != data2, "ECDSA signatures should be randomized"


# =============================================================================
# Main
# =============================================================================

if __name__ == "__main__":
    pytest.main([__file__, "-v"])
