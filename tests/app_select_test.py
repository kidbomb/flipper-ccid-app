#!/usr/bin/env python3
"""
Test script for multi-application SELECT command.

Tests:
1. SELECT PIV application by AID
2. SELECT Generic test application by AID
3. SELECT by partial AID (truncated)
4. Deselect and re-select different apps
5. Verify app-specific command handling

Usage:
    python app_select_test.py

Requirements:
    pip install pyscard
"""

from smartcard.System import readers
from smartcard.util import toHexString, toBytes
import sys

# Application AIDs
PIV_AID = [0xA0, 0x00, 0x00, 0x03, 0x08, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00]
PIV_AID_TRUNCATED = [0xA0, 0x00, 0x00, 0x03, 0x08, 0x00, 0x00, 0x10, 0x00]  # 9 bytes
GENERIC_AID = [0xF0, 0x54, 0x45, 0x53, 0x54, 0x41, 0x50, 0x50]  # "TESTAPP"

# Status words
SW_OK = [0x90, 0x00]
SW_FILE_NOT_FOUND = [0x6A, 0x82]


def select_by_aid(connection, aid, description=""):
    """Send SELECT command with AID"""
    apdu = [0x00, 0xA4, 0x04, 0x00, len(aid)] + aid + [0x00]
    print(f"\n{'=' * 60}")
    print(f"SELECT {description}")
    print(f"  APDU: {toHexString(apdu)}")
    
    response, sw1, sw2 = connection.transmit(apdu)
    sw = [sw1, sw2]
    
    print(f"  Response: {toHexString(response) if response else '(empty)'}")
    print(f"  SW: {toHexString(sw)}")
    
    return response, sw


def test_select_piv(connection):
    """Test SELECT PIV application"""
    print("\n" + "=" * 60)
    print("TEST: SELECT PIV Application (full AID)")
    print("=" * 60)
    
    response, sw = select_by_aid(connection, PIV_AID, "PIV")
    
    if sw == SW_OK:
        print("  ✅ PASS: PIV application selected successfully")
        if response:
            print(f"  FCI: {toHexString(response)}")
        return True
    else:
        print(f"  ❌ FAIL: Expected 9000, got {toHexString(sw)}")
        return False


def test_select_piv_truncated(connection):
    """Test SELECT PIV application with truncated AID"""
    print("\n" + "=" * 60)
    print("TEST: SELECT PIV Application (truncated AID - 9 bytes)")
    print("=" * 60)
    
    response, sw = select_by_aid(connection, PIV_AID_TRUNCATED, "PIV (truncated)")
    
    if sw == SW_OK:
        print("  ✅ PASS: PIV application selected with truncated AID")
        return True
    else:
        print(f"  ❌ FAIL: Expected 9000, got {toHexString(sw)}")
        return False


def test_select_generic(connection):
    """Test SELECT generic test application"""
    print("\n" + "=" * 60)
    print("TEST: SELECT Generic Test Application")
    print("=" * 60)
    
    response, sw = select_by_aid(connection, GENERIC_AID, "TestApp")
    
    if sw == SW_OK:
        print("  ✅ PASS: Generic application selected successfully")
        if response:
            print(f"  FCI: {toHexString(response)}")
        return True
    else:
        print(f"  ❌ FAIL: Expected 9000, got {toHexString(sw)}")
        return False


def test_select_nonexistent(connection):
    """Test SELECT with non-existent AID"""
    print("\n" + "=" * 60)
    print("TEST: SELECT Non-existent Application")
    print("=" * 60)
    
    fake_aid = [0xFF, 0x00, 0x11, 0x22, 0x33]
    response, sw = select_by_aid(connection, fake_aid, "Fake (should fail)")
    
    if sw == SW_FILE_NOT_FOUND:
        print("  ✅ PASS: Correctly returned 6A82 (file not found)")
        return True
    else:
        print(f"  ❌ FAIL: Expected 6A82, got {toHexString(sw)}")
        return False


def test_generic_app_command(connection):
    """Test proprietary command after selecting generic app"""
    print("\n" + "=" * 60)
    print("TEST: Generic App Proprietary Command (GET APP INFO)")
    print("=" * 60)
    
    # First select the generic app
    select_by_aid(connection, GENERIC_AID, "TestApp")
    
    # Send proprietary GET APP INFO command (CLA=0x80, INS=0x01)
    apdu = [0x80, 0x01, 0x00, 0x00, 0x00]
    print(f"  APDU: {toHexString(apdu)}")
    
    response, sw1, sw2 = connection.transmit(apdu)
    sw = [sw1, sw2]
    
    print(f"  Response: {toHexString(response) if response else '(empty)'}")
    print(f"  SW: {toHexString(sw)}")
    
    if sw == SW_OK and len(response) > 0:
        # Parse response: length + name + 4-byte count
        name_len = response[0]
        name = bytes(response[1:1+name_len]).decode('ascii')
        print(f"  ✅ PASS: App name = '{name}'")
        return True
    else:
        print(f"  ❌ FAIL: Expected 9000 with data")
        return False


def test_app_switching(connection):
    """Test switching between applications"""
    print("\n" + "=" * 60)
    print("TEST: Application Switching")
    print("=" * 60)
    
    # Select PIV
    _, sw1 = select_by_aid(connection, PIV_AID, "PIV")
    
    # Select Generic (should deselect PIV)
    _, sw2 = select_by_aid(connection, GENERIC_AID, "TestApp")
    
    # Select PIV again
    _, sw3 = select_by_aid(connection, PIV_AID, "PIV (again)")
    
    if sw1 == SW_OK and sw2 == SW_OK and sw3 == SW_OK:
        print("  ✅ PASS: Successfully switched between applications")
        return True
    else:
        print("  ❌ FAIL: Application switching failed")
        return False


def main():
    """Main test runner"""
    print("=" * 60)
    print("Multi-Application SELECT Test")
    print("=" * 60)
    
    # Get available readers
    r = readers()
    if not r:
        print("No smart card readers found!")
        print("Make sure the Flipper Zero is connected and CCID app is running.")
        sys.exit(1)
    
    print(f"\nAvailable readers: {r}")
    
    # Use first reader
    reader = r[0]
    print(f"Using reader: {reader}")
    
    # Connect to card
    try:
        connection = reader.createConnection()
        connection.connect()
        print("Connected to card")
    except Exception as e:
        print(f"Failed to connect: {e}")
        sys.exit(1)
    
    # Run tests
    results = []
    
    results.append(("SELECT PIV (full AID)", test_select_piv(connection)))
    results.append(("SELECT PIV (truncated AID)", test_select_piv_truncated(connection)))
    results.append(("SELECT Generic", test_select_generic(connection)))
    results.append(("SELECT Non-existent", test_select_nonexistent(connection)))
    results.append(("Generic App Command", test_generic_app_command(connection)))
    results.append(("App Switching", test_app_switching(connection)))
    
    # Summary
    print("\n" + "=" * 60)
    print("TEST SUMMARY")
    print("=" * 60)
    
    passed = 0
    failed = 0
    for name, result in results:
        status = "✅ PASS" if result else "❌ FAIL"
        print(f"  {status}: {name}")
        if result:
            passed += 1
        else:
            failed += 1
    
    print(f"\nTotal: {passed} passed, {failed} failed")
    
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
