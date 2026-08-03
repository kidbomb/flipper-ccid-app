# ISO7816 Applications Guide

## Overview

This document describes the ISO7816 smart card applications implemented in the Flipper Zero CCID test application. The system supports multiple applications that can be selected via the standard ISO7816-4 SELECT command using Application Identifiers (AIDs).

**Last Updated**: December 17, 2025

---

## Table of Contents

1. [Application Architecture](#application-architecture)
2. [PIV Application](#piv-application)
3. [Generic Test Application](#generic-test-application)
4. [Application Selection](#application-selection)
5. [Adding New Applications](#adding-new-applications)

---

## Application Architecture

### Multi-Application Support

The ISO7816 stack supports multiple concurrent applications, each registered under the Master File (MF). Applications are identified by their AID (Application Identifier) and can be selected using the SELECT command (INS=0xA4, P1=0x04).

```
MF (3F00)
├── EF.DIR (2F00)     # Application directory
├── EF.ATR (2F01)     # ATR information
├── PIV DF (5000)     # PIV Application (AID: A0 00 00 03 08...)
│   └── [PIV-specific EFs]
└── TestApp DF (7F01) # Generic Test Application (AID: F0 54 45 53 54...)
    └── [App-specific EFs]
```

### Application Lifecycle

Each application has three key callbacks:

| Callback | Purpose |
|----------|---------|
| `on_select` | Called when application is selected via SELECT command |
| `on_deselect` | Called when a different application is selected |
| `handler` | Processes application-specific commands |

### Registration Flow

```c
// 1. Define application
static const Iso7816AppDefinition my_app = {
    .aid = {0xF0, 0x01, 0x02, 0x03, 0x04},
    .aid_len = 5,
    .name = "MyApp",
    .handler = my_command_handler,
    .on_select = my_on_select,
    .on_deselect = my_on_deselect,
    .context = &my_context,
};

// 2. Register with filesystem
Iso7816File* df = iso7816_fs_register_app(fs, NULL, 0x7F10, &my_app);
```

---

## PIV Application

### Overview

The PIV (Personal Identity Verification) application implements the NIST SP 800-73-5 standard for federal employee and contractor identity verification. This is a real-world application used on U.S. government smart cards.

### Application Identifier

| Field | Value | Description |
|-------|-------|-------------|
| **RID** | `A0 00 00 03 08` | NIST Registered Identifier |
| **PIX** | `00 00 10 00` | PIV Proprietary Identifier Extension |
| **Version** | `01 00` | Version 1.0 |
| **Full AID** | `A0 00 00 03 08 00 00 10 00 01 00` | 11 bytes |

### File Location

```
piv/
├── piv_app.h    # Interface and constants
└── piv_app.c    # Implementation
```

### DF File Identifier

- **FID**: `0x5000`

### Supported Commands

| INS | Command | Status | Description |
|-----|---------|--------|-------------|
| 0xA4 | SELECT | ✅ Inherited | Select PIV application |
| 0xCA | GET DATA | ✅ Inherited | Get data objects |
| 0xCB | GET DATA (odd) | 🔴 Planned | PIV container retrieval |
| 0x20 | VERIFY | ✅ Inherited | PIN verification |
| 0x24 | CHANGE REF DATA | 🔴 Stub | Change PIN |
| 0x2C | RESET RETRY CTR | 🔴 Stub | Unblock PIN with PUK |
| 0x87 | GENERAL AUTH | 🔴 Stub | Cryptographic operations |
| 0x47 | GENERATE KEY | 🔴 Stub | Generate key pair |
| 0xDB | PUT DATA | 🔴 Stub | Store data objects |

### SELECT Response

When PIV is selected, it returns an FCI (File Control Information) template:

```
6F XX                    # FCI Template
   83 02 50 00           # File Identifier (5000)
   84 0B A0 00 00 03 08  # DF Name (AID)
         00 00 10 00 01 00
   82 01 38              # File Descriptor (DF)
   8A 01 05              # Life Cycle Status (activated)
```

### Usage Example

```
# Select PIV application
>> 00 A4 04 00 0B A0 00 00 03 08 00 00 10 00 01 00 00
<< [FCI data] 90 00

# Select with truncated AID (also valid)
>> 00 A4 04 00 09 A0 00 00 03 08 00 00 10 00 00
<< [FCI data] 90 00
```

### Context Structure

```c
typedef struct {
    bool selected;           // Application is currently selected
    bool pin_verified;       // PIV PIN (ref 0x80) has been verified
    uint8_t security_status; // Current security state
    Iso7816FileSystem* fs;   // Reference to filesystem
} PivAppContext;
```

### Future Features (Planned)

- PIV data containers (CCC, CHUID, Discovery Object)
- Certificate storage (PIV Auth, Card Auth, Digital Sig, Key Mgmt)
- PIV PIN management (8-digit, 0xFF padded)
- GENERAL AUTHENTICATE for cryptographic operations

---

## Generic Test Application

### Overview

A simple test application with an arbitrary proprietary AID. Used for:
- Testing multi-application SELECT functionality
- Demonstrating application registration
- Testing proprietary (CLA=0x80) command handling

### Application Identifier

| Field | Value | Description |
|-------|-------|-------------|
| **Category** | `F0` | Proprietary (no registration required) |
| **Identifier** | `54 45 53 54 41 50 50` | ASCII: "TESTAPP" |
| **Full AID** | `F0 54 45 53 54 41 50 50` | 8 bytes |

### File Location

```
apps/
├── generic_app.h    # Interface and constants
└── generic_app.c    # Implementation
```

### DF File Identifier

- **FID**: `0x7F01`

### Supported Commands

#### Standard Commands (CLA=0x00)

Inherited from the ISO7816 default handler:

| INS | Command | Description |
|-----|---------|-------------|
| 0xA4 | SELECT | Select application |
| 0xCA | GET DATA | Get data objects |
| 0x20 | VERIFY | PIN verification |

#### Proprietary Commands (CLA=0x80)

| INS | Command | Description |
|-----|---------|-------------|
| 0x01 | GET APP INFO | Returns app name and command count |
| 0x02 | ECHO | Echoes command data back |
| 0x03 | GET STATUS | Returns 1-byte status |

### Proprietary Command Details

#### GET APP INFO (80 01 00 00 00)

Returns application name and command counter.

**Request:**
```
CLA: 80
INS: 01
P1:  00
P2:  00
Le:  00
```

**Response:**
```
[name_len] [name_bytes...] [count_4_bytes]
```

Example:
```
>> 80 01 00 00 00
<< 0E 47 65 6E 65 72 69 63 54 65 73 74 41 70 70  # "GenericTestApp"
   00 00 00 01                                     # Command count = 1
   90 00
```

#### ECHO (80 02 00 00 Lc Data Le)

Echoes the command data back in the response.

**Request:**
```
CLA: 80
INS: 02
P1:  00
P2:  00
Lc:  [data length]
Data: [bytes to echo]
Le:  [expected length]
```

**Response:**
```
[echoed data] 90 00
```

Example:
```
>> 80 02 00 00 04 CA FE BA BE 04
<< CA FE BA BE 90 00
```

#### GET STATUS (80 03 00 00 01)

Returns a single status byte.

**Response:**
```
[status] 90 00
```

Where status:
- `01` = Application is selected
- `00` = Application is not selected

### Context Structure

```c
typedef struct {
    bool selected;           // Application is currently selected
    uint32_t command_count;  // Number of commands processed
    Iso7816FileSystem* fs;   // Reference to filesystem
} GenericAppContext;
```

### Usage Example

```
# Select Generic Test Application
>> 00 A4 04 00 08 F0 54 45 53 54 41 50 50 00
<< [FCI data] 90 00

# Get app info (proprietary command)
>> 80 01 00 00 00
<< 0E 47 65 6E 65 72 69 63 54 65 73 74 41 70 70 00 00 00 01 90 00

# Echo data
>> 80 02 00 00 03 01 02 03 03
<< 01 02 03 90 00
```

---

## Application Selection

### SELECT Command Format

```
CLA: 00
INS: A4
P1:  04        # Select by DF name (AID)
P2:  00        # First occurrence, return FCI
Lc:  [AID length]
Data: [AID bytes]
Le:  00        # Return all available data
```

### P2 Options

| P2 | Meaning |
|----|---------|
| 0x00 | First occurrence, return FCI |
| 0x04 | First occurrence, return FCP |
| 0x0C | First occurrence, no response data |

### Partial AID Matching

The implementation supports partial AID matching. A truncated AID will match if it's a prefix of a registered AID:

```
# Full PIV AID (11 bytes)
A0 00 00 03 08 00 00 10 00 01 00

# Truncated (9 bytes) - still matches PIV
A0 00 00 03 08 00 00 10 00

# Truncated (5 bytes) - matches PIV (RID only)
A0 00 00 03 08
```

### Application Switching

When selecting a different application:

1. Current app's `on_deselect` callback is invoked
2. New app's DF becomes the current DF
3. New app's `on_select` callback is invoked
4. FCI of new app is returned

---

## Adding New Applications

### Step 1: Create Header File

```c
// apps/my_app.h
#pragma once

#include "../iso7816/iso7816_filesystem.h"

#define MY_APP_AID_BYTES  0xF0, 0x4D, 0x59, 0x41, 0x50, 0x50  // "MYAPP"
#define MY_APP_AID_LEN    6
#define MY_APP_DF_FID     0x7F02

Iso7816File* my_app_init(Iso7816FileSystem* fs);
bool my_app_is_selected(void);
```

### Step 2: Create Implementation

```c
// apps/my_app.c
#include "my_app.h"
#include "../iso7816/iso7816_response.h"

static const uint8_t my_app_aid[] = {MY_APP_AID_BYTES};

static struct {
    bool selected;
} my_context;

static void on_select(void* ctx) {
    my_context.selected = true;
}

static void on_deselect(void* ctx) {
    my_context.selected = false;
}

static void command_handler(void* ctx, const void* cmd, void* resp) {
    // Handle app-specific commands
}

static const Iso7816AppDefinition my_app_def = {
    .aid = {MY_APP_AID_BYTES},
    .aid_len = MY_APP_AID_LEN,
    .name = "MyApp",
    .handler = command_handler,
    .on_select = on_select,
    .on_deselect = on_deselect,
    .context = &my_context,
};

Iso7816File* my_app_init(Iso7816FileSystem* fs) {
    return iso7816_fs_register_app(fs, NULL, MY_APP_DF_FID, &my_app_def);
}
```

### Step 3: Register in Main App

```c
// ccid_test_app.c
#include "apps/my_app.h"

CcidTestApp* ccid_test_app_alloc(void) {
    // ... existing code ...
    
    Iso7816FileSystem* fs = iso7816_handler_get_filesystem();
    
    // Register your new app
    my_app_init(fs);
    
    // ... rest of code ...
}
```

### Step 4: Build and Test

```bash
./fbt fap_ccid_test
```

---

## Test Script

A Python test script is provided to verify application selection:

```bash
cd applications/debug/ccid_test/client
pip install pyscard
python app_select_test.py
```

### Test Cases

| Test | Description |
|------|-------------|
| SELECT PIV (full AID) | Select PIV with 11-byte AID |
| SELECT PIV (truncated) | Select PIV with 9-byte AID |
| SELECT Generic | Select test app by AID |
| SELECT Non-existent | Verify 6A82 for unknown AID |
| Generic App Command | Test proprietary command after SELECT |
| App Switching | Switch between PIV and Generic |

---

## Quick Reference

### AIDs

| Application | AID | Length |
|-------------|-----|--------|
| PIV | `A0 00 00 03 08 00 00 10 00 01 00` | 11 |
| Generic Test | `F0 54 45 53 54 41 50 50` | 8 |

### SELECT APDUs

```
# Select PIV
00 A4 04 00 0B A0 00 00 03 08 00 00 10 00 01 00 00

# Select Generic Test App
00 A4 04 00 08 F0 54 45 53 54 41 50 50 00

# Select MF (deselect all apps)
00 A4 00 00 02 3F 00 00
```

### Status Words

| SW | Meaning |
|----|---------|
| 90 00 | Success |
| 6A 82 | Application/file not found |
| 6A 86 | Wrong P1-P2 |
| 62 83 | Selected file deactivated |

---

## References

- ISO/IEC 7816-4:2005 - Organization, security and commands for interchange
- ISO/IEC 7816-5:2004 - Registration of application providers
- NIST SP 800-73-5 - PIV Card Application Specification
- [PIV Implementation Guide](./PIV_IMPLEMENTATION_GUIDE.md)
- [ISO7816 Implementation Guide](./ISO7816_IMPLEMENTATION_GUIDE.md)
