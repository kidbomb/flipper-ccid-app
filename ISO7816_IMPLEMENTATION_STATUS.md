# ISO/IEC 7816-4 Implementation Status

## Overview

This document tracks the implementation status of ISO/IEC 7816-4:2005 smart card commands and features in the Flipper Zero CCID test application.

**Reference Standard**: ISO/IEC 7816-4:2005 - Identification cards — Integrated circuit cards — Part 4: Organization, security and commands for interchange

**Last Updated**: December 14, 2025

---

## Implementation Summary

| Category | Implemented | Total | Progress |
|----------|-------------|-------|----------|
| Commands | 3 | 7 | 43% |
| Status Words | 40+ | 40+ | ✅ Complete |
| File System | ✅ | - | Complete |
| Data Structures | Partial | - | ~70% |

---

## Commands Implementation Status

### 🔴 High Priority Commands

| Command | INS | Status | Notes |
|---------|-----|--------|-------|
| SELECT | 0xA4 | ✅ **Implemented** | Full support for all P1 modes |
| GET RESPONSE | 0xC0 | ❌ Not Started | Required for chained responses |
| Status Codes | - | ✅ **Implemented** | 40+ status words defined |

### 🟡 Medium Priority Commands

| Command | INS | Status | Notes |
|---------|-----|--------|-------|
| READ BINARY | 0xB0 | ❌ Not Started | Transparent EF access |
| READ RECORD | 0xB2 | ❌ Not Started | Record-oriented EF access |
| GET DATA | 0xCA | ✅ **Implemented** | Built-in + registered data objects |
| VERIFY | 0x20 | ✅ **Implemented** | PIN verification with retry counter |

### 🟢 Low Priority Commands

| Command | INS | Status | Notes |
|---------|-----|--------|-------|
| GET CHALLENGE | 0x84 | ❌ Not Started | Random number generation |
| EXTERNAL AUTHENTICATE | 0x82 | ❌ Not Started | Card authentication |
| INTERNAL AUTHENTICATE | 0x88 | ❌ Not Started | Terminal authentication |
| MANAGE CHANNEL | 0x70 | ❌ Not Started | Logical channel management |

---

## SELECT Command (0xA4) - ✅ Implemented

### Supported P1 Selection Modes

| P1 Value | Mode | Status | Description |
|----------|------|--------|-------------|
| 0x00 | Select by FID | ✅ Implemented | Select MF, DF, or EF by file identifier |
| 0x01 | Select child DF | ✅ Implemented | Select child DF by identifier |
| 0x02 | Select EF under DF | ✅ Implemented | Select EF under current DF |
| 0x03 | Select parent DF | ✅ Implemented | Navigate to parent directory |
| 0x04 | Select by AID | ✅ Implemented | Select application by AID |
| 0x08 | Select by path from MF | ✅ Implemented | Absolute path selection |
| 0x09 | Select by path from current DF | ✅ Implemented | Relative path selection |

### Supported P2 Response Options

| P2 Bits 3-2 | Mode | Status | Description |
|-------------|------|--------|-------------|
| 00 | Return FCI | ✅ Implemented | File Control Information template (tag 6F) |
| 01 | Return FCP | ✅ Implemented | File Control Parameters template (tag 62) |
| 10 | Return FMD | ✅ Implemented | File Management Data template (tag 64) |
| 11 | No response | ✅ Implemented | No data returned |

### Implementation Files

- `iso7816/commands/iso7816_cmd_select.h` - Command constants and interface
- `iso7816/commands/iso7816_cmd_select.c` - Full SELECT implementation

---

## GET DATA Command (0xCA) - ✅ Implemented

### Supported Tag Ranges

| P1-P2 Range | Tag Type | Status | Description |
|-------------|----------|--------|-------------|
| 0x0000 | Dump current EF | ✅ Implemented | Returns content of selected EF |
| 0x0040-0x00FF | Single-byte BER-TLV | ✅ Implemented | Tag in P2 (e.g., 4F, 84, 50) |
| 0x0100-0x01FF | Proprietary | ✅ Implemented | Custom registered data objects |
| 0x0200-0x02FF | SIMPLE-TLV | ✅ Implemented | Tag in P2 |
| 0x4000-0xFFFF | Two-byte BER-TLV | ✅ Implemented | Tag in P1-P2 (e.g., 5F51, 5F52) |

### Built-in Data Objects

| Tag (P1-P2) | Name | Description |
|-------------|------|-------------|
| 0x5F51 | ATR Information | Card identification ("Flipper Zero") |
| 0x5F52 | Historical Bytes | Card capabilities and status |
| 0x5F56 | Extended Length Info | Max APDU lengths (2048 bytes) |

### Single-Byte Tags (P1=0x00)

| Tag (P2) | Name | Status | Description |
|----------|------|--------|-------------|
| 0x4F | Application Identifier | ✅ Implemented | AID of current DF |
| 0x84 | DF Name | ✅ Implemented | AID/name of current DF |
| 0x50 | Application Label | ✅ Implemented | Human-readable app name |

### Features

- **Built-in data objects**: ATR info, historical bytes, extended length
- **Registered data objects**: Applications can register custom tags
- **Context-aware**: Single-byte tags return data from current DF
- **Error handling**: Proper SW for not found (6A88), no EF (6986)

### Implementation Files

- `iso7816/commands/iso7816_cmd_get_data.h` - Command constants and interface
- `iso7816/commands/iso7816_cmd_get_data.c` - Full GET DATA implementation

---

## VERIFY Command (0x20) - ✅ Implemented

### Overview

The VERIFY command validates user-provided reference data (PIN/password) against stored reference data in the card. Per ISO/IEC 7816-4:2005 Section 7.5.6.

### P1-P2 Encoding

| P1 | P2 | Description |
|----|------|-------------|
| 0x00 | Qualifier | Reference data qualifier |

**P2 Bit Encoding:**
| Bits | Description | Status |
|------|-------------|--------|
| b8 (0x80) | Scope: 0=Global, 1=Specific (to current DF) | ✅ Supported |
| b7-b6 (0x40-0x60) | Reserved (must be 0) | ✅ Validated |
| b5-b1 (0x01-0x1F) | Reference qualifier (PIN ID 1-31) | ✅ Supported |

### Modes of Operation

| Mode | Data | Description | Status |
|------|------|-------------|--------|
| Verify PIN | PIN bytes | Compares sent PIN with stored PIN | ✅ Implemented |
| Check Retries | Empty | Returns remaining retry count | ✅ Implemented |

### Response Status Words

| SW1-SW2 | Meaning |
|---------|---------|
| 9000 | PIN verified successfully |
| 63CX | Wrong PIN, X retries remaining (X=0-F) |
| 6983 | PIN is blocked (no retries left) |
| 6984 | Reference data not usable (blocked) |
| 6A86 | Wrong P1-P2 (invalid encoding) |
| 6A88 | Reference data not found (PIN not registered) |
| 6A81 | Function not supported (for INS=0x21 biometric) |

### PIN Storage Features

| Feature | Value |
|---------|-------|
| Max PINs per filesystem | 8 |
| Max PIN length | 8 bytes |
| Default retry count | 3 attempts |
| PIN blocking | After all retries exhausted |
| Retry counter reset | On successful verification |

### Security Features

- **Retry counter**: Decrements on each failed attempt
- **Blocking**: PIN becomes permanently blocked after retry count reaches 0
- **Counter reset**: Successful verification resets counter to default
- **0xFF padding**: PIN data can be 0xFF-padded to 8 bytes

### Example APDUs

```
# Verify PIN "1234" with global reference 1
00 20 00 01 04 31 32 33 34
Response: 90 00 (success)

# Check remaining retries for reference 1
00 20 00 01
Response: 63 C3 (3 retries remaining)

# Wrong PIN - decrements counter
00 20 00 01 04 39 39 39 39
Response: 63 C2 (2 retries remaining)
```

### Implementation Files

- `iso7816/commands/iso7816_cmd_verify.h` - Command constants and interface
- `iso7816/commands/iso7816_cmd_verify.c` - Full VERIFY implementation

---

## File System - ✅ Implemented

### Supported File Types

| Type | Status | Description |
|------|--------|-------------|
| MF (Master File) | ✅ Implemented | Root directory, FID 3F00 |
| DF (Dedicated File) | ✅ Implemented | Application directories with AID support |
| EF Transparent | ✅ Implemented | Binary data files |
| EF Linear Fixed | ✅ Implemented | Fixed-size record files |
| EF Linear Variable | ✅ Implemented | Variable-size record files |
| EF Cyclic | ✅ Implemented | Circular buffer record files |

### Default File Structure

```
MF (3F00)
├── EF.DIR (2F00)    - Application directory (transparent, 256 bytes)
└── EF.ATR (2F01)    - ATR/Card info (transparent, 64 bytes)
```

### Application Registration

Applications can be dynamically registered with:
- Custom AID (Application Identifier)
- Custom DF name
- Auto-created DF under MF
- Custom file hierarchy per application

### Implementation Files

- `iso7816/iso7816_filesystem.h` - File system structures and constants
- `iso7816/iso7816_filesystem.c` - File system implementation

---

## Status Words - ✅ Implemented

### Normal Processing

| SW1-SW2 | Macro | Status |
|---------|-------|--------|
| 9000 | `ISO7816_SW_SUCCESS` | ✅ |
| 61XX | `ISO7816_SW_MORE_DATA(len)` | ✅ |

### Warning Processing (62XX, 63XX)

| SW1-SW2 | Macro | Status |
|---------|-------|--------|
| 6200 | `ISO7816_SW_WARNING_NO_INFO` | ✅ |
| 6281 | `ISO7816_SW_WARNING_DATA_CORRUPTED` | ✅ |
| 6282 | `ISO7816_SW_WARNING_EOF` | ✅ |
| 6283 | `ISO7816_SW_WARNING_FILE_DEACTIVATED` | ✅ |
| 6284 | `ISO7816_SW_WARNING_FCI_FORMAT` | ✅ |
| 6285 | `ISO7816_SW_WARNING_FILE_TERMINATED` | ✅ |
| 6300 | `ISO7816_SW_WARNING_STATE_UNCHANGED` | ✅ |
| 6381 | `ISO7816_SW_WARNING_FILE_FILLED` | ✅ |
| 63CX | `ISO7816_SW_PIN_RETRIES(n)` | ✅ |

### Execution Errors (64XX, 65XX)

| SW1-SW2 | Macro | Status |
|---------|-------|--------|
| 6400 | `ISO7816_SW_EXEC_ERROR` | ✅ |
| 6401 | `ISO7816_SW_EXEC_IMMEDIATE_RESPONSE` | ✅ |
| 6500 | `ISO7816_SW_EXEC_MEMORY_UNCHANGED` | ✅ |
| 6581 | `ISO7816_SW_EXEC_MEMORY_FAILURE` | ✅ |

### Checking Errors (67XX - 6FXX)

| SW1-SW2 | Macro | Status |
|---------|-------|--------|
| 6700 | `ISO7816_SW_WRONG_LENGTH` | ✅ |
| 6800 | `ISO7816_SW_FUNC_NOT_SUPPORTED_NO_INFO` | ✅ |
| 6881 | `ISO7816_SW_LOGICAL_CHANNEL_NOT_SUPPORTED` | ✅ |
| 6882 | `ISO7816_SW_SM_NOT_SUPPORTED` | ✅ |
| 6883 | `ISO7816_SW_LAST_COMMAND_EXPECTED` | ✅ |
| 6884 | `ISO7816_SW_CHAINING_NOT_SUPPORTED` | ✅ |
| 6900 | `ISO7816_SW_CMD_NOT_ALLOWED_NO_INFO` | ✅ |
| 6981 | `ISO7816_SW_CMD_INCOMPATIBLE` | ✅ |
| 6982 | `ISO7816_SW_SECURITY_NOT_SATISFIED` | ✅ |
| 6983 | `ISO7816_SW_AUTH_BLOCKED` | ✅ |
| 6984 | `ISO7816_SW_REF_DATA_NOT_USABLE` | ✅ |
| 6985 | `ISO7816_SW_CONDITIONS_NOT_SATISFIED` | ✅ |
| 6986 | `ISO7816_SW_CMD_NOT_ALLOWED_NO_EF` | ✅ |
| 6987 | `ISO7816_SW_SM_DATA_MISSING` | ✅ |
| 6988 | `ISO7816_SW_SM_DATA_INCORRECT` | ✅ |
| 6A00 | `ISO7816_SW_WRONG_PARAMS_NO_INFO` | ✅ |
| 6A80 | `ISO7816_SW_WRONG_DATA` | ✅ |
| 6A81 | `ISO7816_SW_FUNC_NOT_SUPPORTED` | ✅ |
| 6A82 | `ISO7816_SW_FILE_NOT_FOUND` | ✅ |
| 6A83 | `ISO7816_SW_RECORD_NOT_FOUND` | ✅ |
| 6A84 | `ISO7816_SW_NOT_ENOUGH_MEMORY` | ✅ |
| 6A85 | `ISO7816_SW_LC_TLV_INCONSISTENT` | ✅ |
| 6A86 | `ISO7816_SW_WRONG_P1P2` | ✅ |
| 6A87 | `ISO7816_SW_LC_P1P2_INCONSISTENT` | ✅ |
| 6A88 | `ISO7816_SW_REF_DATA_NOT_FOUND` | ✅ |
| 6A89 | `ISO7816_SW_FILE_EXISTS` | ✅ |
| 6A8A | `ISO7816_SW_DF_NAME_EXISTS` | ✅ |
| 6B00 | `ISO7816_SW_WRONG_P1P2_6B` | ✅ |
| 6CXX | `ISO7816_SW_WRONG_LE(len)` | ✅ |
| 6D00 | `ISO7816_SW_INS_NOT_SUPPORTED` | ✅ |
| 6E00 | `ISO7816_SW_CLA_NOT_SUPPORTED` | ✅ |
| 6F00 | `ISO7816_SW_UNKNOWN_ERROR` | ✅ |

### Implementation Files

- `iso7816/iso7816_response.h` - Status word definitions
- `iso7816/iso7816_response.c` - Response helper functions

---

## Data Structures

### File Control Information (FCI/FCP/FMD)

| Tag | Description | Status |
|-----|-------------|--------|
| 62 | FCP Template | ✅ Implemented |
| 64 | FMD Template | ✅ Implemented |
| 6F | FCI Template | ✅ Implemented |
| 80 | Data bytes (excl. structural) | ✅ Implemented |
| 81 | Data bytes (incl. structural) | ✅ Implemented |
| 82 | File descriptor | ✅ Implemented |
| 83 | File identifier | ✅ Implemented |
| 84 | DF name / AID | ✅ Implemented |
| 88 | Short EF identifier | ✅ Implemented |
| 8A | Life cycle status | ✅ Implemented |

### Application Identifier (AID)

| Feature | Status |
|---------|--------|
| RID (5 bytes) | ✅ Supported |
| PIX (up to 11 bytes) | ✅ Supported |
| Partial AID matching | ✅ Supported |
| Multiple AIDs | ✅ Supported |

---

## Architecture

### Command Handler Flow

```
USB CCID Driver (HAL)
        │
        ▼
┌───────────────────┐
│ iso7816_handler.c │  ◄── Main APDU dispatcher
└────────┬──────────┘
         │
    ┌────┴────┐
    │         │
    ▼         ▼
┌───────┐  ┌──────────────────┐
│ App   │  │ Standard Handler │
│Custom │  │ (CLA=0x00)       │
└───────┘  └────────┬─────────┘
                    │
         ┌──────────┼──────────┐
         │          │          │
         ▼          ▼          ▼
    ┌────────┐ ┌────────┐ ┌────────┐
    │ SELECT │ │  READ  │ │ VERIFY │
    │ 0xA4   │ │  0xB0  │ │  0x20  │
    └────────┘ └────────┘ └────────┘
```

### File System Structure

```c
Iso7816FileSystem
├── master_file (Iso7816File*)     // MF - root of hierarchy
├── current_df (Iso7816File*)      // Currently selected DF
├── current_ef (Iso7816File*)      // Currently selected EF
├── apps (Iso7816AppDefinition*[]) // Registered applications
└── app_count                      // Number of registered apps
```

---

## Testing

### Python Test Suite

Location: `client/`

Run tests:
```bash
cd applications/debug/ccid_test/client
pytest select_test.py -v      # SELECT command tests
pytest get_data_test.py -v    # GET DATA command tests
pytest -v                     # Run all tests
```

### Test Coverage

| Test Category | Count | Status |
|---------------|-------|--------|
| SELECT by FID | 5 | ✅ |
| SELECT by AID | 3 | ✅ |
| SELECT by path | 3 | ✅ |
| SELECT parent | 2 | ✅ |
| SELECT error cases | 4 | ✅ |
| GET DATA two-byte tags | 4 | ✅ |
| GET DATA single-byte tags | 2 | ✅ |
| GET DATA dump | 2 | ✅ |
| GET DATA error cases | 2 | ✅ |
| GET DATA content validation | 2 | ✅ |

**Total: 28 tests passing**

---

## Next Steps / Roadmap

### Phase 2: Core Commands (Estimated: 16 hours)

1. **GET RESPONSE (0xC0)** - 4 hours
   - Response buffering
   - SW1=61 handling
   - Chained response support

2. **READ BINARY (0xB0)** - 6 hours
   - Offset addressing
   - Short EF identifier support
   - Boundary checking

3. **READ RECORD (0xB2)** - 6 hours
   - Record number mode
   - Record identifier mode
   - Multiple record reading

### Phase 3: Security Commands (Estimated: 14 hours)

5. **VERIFY (0x20)** - 6 hours
   - PIN verification
   - Retry counter
   - Block/unblock logic

6. **GET CHALLENGE (0x84)** - 2 hours
   - Random number generation
   - Challenge storage

7. **EXTERNAL AUTHENTICATE (0x82)** - 6 hours
   - Challenge-response
   - Cryptographic verification

### Phase 4: Advanced Features (Estimated: 20 hours)

8. **UPDATE BINARY (0xD6)** - 4 hours
9. **UPDATE RECORD (0xDC)** - 4 hours
10. **Secure Messaging** - 8 hours
11. **Logical Channels** - 4 hours

---

## Build Instructions

```bash
# Build the CCID test application
./fbt fap_ccid_test

# Clean and rebuild
./fbt fap_ccid_test --clean
./fbt fap_ccid_test

# Flash and run
./fbt launch APPSRC=applications/debug/ccid_test
```

---

## File Structure

```
applications/debug/ccid_test/
├── ccid_test.c                      # Main application
├── ccid_test_app_commands.c         # Custom app commands (CLA=0x01)
├── ccid_test_app_commands.h
├── application.fam                  # App manifest
│
├── iso7816/
│   ├── iso7816_handler.c            # Main APDU handler
│   ├── iso7816_handler.h
│   ├── iso7816_response.c           # Status word helpers
│   ├── iso7816_response.h           # Status word definitions
│   ├── iso7816_filesystem.c         # Virtual file system
│   ├── iso7816_filesystem.h
│   │
│   └── commands/
│       ├── iso7816_cmd_select.c     # SELECT command
│       └── iso7816_cmd_select.h
│
├── client/
│   ├── select_test.py               # SELECT command tests
│   ├── get_data_test.py             # GET DATA command tests
│   └── .gitignore                   # Python cache exclusions
│
├── ISO7816_IMPLEMENTATION_GUIDE.md  # ISO7816-4 reference
└── ISO7816_IMPLEMENTATION_STATUS.md # This file
```

---

## References

- ISO/IEC 7816-3:2006 - Electrical interface and transmission protocols
- ISO/IEC 7816-4:2005 - Organization, security and commands for interchange
- ISO/IEC 7816-5:2004 - Registration of application providers
- USB Device Class: Smart Card CCID, Revision 1.1

---

## Changelog

### 2025-12-14 (Session 2)

- ✅ Implemented GET DATA command (INS=0xCA)
- ✅ Added built-in data objects (5F51, 5F52, 5F56)
- ✅ Added data object registration API
- ✅ Created GET DATA test suite (12 tests)
- ✅ Added .gitignore for Python cache files

### 2025-12-14 (Session 1)

- ✅ Created initial implementation status document
- ✅ Implemented ISO7816 virtual file system (MF, DF, EF support)
- ✅ Implemented SELECT command (all P1 modes)
- ✅ Added 40+ ISO7816 status words
- ✅ Created Python test suite for SELECT command (16 tests)
- ✅ Integrated with CCID test application
