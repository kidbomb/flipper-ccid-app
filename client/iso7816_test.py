#!/usr/bin/env python3
"""
ISO7816 APDU-level test client for Flipper Zero USB CCID implementation.

This test suite tests the ISO7816 APDU layer using the PC/SC abstraction,
as opposed to the CCID-level tests in ccid_test.py which use raw USB communication.

Requirements:
    pip install pyscard

Usage:
    python iso7816_test.py

Reference: ISO/IEC 7816-4
"""

from smartcard.System import readers

# Test counters
passed = 0
failed = 0


def test_apdu(connection, test_name, apdu, expected_sw1, expected_sw2, expected_data):
    global passed, failed
    
    data, sw1, sw2 = connection.transmit(apdu)
    failures = []

    if sw1 != expected_sw1:
        failures.append(f"SW1: Expected {expected_sw1:02X}, actual {sw1:02X}")

    if sw2 != expected_sw2:
        failures.append(f"SW2: Expected {expected_sw2:02X}, actual {sw2:02X}")

    if len(data) != len(expected_data):
        failures.append(f"Data length: Expected {len(expected_data)}, actual {len(data)}")
    elif len(data) > 0:
        if data != expected_data:
            failures.append(f"Data: Expected {expected_data}, actual {data}")

    if len(failures) > 0:
        failed += 1
        print(f"  ❌ FAIL: {test_name}")
        for failure in failures:
            print(f"         {failure}")
    else:
        passed += 1
        print(f"  ✅ PASS: {test_name}")


def main():
    global passed, failed
    
    print("=" * 60)
    print("ISO7816 APDU-Level Test Suite")
    print("Smart Card Protocol Compliance Tests")
    print("=" * 60)
    
    r = readers()
    
    if not r:
        print("No smart card readers found")
        return
    
    print("Found smart card readers:")
    for i, sc in enumerate(r):
        print(f"  [{i}] {sc}")

    # Auto-select if only one reader
    if len(r) == 1:
        reader_index = 0
        print(f"\nAuto-selecting reader [{reader_index}]")
    else:
        print("\nSelect reader index: ", end="")
        reader_index = int(input())

    if reader_index >= len(r):
        print("Invalid reader index")
        return
    
    connection = r[reader_index].createConnection()
    connection.connect()
    print(f"Connected to: {r[reader_index]}")

    print("\n[TEST] Basic APDU Commands")
    test_apdu(
        connection,
        "INS 0x01 - No data exchange",
        [0x01, 0x01, 0x00, 0x00],
        0x90,
        0x00,
        [],
    )

    test_apdu(
        connection,
        "INS 0x02 - Response data (Le=2)",
        [0x01, 0x02, 0x00, 0x00, 0x02],
        0x90,
        0x00,
        [0x62, 0x63],
    )

    test_apdu(
        connection,
        "INS 0x03 - Command data (Lc=2)",
        [0x01, 0x03, 0x00, 0x00, 0x02, 0xCA, 0xFE],
        0x90,
        0x00,
        [],
    )

    test_apdu(
        connection,
        "INS 0x04 - Echo data (Lc=2, Le=2)",
        [0x01, 0x04, 0x00, 0x00, 0x02, 0xCA, 0xFE, 0x02],
        0x90,
        0x00,
        [0xCA, 0xFE],
    )

    print("\n[TEST] Buffer Size Tests")
    small_apdu = list(range(0, 0x0F))
    test_apdu(
        connection,
        "INS 0x04 - Small buffer (15 bytes)",
        [0x01, 0x04, 0x00, 0x00, 0x0F] + small_apdu + [0x0F],
        0x90,
        0x00,
        small_apdu,
    )

    upper_bound = 0xF0
    max_apdu = list(range(0, upper_bound))
    test_apdu(
        connection,
        f"INS 0x04 - Large buffer ({upper_bound} bytes)",
        [0x01, 0x04, 0x00, 0x00, upper_bound] + max_apdu + [upper_bound],
        0x90,
        0x00,
        max_apdu,
    )

    # Print summary
    total = passed + failed
    print("\n" + "=" * 60)
    print(f"Test Summary: {passed}/{total} passed, {failed} failed")
    if failed == 0:
        print("All tests passed! ✅")
    else:
        print(f"{failed} test(s) failed ❌")


if __name__ == "__main__":
    main()
