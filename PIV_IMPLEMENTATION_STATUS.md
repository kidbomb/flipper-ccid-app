# PIV Implementation Status

## Overview

This document tracks the implementation status of the NIST SP 800-73-5 PIV (Personal Identity Verification) card application on the Flipper Zero USB CCID platform.

**Reference Standard**: NIST SP 800-73-5 Part 1 & 2 (July 2024)
**Last Updated**: December 18, 2025

---

## Implementation Summary

| Component | Status | Tests | Notes |
|-----------|--------|-------|-------|
| Multi-App Architecture | ✅ Complete | 6 | PIV + Generic app coexistence |
| PIV Application Selection | ✅ Complete | 6 | Full AID + truncated AID support |
| Discovery Object | ✅ Complete | 3 | PIV AID + PIN policy |
| Card Capability Container | ✅ Complete | 4 | GSC-IS 2.0 compatible |
| CHUID | ✅ Complete | 4 | FASC-N, GUID, expiration |
| PIV PIN Management | ✅ Complete | 20 | CHANGE REF DATA, RESET RETRY |
| PIV PUK Management | ✅ Complete | 5 | Unblocking key support |
| Cryptographic Operations | ✅ Complete | 15 | GENERAL AUTHENTICATE (ECC P-256) |
| Certificate Storage | ✅ Complete | 16 | X.509 for PIV Auth & Card Auth |
| PUT DATA | ❌ Not Started | 0 | Admin provisioning |

**Total Tests**: 135 (119 pytest + 16 cert tests)

---

## Changelog

### December 18, 2025 - Phase 5: Certificate Storage

- ✅ **X.509 Certificate for PIV Authentication (0x5FC105)**: Demo ECC P-256 cert
- ✅ **X.509 Certificate for Card Authentication (0x5FC101)**: Demo ECC P-256 cert
- ✅ **PIV Container Format**: Tag 53 with 70 (cert), 71 (info), FE (LRC)
- ✅ **Certificate Info Byte**: Compression indicator (uncompressed)
- ✅ **Integrated with GET DATA (INS=0xCB)**: Certificates retrievable via BER-TLV tag
- ✅ **No PIN required**: Certificates readable without authentication (contact interface)
- ✅ **16 new tests** in `piv_cert_test.py`

**Files Created**:
- `piv/piv_cert.h` - Certificate container definitions and function declarations
- `piv/piv_cert.c` - Demo X.509 certificates (~360 lines)
- `client/piv_cert_test.py` - Certificate test suite (16 tests)

**Files Modified**:
- `piv/piv_data.c` - Integrated certificate lookup into GET DATA
- `piv/piv_app.c` - Initialize certificate module on startup

### December 18, 2025 - Phase 4: Cryptographic Operations

- ✅ **GENERAL AUTHENTICATE (INS=0x87)**: Internal authenticate with ECC P-256
- ✅ **Key References**: 0x9A (PIV Auth), 0x9C (Digital Sig), 0x9E (Card Auth)
- ✅ **ECC P-256 signing**: ECDSA signatures using mbedTLS
- ✅ **Dynamic Auth Template (0x7C)**: Challenge/Response protocol
- ✅ **PIN protection**: Key 0x9E (Card Auth) works without PIN, others require PIN
- ✅ **Demo test key**: Pre-generated P-256 key for testing
- ✅ **15 new tests** in `piv_crypto_test.py`

**Files Created**:
- `piv/piv_keys.h` - Key reference definitions and storage structures
- `piv/piv_keys.c` - Demo ECC P-256 key storage (~150 lines)
- `piv/piv_crypto.h` - GENERAL AUTHENTICATE interface
- `piv/piv_crypto.c` - Full crypto implementation (~400 lines)
- `client/piv_crypto_test.py` - Crypto test suite (15 tests)

**Files Modified**:
- `piv/piv_app.c` - Route INS=0x87 to crypto handler
- `application.fam` - Added mbedtls library dependency

### December 18, 2025 - Phase 3: PIV PIN Management

- ✅ **PIV PIN (ref 0x80)**: 6-8 numeric digits, default "123456"
- ✅ **PIV PUK (ref 0x81)**: 8-digit numeric, default "12345678"
- ✅ **CHANGE REFERENCE DATA (INS=0x24)**: Change PIN/PUK with old+new value
- ✅ **RESET RETRY COUNTER (INS=0x2C)**: Unblock PIN using PUK
- ✅ **PIN format validation**: 6-8 ASCII digits (0x30-0x39), 0xFF padding
- ✅ **Max 10 retries** for both PIN and PUK
- ✅ **20 new tests** in `piv_pin_test.py`

**Files Created**:
- `piv/piv_pin.h` - PIV PIN definitions and function declarations
- `piv/piv_pin.c` - Full implementation (~350 lines)
- `client/piv_pin_test.py` - Comprehensive PIV PIN test suite

### December 17, 2025 - Phase 2: PIV Data Containers

- ✅ **Discovery Object (0x7E)**: PIV AID + PIN policy
- ✅ **Card Capability Container (0x5FC107)**: GSC-IS compatibility
- ✅ **Card Holder Unique Identifier (0x5FC102)**: FASC-N, GUID, expiration
- ✅ **PIV GET DATA (INS=0xCB)**: BER-TLV tag retrieval
- ✅ **15 new tests** in `piv_get_data_test.py`

**Files Created**:
- `piv/piv_data.h` - PIV container definitions and tags
- `piv/piv_data.c` - Demo data container implementations
- `client/piv_get_data_test.py` - PIV GET DATA test suite

### December 17, 2025 - Phase 1: Multi-Application Architecture

- ✅ **Application registry** with callback-based architecture
- ✅ **PIV application** with AID A0 00 00 03 08 00 00 10 00 01 00
- ✅ **Generic test application** with AID F0 54 45 53 54 41 50 50
- ✅ **Truncated AID matching** (9-byte PIV AID works)
- ✅ **Application lifecycle**: on_select, on_deselect, handle_apdu
- ✅ **FCI template** in SELECT response

**Files Created**:
- `iso7816/iso7816_app.h` - Application registry interface
- `iso7816/iso7816_app.c` - Application management implementation
- `apps/generic_app.h` - Generic test application
- `apps/generic_app.c` - Test commands (GET APP INFO)
- `piv/piv_app.h` - PIV application interface
- `piv/piv_app.c` - PIV command routing

---

## Architecture

### Application Stack

```
┌─────────────────────────────────────────────────────────────┐
│                   Application Layer                         │
│  ┌─────────────────────┐  ┌─────────────────────────────┐  │
│  │    PIV Application  │  │   Generic Test Application  │  │
│  │  AID: A0...08..10.. │  │   AID: F0 54 45 53 54...    │  │
│  │                     │  │                             │  │
│  │ • Discovery Object  │  │ • GET APP INFO (INS=01)     │  │
│  │ • CCC, CHUID        │  │ • Echo commands             │  │
│  │ • PIN Management    │  │                             │  │
│  │ • GET DATA (0xCB)   │  │                             │  │
│  └─────────────────────┘  └─────────────────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│              ISO7816 Application Registry                    │
│  • Application registration and lookup                       │
│  • AID matching (full and truncated)                         │
│  • Application lifecycle management                          │
├─────────────────────────────────────────────────────────────┤
│                   ISO7816 Handler                            │
│  • APDU parsing (short and extended)                         │
│  • Command routing to active application                     │
│  • File system integration                                   │
├─────────────────────────────────────────────────────────────┤
│                     USB CCID Layer                           │
│  • PC_to_RDR_XfrBlock / RDR_to_PC_DataBlock                  │
│  • Extended APDU support (up to 2048 bytes)                  │
└─────────────────────────────────────────────────────────────┘
```

### PIV Module Structure

```
piv/
├── piv_app.h        # PIV application interface
├── piv_app.c        # PIV command handler, app lifecycle
├── piv_data.h       # Container IDs, BER-TLV tags, data structures
├── piv_data.c       # Data container implementations (Discovery, CCC, CHUID)
├── piv_pin.h        # PIV PIN/PUK definitions
└── piv_pin.c        # CHANGE REFERENCE DATA, RESET RETRY COUNTER
```

---

## PIV Commands Implementation Status

### Implemented Commands

| Command | INS | CLA | P1 | P2 | Status | Notes |
|---------|-----|-----|----|----|--------|-------|
| SELECT | 0xA4 | 0x00 | 0x04 | 0x00 | ✅ Complete | Full + truncated AID |
| GET DATA (PIV) | 0xCB | 0x00 | 0x3F | 0xFF | ✅ Complete | BER-TLV tag retrieval |
| VERIFY | 0x20 | 0x00 | 0x00 | ref | ✅ Complete | PIN ref 0x80, PUK ref 0x81 |
| CHANGE REFERENCE DATA | 0x24 | 0x00 | 0x00 | ref | ✅ Complete | PIN/PUK change |
| RESET RETRY COUNTER | 0x2C | 0x00 | 0x00 | ref | ✅ Complete | Unblock with PUK |

### Pending Commands

| Command | INS | CLA | Priority | Description |
|---------|-----|-----|----------|-------------|
| GENERAL AUTHENTICATE | 0x87 | 0x00 | High | Cryptographic operations |
| PUT DATA | 0xDB | 0x00 | Medium | Store data objects |
| GENERATE ASYMMETRIC KEY PAIR | 0x47 | 0x00 | Low | On-card key generation |

---

## PIV Data Objects Status

### Mandatory Objects (7 total)

| Object | BER-TLV Tag | Status | Access | Notes |
|--------|-------------|--------|--------|-------|
| Card Capability Container | 0x5FC107 | ✅ Implemented | Always | GSC-IS 2.0 compatible |
| Card Holder Unique Identifier | 0x5FC102 | ✅ Implemented | Always | Demo FASC-N, GUID |
| X.509 Certificate PIV Auth | 0x5FC105 | ❌ Stub | Always | Returns FILE_NOT_FOUND |
| Cardholder Fingerprints | 0x5FC103 | ❌ Stub | PIN | Returns FILE_NOT_FOUND |
| Security Object | 0x5FC106 | ❌ Stub | Always | Returns FILE_NOT_FOUND |
| Cardholder Facial Image | 0x5FC108 | ❌ Stub | PIN | Returns FILE_NOT_FOUND |
| X.509 Certificate Card Auth | 0x5FC101 | ❌ Stub | Always | Returns FILE_NOT_FOUND |

### Optional Objects (implemented)

| Object | BER-TLV Tag | Status | Access | Notes |
|--------|-------------|--------|--------|-------|
| Discovery Object | 0x7E | ✅ Implemented | Always | PIV AID + PIN policy |
| Printed Information | 0x5FC109 | ❌ Stub | PIN | Returns FILE_NOT_FOUND |
| Key History Object | 0x5FC10C | ❌ Stub | Always | Returns FILE_NOT_FOUND |

---

## PIN/PUK Management

### Key References

| Reference | Type | Format | Default | Retries |
|-----------|------|--------|---------|---------|
| 0x80 | PIV Card Application PIN | 6-8 numeric digits | "123456" | 10 |
| 0x81 | PIN Unblocking Key (PUK) | 8 numeric digits | "12345678" | 10 |

### PIN Format (NIST SP 800-73-5)

```
PIV PIN: 6-8 numeric ASCII digits (0x30-0x39), padded with 0xFF to 8 bytes

Example: "123456" → [0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0xFF, 0xFF]
```

### Commands

**CHANGE REFERENCE DATA (INS=0x24)**
```
Command: 00 24 00 80 10 <old PIN 8 bytes><new PIN 8 bytes>
Response: 90 00 (success) or 63 CX (X retries remaining)
```

**RESET RETRY COUNTER (INS=0x2C)**
```
Command: 00 2C 00 80 10 <PUK 8 bytes><new PIN 8 bytes>
Response: 90 00 (success) or 63 CX (PUK retries remaining)
```

---

## Test Coverage

### Test Files

| File | Tests | Description |
|------|-------|-------------|
| `iso7816_test.py` | 6 | Core APDU tests |
| `select_test.py` | 11 | SELECT command tests |
| `verify_test.py` | 26 | PIN verification tests |
| `get_data_test.py` | 11 | GET DATA tests |
| `piv_get_data_test.py` | 15 | PIV-specific GET DATA |
| `piv_pin_test.py` | 20 | PIV PIN management |
| `app_select_test.py` | 6 | Multi-app selection (standalone) |

### Running Tests

```bash
# Launch the CCID test app
./fbt launch APPSRC=applications/debug/ccid_test

# Run PC/SC tests (in another terminal)
cd applications/debug/ccid_test/client
source .venv/bin/activate
pytest iso7816_test.py select_test.py verify_test.py get_data_test.py piv_get_data_test.py piv_pin_test.py -v

# Run standalone app selection tests
python app_select_test.py

# Run CCID protocol tests (requires root)
sudo .venv/bin/python3 ccid_test.py
```

---

## Implementation Roadmap

### Phase 4: Cryptographic Operations (Next)

**Goal**: Implement GENERAL AUTHENTICATE for PIV authentication

**Tasks**:
1. Define PIV key references (0x9A, 0x9B, 0x9C, 0x9D, 0x9E)
2. Implement GENERAL AUTHENTICATE (INS=0x87)
3. Support RSA and ECC algorithms
4. Add test certificates and keys
5. Create `piv_crypto_test.py`

**Dependencies**:
- mbedTLS for cryptographic primitives
- Key storage mechanism

### Phase 5: Certificate Storage

**Goal**: Store and retrieve X.509 certificates

**Tasks**:
1. Certificate container storage
2. Certificate chain validation
3. PUT DATA for certificate upload
4. Memory management for large objects

### Phase 6: Full PIV Compliance

**Goal**: Complete NIST SP 800-73-5 compliance

**Tasks**:
1. Security Object generation
2. Biometric data containers (stubs)
3. Access control enforcement
4. Secure messaging (optional)

---

## File Structure

```
applications/debug/ccid_test/
├── CCID_IMPLEMENTATION_STATUS.md   # CCID layer status
├── ISO7816_IMPLEMENTATION_GUIDE.md # ISO7816 command reference
├── PIV_IMPLEMENTATION_GUIDE.md     # PIV specification details
├── PIV_IMPLEMENTATION_STATUS.md    # This file
├── ccid_test_app.c                 # Main application
├── iso7816/
│   ├── iso7816_app.h/c             # Application registry
│   ├── iso7816_handler.h/c         # Command dispatcher
│   ├── iso7816_filesystem.h/c      # File system with PIN support
│   ├── iso7816_response.h/c        # Status word helpers
│   └── commands/
│       ├── iso7816_cmd_select.c
│       ├── iso7816_cmd_get_data.c
│       └── iso7816_cmd_verify.c
├── piv/
│   ├── piv_app.h/c                 # PIV application
│   ├── piv_data.h/c                # Data containers
│   └── piv_pin.h/c                 # PIN management
├── apps/
│   └── generic_app.h/c             # Test application
└── client/
    ├── requirements.txt
    ├── ccid_test.py                # CCID protocol tests
    ├── iso7816_test.py             # APDU tests
    ├── select_test.py              # SELECT tests
    ├── verify_test.py              # VERIFY tests
    ├── get_data_test.py            # GET DATA tests
    ├── piv_get_data_test.py        # PIV GET DATA tests
    ├── piv_pin_test.py             # PIV PIN tests
    └── app_select_test.py          # Multi-app tests
```

---

## Security Considerations

### Current Implementation

- ✅ PIN retry counter (blocks after 10 failures)
- ✅ PUK retry counter (blocks after 10 failures)
- ✅ PIN format validation (numeric only)
- ✅ PIN length validation (6-8 digits)
- ✅ PIN cleared on application deselect

### Future Enhancements

- ⚠️ Secure key storage (not yet implemented)
- ⚠️ Transaction atomicity for PIN changes
- ⚠️ Audit logging
- ⚠️ Secure messaging for contactless (not applicable for USB)

---

## References

1. **NIST SP 800-73-5 Part 1** - PIV Card Application Namespace, Data Model
2. **NIST SP 800-73-5 Part 2** - PIV Card Command Interface
3. **NIST SP 800-73-5 Part 3** - PIV Client API
4. **FIPS 201-3** - Personal Identity Verification (PIV)
5. **ISO/IEC 7816-4:2020** - Organization, security and commands

---

*Document maintained as part of the Flipper Zero PIV implementation*
