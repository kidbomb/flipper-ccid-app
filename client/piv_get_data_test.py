#!/usr/bin/env python3
"""
PIV GET DATA Command Tests

Tests the PIV GET DATA (INS=0xCB) command for retrieving PIV data containers.
Per NIST SP 800-73-5, PIV uses the odd instruction 0xCB for BER-TLV encoded data.

Command format: 00 CB 3F FF Lc 5C LL <tag> 00
"""

import pytest
from smartcard.System import readers
from smartcard.util import toHexString, toBytes


# PIV AID
PIV_AID = [0xA0, 0x00, 0x00, 0x03, 0x08, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00]

# PIV BER-TLV Tags (3 bytes)
PIV_TAG_DISCOVERY = [0x7E]              # 1 byte
PIV_TAG_CCC = [0x5F, 0xC1, 0x07]        # 3 bytes
PIV_TAG_CHUID = [0x5F, 0xC1, 0x02]      # 3 bytes
PIV_TAG_CERT_PIV_AUTH = [0x5F, 0xC1, 0x05]
PIV_TAG_CERT_CARD_AUTH = [0x5F, 0xC1, 0x01]
PIV_TAG_FINGERPRINTS = [0x5F, 0xC1, 0x03]
PIV_TAG_PRINTED_INFO = [0x5F, 0xC1, 0x09]

# Status words
SW_OK = [0x90, 0x00]
SW_FILE_NOT_FOUND = [0x6A, 0x82]
SW_SECURITY_NOT_SATISFIED = [0x69, 0x82]
SW_WRONG_DATA = [0x6A, 0x80]
SW_INCORRECT_PARAMS = [0x6A, 0x86]


@pytest.fixture(scope="module")
def card_connection():
    """Connect to the Flipper Zero CCID reader."""
    r = readers()
    if not r:
        pytest.skip("No smart card readers found")
    
    # Use the first available reader (Flipper shows as Generic USB Smart Card Reader)
    reader = r[0]
    
    connection = reader.createConnection()
    connection.connect()
    yield connection
    connection.disconnect()


@pytest.fixture(scope="module")
def piv_selected(card_connection):
    """Select PIV application before tests."""
    # SELECT PIV application
    select_cmd = [0x00, 0xA4, 0x04, 0x00, len(PIV_AID)] + PIV_AID
    response, sw1, sw2 = card_connection.transmit(select_cmd)
    assert [sw1, sw2] == SW_OK, f"Failed to select PIV: {sw1:02X} {sw2:02X}"
    return card_connection


def build_get_data_cmd(tag):
    """Build PIV GET DATA command for a given tag.
    
    Format: 00 CB 3F FF Lc 5C LL <tag> 00
    """
    tag_len = len(tag)
    data = [0x5C, tag_len] + tag  # Tag list: 5C LL <tag>
    return [0x00, 0xCB, 0x3F, 0xFF, len(data)] + data + [0x00]


class TestPivGetDataDiscovery:
    """Test GET DATA for Discovery Object (tag 0x7E)."""
    
    def test_get_discovery_object(self, piv_selected):
        """Get Discovery Object - should return PIV AID and PIN policy."""
        cmd = build_get_data_cmd(PIV_TAG_DISCOVERY)
        response, sw1, sw2 = piv_selected.transmit(cmd)
        
        assert [sw1, sw2] == SW_OK, f"GET DATA failed: {sw1:02X} {sw2:02X}"
        assert len(response) > 0, "Empty response"
        
        # Verify Discovery Object structure
        # Should start with 7E (Discovery Object tag)
        assert response[0] == 0x7E, f"Expected 7E, got {response[0]:02X}"
        
        # Should contain PIV AID (4F tag)
        response_hex = toHexString(response)
        assert "4F" in response_hex, "Missing AID tag (4F)"
        
        print(f"Discovery Object ({len(response)} bytes): {toHexString(response)}")
    
    def test_discovery_contains_aid(self, piv_selected):
        """Verify Discovery Object contains the correct PIV AID."""
        cmd = build_get_data_cmd(PIV_TAG_DISCOVERY)
        response, sw1, sw2 = piv_selected.transmit(cmd)
        
        assert [sw1, sw2] == SW_OK
        
        # Find AID in response (after 4F 0B tag)
        piv_aid_str = toHexString(PIV_AID).replace(" ", "")
        response_str = toHexString(response).replace(" ", "")
        
        assert piv_aid_str in response_str, "PIV AID not found in Discovery Object"
    
    def test_discovery_contains_pin_policy(self, piv_selected):
        """Verify Discovery Object contains PIN policy (5F2F tag)."""
        cmd = build_get_data_cmd(PIV_TAG_DISCOVERY)
        response, sw1, sw2 = piv_selected.transmit(cmd)
        
        assert [sw1, sw2] == SW_OK
        
        # Look for PIN policy tag 5F2F
        response_hex = toHexString(response).replace(" ", "")
        assert "5F2F" in response_hex, "Missing PIN policy tag (5F2F)"


class TestPivGetDataCCC:
    """Test GET DATA for Card Capability Container (tag 5FC107)."""
    
    def test_get_ccc(self, piv_selected):
        """Get Card Capability Container."""
        cmd = build_get_data_cmd(PIV_TAG_CCC)
        response, sw1, sw2 = piv_selected.transmit(cmd)
        
        assert [sw1, sw2] == SW_OK, f"GET DATA failed: {sw1:02X} {sw2:02X}"
        assert len(response) > 0, "Empty response"
        
        # CCC should start with F0 (Card Identifier tag)
        assert response[0] == 0xF0, f"Expected F0 (Card ID), got {response[0]:02X}"
        
        print(f"CCC ({len(response)} bytes): {toHexString(response)}")
    
    def test_ccc_has_version(self, piv_selected):
        """Verify CCC contains version number (F1 tag)."""
        cmd = build_get_data_cmd(PIV_TAG_CCC)
        response, sw1, sw2 = piv_selected.transmit(cmd)
        
        assert [sw1, sw2] == SW_OK
        
        # Look for F1 (Capability Container Version)
        response_hex = toHexString(response).replace(" ", "")
        assert "F101" in response_hex, "Missing version tag (F1)"
    
    def test_ccc_has_error_detection(self, piv_selected):
        """Verify CCC ends with error detection code (FE tag)."""
        cmd = build_get_data_cmd(PIV_TAG_CCC)
        response, sw1, sw2 = piv_selected.transmit(cmd)
        
        assert [sw1, sw2] == SW_OK
        
        # Look for FE (Error Detection Code)
        response_hex = toHexString(response).replace(" ", "")
        assert "FE00" in response_hex or "FE" in response_hex[-4:], \
            "Missing error detection tag (FE)"


class TestPivGetDataCHUID:
    """Test GET DATA for Card Holder Unique Identifier (tag 5FC102)."""
    
    def test_get_chuid(self, piv_selected):
        """Get CHUID - should return FASC-N, GUID, expiration."""
        cmd = build_get_data_cmd(PIV_TAG_CHUID)
        response, sw1, sw2 = piv_selected.transmit(cmd)
        
        assert [sw1, sw2] == SW_OK, f"GET DATA failed: {sw1:02X} {sw2:02X}"
        assert len(response) > 0, "Empty response"
        
        # CHUID should start with 30 (FASC-N tag)
        assert response[0] == 0x30, f"Expected 30 (FASC-N), got {response[0]:02X}"
        
        print(f"CHUID ({len(response)} bytes): {toHexString(response)}")
    
    def test_chuid_has_fasc_n(self, piv_selected):
        """Verify CHUID contains FASC-N (tag 30, 25 bytes)."""
        cmd = build_get_data_cmd(PIV_TAG_CHUID)
        response, sw1, sw2 = piv_selected.transmit(cmd)
        
        assert [sw1, sw2] == SW_OK
        
        # FASC-N: tag 30, length 19 (25 bytes)
        assert response[0] == 0x30, "Missing FASC-N tag"
        assert response[1] == 0x19, f"FASC-N length should be 25, got {response[1]}"
    
    def test_chuid_has_guid(self, piv_selected):
        """Verify CHUID contains GUID (tag 34, 16 bytes)."""
        cmd = build_get_data_cmd(PIV_TAG_CHUID)
        response, sw1, sw2 = piv_selected.transmit(cmd)
        
        assert [sw1, sw2] == SW_OK
        
        # Look for GUID tag (34 10 = tag + 16 byte length)
        response_hex = toHexString(response).replace(" ", "")
        assert "3410" in response_hex, "Missing GUID tag (34) with 16 bytes"
    
    def test_chuid_has_expiration(self, piv_selected):
        """Verify CHUID contains expiration date (tag 35)."""
        cmd = build_get_data_cmd(PIV_TAG_CHUID)
        response, sw1, sw2 = piv_selected.transmit(cmd)
        
        assert [sw1, sw2] == SW_OK
        
        # Look for expiration tag (35 08)
        response_hex = toHexString(response).replace(" ", "")
        assert "3508" in response_hex, "Missing expiration date tag (35)"


class TestPivGetDataErrors:
    """Test error handling for PIV GET DATA."""
    
    def test_unknown_tag(self, piv_selected):
        """GET DATA with unknown tag should return 6A 82 (file not found)."""
        # Use an invalid/unknown tag
        unknown_tag = [0x5F, 0xC1, 0xFF]  # Invalid PIV tag
        cmd = build_get_data_cmd(unknown_tag)
        response, sw1, sw2 = piv_selected.transmit(cmd)
        
        assert [sw1, sw2] == SW_FILE_NOT_FOUND, \
            f"Expected 6A82, got {sw1:02X} {sw2:02X}"
    
    def test_invalid_p1p2(self, piv_selected):
        """GET DATA with wrong P1-P2 should return error."""
        # Use wrong P1-P2 (not 3F FF)
        cmd = [0x00, 0xCB, 0x00, 0x00, 0x04, 0x5C, 0x01, 0x7E, 0x00]
        response, sw1, sw2 = piv_selected.transmit(cmd)
        
        # Should return 6A 86 (incorrect parameters P1-P2)
        assert sw1 == 0x6A, f"Expected 6A xx, got {sw1:02X} {sw2:02X}"
    
    def test_missing_tag_list(self, piv_selected):
        """GET DATA without 5C tag list should return error."""
        # Send GET DATA without proper 5C tag
        cmd = [0x00, 0xCB, 0x3F, 0xFF, 0x01, 0x7E, 0x00]
        response, sw1, sw2 = piv_selected.transmit(cmd)
        
        # Should return error (6A 85 = Nc inconsistent with TLV, or 67 00 = wrong length)
        assert sw1 in [0x6A, 0x67], f"Expected error 6Axx or 67xx, got {sw1:02X} {sw2:02X}"
    
    def test_empty_data(self, piv_selected):
        """GET DATA with empty data should return error."""
        # Send GET DATA with Lc=0
        cmd = [0x00, 0xCB, 0x3F, 0xFF, 0x00]
        response, sw1, sw2 = piv_selected.transmit(cmd)
        
        # Should return error (wrong length or wrong data)
        assert sw1 in [0x67, 0x6A], f"Expected error, got {sw1:02X} {sw2:02X}"


class TestPivGetDataNotImplemented:
    """Test GET DATA for containers that are not yet implemented."""
    
    def test_get_piv_auth_cert_not_implemented(self, piv_selected):
        """PIV Authentication Certificate - not yet implemented."""
        cmd = build_get_data_cmd(PIV_TAG_CERT_PIV_AUTH)
        response, sw1, sw2 = piv_selected.transmit(cmd)
        
        # Should return file not found (not implemented yet)
        assert [sw1, sw2] == SW_FILE_NOT_FOUND, \
            f"Expected 6A82, got {sw1:02X} {sw2:02X}"
    
    def test_get_fingerprints_not_implemented(self, piv_selected):
        """Fingerprints container - not yet implemented."""
        cmd = build_get_data_cmd(PIV_TAG_FINGERPRINTS)
        response, sw1, sw2 = piv_selected.transmit(cmd)
        
        # Should return file not found (not implemented yet)
        assert [sw1, sw2] == SW_FILE_NOT_FOUND, \
            f"Expected 6A82, got {sw1:02X} {sw2:02X}"


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
