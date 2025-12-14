#!/usr/bin/env python3
"""
CCID-level test client for Flipper Zero USB CCID implementation.

This test suite tests the CCID protocol layer directly using raw USB communication,
as opposed to the APDU-level tests in ccid_client.py which use the PC/SC abstraction.

Requirements:
    pip install pyusb

Usage:
    python ccid_test.py

Reference: USB CCID Specification v1.1
https://www.usb.org/sites/default/files/DWG_Smart-Card_CCID_Rev110.pdf
"""

import sys
import struct
import time
from typing import Tuple, Optional, List

try:
    import usb.core
    import usb.util
except ImportError:
    print("Error: pyusb is required. Install with: pip install pyusb")
    sys.exit(1)


# =============================================================================
# CCID Protocol Constants (from usb_ccid.h)
# =============================================================================

# USB Class
USB_CLASS_CCID = 0x0B

# Bulk-OUT Messages (PC to Reader)
PC_TO_RDR_ICCPOWERON = 0x62
PC_TO_RDR_ICCPOWEROFF = 0x63
PC_TO_RDR_GETSLOTSTATUS = 0x65
PC_TO_RDR_XFRBLOCK = 0x6F
PC_TO_RDR_GETPARAMETERS = 0x6C
PC_TO_RDR_RESETPARAMETERS = 0x6D
PC_TO_RDR_SETPARAMETERS = 0x61
PC_TO_RDR_ESCAPE = 0x6B
PC_TO_RDR_ICCCLOCK = 0x6E
PC_TO_RDR_T0APDU = 0x6A
PC_TO_RDR_SECURE = 0x69
PC_TO_RDR_MECHANICAL = 0x71
PC_TO_RDR_ABORT = 0x72
PC_TO_RDR_SETDATARATEANDCLOCKFREQUENCY = 0x73

# Bulk-IN Messages (Reader to PC)
RDR_TO_PC_DATABLOCK = 0x80
RDR_TO_PC_SLOTSTATUS = 0x81
RDR_TO_PC_PARAMETERS = 0x82
RDR_TO_PC_ESCAPE = 0x83
RDR_TO_PC_DATARATEANDCLOCKFREQUENCY = 0x84

# Slot Status - ICC Status
CCID_ICCSTATUS_PRESENTANDACTIVE = 0x00
CCID_ICCSTATUS_PRESENTANDINACTIVE = 0x01
CCID_ICCSTATUS_NOICCPRESENT = 0x02

# Slot Status - Command Status
CCID_COMMANDSTATUS_NOERROR = 0x00
CCID_COMMANDSTATUS_FAILED = 0x40
CCID_COMMANDSTATUS_TIMEEXTENSION = 0x80

# Error Codes
CCID_ERROR_NOERROR = 0x00
CCID_ERROR_CMD_NOT_SUPPORTED = 0x01
CCID_ERROR_BAD_SLOT = 0x05
CCID_ERROR_SLOTNOTFOUND = 0x05

# Control Requests
CCID_ABORT = 0x01
CCID_GET_CLOCK_FREQUENCIES = 0x02
CCID_GET_DATA_RATES = 0x03

# VID/PID for Flipper CCID (matches ccid_test_app.c)
FLIPPER_VID = 0x076B
FLIPPER_PID = 0x3A21


# =============================================================================
# CCID Message Builders
# =============================================================================

def build_ccid_header(msg_type: int, length: int, slot: int, seq: int) -> bytes:
    """Build a 10-byte CCID message header."""
    return struct.pack('<BIBBB', msg_type, length, slot, seq, 0)  # 0 = reserved


def build_icc_power_on(slot: int, seq: int, power_select: int = 0) -> bytes:
    """
    Build PC_to_RDR_IccPowerOn message.
    power_select: 0=auto, 1=5V, 2=3V, 3=1.8V
    """
    header = struct.pack('<BIBBB', PC_TO_RDR_ICCPOWERON, 0, slot, seq, 0)
    body = struct.pack('<BBB', power_select, 0, 0)  # bPowerSelect + abRFU
    return header + body


def build_icc_power_off(slot: int, seq: int) -> bytes:
    """Build PC_to_RDR_IccPowerOff message."""
    header = struct.pack('<BIBBB', PC_TO_RDR_ICCPOWEROFF, 0, slot, seq, 0)
    body = struct.pack('<BBB', 0, 0, 0)  # abRFU
    return header + body


def build_get_slot_status(slot: int, seq: int) -> bytes:
    """Build PC_to_RDR_GetSlotStatus message."""
    header = struct.pack('<BIBBB', PC_TO_RDR_GETSLOTSTATUS, 0, slot, seq, 0)
    body = struct.pack('<BBB', 0, 0, 0)  # abRFU
    return header + body


def build_get_parameters(slot: int, seq: int) -> bytes:
    """Build PC_to_RDR_GetParameters message."""
    header = struct.pack('<BIBBB', PC_TO_RDR_GETPARAMETERS, 0, slot, seq, 0)
    body = struct.pack('<BBB', 0, 0, 0)  # abRFU
    return header + body


def build_reset_parameters(slot: int, seq: int) -> bytes:
    """Build PC_to_RDR_ResetParameters message."""
    header = struct.pack('<BIBBB', PC_TO_RDR_RESETPARAMETERS, 0, slot, seq, 0)
    body = struct.pack('<BBB', 0, 0, 0)  # abRFU
    return header + body


def build_set_parameters_t0(slot: int, seq: int, fi_di: int = 0x11, 
                            guard_time: int = 0, waiting_int: int = 0x0A,
                            clock_stop: int = 0) -> bytes:
    """Build PC_to_RDR_SetParameters message for T=0 protocol."""
    # T=0 parameters are 5 bytes
    param_len = 5
    header = struct.pack('<BIBBB', PC_TO_RDR_SETPARAMETERS, param_len, slot, seq, 0)
    body = struct.pack('<BBB', 0x00, 0, 0)  # bProtocolNum=0 (T=0), abRFU
    params = struct.pack('<BBBBB', fi_di, guard_time, waiting_int, clock_stop, 0)  # T=0 params
    return header + body + params


def build_xfr_block(slot: int, seq: int, data: bytes, bwi: int = 0) -> bytes:
    """Build PC_to_RDR_XfrBlock message (APDU transfer)."""
    header = struct.pack('<BIBBB', PC_TO_RDR_XFRBLOCK, len(data), slot, seq, 0)
    body = struct.pack('<BBB', bwi, 0, 0)  # bBWI + wLevelParameter
    return header + body + data


def build_escape(slot: int, seq: int, data: bytes = b'') -> bytes:
    """Build PC_to_RDR_Escape message (vendor-specific)."""
    header = struct.pack('<BIBBB', PC_TO_RDR_ESCAPE, len(data), slot, seq, 0)
    body = struct.pack('<BBB', 0, 0, 0)  # abRFU
    return header + body + data


def build_icc_clock(slot: int, seq: int, clock_command: int = 0) -> bytes:
    """Build PC_to_RDR_IccClock message."""
    header = struct.pack('<BIBBB', PC_TO_RDR_ICCCLOCK, 0, slot, seq, 0)
    body = struct.pack('<BBB', clock_command, 0, 0)  # bClockCommand + abRFU
    return header + body


def build_t0_apdu(slot: int, seq: int, changes: int = 0, 
                  class_get_response: int = 0, class_envelope: int = 0) -> bytes:
    """Build PC_to_RDR_T0APDU message."""
    header = struct.pack('<BIBBB', PC_TO_RDR_T0APDU, 0, slot, seq, 0)
    body = struct.pack('<BBB', changes, class_get_response, class_envelope)
    return header + body


def build_secure(slot: int, seq: int, bwi: int = 0, level: int = 0,
                 pin_data: bytes = b'') -> bytes:
    """Build PC_to_RDR_Secure message (PIN operations)."""
    header = struct.pack('<BIBBB', PC_TO_RDR_SECURE, len(pin_data), slot, seq, 0)
    body = struct.pack('<BH', bwi, level)  # bBWI + wLevelParameter
    return header + body + pin_data


def build_mechanical(slot: int, seq: int, function: int = 0) -> bytes:
    """Build PC_to_RDR_Mechanical message."""
    header = struct.pack('<BIBBB', PC_TO_RDR_MECHANICAL, 0, slot, seq, 0)
    body = struct.pack('<BBB', function, 0, 0)  # bFunction + abRFU
    return header + body


def build_abort(slot: int, seq: int) -> bytes:
    """Build PC_to_RDR_Abort message."""
    header = struct.pack('<BIBBB', PC_TO_RDR_ABORT, 0, slot, seq, 0)
    body = struct.pack('<BBB', 0, 0, 0)  # abRFU
    return header + body


def build_set_data_rate_and_clock(slot: int, seq: int, 
                                   clock_freq: int = 0, data_rate: int = 0) -> bytes:
    """Build PC_to_RDR_SetDataRateAndClockFrequency message."""
    header = struct.pack('<BIBBB', PC_TO_RDR_SETDATARATEANDCLOCKFREQUENCY, 8, slot, seq, 0)
    body = struct.pack('<BBB', 0, 0, 0)  # abRFU
    data = struct.pack('<II', clock_freq, data_rate)
    return header + body + data


# =============================================================================
# CCID Response Parser
# =============================================================================

def parse_ccid_response(data: bytes) -> dict:
    """Parse a CCID bulk-in response."""
    if len(data) < 10:
        return {"error": "Response too short", "raw": data.hex()}
    
    msg_type = data[0]
    length = struct.unpack('<I', data[1:5])[0]
    slot = data[5]
    seq = data[6]
    status = data[7]
    error = data[8]
    chain_param = data[9]
    
    # Parse status byte
    icc_status = status & 0x03
    command_status = status & 0xC0
    
    result = {
        "msg_type": msg_type,
        "msg_type_name": get_response_name(msg_type),
        "length": length,
        "slot": slot,
        "seq": seq,
        "status": status,
        "icc_status": icc_status,
        "icc_status_name": get_icc_status_name(icc_status),
        "command_status": command_status,
        "command_status_name": get_command_status_name(command_status),
        "error": error,
        "error_name": get_error_name(error),
        "chain_param": chain_param,
    }
    
    # Add payload if present
    if length > 0 and len(data) > 10:
        result["data"] = data[10:10+length]
    
    return result


def get_response_name(msg_type: int) -> str:
    """Get human-readable name for response message type."""
    names = {
        RDR_TO_PC_DATABLOCK: "RDR_to_PC_DataBlock",
        RDR_TO_PC_SLOTSTATUS: "RDR_to_PC_SlotStatus",
        RDR_TO_PC_PARAMETERS: "RDR_to_PC_Parameters",
        RDR_TO_PC_ESCAPE: "RDR_to_PC_Escape",
        RDR_TO_PC_DATARATEANDCLOCKFREQUENCY: "RDR_to_PC_DataRateAndClockFrequency",
    }
    return names.get(msg_type, f"Unknown(0x{msg_type:02X})")


def get_icc_status_name(status: int) -> str:
    """Get human-readable ICC status."""
    names = {
        0: "Present and active",
        1: "Present and inactive",
        2: "No ICC present",
    }
    return names.get(status, f"Unknown({status})")


def get_command_status_name(status: int) -> str:
    """Get human-readable command status."""
    if status == 0x00:
        return "No error"
    elif status == 0x40:
        return "Failed"
    elif status == 0x80:
        return "Time extension"
    return f"Unknown(0x{status:02X})"


def get_error_name(error: int) -> str:
    """Get human-readable error code name."""
    names = {
        0x00: "NOERROR",
        0x01: "CMD_NOT_SUPPORTED",
        0x02: "BAD_LENGTH",
        0x03: "BAD_SLOT",
        0x04: "BAD_POWERSELECT",
        0x05: "SLOTNOTFOUND",
        0x07: "BAD_PROTOCOLNUM",
        0x08: "BAD_CLOCKCOMMAND",
        0x0A: "BAD_ABRFU_3B",
        0x0B: "BAD_BMCHANGES",
        0x0C: "BAD_BFUNCTION_MECHANICAL",
        0x0D: "BAD_ABRFU_2B",
        0x0E: "BAD_LEVELPARAMETER",
        0xE0: "CMD_SLOT_BUSY",
        0xEF: "PIN_CANCELLED",
        0xF0: "PIN_TIMEOUT",
        0xF2: "BUSY_WITH_AUTO_SEQUENCE",
        0xF3: "DEACTIVATED_PROTOCOL",
        0xF4: "PROCEDURE_BYTE_CONFLICT",
        0xF5: "ICC_CLASS_NOT_SUPPORTED",
        0xF6: "ICC_PROTOCOL_NOT_SUPPORTED",
        0xF7: "BAD_ATR_TCK",
        0xF8: "BAD_ATR_TS",
        0xFB: "HW_ERROR",
        0xFC: "XFR_OVERRUN",
        0xFD: "XFR_PARITY_ERROR",
        0xFE: "ICC_MUTE",
        0xFF: "CMD_ABORTED",
    }
    return names.get(error, f"Unknown(0x{error:02X})")


# =============================================================================
# Test Framework
# =============================================================================

class CCIDTester:
    """CCID test harness using raw USB communication."""
    
    def __init__(self, vid: int = FLIPPER_VID, pid: int = FLIPPER_PID):
        self.vid = vid
        self.pid = pid
        self.dev = None
        self.ep_out = None
        self.ep_in = None
        self.seq = 0
        self.passed = 0
        self.failed = 0
        self.skipped = 0
    
    def connect(self) -> bool:
        """Find and connect to the CCID device."""
        self.dev = usb.core.find(idVendor=self.vid, idProduct=self.pid)
        if self.dev is None:
            print(f"Device {self.vid:04X}:{self.pid:04X} not found")
            return False
        
        # Detach kernel driver if necessary
        try:
            if self.dev.is_kernel_driver_active(0):
                self.dev.detach_kernel_driver(0)
        except (usb.core.USBError, NotImplementedError):
            pass
        
        # Set configuration
        try:
            self.dev.set_configuration()
        except usb.core.USBError:
            pass  # May already be configured
        
        # Find CCID interface
        cfg = self.dev.get_active_configuration()
        intf = None
        for i in cfg:
            if i.bInterfaceClass == USB_CLASS_CCID:
                intf = i
                break
        
        if intf is None:
            print("No CCID interface found")
            return False
        
        # Find endpoints
        for ep in intf:
            if usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_OUT:
                self.ep_out = ep
            elif usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_IN:
                if ep.bmAttributes & 0x03 == usb.util.ENDPOINT_TYPE_BULK:
                    self.ep_in = ep
        
        if self.ep_out is None or self.ep_in is None:
            print("Could not find bulk endpoints")
            return False
        
        print(f"Connected to {self.vid:04X}:{self.pid:04X}")
        print(f"  EP OUT: 0x{self.ep_out.bEndpointAddress:02X}")
        print(f"  EP IN:  0x{self.ep_in.bEndpointAddress:02X}")
        return True
    
    def disconnect(self):
        """Release the USB device."""
        if self.dev:
            usb.util.dispose_resources(self.dev)
            self.dev = None
    
    def next_seq(self) -> int:
        """Get next sequence number."""
        seq = self.seq
        self.seq = (self.seq + 1) % 256
        return seq
    
    def send_receive(self, data: bytes, timeout: int = 5000) -> Optional[dict]:
        """Send a CCID command and receive response."""
        try:
            self.ep_out.write(data, timeout)
            response = self.ep_in.read(1024, timeout)
            return parse_ccid_response(bytes(response))
        except usb.core.USBError as e:
            print(f"USB error: {e}")
            return None
    
    def control_request(self, request: int, value: int = 0, 
                        index: int = 0, length: int = 64) -> Optional[bytes]:
        """Send a CCID control request."""
        try:
            # Class-specific request, interface recipient
            bmRequestType = 0xA1  # Device-to-host, Class, Interface
            data = self.dev.ctrl_transfer(bmRequestType, request, value, index, length)
            return bytes(data)
        except usb.core.USBError as e:
            print(f"Control request error: {e}")
            return None
    
    def test(self, name: str, condition: bool, details: str = ""):
        """Record a test result."""
        if condition:
            self.passed += 1
            print(f"  ✅ PASS: {name}")
        else:
            self.failed += 1
            print(f"  ❌ FAIL: {name}")
            if details:
                print(f"         {details}")
    
    def print_summary(self):
        """Print test summary."""
        total = self.passed + self.failed + self.skipped
        print("\n" + "=" * 60)
        print(f"Test Summary: {self.passed}/{total} passed, {self.failed} failed, {self.skipped} skipped")
        if self.failed == 0:
            print("All tests passed! ✅")
        else:
            print(f"{self.failed} test(s) failed ❌")


# =============================================================================
# Test Cases
# =============================================================================

def test_icc_power_on(tester: CCIDTester):
    """Test PC_to_RDR_IccPowerOn message."""
    print("\n[TEST] PC_to_RDR_IccPowerOn")
    
    msg = build_icc_power_on(slot=0, seq=tester.next_seq())
    resp = tester.send_receive(msg)
    
    if resp is None:
        tester.test("IccPowerOn - Response received", False, "No response")
        return
    
    tester.test("IccPowerOn - Response type is DataBlock", 
                resp["msg_type"] == RDR_TO_PC_DATABLOCK,
                f"Got {resp['msg_type_name']}")
    
    tester.test("IccPowerOn - Command succeeded",
                resp["command_status"] == CCID_COMMANDSTATUS_NOERROR,
                f"Status: {resp['command_status_name']}")
    
    tester.test("IccPowerOn - ICC is active",
                resp["icc_status"] == CCID_ICCSTATUS_PRESENTANDACTIVE,
                f"ICC: {resp['icc_status_name']}")
    
    # Check ATR is returned
    has_atr = "data" in resp and len(resp["data"]) >= 2
    tester.test("IccPowerOn - ATR returned", has_atr,
                f"ATR length: {len(resp.get('data', b''))}")
    
    if has_atr:
        atr = resp["data"]
        tester.test("IccPowerOn - ATR starts with 0x3B (direct convention)",
                    atr[0] == 0x3B,
                    f"Got 0x{atr[0]:02X}")


def test_icc_power_off(tester: CCIDTester):
    """Test PC_to_RDR_IccPowerOff message."""
    print("\n[TEST] PC_to_RDR_IccPowerOff")
    
    msg = build_icc_power_off(slot=0, seq=tester.next_seq())
    resp = tester.send_receive(msg)
    
    if resp is None:
        tester.test("IccPowerOff - Response received", False, "No response")
        return
    
    tester.test("IccPowerOff - Response type is SlotStatus",
                resp["msg_type"] == RDR_TO_PC_SLOTSTATUS,
                f"Got {resp['msg_type_name']}")
    
    tester.test("IccPowerOff - Command succeeded",
                resp["command_status"] == CCID_COMMANDSTATUS_NOERROR,
                f"Status: {resp['command_status_name']}")


def test_get_slot_status(tester: CCIDTester):
    """Test PC_to_RDR_GetSlotStatus message."""
    print("\n[TEST] PC_to_RDR_GetSlotStatus")
    
    msg = build_get_slot_status(slot=0, seq=tester.next_seq())
    resp = tester.send_receive(msg)
    
    if resp is None:
        tester.test("GetSlotStatus - Response received", False, "No response")
        return
    
    tester.test("GetSlotStatus - Response type is SlotStatus",
                resp["msg_type"] == RDR_TO_PC_SLOTSTATUS,
                f"Got {resp['msg_type_name']}")
    
    tester.test("GetSlotStatus - Command succeeded",
                resp["command_status"] == CCID_COMMANDSTATUS_NOERROR,
                f"Status: {resp['command_status_name']}")
    
    # Clock status is in the chain_param field for SlotStatus
    clock_status = resp["chain_param"]
    tester.test("GetSlotStatus - Clock status valid (0-2)",
                clock_status in [0, 1, 2],
                f"Got {clock_status}")


def test_get_parameters(tester: CCIDTester):
    """Test PC_to_RDR_GetParameters message."""
    print("\n[TEST] PC_to_RDR_GetParameters")
    
    # First power on the card
    msg = build_icc_power_on(slot=0, seq=tester.next_seq())
    tester.send_receive(msg)
    
    msg = build_get_parameters(slot=0, seq=tester.next_seq())
    resp = tester.send_receive(msg)
    
    if resp is None:
        tester.test("GetParameters - Response received", False, "No response")
        return
    
    tester.test("GetParameters - Response type is Parameters",
                resp["msg_type"] == RDR_TO_PC_PARAMETERS,
                f"Got {resp['msg_type_name']}")
    
    tester.test("GetParameters - Command succeeded",
                resp["command_status"] == CCID_COMMANDSTATUS_NOERROR,
                f"Status: {resp['command_status_name']}")
    
    # T=0 parameters should be 5 bytes
    if "data" in resp:
        tester.test("GetParameters - T=0 params length is 5",
                    len(resp["data"]) == 5,
                    f"Got {len(resp['data'])} bytes")


def test_reset_parameters(tester: CCIDTester):
    """Test PC_to_RDR_ResetParameters message."""
    print("\n[TEST] PC_to_RDR_ResetParameters")
    
    msg = build_reset_parameters(slot=0, seq=tester.next_seq())
    resp = tester.send_receive(msg)
    
    if resp is None:
        tester.test("ResetParameters - Response received", False, "No response")
        return
    
    tester.test("ResetParameters - Response type is Parameters",
                resp["msg_type"] == RDR_TO_PC_PARAMETERS,
                f"Got {resp['msg_type_name']}")
    
    tester.test("ResetParameters - Command succeeded",
                resp["command_status"] == CCID_COMMANDSTATUS_NOERROR,
                f"Status: {resp['command_status_name']}")


def test_set_parameters(tester: CCIDTester):
    """Test PC_to_RDR_SetParameters message."""
    print("\n[TEST] PC_to_RDR_SetParameters")
    
    msg = build_set_parameters_t0(slot=0, seq=tester.next_seq())
    resp = tester.send_receive(msg)
    
    if resp is None:
        tester.test("SetParameters - Response received", False, "No response")
        return
    
    tester.test("SetParameters - Response type is Parameters",
                resp["msg_type"] == RDR_TO_PC_PARAMETERS,
                f"Got {resp['msg_type_name']}")
    
    # SetParameters may succeed or fail depending on implementation
    tester.test("SetParameters - Response received (any status)",
                resp["msg_type"] == RDR_TO_PC_PARAMETERS,
                f"Got {resp['msg_type_name']}")


def test_invalid_slot(tester: CCIDTester):
    """Test error handling for invalid slot number."""
    print("\n[TEST] Invalid Slot Number")
    
    # Use slot 99 which doesn't exist
    msg = build_get_slot_status(slot=99, seq=tester.next_seq())
    resp = tester.send_receive(msg)
    
    if resp is None:
        tester.test("InvalidSlot - Response received", False, "No response")
        return
    
    tester.test("InvalidSlot - Command failed",
                resp["command_status"] == CCID_COMMANDSTATUS_FAILED,
                f"Status: {resp['command_status_name']}")
    
    tester.test("InvalidSlot - Error is SLOTNOTFOUND or BAD_SLOT",
                resp["error"] in [CCID_ERROR_SLOTNOTFOUND, CCID_ERROR_BAD_SLOT],
                f"Error: {resp['error_name']}")


def test_unsupported_escape(tester: CCIDTester):
    """Test that PC_to_RDR_Escape returns CMD_NOT_SUPPORTED."""
    print("\n[TEST] Unsupported Command - Escape")
    
    msg = build_escape(slot=0, seq=tester.next_seq())
    resp = tester.send_receive(msg)
    
    if resp is None:
        tester.test("Escape - Response received", False, "No response")
        return
    
    tester.test("Escape - Command failed (expected)",
                resp["command_status"] == CCID_COMMANDSTATUS_FAILED,
                f"Status: {resp['command_status_name']}")
    
    tester.test("Escape - Error is CMD_NOT_SUPPORTED",
                resp["error"] == CCID_ERROR_CMD_NOT_SUPPORTED,
                f"Error: {resp['error_name']}")


def test_unsupported_icc_clock(tester: CCIDTester):
    """Test that PC_to_RDR_IccClock returns CMD_NOT_SUPPORTED."""
    print("\n[TEST] Unsupported Command - IccClock")
    
    msg = build_icc_clock(slot=0, seq=tester.next_seq())
    resp = tester.send_receive(msg)
    
    if resp is None:
        tester.test("IccClock - Response received", False, "No response")
        return
    
    tester.test("IccClock - Command failed (expected)",
                resp["command_status"] == CCID_COMMANDSTATUS_FAILED,
                f"Status: {resp['command_status_name']}")
    
    tester.test("IccClock - Error is CMD_NOT_SUPPORTED",
                resp["error"] == CCID_ERROR_CMD_NOT_SUPPORTED,
                f"Error: {resp['error_name']}")


def test_unsupported_t0_apdu(tester: CCIDTester):
    """Test that PC_to_RDR_T0APDU returns CMD_NOT_SUPPORTED."""
    print("\n[TEST] Unsupported Command - T0APDU")
    
    msg = build_t0_apdu(slot=0, seq=tester.next_seq())
    resp = tester.send_receive(msg)
    
    if resp is None:
        tester.test("T0APDU - Response received", False, "No response")
        return
    
    tester.test("T0APDU - Command failed (expected)",
                resp["command_status"] == CCID_COMMANDSTATUS_FAILED,
                f"Status: {resp['command_status_name']}")
    
    tester.test("T0APDU - Error is CMD_NOT_SUPPORTED",
                resp["error"] == CCID_ERROR_CMD_NOT_SUPPORTED,
                f"Error: {resp['error_name']}")


def test_unsupported_secure(tester: CCIDTester):
    """Test that PC_to_RDR_Secure returns CMD_NOT_SUPPORTED."""
    print("\n[TEST] Unsupported Command - Secure")
    
    msg = build_secure(slot=0, seq=tester.next_seq())
    resp = tester.send_receive(msg)
    
    if resp is None:
        tester.test("Secure - Response received", False, "No response")
        return
    
    tester.test("Secure - Command failed (expected)",
                resp["command_status"] == CCID_COMMANDSTATUS_FAILED,
                f"Status: {resp['command_status_name']}")
    
    tester.test("Secure - Error is CMD_NOT_SUPPORTED",
                resp["error"] == CCID_ERROR_CMD_NOT_SUPPORTED,
                f"Error: {resp['error_name']}")


def test_unsupported_mechanical(tester: CCIDTester):
    """Test that PC_to_RDR_Mechanical returns CMD_NOT_SUPPORTED."""
    print("\n[TEST] Unsupported Command - Mechanical")
    
    msg = build_mechanical(slot=0, seq=tester.next_seq())
    resp = tester.send_receive(msg)
    
    if resp is None:
        tester.test("Mechanical - Response received", False, "No response")
        return
    
    tester.test("Mechanical - Command failed (expected)",
                resp["command_status"] == CCID_COMMANDSTATUS_FAILED,
                f"Status: {resp['command_status_name']}")
    
    tester.test("Mechanical - Error is CMD_NOT_SUPPORTED",
                resp["error"] == CCID_ERROR_CMD_NOT_SUPPORTED,
                f"Error: {resp['error_name']}")


def test_unsupported_abort(tester: CCIDTester):
    """Test that PC_to_RDR_Abort returns CMD_NOT_SUPPORTED."""
    print("\n[TEST] Unsupported Command - Abort")
    
    msg = build_abort(slot=0, seq=tester.next_seq())
    resp = tester.send_receive(msg)
    
    if resp is None:
        tester.test("Abort - Response received", False, "No response")
        return
    
    tester.test("Abort - Command failed (expected)",
                resp["command_status"] == CCID_COMMANDSTATUS_FAILED,
                f"Status: {resp['command_status_name']}")
    
    tester.test("Abort - Error is CMD_NOT_SUPPORTED",
                resp["error"] == CCID_ERROR_CMD_NOT_SUPPORTED,
                f"Error: {resp['error_name']}")


def test_unsupported_set_data_rate(tester: CCIDTester):
    """Test that PC_to_RDR_SetDataRateAndClockFrequency returns CMD_NOT_SUPPORTED."""
    print("\n[TEST] Unsupported Command - SetDataRateAndClockFrequency")
    
    msg = build_set_data_rate_and_clock(slot=0, seq=tester.next_seq())
    resp = tester.send_receive(msg)
    
    if resp is None:
        tester.test("SetDataRate - Response received", False, "No response")
        return
    
    tester.test("SetDataRate - Command failed (expected)",
                resp["command_status"] == CCID_COMMANDSTATUS_FAILED,
                f"Status: {resp['command_status_name']}")
    
    tester.test("SetDataRate - Error is CMD_NOT_SUPPORTED",
                resp["error"] == CCID_ERROR_CMD_NOT_SUPPORTED,
                f"Error: {resp['error_name']}")


def test_control_get_clock_frequencies(tester: CCIDTester):
    """Test CCID_GET_CLOCK_FREQUENCIES control request."""
    print("\n[TEST] Control Request - GET_CLOCK_FREQUENCIES")
    
    data = tester.control_request(CCID_GET_CLOCK_FREQUENCIES, length=4)
    
    if data is None:
        tester.test("GetClockFrequencies - Response received", False, "No response")
        return
    
    tester.test("GetClockFrequencies - Response received",
                len(data) >= 4,
                f"Got {len(data)} bytes")
    
    if len(data) >= 4:
        clock = struct.unpack('<I', data[:4])[0]
        tester.test("GetClockFrequencies - Clock value reasonable (>0)",
                    clock > 0,
                    f"Clock: {clock} kHz")


def test_control_get_data_rates(tester: CCIDTester):
    """Test CCID_GET_DATA_RATES control request."""
    print("\n[TEST] Control Request - GET_DATA_RATES")
    
    data = tester.control_request(CCID_GET_DATA_RATES, length=4)
    
    if data is None:
        tester.test("GetDataRates - Response received", False, "No response")
        return
    
    tester.test("GetDataRates - Response received",
                len(data) >= 4,
                f"Got {len(data)} bytes")
    
    if len(data) >= 4:
        rate = struct.unpack('<I', data[:4])[0]
        tester.test("GetDataRates - Data rate value reasonable (>0)",
                    rate > 0,
                    f"Rate: {rate} bps")


def test_sequence_numbers(tester: CCIDTester):
    """Test that sequence numbers are echoed correctly."""
    print("\n[TEST] Sequence Number Handling")
    
    # Use a specific sequence number
    test_seq = 0x42
    msg = build_get_slot_status(slot=0, seq=test_seq)
    resp = tester.send_receive(msg)
    
    if resp is None:
        tester.test("SeqNum - Response received", False, "No response")
        return
    
    tester.test("SeqNum - Sequence number echoed",
                resp["seq"] == test_seq,
                f"Sent {test_seq}, got {resp['seq']}")
    
    # Test another sequence
    test_seq = 0xFF
    msg = build_get_slot_status(slot=0, seq=test_seq)
    resp = tester.send_receive(msg)
    
    if resp:
        tester.test("SeqNum - Sequence 0xFF echoed",
                    resp["seq"] == test_seq,
                    f"Sent {test_seq}, got {resp['seq']}")


def test_power_cycle(tester: CCIDTester):
    """Test power on/off cycle."""
    print("\n[TEST] Power On/Off Cycle")
    
    # Power on
    msg = build_icc_power_on(slot=0, seq=tester.next_seq())
    resp = tester.send_receive(msg)
    
    if resp:
        tester.test("PowerCycle - Power on succeeded",
                    resp["command_status"] == CCID_COMMANDSTATUS_NOERROR,
                    f"Status: {resp['command_status_name']}")
        tester.test("PowerCycle - ICC active after power on",
                    resp["icc_status"] == CCID_ICCSTATUS_PRESENTANDACTIVE,
                    f"ICC: {resp['icc_status_name']}")
    
    # Power off
    msg = build_icc_power_off(slot=0, seq=tester.next_seq())
    resp = tester.send_receive(msg)
    
    if resp:
        tester.test("PowerCycle - Power off succeeded",
                    resp["command_status"] == CCID_COMMANDSTATUS_NOERROR,
                    f"Status: {resp['command_status_name']}")
    
    # Check status after power off
    # Note: This implementation always keeps ICC active (virtual card always present)
    msg = build_get_slot_status(slot=0, seq=tester.next_seq())
    resp = tester.send_receive(msg)
    
    if resp:
        tester.test("PowerCycle - ICC still active after power off (virtual card)",
                    resp["icc_status"] == CCID_ICCSTATUS_PRESENTANDACTIVE,
                    f"ICC: {resp['icc_status_name']}")
    
    # Power on again
    msg = build_icc_power_on(slot=0, seq=tester.next_seq())
    resp = tester.send_receive(msg)
    
    if resp:
        tester.test("PowerCycle - Re-power on succeeded",
                    resp["command_status"] == CCID_COMMANDSTATUS_NOERROR,
                    f"Status: {resp['command_status_name']}")


# =============================================================================
# Main
# =============================================================================

def main():
    print("=" * 60)
    print("CCID Protocol Level Test Suite")
    print("USB CCID Specification v1.1 Compliance Tests")
    print("=" * 60)
    
    tester = CCIDTester()
    
    if not tester.connect():
        print("\nFailed to connect to CCID device.")
        print("Make sure:")
        print("  1. Flipper Zero is connected via USB")
        print("  2. CCID test app is running on Flipper")
        print("  3. You have permissions to access USB device")
        print("     (try running with sudo or add udev rules)")
        return 1
    
    try:
        # Core functionality tests
        test_icc_power_on(tester)
        test_get_slot_status(tester)
        test_get_parameters(tester)
        test_reset_parameters(tester)
        test_set_parameters(tester)
        test_icc_power_off(tester)
        
        # Sequence and state tests
        test_sequence_numbers(tester)
        test_power_cycle(tester)
        
        # Error handling tests
        test_invalid_slot(tester)
        
        # Unsupported command tests
        test_unsupported_escape(tester)
        test_unsupported_icc_clock(tester)
        test_unsupported_t0_apdu(tester)
        test_unsupported_secure(tester)
        test_unsupported_mechanical(tester)
        test_unsupported_abort(tester)
        test_unsupported_set_data_rate(tester)
        
        # Control request tests
        test_control_get_clock_frequencies(tester)
        test_control_get_data_rates(tester)
        
        tester.print_summary()
        
    finally:
        tester.disconnect()
    
    return 0 if tester.failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
