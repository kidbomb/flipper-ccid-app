# PIV (Personal Identity Verification) Implementation Guide

## Overview

This document provides a detailed implementation guide for NIST SP 800-73-5 Part 1: Personal Identity Verification (PIV) Card Application on the Flipper Zero USB CCID platform.

**Reference Standard**: NIST SP 800-73-5 Part 1 (July 2024) - Interfaces for Personal Identity Verification: PIV Card Application Namespace, Data Model, and Representation

**Companion Documents**:
- NIST SP 800-73-5 Part 2 - PIV Card Application Card Command Interface
- NIST SP 800-73-5 Part 3 - PIV Client Application Programming Interface
- FIPS 201-3 - Personal Identity Verification (PIV) of Federal Employees and Contractors

**Last Updated**: December 17, 2025

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [PIV Card Application AID](#piv-card-application-aid)
3. [Data Model Elements](#data-model-elements)
   - [Mandatory Data Objects](#mandatory-data-objects)
   - [Conditional Data Objects](#conditional-data-objects)
   - [Optional Data Objects](#optional-data-objects)
4. [Container IDs and BER-TLV Tags](#container-ids-and-ber-tlv-tags)
5. [Access Control Rules](#access-control-rules)
6. [Key References](#key-references)
7. [PIV Commands to Implement](#piv-commands-to-implement)
8. [Implementation Phases](#implementation-phases)
9. [File Structure Design](#file-structure-design)
10. [Code Architecture](#code-architecture)
11. [Testing Strategy](#testing-strategy)
12. [Security Considerations](#security-considerations)

---

## Architecture Overview

### Current ISO7816 Stack

The existing ISO7816 implementation provides a solid foundation for PIV:

```
┌─────────────────────────────────────────────────────────────┐
│                    PIV Application (NEW)                     │
│  - PIV-specific command handlers                             │
│  - Data containers (CHUID, certificates, etc.)               │
│  - Authentication mechanisms                                 │
├─────────────────────────────────────────────────────────────┤
│              ISO7816 Application Layer (EXISTS)              │
│  - File system (MF/DF/EF hierarchy)                          │
│  - SELECT, GET DATA, VERIFY commands                         │
│  - Response handling and status codes                        │
├─────────────────────────────────────────────────────────────┤
│                 ISO7816 Handler (EXISTS)                     │
│  - APDU parsing (short and extended)                         │
│  - Command routing                                           │
│  - Application lifecycle management                          │
├─────────────────────────────────────────────────────────────┤
│                   USB CCID Layer (EXISTS)                    │
│  - PC_to_RDR_XfrBlock / RDR_to_PC_DataBlock                  │
│  - ATR generation                                            │
│  - Extended APDU support (2048 bytes)                        │
└─────────────────────────────────────────────────────────────┘
```

### PIV Card Application Structure

```
MF (3F00)
└── PIV Application (5000)  ──────────── AID: A0 00 00 03 08 00 00 10 00 01 00
    ├── Card Capability Container (5FC107)
    ├── Card Holder Unique Identifier (5FC102)
    ├── X.509 Certificate for PIV Auth (5FC105)
    ├── Cardholder Fingerprints (5FC103)
    ├── Security Object (5FC106)
    ├── Cardholder Facial Image (5FC108)
    ├── X.509 Certificate for Card Auth (5FC101)
    ├── X.509 Certificate for Digital Signature (5FC10A)
    ├── X.509 Certificate for Key Management (5FC10B)
    ├── Printed Information (5FC109)
    ├── Discovery Object (7E)
    ├── Key History Object (5FC10C)
    └── ... (additional optional containers)
```

---

## PIV Card Application AID

### Full AID (11 bytes)

```
A0 00 00 03 08 00 00 10 00 01 00
│          │        │     │
│          │        │     └── Version: 01 00 (v1.0)
│          │        └── Application: PIV Card Application
│          └── PIX: 00 00 10 00
└── NIST RID: A0 00 00 03 08
```

### Right-Truncated AID (9 bytes) - Also Valid for Selection

```
A0 00 00 03 08 00 00 10 00
```

### Registration

```c
// PIV AID constant
static const uint8_t PIV_AID[] = {
    0xA0, 0x00, 0x00, 0x03, 0x08,  // NIST RID
    0x00, 0x00, 0x10, 0x00,         // PIV PIX
    0x01, 0x00                       // Version
};
#define PIV_AID_LEN 11
```

---

## Data Model Elements

### Mandatory Data Objects (7 Objects)

These **SHALL** be present on all PIV Cards:

| # | Container Name | Container ID | BER-TLV Tag | Max Bytes | Description |
|---|----------------|--------------|-------------|-----------|-------------|
| 1 | Card Capability Container | 0xDB00 | 5FC107 | 170 | GSC-IS compatibility info |
| 2 | Card Holder Unique Identifier (CHUID) | 0x3000 | 5FC102 | 2881 | FASC-N, GUID, expiration, signature |
| 3 | X.509 Certificate for PIV Authentication | 0x0101 | 5FC105 | 1857 | Cardholder + card authentication |
| 4 | Cardholder Fingerprints | 0x6010 | 5FC103 | 4006 | Primary & secondary fingerprints |
| 5 | Security Object | 0x9000 | 5FC106 | 1336 | Maps DG to ContainerIDs, hashes |
| 6 | Cardholder Facial Image | 0x6030 | 5FC108 | 12710 | Facial image for verification |
| 7 | X.509 Certificate for Card Authentication | 0x0500 | 5FC101 | 1857 | Card-only authentication |

### Conditional Data Objects (2 Objects)

Mandatory if cardholder has government-issued email:

| # | Container Name | Container ID | BER-TLV Tag | Max Bytes | Description |
|---|----------------|--------------|-------------|-----------|-------------|
| 1 | X.509 Certificate for Digital Signature | 0x0100 | 5FC10A | 1857 | Document signing (non-repudiation) |
| 2 | X.509 Certificate for Key Management | 0x0102 | 5FC10B | 1857 | Encryption key management |

### Optional Data Objects (27 Objects)

| Container Name | Container ID | BER-TLV Tag | Max Bytes |
|----------------|--------------|-------------|-----------|
| Printed Information | 0x3001 | 5FC109 | 245 |
| Discovery Object | 0x6050 | 7E | 19 |
| Key History Object | 0x6060 | 5FC10C | 128 |
| Retired X.509 Cert for Key Mgmt 1-20 | 0x1001-0x1014 | 5FC10D-5FC120 | 1895 each |
| Cardholder Iris Images | 0x1015 | 5FC121 | 7106 |
| Biometric Info Templates Group | 0x1016 | 7F61 | 65 |
| Secure Messaging Certificate Signer | 0x1017 | 5FC122 | 2471 |
| Pairing Code Reference Data Container | 0x1018 | 5FC123 | 12 |

---

## Container IDs and BER-TLV Tags

### Complete Container Mapping

```c
// PIV Container IDs
#define PIV_CONTAINER_CCC                    0xDB00
#define PIV_CONTAINER_CHUID                  0x3000
#define PIV_CONTAINER_CERT_PIV_AUTH          0x0101
#define PIV_CONTAINER_FINGERPRINTS           0x6010
#define PIV_CONTAINER_SECURITY_OBJECT        0x9000
#define PIV_CONTAINER_FACIAL_IMAGE           0x6030
#define PIV_CONTAINER_CERT_CARD_AUTH         0x0500
#define PIV_CONTAINER_CERT_DIGITAL_SIG       0x0100
#define PIV_CONTAINER_CERT_KEY_MGMT          0x0102
#define PIV_CONTAINER_PRINTED_INFO           0x3001
#define PIV_CONTAINER_DISCOVERY              0x6050
#define PIV_CONTAINER_KEY_HISTORY            0x6060
#define PIV_CONTAINER_RETIRED_CERT_1         0x1001
// ... through 0x1014
#define PIV_CONTAINER_IRIS_IMAGES            0x1015
#define PIV_CONTAINER_BIT_GROUP              0x1016
#define PIV_CONTAINER_SM_CERT_SIGNER         0x1017
#define PIV_CONTAINER_PAIRING_CODE           0x1018

// BER-TLV Tags for GET DATA command (P1-P2 values)
#define PIV_TAG_CCC                          0x5FC107
#define PIV_TAG_CHUID                        0x5FC102
#define PIV_TAG_CERT_PIV_AUTH                0x5FC105
#define PIV_TAG_FINGERPRINTS                 0x5FC103
#define PIV_TAG_SECURITY_OBJECT              0x5FC106
#define PIV_TAG_FACIAL_IMAGE                 0x5FC108
#define PIV_TAG_CERT_CARD_AUTH               0x5FC101
#define PIV_TAG_CERT_DIGITAL_SIG             0x5FC10A
#define PIV_TAG_CERT_KEY_MGMT                0x5FC10B
#define PIV_TAG_PRINTED_INFO                 0x5FC109
#define PIV_TAG_DISCOVERY                    0x7E
#define PIV_TAG_KEY_HISTORY                  0x5FC10C
```

---

## Access Control Rules

### Data Object Access Rules by Interface

| Container Name | Contact Read | Contactless Read | M/O/C |
|----------------|--------------|------------------|-------|
| Card Capability Container | Always | VCI | M |
| Card Holder Unique Identifier | Always | Always | M |
| X.509 Cert for PIV Auth | Always | VCI | M |
| Cardholder Fingerprints | PIN | VCI and PIN | M |
| Security Object | Always | VCI | M |
| Cardholder Facial Image | PIN | VCI and PIN | M |
| X.509 Cert for Card Auth | Always | Always | M |
| X.509 Cert for Digital Sig | Always | VCI | C |
| X.509 Cert for Key Mgmt | Always | VCI | C |
| Printed Information | PIN or OCC | VCI and (PIN or OCC) | O |
| Discovery Object | Always | Always | O |
| Key History Object | Always | VCI | O |

**Legend**:
- **Always**: No access control restrictions
- **PIN**: PIN verification required
- **VCI**: Virtual Contact Interface (secure messaging established)
- **OCC**: On-Card biometric Comparison
- **M/O/C**: Mandatory/Optional/Conditional

### Implementation Notes for Flipper Zero

Since Flipper Zero operates as a **USB contact interface only** (no contactless), the implementation should:

1. Use "Contact" access rules only
2. Implement PIN verification for protected containers
3. Skip VCI/SM requirements (contact interface doesn't need them)

---

## Key References

### Authentication Data References (Table 4)

| Key Ref | Data Type | Authenticatable Entity | Security Condition |
|---------|-----------|------------------------|-------------------|
| 0x00 | Global PIN | Cardholder | Always |
| 0x80 | PIV Card Application PIN | Cardholder | Always |
| 0x81 | PIN Unblocking Key (PUK) | Administrator | Always |
| 0x96 | Primary Finger OCC | Cardholder | SM |
| 0x97 | Secondary Finger OCC | Cardholder | SM |
| 0x98 | Pairing Code | Cardholder | SM |

### Key References (Table 5)

| Key Ref | Key Type | Administrator | Security Condition |
|---------|----------|---------------|-------------------|
| 0x04 | PIV Secure Messaging Key | Admin | Always |
| 0x9A | PIV Authentication Key | Admin | PIN or OCC |
| 0x9B | PIV Card Admin Key | Admin | Always |
| 0x9C | Digital Signature Key | Admin | PIN Always or OCC Always |
| 0x9D | Key Management Key | Admin | PIN or OCC |
| 0x9E | Card Authentication Key | Admin | Always |
| 0x82-0x95 | Retired Key Management Keys | Admin | PIN or OCC |

---

## PIV Commands to Implement

### Core PIV Commands

Based on SP 800-73-5 Part 2, these commands are needed:

| Command | INS | Priority | Description |
|---------|-----|----------|-------------|
| SELECT | A4 | ✅ Exists | Select PIV application by AID |
| GET DATA | CB | ⚠️ Extend | Retrieve data objects (odd INS for BER-TLV) |
| VERIFY | 20 | ✅ Exists | PIN verification |
| CHANGE REFERENCE DATA | 24 | 🔴 New | Change PIN |
| RESET RETRY COUNTER | 2C | 🔴 New | Unblock PIN with PUK |
| GENERAL AUTHENTICATE | 87 | 🔴 New | Cryptographic operations |
| PUT DATA | DB | 🔴 New | Store data objects (admin) |
| GENERATE ASYMMETRIC KEY PAIR | 47 | 🔴 New | Generate keys on card |

### PIV-Specific GET DATA Usage

PIV uses GET DATA with INS = 0xCB (odd instruction) for BER-TLV encoded data:

```
Command: 00 CB 3F FF 05 5C 03 5F C1 02 00
         │  │  │  │  │  │  │  │  │  │  └─ Le = 0 (max response)
         │  │  │  │  │  │  │  │  │  └──── Container tag (02)
         │  │  │  │  │  │  │  └──────── Container tag (C1 5F = 5FC1)
         │  │  │  │  │  └────────────── Tag list tag (5C)
         │  │  │  │  └───────────────── Lc = 5 bytes
         │  │  │  └──────────────────── P2 = FF (current DF)
         │  │  └─────────────────────── P1 = 3F (BER-TLV tag in data field)
         │  └────────────────────────── INS = CB (GET DATA odd)
         └───────────────────────────── CLA = 00
```

---

## Implementation Phases

### Phase 1: PIV Application Registration (Week 1)

**Goal**: Register PIV application with existing file system

**Tasks**:
1. Create `piv_app.h` and `piv_app.c` in new `applications/piv/` folder
2. Define PIV AID and register with `iso7816_fs_register_app()`
3. Implement application lifecycle callbacks (on_select, on_deselect)
4. Add PIV-specific response to SELECT command

**Files to Create**:
```
applications/debug/ccid_test/
├── piv/
│   ├── piv_app.h           # PIV application interface
│   ├── piv_app.c           # PIV application implementation
│   ├── piv_data_objects.h  # Container and tag definitions
│   └── piv_data_objects.c  # Data object storage
```

**Code Example**:
```c
// piv_app.h
#pragma once

#include "../iso7816/iso7816_filesystem.h"

// PIV Application Identifier
extern const uint8_t PIV_AID[];
extern const uint8_t PIV_AID_LEN;

// Initialize PIV application
void piv_app_init(Iso7816FileSystem* fs);

// PIV command handler
void piv_app_command_handler(
    void* context,
    const void* command,
    void* response);
```

### Phase 2: Mandatory Data Containers (Week 2-3)

**Goal**: Implement all 7 mandatory PIV data objects

**Priority Order**:
1. **Discovery Object** (simplest, 19 bytes)
2. **Card Capability Container** (CCC)
3. **Card Holder Unique Identifier** (CHUID)
4. **X.509 Certificate for Card Authentication**
5. **X.509 Certificate for PIV Authentication**
6. **Security Object**
7. **Cardholder Fingerprints**
8. **Cardholder Facial Image**

**Data Object Storage Strategy**:

```c
// Option 1: Static data in flash (for demo/test certificates)
static const uint8_t piv_demo_chuid[] = {
    // FASC-N (tag 0x30)
    0x30, 0x19, /* 25 bytes of FASC-N */
    // GUID (tag 0x34)
    0x34, 0x10, /* 16 bytes UUID */
    // Expiration Date (tag 0x35)
    0x35, 0x08, '2', '0', '3', '0', '0', '1', '0', '1',
    // Issuer Asymmetric Signature (tag 0x3E)
    0x3E, 0x82, /* variable length CMS signature */
    // Error Detection Code (tag 0xFE)
    0xFE, 0x00
};

// Option 2: Dynamic storage in filesystem EF
Iso7816File* chuid_ef = iso7816_fs_create_ef_transparent(
    fs, piv_df, 0x3000, 0, 2881);
```

### Phase 3: GET DATA for PIV (Week 3)

**Goal**: Extend GET DATA to handle PIV container tags

**Implementation**:
```c
// In iso7816_cmd_get_data.c - add PIV tag handling

static bool piv_get_data_by_tag(
    uint32_t tag,
    uint8_t* buffer,
    uint16_t* length) {
    
    switch(tag) {
        case 0x5FC102:  // CHUID
            return piv_get_chuid(buffer, length);
        case 0x5FC107:  // CCC
            return piv_get_ccc(buffer, length);
        case 0x5FC105:  // PIV Auth Cert
            return piv_get_cert_piv_auth(buffer, length);
        // ... etc
        default:
            return false;
    }
}
```

### Phase 4: PIN Management (Week 4)

**Goal**: Full PIN lifecycle for PIV

**Components**:
1. Extend existing VERIFY for PIV PIN (ref 0x80)
2. Implement CHANGE REFERENCE DATA (INS 0x24)
3. Implement RESET RETRY COUNTER (INS 0x2C)

**PIN Format (PIV-specific)**:
- 6-8 numeric digits (ASCII 0x30-0x39)
- Padded with 0xFF to 8 bytes

```c
// PIN storage structure for PIV
typedef struct {
    uint8_t pin[8];           // 6-8 digit PIN, 0xFF padded
    uint8_t puk[8];           // PIN Unblocking Key
    uint8_t retries_pin;      // Remaining PIN retries (max 10)
    uint8_t retries_puk;      // Remaining PUK retries
    bool verified;            // Current verification status
} PivPinData;
```

### Phase 5: Cryptographic Operations (Week 5-6)

**Goal**: GENERAL AUTHENTICATE command

**Operations**:
1. Internal Authenticate (prove card identity)
2. External Authenticate (verify external entity) - optional
3. Mutual Authenticate - optional

**Key Algorithms (from Table 6)**:

| Mechanism ID | Description | Parameter |
|--------------|-------------|-----------|
| 0x05 | RSA 3072 | Optional public exponent |
| 0x07 | RSA 2048 | Optional public exponent |
| 0x11 | ECC P-256 | None |
| 0x14 | ECC P-384 | None |

**Note**: Cryptographic operations require secure key storage. See `SECURE_KEY_STORAGE_GUIDE.md` for hardware key storage considerations.

### Phase 6: Optional Features (Week 7+)

**Lower Priority Items**:
1. Printed Information container
2. Key History Object
3. Retired certificates (20 slots)
4. Biometric templates
5. Secure Messaging

---

## File Structure Design

### PIV DF and EF Layout

```c
void piv_app_init(Iso7816FileSystem* fs) {
    // Register PIV as application under MF
    Iso7816AppDefinition piv_app_def = {
        .aid = PIV_AID,
        .aid_len = PIV_AID_LEN,
        .name = "PIV",
        .handler = piv_app_command_handler,
        .on_select = piv_on_select,
        .on_deselect = piv_on_deselect,
        .context = NULL
    };
    
    Iso7816File* piv_df = iso7816_fs_register_app(
        fs, NULL, 0x5000, &piv_app_def);
    
    // Create EFs for each mandatory container
    // Note: Container IDs become EF FIDs
    
    // Card Capability Container (always readable)
    iso7816_fs_create_ef_transparent(fs, piv_df, 
        PIV_CONTAINER_CCC, 0, 170);
    
    // CHUID (always readable)
    iso7816_fs_create_ef_transparent(fs, piv_df,
        PIV_CONTAINER_CHUID, 0, 2881);
    
    // PIV Auth Certificate (always readable)
    iso7816_fs_create_ef_transparent(fs, piv_df,
        PIV_CONTAINER_CERT_PIV_AUTH, 0, 1857);
    
    // Fingerprints (PIN protected)
    iso7816_fs_create_ef_transparent(fs, piv_df,
        PIV_CONTAINER_FINGERPRINTS, 0, 4006);
    
    // ... create remaining containers
}
```

---

## Code Architecture

### Recommended Module Structure

```
applications/debug/ccid_test/
├── piv/
│   ├── piv_app.h              # Main PIV application interface
│   ├── piv_app.c              # Application registration & lifecycle
│   ├── piv_containers.h       # Container ID and tag definitions
│   ├── piv_containers.c       # Container storage and retrieval
│   ├── piv_commands.h         # PIV-specific command handlers
│   ├── piv_commands.c         # GET DATA, GENERAL AUTH implementations
│   ├── piv_crypto.h           # Cryptographic operation interface
│   ├── piv_crypto.c           # Key operations (stub for now)
│   ├── piv_pin.h              # PIN management interface
│   ├── piv_pin.c              # PIN verify, change, reset
│   ├── data/
│   │   ├── piv_demo_certs.h   # Demo certificate data
│   │   ├── piv_demo_chuid.h   # Demo CHUID data
│   │   └── piv_demo_ccc.h     # Demo CCC data
│   └── test/
│       └── piv_test_vectors.h # Test data for validation
├── iso7816/                   # Existing ISO7816 stack
│   ├── commands/
│   │   ├── iso7816_cmd_select.c
│   │   ├── iso7816_cmd_get_data.c
│   │   └── iso7816_cmd_verify.c
│   └── ...
└── ccid_test_app.c            # Main application
```

### Integration with Existing Code

```c
// In ccid_test_app.c - initialize PIV
void ccid_test_app_init(void) {
    // Existing initialization
    iso7816_handler_alloc();
    Iso7816FileSystem* fs = iso7816_handler_get_filesystem();
    
    // Add PIV application
    piv_app_init(fs);
    
    // Continue with USB setup...
}
```

---

## Testing Strategy

### Test Categories

#### 1. Application Selection Tests
```python
# Select PIV by full AID
apdu = "00 A4 04 00 0B A0 00 00 03 08 00 00 10 00 01 00"
# Expected: 90 00 (or 61 XX with FCI)

# Select PIV by truncated AID
apdu = "00 A4 04 00 09 A0 00 00 03 08 00 00 10 00"
# Expected: 90 00
```

#### 2. GET DATA Tests
```python
# Get CHUID
apdu = "00 CB 3F FF 05 5C 03 5F C1 02 00"
# Expected: Data + 90 00

# Get Discovery Object
apdu = "00 CB 3F FF 03 5C 01 7E 00"
# Expected: 7E 12 ... + 90 00

# Get non-existent container
apdu = "00 CB 3F FF 05 5C 03 5F C1 FF 00"
# Expected: 6A 82 (file not found)
```

#### 3. PIN Verification Tests
```python
# Verify PIN (ref 0x80)
apdu = "00 20 00 80 08 31 32 33 34 35 36 FF FF"
# Expected: 90 00 (correct PIN)
# Expected: 63 CX (wrong PIN, X retries left)

# Check PIN retries
apdu = "00 20 00 80"
# Expected: 63 CX (X retries remaining)
```

#### 4. Access Control Tests
```python
# Try to read fingerprints without PIN
apdu = "00 CB 3F FF 05 5C 03 5F C1 03 00"
# Expected: 69 82 (security status not satisfied)

# Verify PIN, then read fingerprints
apdu = "00 20 00 80 08 31 32 33 34 35 36 FF FF"
apdu = "00 CB 3F FF 05 5C 03 5F C1 03 00"
# Expected: Data + 90 00
```

### Python Test Client Extension

```python
# Add to client/iso7816_test.py

class PIVTest:
    """PIV-specific test utilities"""
    
    PIV_AID = [0xA0, 0x00, 0x00, 0x03, 0x08, 
               0x00, 0x00, 0x10, 0x00, 0x01, 0x00]
    
    # Container tags
    TAG_CHUID = [0x5F, 0xC1, 0x02]
    TAG_DISCOVERY = [0x7E]
    TAG_CCC = [0x5F, 0xC1, 0x07]
    
    def select_piv(self, connection):
        """Select PIV application"""
        apdu = [0x00, 0xA4, 0x04, 0x00, len(self.PIV_AID)] + self.PIV_AID
        return connection.transmit(apdu)
    
    def get_data(self, connection, tag):
        """GET DATA for PIV container"""
        # Build tag list TLV
        tag_list = [0x5C, len(tag)] + tag
        apdu = [0x00, 0xCB, 0x3F, 0xFF, len(tag_list)] + tag_list + [0x00]
        return connection.transmit(apdu)
    
    def verify_pin(self, connection, pin):
        """Verify PIV Application PIN"""
        # Pad PIN to 8 bytes with 0xFF
        pin_bytes = [ord(c) for c in pin]
        while len(pin_bytes) < 8:
            pin_bytes.append(0xFF)
        apdu = [0x00, 0x20, 0x00, 0x80, 0x08] + pin_bytes
        return connection.transmit(apdu)
```

---

## Security Considerations

### Key Storage

⚠️ **Critical**: PIV private keys require secure storage.

Options for Flipper Zero:
1. **Demo/Test Only**: Store keys in RAM (cleared on reset)
2. **Persistent Storage**: Encrypt keys with device-specific key
3. **Future**: Hardware secure element integration

See `SECURE_KEY_STORAGE_GUIDE.md` for detailed implementation.

### PIN Protection

- Default PIN retry limit: 10 attempts (configurable, max 10 per FIPS 201)
- PIN blocked after retries exhausted
- PUK can reset PIN retry counter
- Global PIN (0x00) vs PIV Application PIN (0x80)

### Certificate Validation

For a complete PIV implementation:
1. Certificates should be valid X.509 format
2. CHUID signature should be verifiable
3. Security Object should contain valid hashes

**For Demo/Test Mode**: Use self-signed certificates with appropriate test indicators.

---

## Status Words Reference

### PIV-Specific Status Words

| SW1-SW2 | Meaning | Context |
|---------|---------|---------|
| 90 00 | Success | All commands |
| 61 XX | More data available | GET DATA, large responses |
| 63 CX | PIN wrong, X retries | VERIFY |
| 69 82 | Security status not satisfied | PIN-protected containers |
| 69 83 | Authentication blocked | PIN blocked |
| 69 84 | Reference data not usable | Data corrupted |
| 6A 80 | Incorrect parameters | Invalid data format |
| 6A 81 | Function not supported | Unsupported operation |
| 6A 82 | Data object not found | Unknown container tag |
| 6A 86 | Incorrect P1-P2 | Invalid parameters |
| 6A 88 | Referenced data not found | Key/object not found |

---

## Appendix A: Demo Data Structures

### Card Capability Container (CCC)

```c
// Table 9 - CCC structure
static const uint8_t piv_demo_ccc[] = {
    // Card Identifier (tag 0xF0)
    0xF0, 0x15,  // 21 bytes
        // GSC-RID (5 bytes) - use NIST RID
        0xA0, 0x00, 0x00, 0x03, 0x08,
        // Manufacturer ID (1 byte)
        0x00,
        // Card type (1 byte)
        0x00,
        // Card ID (14 bytes)
        'F', 'L', 'I', 'P', 'P', 'E', 'R', '0', '0', '0', '0', '0', '0', '1',
    
    // Capability Container version (tag 0xF1)
    0xF1, 0x01, 0x21,  // Version 2.1
    
    // Capability Grammar version (tag 0xF2)
    0xF2, 0x01, 0x21,  // Version 2.1
    
    // Applications CardURL (tag 0xF3)
    0xF3, 0x00,  // Empty (no GSC apps)
    
    // PKCS#15 (tag 0xF4)
    0xF4, 0x00,  // Not present
    
    // Registered Data Model number (tag 0xF5)
    0xF5, 0x01, 0x10,  // Data model 0x10 (PIV)
    
    // Access Control Rule Table (tag 0xF6)
    0xF6, 0x00,  // Empty
    
    // Card APDUs (tag 0xF7)
    0xF7, 0x00,  // None
    
    // Redirection Tag (tag 0xFA)
    0xFA, 0x00,  // None
    
    // Capability Tuples (tag 0xFB)
    0xFB, 0x00,  // None
    
    // Status Tuples (tag 0xFC)
    0xFC, 0x00,  // None
    
    // Next CCC (tag 0xFD)
    0xFD, 0x00,  // None
    
    // Error Detection Code (tag 0xFE)
    0xFE, 0x00   // LRC (empty per PIV)
};
```

### Discovery Object

```c
// Table 19 - Discovery Object
static const uint8_t piv_demo_discovery[] = {
    0x7E, 0x12,  // Template tag and length
    
    // PIV Card Application AID (tag 0x4F)
    0x4F, 0x0B,  // 11 bytes
        0xA0, 0x00, 0x00, 0x03, 0x08, 
        0x00, 0x00, 0x10, 0x00, 0x01, 0x00,
    
    // PIN Usage Policy (tag 0x5F2F)
    0x5F, 0x2F, 0x02,
        0x40,  // PIV Card Application PIN satisfies ACRs
        0x00   // No Global PIN preference
};
```

### CHUID Structure

```c
// Table 10 - CHUID structure
typedef struct {
    uint8_t fasc_n[25];      // tag 0x30 - Federal Agency Smart Credential Number
    uint8_t guid[16];        // tag 0x34 - Card UUID (RFC 4122)
    uint8_t expiration[8];   // tag 0x35 - YYYYMMDD ASCII
    uint8_t cardholder_uuid[16]; // tag 0x36 - Optional cardholder UUID
    uint8_t signature[];     // tag 0x3E - CMS signature (variable)
    uint8_t lrc;             // tag 0xFE - Error detection (empty)
} PivChuid;
```

---

## Appendix B: Quick Reference Card

### Essential APDUs

| Operation | APDU |
|-----------|------|
| Select PIV | `00 A4 04 00 0B A0 00 00 03 08 00 00 10 00 01 00` |
| Get CHUID | `00 CB 3F FF 05 5C 03 5F C1 02 00` |
| Get Discovery | `00 CB 3F FF 03 5C 01 7E 00` |
| Get CCC | `00 CB 3F FF 05 5C 03 5F C1 07 00` |
| Verify PIN | `00 20 00 80 08 [8-byte PIN]` |
| Check Retries | `00 20 00 80` |

### Container Tag Quick Reference

| Container | Tag (for GET DATA P1-P2) |
|-----------|--------------------------|
| CCC | 5F C1 07 |
| CHUID | 5F C1 02 |
| PIV Auth Cert | 5F C1 05 |
| Card Auth Cert | 5F C1 01 |
| Fingerprints | 5F C1 03 |
| Facial Image | 5F C1 08 |
| Security Object | 5F C1 06 |
| Discovery | 7E |

---

## References

1. NIST SP 800-73-5 Part 1: PIV Card Application Namespace, Data Model, and Representation (July 2024)
2. NIST SP 800-73-5 Part 2: PIV Card Application Card Command Interface
3. NIST SP 800-73-5 Part 3: PIV Client Application Programming Interface
4. FIPS 201-3: Personal Identity Verification (PIV) of Federal Employees and Contractors
5. ISO/IEC 7816-4:2005: Organization, security and commands for interchange
6. RFC 4122: A Universally Unique IDentifier (UUID) URN Namespace

---

**Document Version**: 1.0  
**Author**: Generated from NIST SP 800-73pt1-5 specification  
**Status**: Implementation Guide
