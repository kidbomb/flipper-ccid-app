#!/usr/bin/env python3
"""
PIV Certificate Retrieval Test Suite

Tests PIV X.509 certificate containers per NIST SP 800-73-5:
- X.509 Certificate for PIV Authentication (0x5FC105)
- X.509 Certificate for Card Authentication (0x5FC101)
- X.509 Certificate for Digital Signature (0x5FC10A) 
- X.509 Certificate for Key Management (0x5FC10B)

Certificate Container Format:
    53 <len>           - Certificate container tag
      70 <len> <cert>  - X.509 certificate
      71 01 <info>     - Certificate info (compression)
      FE 00            - Error detection (LRC)

Usage:
    pytest piv_cert_test.py -v
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

# Certificate Container Tags (3-byte BER-TLV)
PIV_CERT_PIV_AUTH = [0x5F, 0xC1, 0x05]      # 0x5FC105 - PIV Authentication
PIV_CERT_CARD_AUTH = [0x5F, 0xC1, 0x01]     # 0x5FC101 - Card Authentication
PIV_CERT_DIGITAL_SIG = [0x5F, 0xC1, 0x0A]   # 0x5FC10A - Digital Signature
PIV_CERT_KEY_MGMT = [0x5F, 0xC1, 0x0B]      # 0x5FC10B - Key Management

# Certificate container internal tags
TAG_CERT_CONTAINER = 0x53
TAG_X509_CERT = 0x70
TAG_CERT_INFO = 0x71
TAG_LRC = 0xFE

# Certificate info values
CERT_INFO_UNCOMPRESSED = 0x00
CERT_INFO_GZIP = 0x01

# Status words
SW_OK = [0x90, 0x00]
SW_FILE_NOT_FOUND = [0x6A, 0x82]
SW_REF_DATA_NOT_FOUND = [0x6A, 0x88]

# Default PIV PIN
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


def get_data_piv(connection, tag_bytes: list, description: str = ""):
    """
    Send PIV GET DATA command.
    
    Command format: 00 CB 3F FF Lc 5C <tag_len> <tag_bytes> 00
    """
    tag_len = len(tag_bytes)
    data_field = [0x5C, tag_len] + tag_bytes
    lc = len(data_field)
    
    apdu = [0x00, 0xCB, 0x3F, 0xFF, lc] + data_field + [0x00]
    
    if not description:
        description = f"GET DATA tag={toHexString(tag_bytes)}"
    
    return transmit_and_log(connection, apdu, description)


def parse_tlv(data: list) -> dict:
    """Parse TLV structure into a dictionary. Handles multi-byte length."""
    result = {}
    i = 0
    while i < len(data):
        if i >= len(data):
            break
            
        tag = data[i]
        i += 1
        
        if i >= len(data):
            break
            
        # Handle length
        length_byte = data[i]
        i += 1
        
        if length_byte == 0x81:
            if i >= len(data):
                break
            length = data[i]
            i += 1
        elif length_byte == 0x82:
            if i + 1 >= len(data):
                break
            length = (data[i] << 8) | data[i + 1]
            i += 2
        elif length_byte < 0x80:
            length = length_byte
        else:
            # Unsupported length encoding
            break
        
        # Don't go past end of data
        end = min(i + length, len(data))
        value = data[i:end]
        i = end
        
        result[tag] = value
    
    return result


def find_tlv_in_data(data: list, target_tag: int):
    """Find a TLV tag anywhere in the data (flat search)."""
    i = 0
    while i < len(data) - 1:
        tag = data[i]
        if i + 1 >= len(data):
            break
            
        length_byte = data[i + 1]
        
        if length_byte == 0x81:
            if i + 2 >= len(data):
                break
            length = data[i + 2]
            header_len = 3
        elif length_byte == 0x82:
            if i + 3 >= len(data):
                break
            length = (data[i + 2] << 8) | data[i + 3]
            header_len = 4
        elif length_byte < 0x80:
            length = length_byte
            header_len = 2
        else:
            i += 1
            continue
        
        if tag == target_tag:
            start = i + header_len
            end = min(start + length, len(data))
            return data[start:end]
        
        i += header_len + length
    
    return None


# =============================================================================
# Certificate Container Tests
# =============================================================================

class TestGetCertificateCardAuth:
    """Test Card Authentication Certificate (0x5FC101)."""
    
    def test_get_card_auth_cert_success(self, piv_selected):
        """Should retrieve Card Auth certificate container."""
        data, sw1, sw2 = get_data_piv(piv_selected, PIV_CERT_CARD_AUTH,
            "GET DATA Card Auth Certificate (5FC101)")
        
        assert [sw1, sw2] == SW_OK, f"Expected 9000, got {sw1:02X}{sw2:02X}"
        assert len(data) > 0, "Expected certificate data"
    
    def test_card_auth_cert_container_format(self, piv_selected):
        """Certificate should be wrapped in 53 container."""
        data, sw1, sw2 = get_data_piv(piv_selected, PIV_CERT_CARD_AUTH)
        
        assert [sw1, sw2] == SW_OK
        assert data[0] == TAG_CERT_CONTAINER, \
            f"Expected 53 (cert container), got {data[0]:02X}"
    
    def test_card_auth_cert_contains_x509(self, piv_selected):
        """Container should contain X.509 certificate (tag 70)."""
        data, sw1, sw2 = get_data_piv(piv_selected, PIV_CERT_CARD_AUTH)
        
        assert [sw1, sw2] == SW_OK
        
        # Parse container TLV
        tlv = parse_tlv(data)
        assert TAG_CERT_CONTAINER in tlv, "Missing 53 container"
        
        # Parse inner TLV
        inner = parse_tlv(tlv[TAG_CERT_CONTAINER])
        assert TAG_X509_CERT in inner, "Missing 70 (X.509 cert) tag"
    
    def test_card_auth_cert_has_info_byte(self, piv_selected):
        """Container should have cert info byte (tag 71)."""
        data, sw1, sw2 = get_data_piv(piv_selected, PIV_CERT_CARD_AUTH)
        
        assert [sw1, sw2] == SW_OK
        
        # Look for "71 01 00" sequence in raw response
        # This is the cert info tag with length 1 and value 0 (uncompressed)
        found = False
        for i in range(len(data) - 2):
            if data[i] == 0x71 and data[i+1] == 0x01 and data[i+2] == 0x00:
                found = True
                break
        
        assert found, "Missing 71 01 00 (cert info: uncompressed) in response"
    
    def test_card_auth_cert_uncompressed(self, piv_selected):
        """Demo certificate should be uncompressed."""
        data, sw1, sw2 = get_data_piv(piv_selected, PIV_CERT_CARD_AUTH)
        
        assert [sw1, sw2] == SW_OK
        
        # Look for cert info byte sequence: 71 01 XX where XX is compression
        for i in range(len(data) - 2):
            if data[i] == 0x71 and data[i+1] == 0x01:
                cert_info = data[i+2]
                compression = cert_info & 0x03
                assert compression == CERT_INFO_UNCOMPRESSED, \
                    f"Expected uncompressed (0), got {compression}"
                return
        
        pytest.fail("Cert info tag (71) not found in response")
    
    def test_card_auth_cert_no_pin_required(self, piv_selected):
        """Card Auth cert should be readable without PIN (contact interface)."""
        # Just select PIV, don't verify PIN
        data, sw1, sw2 = get_data_piv(piv_selected, PIV_CERT_CARD_AUTH)
        
        assert [sw1, sw2] == SW_OK, \
            "Card Auth cert should not require PIN on contact interface"


class TestGetCertificatePivAuth:
    """Test PIV Authentication Certificate (0x5FC105)."""
    
    def test_get_piv_auth_cert_success(self, piv_selected):
        """Should retrieve PIV Auth certificate container."""
        data, sw1, sw2 = get_data_piv(piv_selected, PIV_CERT_PIV_AUTH,
            "GET DATA PIV Auth Certificate (5FC105)")
        
        assert [sw1, sw2] == SW_OK, f"Expected 9000, got {sw1:02X}{sw2:02X}"
        assert len(data) > 0, "Expected certificate data"
    
    def test_piv_auth_cert_container_format(self, piv_selected):
        """Certificate should be wrapped in 53 container."""
        data, sw1, sw2 = get_data_piv(piv_selected, PIV_CERT_PIV_AUTH)
        
        assert [sw1, sw2] == SW_OK
        assert data[0] == TAG_CERT_CONTAINER
    
    def test_piv_auth_cert_no_pin_required(self, piv_selected):
        """PIV Auth cert should be readable without PIN (contact interface)."""
        data, sw1, sw2 = get_data_piv(piv_selected, PIV_CERT_PIV_AUTH)
        
        assert [sw1, sw2] == SW_OK


class TestGetCertificateNotPresent:
    """Test certificates that are not present in demo."""
    
    def test_digital_sig_cert_not_present(self, piv_selected):
        """Digital Signature certificate not in demo - should return error."""
        data, sw1, sw2 = get_data_piv(piv_selected, PIV_CERT_DIGITAL_SIG,
            "GET DATA Digital Signature Certificate (5FC10A)")
        
        # Should return file not found
        assert [sw1, sw2] == SW_FILE_NOT_FOUND, \
            f"Expected 6A82 (file not found), got {sw1:02X}{sw2:02X}"
    
    def test_key_mgmt_cert_not_present(self, piv_selected):
        """Key Management certificate not in demo - should return error."""
        data, sw1, sw2 = get_data_piv(piv_selected, PIV_CERT_KEY_MGMT,
            "GET DATA Key Management Certificate (5FC10B)")
        
        assert [sw1, sw2] == SW_FILE_NOT_FOUND


class TestCertificateX509Content:
    """Test X.509 certificate content structure."""
    
    def test_x509_starts_with_sequence(self, piv_selected):
        """X.509 certificate should start with SEQUENCE (0x30)."""
        data, sw1, sw2 = get_data_piv(piv_selected, PIV_CERT_CARD_AUTH)
        
        assert [sw1, sw2] == SW_OK
        
        tlv = parse_tlv(data)
        container = tlv.get(TAG_CERT_CONTAINER, [])
        x509 = find_tlv_in_data(container, TAG_X509_CERT)
        
        assert x509 is not None, "Missing X.509 certificate"
        assert x509[0] == 0x30, \
            f"X.509 should start with SEQUENCE (30), got {x509[0]:02X}"
    
    def test_x509_has_valid_structure(self, piv_selected):
        """X.509 certificate should have valid ASN.1 structure."""
        data, sw1, sw2 = get_data_piv(piv_selected, PIV_CERT_CARD_AUTH)
        
        assert [sw1, sw2] == SW_OK
        
        tlv = parse_tlv(data)
        container = tlv.get(TAG_CERT_CONTAINER, [])
        x509 = find_tlv_in_data(container, TAG_X509_CERT)
        
        assert x509 is not None, "Missing X.509 certificate"
        
        # X.509 should start with SEQUENCE (0x30)
        assert x509[0] == 0x30, "X.509 should start with SEQUENCE"
        
        # Check length encoding is valid
        length_byte = x509[1]
        assert length_byte in [0x81, 0x82] or length_byte < 0x80, \
            "Invalid length encoding"
        
        # Certificate should be at least 100 bytes
        assert len(x509) >= 100, f"X.509 too short: {len(x509)} bytes"
    
    def test_different_certs_have_different_content(self, piv_selected):
        """PIV Auth and Card Auth should have different certificates."""
        data1, sw1, sw2 = get_data_piv(piv_selected, PIV_CERT_PIV_AUTH)
        assert [sw1, sw2] == SW_OK
        
        data2, sw1, sw2 = get_data_piv(piv_selected, PIV_CERT_CARD_AUTH)
        assert [sw1, sw2] == SW_OK
        
        assert data1 != data2, "PIV Auth and Card Auth certs should be different"


class TestCertificateErrorCases:
    """Test error handling for certificate operations."""
    
    def test_invalid_cert_tag(self, piv_selected):
        """Unknown certificate tag should return error."""
        invalid_tag = [0x5F, 0xC1, 0xFF]  # Not a valid PIV cert tag
        data, sw1, sw2 = get_data_piv(piv_selected, invalid_tag,
            "GET DATA invalid tag 5FC1FF")
        
        assert [sw1, sw2] == SW_FILE_NOT_FOUND
    
    def test_cert_size_reasonable(self, piv_selected):
        """Certificate should be within PIV size limits."""
        data, sw1, sw2 = get_data_piv(piv_selected, PIV_CERT_CARD_AUTH)
        
        assert [sw1, sw2] == SW_OK
        assert len(data) <= 1857, \
            f"Certificate too large: {len(data)} > 1857 bytes"


# =============================================================================
# Main
# =============================================================================

if __name__ == "__main__":
    pytest.main([__file__, "-v"])
