# ISO/IEC 7816-4 Implementation Guide

## Overview

This document provides detailed implementation guidance for ISO/IEC 7816-4:2005 smart card commands. The standard specifies organization, security, and commands for interchange with integrated circuit cards.

**Reference Standard**: ISO/IEC 7816-4:2005 - Identification cards — Integrated circuit cards — Part 4: Organization, security and commands for interchange

**Last Updated**: December 14, 2025

---

## Table of Contents

1. [Command-Response Pair Structure](#command-response-pair-structure)
2. [Class Byte (CLA) Encoding](#class-byte-cla-encoding)
3. [Instruction Byte (INS) Reference](#instruction-byte-ins-reference)
4. [Status Words (SW1-SW2)](#status-words-sw1-sw2)
5. [Commands for Interchange](#commands-for-interchange)
   - [SELECT](#select-command-0xa4)
   - [READ BINARY](#read-binary-command-0xb0)
   - [READ RECORD](#read-record-command-0xb2)
   - [GET DATA](#get-data-command-0xca)
   - [GET RESPONSE](#get-response-command-0xc0)
   - [VERIFY](#verify-command-0x20)
   - [GET CHALLENGE](#get-challenge-command-0x84)
6. [Data Structures](#data-structures)
   - [File Structure Hierarchy](#file-structure-hierarchy)
   - [File Control Information](#file-control-information-templates-table-11)
   - [Application Identifier (AID)](#application-identifier-aid-structure)
7. [Implementation Priorities](#implementation-priorities)

---

## Command-Response Pair Structure

### APDU Format (Table 1 - ISO 7816-4)

| Field | Description | Number of bytes |
|-------|-------------|-----------------|
| **Command header** | | |
| CLA | Class byte | 1 |
| INS | Instruction byte | 1 |
| P1-P2 | Parameter bytes | 2 |
| **Lc field** | Encodes Nc (data length) | 0, 1, or 3 |
| **Command data field** | Nc bytes of data | Nc |
| **Le field** | Encodes Ne (expected response) | 0, 1, 2, or 3 |
| **Response data field** | Nr bytes of response | Nr (at most Ne) |
| **Response trailer** | Status bytes SW1-SW2 | 2 |

### Short vs Extended Length Fields

**Short Lc field** (1 byte):
- From '01' to 'FF': encodes Nc from 1 to 255

**Extended Lc field** (3 bytes):
- First byte set to '00', followed by two bytes
- From '0001' to 'FFFF': encodes Nc from 1 to 65,535

**Short Le field** (1 byte):
- From '01' to 'FF': encodes Ne from 1 to 255
- '00': encodes Ne = 256

**Extended Le field** (2 or 3 bytes):
- If Lc field is absent: 3 bytes ('00' + 2 bytes)
- If extended Lc field is present: 2 bytes
- From '0001' to 'FFFF': encodes Ne from 1 to 65,535
- '0000': encodes Ne = 65,536

---

## Class Byte (CLA) Encoding

### First Interindustry Values (000x xxxx) - Table 2

| b8 | b7 | b6 | b5 | b4 | b3 | b2 | b1 | Meaning |
|----|----|----|----|----|----|----|----|----|
| 0 | 0 | 0 | x | - | - | - | - | Command chaining control |
| 0 | 0 | 0 | 0 | - | - | - | - | — Last or only command of a chain |
| 0 | 0 | 0 | 1 | - | - | - | - | — Not the last command of a chain |
| 0 | 0 | 0 | - | x | x | - | - | Secure messaging indication |
| 0 | 0 | 0 | - | 0 | 0 | - | - | — No SM or no indication |
| 0 | 0 | 0 | - | 0 | 1 | - | - | — Proprietary SM format |
| 0 | 0 | 0 | - | 1 | 0 | - | - | — SM according to section 6, header not authenticated |
| 0 | 0 | 0 | - | 1 | 1 | - | - | — SM according to section 6, header authenticated |
| 0 | 0 | 0 | - | - | - | x | x | Logical channel number (0-3) |

### Common CLA Values

| CLA | Meaning |
|-----|---------|
| `0x00` | Interindustry, no SM, no chaining, channel 0 |
| `0x01` | Interindustry, no SM, no chaining, channel 1 |
| `0x0C` | Interindustry, SM (header authenticated), channel 0 |
| `0x10` | Interindustry, command chaining (not last), channel 0 |

**Note**: CLA = 'FF' is invalid per ISO/IEC 7816-3.

---

## Instruction Byte (INS) Reference

### Commands in Alphabetical Order (Table 4.1)

| Command Name | INS | Section |
|--------------|-----|---------|
| APPEND RECORD | 'E2' | 7.3.7 |
| CHANGE REFERENCE DATA | '24' | 7.5.7 |
| ERASE BINARY | '0E', '0F' | 7.2.7 |
| ERASE RECORD(S) | '0C' | 7.3.8 |
| EXTERNAL AUTHENTICATE | '82' | 7.5.4 |
| GENERAL AUTHENTICATE | '86', '87' | 7.5.5 |
| GET CHALLENGE | '84' | 7.5.3 |
| GET DATA | 'CA', 'CB' | 7.4.2 |
| GET RESPONSE | 'C0' | 7.6.1 |
| INTERNAL AUTHENTICATE | '88' | 7.5.2 |
| MANAGE CHANNEL | '70' | 7.1.2 |
| PUT DATA | 'DA', 'DB' | 7.4.3 |
| READ BINARY | 'B0', 'B1' | 7.2.3 |
| READ RECORD(S) | 'B2', 'B3' | 7.3.3 |
| RESET RETRY COUNTER | '2C' | 7.5.10 |
| SEARCH BINARY | 'A0', 'A1' | 7.2.6 |
| SEARCH RECORD | 'A2' | 7.3.7 |
| SELECT | 'A4' | 7.1.1 |
| UPDATE BINARY | 'D6', 'D7' | 7.2.5 |
| UPDATE RECORD | 'DC', 'DD' | 7.3.5 |
| VERIFY | '20', '21' | 7.5.6 |
| WRITE BINARY | 'D0', 'D1' | 7.2.4 |
| WRITE RECORD | 'D2' | 7.3.4 |

**Note**: INS values '6X' and '9X' are invalid per ISO/IEC 7816-3.

---

## Status Words (SW1-SW2)

### Structural Scheme (Figure 1 - ISO 7816-4)

```
                           SW1-SW2
                              │
              ┌───────────────┴───────────────┐
         Process completed              Process aborted
              │                               │
      ┌───────┴───────┐               ┌───────┴───────┐
   Normal         Warning          Execution      Checking
 processing     processing           error          error
 '9000','61XX' '62XX','63XX'     '64XX'-'66XX'  '67XX'-'6FXX'
```

### Normal Processing

| SW1-SW2 | Meaning |
|---------|---------|
| `9000` | No further qualification (success) |
| `61XX` | SW2 encodes number of data bytes still available (use GET RESPONSE) |

### Warning Processing (Table 6)

| SW1 | SW2 | Meaning |
|-----|-----|---------|
| `62` | `00` | No information given |
| `62` | `81` | Part of returned data may be corrupted |
| `62` | `82` | End of file/record reached before reading Ne bytes |
| `62` | `83` | Selected file deactivated |
| `62` | `84` | File control information not formatted correctly |
| `62` | `85` | Selected file in termination state |
| `63` | `00` | No information given |
| `63` | `81` | File filled up by the last write |
| `63` | `CX` | Counter from 0-15 (e.g., remaining PIN retries) |

### Execution Errors (Table 6)

| SW1 | SW2 | Meaning |
|-----|-----|---------|
| `64` | `00` | Execution error |
| `64` | `01` | Immediate response required by the card |
| `65` | `00` | No information given |
| `65` | `81` | Memory failure |

### Checking Errors (Table 5 & 6)

| SW1 | SW2 | Meaning |
|-----|-----|---------|
| `67` | `00` | Wrong length; no further indication |
| `68` | `00` | No information given |
| `68` | `81` | Logical channel not supported |
| `68` | `82` | Secure messaging not supported |
| `68` | `83` | Last command of the chain expected |
| `68` | `84` | Command chaining not supported |
| `69` | `00` | No information given |
| `69` | `81` | Command incompatible with file structure |
| `69` | `82` | Security status not satisfied |
| `69` | `83` | Authentication method blocked |
| `69` | `84` | Reference data not usable |
| `69` | `85` | Conditions of use not satisfied |
| `69` | `86` | Command not allowed (no current EF) |
| `69` | `87` | Expected secure messaging data objects missing |
| `69` | `88` | Incorrect secure messaging data objects |
| `6A` | `00` | No information given |
| `6A` | `80` | Incorrect parameters in the command data field |
| `6A` | `81` | Function not supported |
| `6A` | `82` | File or application not found |
| `6A` | `83` | Record not found |
| `6A` | `84` | Not enough memory space in the file |
| `6A` | `85` | Nc inconsistent with TLV structure |
| `6A` | `86` | Incorrect parameters P1-P2 |
| `6A` | `87` | Nc inconsistent with parameters P1-P2 |
| `6A` | `88` | Referenced data or reference data not found |
| `6A` | `89` | File already exists |
| `6A` | `8A` | DF name already exists |
| `6B` | `00` | Wrong parameters P1-P2 |
| `6C` | `XX` | Wrong Le field; SW2 encodes exact number of available data bytes |
| `6D` | `00` | Instruction code not supported or invalid |
| `6E` | `00` | Class not supported |
| `6F` | `00` | No precise diagnosis |

---

## Commands for Interchange

### SELECT Command (0xA4)

**Purpose**: Select a file (DF, EF, or MF) or application by name, path, or file identifier. Opens a logical channel if not yet open.

#### Command-Response Pair (Table 38)

| Field | Value | Description |
|-------|-------|-------------|
| CLA | As defined in 5.1.1 | Class byte |
| INS | 'A4' | SELECT instruction |
| P1 | See Table 39 | Selection control |
| P2 | See Table 40 | Selection options and response control |
| Lc | Present for Nc > 0 | Length of data field |
| Data | File ID, path, or DF name | Depends on P1 |
| Le | Present for Ne > 0 | Expected response length |
| Data (response) | File control information | Absent or FCI/FCP/FMD |
| SW1-SW2 | See Tables 5 and 6 | Status |

#### P1 - Selection Control (Table 39)

| b8 | b7 | b6 | b5 | b4 | b3 | b2 | b1 | Meaning | Command Data |
|----|----|----|----|----|----|----|----|---------|--------------|
| 0 | 0 | 0 | 0 | 0 | 0 | x | x | Selection by file identifier | |
| 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | Select MF, DF, or EF | File identifier or absent |
| 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | Select child DF | DF identifier |
| 0 | 0 | 0 | 0 | 0 | 0 | 1 | 0 | Select EF under current DF | EF identifier |
| 0 | 0 | 0 | 0 | 0 | 0 | 1 | 1 | Select parent DF of current DF | Absent |
| 0 | 0 | 0 | 0 | 0 | 1 | x | x | Selection by DF name | |
| 0 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | Select by DF name | [truncated] AID |
| 0 | 0 | 0 | 0 | 1 | 0 | x | x | Selection by path | |
| 0 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | Select from the MF | Path without MF identifier |
| 0 | 0 | 0 | 0 | 1 | 0 | 0 | 1 | Select from current DF | Path without current DF identifier |

#### P2 - Selection Options (Table 40)

| b8 | b7 | b6 | b5 | b4 | b3 | b2 | b1 | Meaning |
|----|----|----|----|----|----|----|----|---------|
| 0 | 0 | 0 | 0 | - | - | x | x | File occurrence |
| 0 | 0 | 0 | 0 | - | - | 0 | 0 | — First or only occurrence |
| 0 | 0 | 0 | 0 | - | - | 0 | 1 | — Last occurrence |
| 0 | 0 | 0 | 0 | - | - | 1 | 0 | — Next occurrence |
| 0 | 0 | 0 | 0 | - | - | 1 | 1 | — Previous occurrence |
| 0 | 0 | 0 | 0 | x | x | - | - | File control information |
| 0 | 0 | 0 | 0 | 0 | 0 | - | - | — Return FCI template, optional use of FCI tag |
| 0 | 0 | 0 | 0 | 0 | 1 | - | - | — Return FCP template, mandatory FCP tag |
| 0 | 0 | 0 | 0 | 1 | 0 | - | - | — Return FMD template, mandatory FMD tag |
| 0 | 0 | 0 | 0 | 1 | 1 | - | - | — No response data if Le absent, proprietary if present |

#### Implementation Example

```c
// SELECT by AID (application identifier)
// Command: 00 A4 04 00 07 A0000000031010 00
// P1=04: Select by DF name (AID)
// P2=00: First occurrence, return FCI
// Data: A0000000031010 (Visa AID example)
// Le=00: Return all available data

typedef struct {
    uint8_t cla;     // 0x00
    uint8_t ins;     // 0xA4
    uint8_t p1;      // Selection control
    uint8_t p2;      // Selection options
    uint8_t lc;      // Length of AID
    uint8_t* data;   // AID or file identifier
    uint8_t le;      // Expected response length
} SelectCommand;

// Response: File Control Information (FCI) + SW1-SW2
```

#### Common Use Cases

| P1 | P2 | Data | Description |
|----|----|----- |-------------|
| 00 | 00 | 3F00 | Select MF (Master File) |
| 00 | 00 | 7F10 | Select DF by file ID |
| 02 | 00 | - | Select EF under current DF |
| 03 | 00 | - | Select parent DF |
| 04 | 00 | AID | Select application by AID |
| 04 | 02 | AID | Select next application matching AID |

---

### READ BINARY Command (0xB0)

**Purpose**: Read [part of] the content of an EF with transparent structure.

#### Command-Response Pair (Table 42)

| Field | Value | Description |
|-------|-------|-------------|
| CLA | As defined in 5.1.1 | Class byte |
| INS | 'B0' or 'B1' | READ BINARY instruction |
| P1-P2 | See 7.2.2 | Offset or EF + offset |
| Lc | Absent (INS='B0') or Present (INS='B1') | Data present for INS='B1' |
| Data | Absent (INS='B0') or offset data object | Offset in discretionary DO |
| Le | Present for Ne > 0 | Number of bytes to read |
| Data (response) | Data read | File content |
| SW1-SW2 | Status | |

#### P1-P2 Encoding (Section 7.2.2)

**When INS = 'B0'**:
- If bit 1 of INS is 0 and bit 8 of P1 is 0:
  - P1-P2 (15 bits) encodes offset from 0 to 32,767

- If bit 1 of INS is 0 and bit 8 of P1 is 1:
  - Bits 5-1 of P1 encode short EF identifier (1-30)
  - P2 encodes offset from 0 to 255

**When INS = 'B1'** (odd INS, uses offset data object):
- P1-P2 shall identify an EF via short EF identifier or file identifier
- Data field contains offset data object (tag '54')

#### Implementation Example

```c
// READ BINARY from current EF, offset 0, read 16 bytes
// Command: 00 B0 00 00 10
// P1-P2=0000: Offset 0 from current EF
// Le=10: Read 16 bytes

// READ BINARY from EF with short ID 5, offset 10
// Command: 00 B0 85 0A 10
// P1=85: b8=1, bits 5-1 = 00101 = EF short ID 5
// P2=0A: Offset 10
// Le=10: Read 16 bytes

typedef struct {
    uint8_t cla;     // 0x00
    uint8_t ins;     // 0xB0 or 0xB1
    uint8_t p1;      // High byte of offset or 0x80|EF_ID
    uint8_t p2;      // Low byte of offset
    uint8_t le;      // Number of bytes to read
} ReadBinaryCommand;
```

#### Response

- If Le='00': Read until end of file or 256 bytes (short) / 65536 bytes (extended)
- Returns data and SW1-SW2

#### Status Words

| SW1-SW2 | Meaning |
|---------|---------|
| 9000 | Success |
| 6281 | Part of returned data may be corrupted |
| 6282 | End of file reached before reading Ne bytes |
| 6700 | Wrong length |
| 6981 | Command incompatible with file structure |
| 6982 | Security status not satisfied |
| 6A82 | File not found |
| 6B00 | Offset outside the EF |
| 6Cxx | Wrong Le; xx = available bytes |

---

### READ RECORD Command (0xB2)

**Purpose**: Read [partial] contents of specified record(s) from an EF with record structure.

#### Command-Response Pair (Table 48)

| Field | Value | Description |
|-------|-------|-------------|
| CLA | As defined in 5.1.1 | Class byte |
| INS | 'B2' or 'B3' | READ RECORD instruction |
| P1 | Record number or identifier | '00' references current record |
| P2 | See Table 49 | Record selection options |
| Lc | Absent (INS='B2') or Present (INS='B3') | Offset data for partial read |
| Data | Absent or offset data object | For INS='B3' |
| Le | Present for Ne > 0 | Expected response length |
| Data (response) | Record data | Record content(s) |
| SW1-SW2 | Status | |

#### P2 Encoding (Table 49)

| b8 | b7 | b6 | b5 | b4 | b3 | b2 | b1 | Meaning |
|----|----|----|----|----|----|----|----|---------|
| x | x | x | x | x | - | - | - | Short EF identifier (bits 8-4) according to Table 47 |
| - | - | - | - | - | 0 | x | x | Record identifier in P1 |
| - | - | - | - | - | 0 | 0 | 0 | — Read first occurrence |
| - | - | - | - | - | 0 | 0 | 1 | — Read last occurrence |
| - | - | - | - | - | 0 | 1 | 0 | — Read next occurrence |
| - | - | - | - | - | 0 | 1 | 1 | — Read previous occurrence |
| - | - | - | - | - | 1 | x | x | Record number in P1 |
| - | - | - | - | - | 1 | 0 | 0 | — Read record P1 |
| - | - | - | - | - | 1 | 0 | 1 | — Read all records from P1 up to the last |
| - | - | - | - | - | 1 | 1 | 0 | — Read all records from the last up to P1 |
| - | - | - | - | - | 1 | 1 | 1 | — Reserved for future use |

#### Short EF Identifier in P2 (Table 47)

| b8 | b7 | b6 | b5 | b4 | Meaning |
|----|----|----|----|----|---------|
| 0 | 0 | 0 | 0 | 0 | Current EF |
| Not all equal | | | | | Short EF identifier (1 to 30) |
| 1 | 1 | 1 | 1 | 1 | Reserved for future use |

#### Implementation Example

```c
// READ RECORD 1 from current EF
// Command: 00 B2 01 04 00
// P1=01: Record number 1
// P2=04: Current EF (bits 8-4=00000), record number in P1 (bits 3-1=100)
// Le=00: Read complete record

// READ RECORD 3 from EF with short ID 5
// Command: 00 B2 03 2C 00
// P1=03: Record number 3
// P2=2C: EF short ID 5 (00101 << 3 = 0x28) | 0x04 (record number mode)

typedef struct {
    uint8_t cla;     // 0x00
    uint8_t ins;     // 0xB2 or 0xB3
    uint8_t p1;      // Record number or identifier
    uint8_t p2;      // Short EF ID (bits 8-4) + mode (bits 3-1)
    uint8_t le;      // Expected length
} ReadRecordCommand;
```

#### Response Format (Table 50)

For SIMPLE-TLV records (INS='B2'):

| Case | Condition | Response Format |
|------|-----------|-----------------|
| a | Partial read of one record (Le ≠ '00') | Tn, Ln, first bytes of Vn |
| b | Complete read of one record (Le = '00') | Tn, Ln, all bytes of Vn |
| c | Partial read of sequence (Le ≠ '00') | Tn-Ln-Vn, ..., Tn+m-Ln+m-first bytes |
| d | Read several records up to file end | Tn-Ln-Vn, ..., Tn+m-Ln+m-Vn+m |

---

### GET DATA Command (0xCA)

**Purpose**: Retrieve content of an EF with TLV structure, or one data object within the current context.

#### Command-Response Pair (Table 63)

| Field | Value | Description |
|-------|-------|-------------|
| CLA | As defined in 5.1.1 | Class byte |
| INS | 'CA' or 'CB' | GET DATA instruction |
| P1-P2 | See Table 62 | Tag or file identifier |
| Lc | Absent (INS='CA') or Present (INS='CB') | |
| Data | Absent (INS='CA') or tag list/header list | For INS='CB' |
| Le | Present for Ne > 0 | Expected response length |
| Data (response) | Data objects | According to P1-P2 |
| SW1-SW2 | Status | |

#### P1-P2 Encoding (Table 62)

| Condition | Value of P1-P2 | Meaning |
|-----------|----------------|---------|
| Even INS code | '0000' | Dumping a file, or card-originated byte strings |
| Even INS code | '0040' to '00FF' | BER-TLV tag (one byte) in P2 |
| Even INS code | '0100' to '01FF' | Proprietary |
| Even INS code | '0200' to '02FF' | SIMPLE-TLV tag in P2 |
| Even INS code | '4000' to 'FFFF' | BER-TLV tag (two bytes) in P1-P2 |
| Odd INS code | Any value | File identifier or short EF identifier |

#### Implementation Example

```c
// GET DATA for ATR information (tag '5F51')
// Command: 00 CA 5F 51 00
// P1-P2=5F51: Two-byte BER-TLV tag
// Le=00: Return all available data

// GET DATA for historical bytes (tag '5F52')
// Command: 00 CA 5F 52 00

// GET DATA with tag list (INS='CB')
// Command: 00 CB 00 00 03 5C 01 4F 00
// Data: Tag list (5C 01 4F) requesting AID tag
// Le=00: Return matching data objects

typedef struct {
    uint8_t cla;     // 0x00
    uint8_t ins;     // 0xCA or 0xCB
    uint8_t p1;      // High byte of tag
    uint8_t p2;      // Low byte of tag
    uint8_t le;      // Expected response length
} GetDataCommand;
```

#### Common Tags

| P1-P2 (Tag) | Meaning |
|-------------|---------|
| 5F51 | Answer-to-Reset (ATR) |
| 5F52 | Historical bytes |
| 00FF | All common BER-TLV data objects |
| 02FF | All common SIMPLE-TLV data objects |

---

### GET RESPONSE Command (0xC0)

**Purpose**: Retrieve [part of] response APDUs that could not be transmitted by the available transmission protocol. Used after receiving SW1='61'.

#### Command-Response Pair (Table 81)

| Field | Value | Description |
|-------|-------|-------------|
| CLA | As defined in 5.1.1 | Class byte |
| INS | 'C0' | GET RESPONSE instruction |
| P1-P2 | '0000' | Any other value is reserved |
| Lc | Absent | No command data |
| Data | Absent | |
| Le | Present for Ne > 0 | Expected length from SW2 of previous response |
| Data (response) | Remaining data | Part of previous response APDU |
| SW1-SW2 | Status | |

#### Implementation Example

```c
// Typical flow:
// 1. Send command, receive SW1-SW2 = 61 XX (XX bytes still available)
// 2. Send GET RESPONSE with Le = XX
// Command: 00 C0 00 00 XX

typedef struct {
    uint8_t cla;     // 0x00
    uint8_t ins;     // 0xC0
    uint8_t p1;      // 0x00
    uint8_t p2;      // 0x00
    uint8_t le;      // Number of bytes to retrieve (from previous SW2)
} GetResponseCommand;

// Example flow:
// Command: 00 A4 04 00 07 A0000000031010 00  (SELECT AID)
// Response: 61 20  (32 bytes available)
// Command: 00 C0 00 00 20  (GET RESPONSE, 32 bytes)
// Response: [32 bytes of FCI] 90 00
```

#### Usage

- Called when previous command returned SW1='61' with SW2 indicating bytes available
- Le field should match SW2 from previous response, or request partial data
- If Le='00', returns within limit of 256 (short) or 65,536 (extended)

---

### VERIFY Command (0x20)

**Purpose**: Initiate comparison of verification data (e.g., password, PIN) sent from the interface device with reference data stored in the card.

#### Command-Response Pair (Table 72)

| Field | Value | Description |
|-------|-------|-------------|
| CLA | As defined in 5.1.1 | Class byte |
| INS | '20' or '21' | VERIFY instruction |
| P1 | '00' | Any other value is reserved |
| P2 | See Table 65 | Reference data qualifier |
| Lc | Absent for Nc = 0, present for Nc > 0 | |
| Data | Verification data or absent | PIN/password |
| Le | Absent | |
| Data (response) | Absent | |
| SW1-SW2 | Status | |

#### P2 - Reference Data Qualifier (Table 65)

| b8 | b7 | b6 | b5 | b4 | b3 | b2 | b1 | Meaning |
|----|----|----|----|----|----|----|----|---------|
| 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | No information given |
| 0 | - | - | - | - | - | - | - | Global reference data (e.g., MF specific password) |
| 1 | - | - | - | - | - | - | - | Specific reference data (e.g., DF specific password) |
| - | x | x | - | - | - | - | - | 00 (any other value is reserved for future use) |
| - | - | - | x | x | x | x | x | Qualifier (number of the reference data or key) |

#### INS Variants

- **INS = '20'**: Verification data (PIN) present in command data field
  - Absence of command data field is used to check retries remaining
  - SW1-SW2 = '63CX' where X = remaining retries, or '9000' if no limit
  
- **INS = '21'**: Verification data object with extended header list (biometric)
  - Empty verification data object = sensor on card

#### Implementation Example

```c
// VERIFY PIN (global, reference 1)
// Command: 00 20 00 01 08 31 32 33 34 FF FF FF FF
// P2=01: Global reference data, qualifier 1
// Data: PIN "1234" padded to 8 bytes with 0xFF

// Check remaining retries (no data)
// Command: 00 20 00 01
// Response: 63 C2 (2 retries remaining) or 6983 (blocked)

typedef struct {
    uint8_t cla;     // 0x00
    uint8_t ins;     // 0x20
    uint8_t p1;      // 0x00
    uint8_t p2;      // Reference data qualifier
    uint8_t lc;      // Length of PIN data
    uint8_t* data;   // PIN data (often padded)
} VerifyCommand;
```

#### Status Words

| SW1-SW2 | Meaning |
|---------|---------|
| 9000 | Verification successful |
| 6300 | Verification failed, no information given |
| 63CX | Verification failed, X retries remaining |
| 6581 | Memory failure |
| 6700 | Wrong length |
| 6982 | Security status not satisfied |
| 6983 | Authentication method blocked |
| 6984 | Reference data not usable |
| 6A80 | Incorrect parameters in command data field |
| 6A86 | Incorrect parameters P1-P2 |
| 6A88 | Reference data not found |

---

### GET CHALLENGE Command (0x84)

**Purpose**: Request a random number (challenge) for use in a security-related procedure (e.g., EXTERNAL AUTHENTICATE).

#### Command-Response Pair (Table 67)

| Field | Value | Description |
|-------|-------|-------------|
| CLA | As defined in 5.1.1 | Class byte |
| INS | '84' | GET CHALLENGE instruction |
| P1 | See 7.5.1 | Algorithm reference (optional) |
| P2 | '00' | Any other value is reserved |
| Lc | Absent | No command data |
| Data | Absent | |
| Le | Present for Ne > 0 | Length of challenge requested |
| Data (response) | Challenge | Random number |
| SW1-SW2 | Status | |

#### Implementation Example

```c
// GET CHALLENGE, request 8 bytes
// Command: 00 84 00 00 08
// Response: [8 random bytes] 90 00

typedef struct {
    uint8_t cla;     // 0x00
    uint8_t ins;     // 0x84
    uint8_t p1;      // 0x00 (or algorithm reference)
    uint8_t p2;      // 0x00
    uint8_t le;      // Length of challenge (e.g., 8)
} GetChallengeCommand;
```

#### Usage

- Challenge is valid at least for the next command
- Typically used before EXTERNAL AUTHENTICATE
- The card generates a random number and stores it internally
- Authentication commands use this challenge for cryptographic verification

#### Status Words

| SW1-SW2 | Meaning |
|---------|---------|
| 9000 | Success |
| 6300 | Verification failed (for 63CX see 7.5.1) |
| 6581 | Memory failure |
| 6700 | Wrong length |
| 6982 | Security status not satisfied |
| 6983 | Authentication method blocked |
| 6984 | Reference data not usable |
| 6A81 | Function not supported |
| 6A82 | File or application not found |
| 6A86 | Incorrect parameters P1-P2 |
| 6A88 | Reference data not found |

---

## Data Structures

### File Structure Hierarchy

ISO 7816-4 defines a hierarchical file system structure for smart cards. Two categories of structures are supported:

- **Dedicated File (DF)**: A container that hosts applications and/or group files and/or store data objects. A DF may be the parent of other files (DFs or EFs).
- **Elementary File (EF)**: Stores data. An EF cannot be the parent of another file.

Two types of EFs are defined:
- **Internal EF**: Stores data interpreted by the card (e.g., keys, PINs)
- **Working EF**: Stores data used by the outside world exclusively

#### Hierarchical Organization (with MF)

The Master File (MF) is the root DF. Any DF may be an Application DF, with or without its own hierarchy:

```
                                    ┌─────┐
                                    │ MF  │ (Master File - Root DF)
                                    │3F00 │
                                    └──┬──┘
           ┌──────────┬───────────────┼───────────────┬──────────┐
           │          │               │               │          │
        ┌──┴──┐    ┌──┴──┐         ┌──┴──┐         ┌──┴──┐    ┌──┴──┐
        │ EF  │    │ DF  │         │ EF  │         │ DF  │    │App  │
        │     │    │     │         │     │         │     │    │ DF  │
        └─────┘    └──┬──┘         └─────┘         └──┬──┘    └──┬──┘
                      │                               │          │
              ┌───────┼───────┐               ┌───────┤       ┌──┴──┐
              │       │       │               │       │       │ DF  │
           ┌──┴──┐ ┌──┴──┐ ┌──┴──┐         ┌──┴──┐ ┌──┴──┐    └──┬──┘
           │ EF  │ │ EF  │ │ DF  │         │ DF  │ │ EF  │       │
           │     │ │     │ │     │         │     │ │     │    ┌──┴──┐
           └─────┘ └─────┘ └─────┘         └──┬──┘ └─────┘    │ EF  │
                                              │               └─────┘
                      Files of              ┌─┴─┐
                      an application        │EF │
                                            └───┘
```

#### Independent Application DFs (without visible MF)

Some cards organize applications in parallel, with no MF seen at the interface. Each Application DF may have its own hierarchy with its corresponding security architecture:

```
    ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐
    │ Application │  │ Application │  │   Files of  │  │ Application │
    │     DF      │  │     DF      │  │     an      │  │     DF      │
    │             │  │             │  │ application │  │             │
    └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘
```

#### Structure Selection Methods

When a structure cannot be implicitly selected, it shall be selected explicitly using one of the following methods:

| Method | Description |
|--------|-------------|
| **By DF name** | A string of up to 16 bytes. An AID may be used as DF name. |
| **By file identifier** | 2 bytes. `3F00` = MF, `FFFF` = reserved, `3FFF` = reserved, `0000` = reserved |
| **By path** | Concatenation of file identifiers (absolute from MF, or relative from current DF) |
| **By short EF identifier** | 5 bits (values 1-30). Used within P1 or P2 of commands. |

#### Reserved File Identifiers

| File ID | Meaning |
|---------|---------|
| `3F00` | Master File (MF) |
| `3FFF` | Reserved for current DF (when DF is not known) |
| `FFFF` | Reserved for future use |
| `0000` | Reserved (see GET DATA command) |
| `2F00` | EF.DIR (directory of applications) at MF level |
| `2F01` | EF.ATR (Answer-to-Reset data) at MF level |

#### Example: Typical Card Structure

```
MF (3F00)
├── EF.DIR (2F00)           # List of applications
├── EF.ATR (2F01)           # Card operating characteristics
│
├── DF.Telecom (7F10)       # Telecom application DF
│   ├── EF.ADN              # Abbreviated dialing numbers
│   ├── EF.FDN              # Fixed dialing numbers
│   └── EF.SMS              # Short messages
│
├── DF.GSM (7F20)           # GSM application DF  
│   ├── EF.IMSI             # International Mobile Subscriber Identity
│   ├── EF.KC               # Ciphering key
│   └── EF.LOCI             # Location information
│
└── AID: A0000000031010     # Visa Application DF (selected by AID)
    ├── EF.PAN              # Primary Account Number
    └── EF.Keys             # Cryptographic keys
```

### File Control Information Templates (Table 11)

| Tag | Value |
|-----|-------|
| '62' | FCP template - Set of file control parameters |
| '64' | FMD template - Set of file management data |
| '6F' | FCI template - Set of FCP and FMD |

### File Control Parameter Data Objects (Table 12)

| Tag | Length | Value | Applies to |
|-----|--------|-------|------------|
| '80' | Var. | Number of data bytes (excluding structural info) | Any EF, Once |
| '81' | 2 | Number of data bytes (including structural info) | Any file, Once |
| '82' | 1-6 | File descriptor byte (and data coding byte) | Any file |
| '83' | 2 | File identifier | Any file |
| '84' | up to 16 | DF name | Any DF |
| '85' | Var. | Proprietary information not encoded in BER-TLV | Any file |
| '86' | Var. | Security attribute in proprietary format | Any file |
| '88' | 0 or 1 | Short EF identifier | Any EF, Once |
| '8A' | 1 | Life cycle status byte (LCS byte) | Any file, Once |

### Life Cycle Status Byte (Table 13)

| Value | Meaning |
|-------|---------|
| 00 | No information given |
| 01 | Creation state |
| 03 | Initialisation state |
| 05 | Operational state (activated) |
| 04 | Operational state (deactivated) |
| 0C | Termination state |
| 10-FF | Proprietary |

### Application Identifier (AID) Structure

An AID consists of up to 16 bytes:
- Bits 8-5 of first byte indicate category:
  - '0'-'9': Issuer identification number (deprecated, see Annex D)
  - 'A': International RID (registered by ISO/IEC 7816-5)
  - 'D': National RID
  - 'E': Standard identification
  - 'F': Proprietary (no registration)

**Structure**:
```
|<-------- RID (5 bytes) -------->|<-- PIX (up to 11 bytes) -->|
```

---

## Implementation Priorities

### 🔴 High Priority (Required for Basic Functionality)

1. **SELECT (0xA4)**
   - Required to select applications and files
   - Minimum: Support P1=04 (select by DF name/AID)
   - Return basic FCI with status
   
2. **GET RESPONSE (0xC0)**
   - Required when response > transmission limit
   - Simple implementation: buffer remaining data, return on request

3. **Response Status Codes**
   - Add all error codes from Table 5 and Table 6
   - Especially: 6A82 (file not found), 6982 (security not satisfied)

### 🟡 Medium Priority (Common Card Features)

4. **READ BINARY (0xB0)**
   - Required for transparent EF access
   - Support offset addressing

5. **READ RECORD (0xB2)**
   - Required for record-oriented EF access
   - Support record number mode

6. **GET DATA (0xCA)**
   - Used to retrieve ATR, historical bytes, data objects
   - Support common tags

7. **VERIFY (0x20)**
   - Required for PIN verification
   - Track retry counter

### 🟢 Low Priority (Advanced Features)

8. **GET CHALLENGE (0x84)**
   - Required for external authentication
   - Needs random number generator

9. **File System Emulation**
   - Support for MF, DF, EF structures
   - File control information management

10. **Secure Messaging**
    - SM data object parsing
    - Cryptographic checksum support

---

## References

- ISO/IEC 7816-3:2006 - Cards with contacts: Electrical interface and transmission protocols
- ISO/IEC 7816-4:2005 - Organization, security and commands for interchange
- ISO/IEC 7816-5:2004 - Registration of application providers
- ISO/IEC 7816-6:2004 - Interindustry data elements for interchange
- ISO/IEC 7816-8:2004 - Commands for security operations
- ISO/IEC 7816-9:2004 - Commands for card management
