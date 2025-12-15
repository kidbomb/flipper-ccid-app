# Secure Key Storage Guide for Flipper Zero ISO7816 Applications

## Overview

This guide explains how to securely store and manage cryptographic keys for ISO7816 smart card applications on the Flipper Zero, using the STM32WB55's secure enclave (FUS - Firmware Upgrade Services).

**Target Platform**: Flipper Zero (STM32WB55)
**Security Level**: Hardware-backed key storage with AES encryption

**Last Updated**: December 14, 2025

---

## ⚠️ Security Disclaimer

From the official Flipper Zero documentation:

> Flipper was never designed to be secure, nor it passed cryptography audit. Despite of the fact that keys are stored in secure enclave there are some types of attack that can be performed against AES engine to recover keys (theoretical). Also there is no way to securely deliver user keys to device and never will be. In addition device is fully open and there is no way to guarantee safety of your data, it can be easily dumped with debugger or modified code.

**Use this for educational purposes and hobby projects only.**

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Secure Enclave Key Slots](#secure-enclave-key-slots)
3. [Key Types and Encryption](#key-types-and-encryption)
4. [Master Key Architecture](#master-key-architecture)
5. [Implementation Guide](#implementation-guide)
6. [ISO7816 Key Storage Design](#iso7816-key-storage-design)
7. [API Reference](#api-reference)
8. [Security Considerations](#security-considerations)

---

## Architecture Overview

### STM32WB55 Dual-Core Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        STM32WB55                                │
│                                                                 │
│  ┌─────────────────────┐     ┌─────────────────────────────┐   │
│  │      CPU1 (M4)      │     │        CPU2 (M0+)           │   │
│  │  User Application   │     │   Wireless Stack + FUS      │   │
│  │                     │     │                             │   │
│  │  ┌───────────────┐  │     │  ┌───────────────────────┐  │   │
│  │  │ furi_hal_     │  │SHCI │  │  Firmware Upgrade     │  │   │
│  │  │ crypto API    │◄─┼─────┼──│  Services (FUS)       │  │   │
│  │  └───────────────┘  │     │  │                       │  │   │
│  │          │          │     │  │  • Key Storage        │  │   │
│  │          ▼          │     │  │  • AES Engine Mgmt    │  │   │
│  │  ┌───────────────┐  │     │  │  • Secure Operations  │  │   │
│  │  │   AES1 HW     │◄─┼─────┼──│                       │  │   │
│  │  │   Engine      │  │     │  └───────────────────────┘  │   │
│  │  └───────────────┘  │     │                             │   │
│  └─────────────────────┘     └─────────────────────────────┘   │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                   Flash Memory                           │   │
│  │  ┌─────────────────────────────────────────────────────┐ │   │
│  │  │  Secure Enclave (Up to 100 key slots)               │ │   │
│  │  │  Keys encrypted and protected by FUS                │ │   │
│  │  └─────────────────────────────────────────────────────┘ │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

### SHCI (System HCI) Communication

The CPU1 (user application) communicates with CPU2 (FUS) via SHCI commands:

| Command | Function | Description |
|---------|----------|-------------|
| `SHCI_C2_FUS_StoreUsrKey` | Store a key | Writes key to enclave, returns slot index |
| `SHCI_C2_FUS_LoadUsrKey` | Load a key | Loads key into AES1 engine (locked) |
| `SHCI_C2_FUS_UnloadUsrKey` | Unload a key | Removes key from AES1, unlocks engine |
| `SHCI_C2_FUS_LockUsrKey` | Lock a key | Permanently prevents key updates |

---

## Secure Enclave Key Slots

The STM32WB55 secure enclave supports up to 100 key slots:

| Slot Range | Purpose | Notes |
|------------|---------|-------|
| 0 | Master Key | Factory provisioned, used for encrypted key delivery |
| 1-10 | Factory Keys | Same on all Flipper devices, used by firmware |
| 11 | Device Unique Key | Generated on first use, unique per device |
| 12-100 | User Keys | Available for applications |

### Slot Allocation for ISO7816

We propose the following allocation for ISO7816 applications:

| Slot | Purpose | Key Type |
|------|---------|----------|
| 11 | Device Master Key | Unique, auto-generated |
| 12 | ISO7816 KEK (Key Encryption Key) | Simple, 256-bit AES |
| 13 | ISO7816 Signing Key | Simple, 256-bit |
| 14-30 | Application Keys | Encrypted with slot 12 |
| 31-50 | Per-Application Keys | Reserved for future |

---

## Key Types and Encryption

### FUS Key Types

The STM32WB55 FUS supports three key types:

```c
enum {
    KEYTYPE_NONE      = 0x00,  // No key (invalid)
    KEYTYPE_SIMPLE    = 0x01,  // Plaintext key (direct storage)
    KEYTYPE_MASTER    = 0x02,  // Master key for encrypting other keys
    KEYTYPE_ENCRYPTED = 0x03,  // Key encrypted with Master key
};
```

### Key Sizes

```c
enum {
    KEYSIZE_16 = 16,  // 128-bit AES key
    KEYSIZE_32 = 32,  // 256-bit AES key
};
```

### Key Data Structure

```c
// Command parameter for storing a key
typedef PACKED_STRUCT {
    uint8_t KeyType;       // KEYTYPE_SIMPLE, KEYTYPE_MASTER, or KEYTYPE_ENCRYPTED
    uint8_t KeySize;       // 16 or 32 bytes
    uint8_t KeyData[44];   // Key data (32 bytes + 12 bytes for encrypted keys)
} SHCI_C2_FUS_StoreUsrKey_Cmd_Param_t;
```

For encrypted keys (`KEYTYPE_ENCRYPTED`), the data format is:
- Bytes 0-31: Encrypted key material (256-bit)
- Bytes 32-43: 12-byte nonce/IV used during encryption

---

## Master Key Architecture

### Hierarchical Key Design

```
┌─────────────────────────────────────────────────────────────┐
│                    Master Key (Slot 0)                      │
│         Factory-provisioned, cannot be extracted            │
│                   (KEYTYPE_MASTER)                          │
└─────────────────────────────┬───────────────────────────────┘
                              │ Encrypts
                              ▼
┌─────────────────────────────────────────────────────────────┐
│              ISO7816 KEK - Key Encryption Key (Slot 12)     │
│           Stored as KEYTYPE_ENCRYPTED with Master Key       │
└─────────────────────────────┬───────────────────────────────┘
                              │ Encrypts
          ┌───────────────────┼───────────────────┐
          ▼                   ▼                   ▼
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐
│  App Key 1      │ │  App Key 2      │ │  App Key N      │
│  (Slot 14)      │ │  (Slot 15)      │ │  (Slot N)       │
│  KEYTYPE_       │ │  KEYTYPE_       │ │  KEYTYPE_       │
│  ENCRYPTED      │ │  ENCRYPTED      │ │  ENCRYPTED      │
└─────────────────┘ └─────────────────┘ └─────────────────┘
```

### Why Use a KEK?

1. **Key Isolation**: Application keys are encrypted, never exposed in plaintext
2. **Key Rotation**: KEK can be changed, re-encrypting all application keys
3. **Secure Distribution**: Keys can be securely provisioned via encrypted transport
4. **Revocation**: Deleting KEK invalidates all derived keys

---

## Implementation Guide

### Step 1: Include Headers

```c
#include <furi_hal_crypto.h>
#include <furi_hal_random.h>
#include <interface/patterns/ble_thread/shci/shci.h>
```

### Step 2: Check Enclave Status

```c
bool check_enclave_status(void) {
    uint8_t keys_nb = 0;
    uint8_t valid_keys_nb = 0;
    
    bool ok = furi_hal_crypto_enclave_verify(&keys_nb, &valid_keys_nb);
    
    FURI_LOG_I("Crypto", "Enclave: %u keys, %u valid", keys_nb, valid_keys_nb);
    
    return ok && (valid_keys_nb >= FURI_HAL_CRYPTO_ENCLAVE_FACTORY_KEY_SLOT_END);
}
```

### Step 3: Generate and Store ISO7816 KEK

```c
#define ISO7816_KEK_SLOT 12

bool iso7816_generate_kek(uint8_t* slot_out) {
    // Generate random 256-bit key
    uint8_t key_data[32];
    furi_hal_random_fill_buf(key_data, sizeof(key_data));
    
    // Prepare key structure
    FuriHalCryptoKey key = {
        .type = FuriHalCryptoKeyTypeSimple,  // Or KEYTYPE_ENCRYPTED for Master-encrypted
        .size = FuriHalCryptoKeySize256,
        .data = key_data,
    };
    
    // Store in enclave
    uint8_t slot = 0;
    bool success = furi_hal_crypto_enclave_store_key(&key, &slot);
    
    // Clear sensitive data
    memset(key_data, 0, sizeof(key_data));
    
    if(success) {
        FURI_LOG_I("ISO7816", "KEK stored in slot %u", slot);
        *slot_out = slot;
    } else {
        FURI_LOG_E("ISO7816", "Failed to store KEK");
    }
    
    return success;
}
```

### Step 4: Encrypt Application Key with KEK

```c
bool iso7816_encrypt_app_key(
    uint8_t kek_slot,
    const uint8_t* app_key,
    uint8_t* encrypted_key,
    uint8_t* iv
) {
    // Generate random IV
    furi_hal_random_fill_buf(iv, 16);
    
    // Load KEK into AES engine
    if(!furi_hal_crypto_enclave_load_key(kek_slot, iv)) {
        FURI_LOG_E("ISO7816", "Failed to load KEK");
        return false;
    }
    
    // Encrypt the application key
    bool success = furi_hal_crypto_encrypt(app_key, encrypted_key, 32);
    
    // Unload KEK
    furi_hal_crypto_enclave_unload_key(kek_slot);
    
    return success;
}
```

### Step 5: Store Encrypted Application Key

```c
bool iso7816_store_encrypted_key(
    const uint8_t* encrypted_key,
    const uint8_t* nonce,  // 12 bytes
    uint8_t* slot_out
) {
    // Prepare encrypted key structure
    SHCI_C2_FUS_StoreUsrKey_Cmd_Param_t param;
    param.KeyType = KEYTYPE_ENCRYPTED;
    param.KeySize = KEYSIZE_32;
    
    // Copy encrypted key data (32 bytes) + nonce (12 bytes)
    memcpy(param.KeyData, encrypted_key, 32);
    memcpy(param.KeyData + 32, nonce, 12);
    
    // Store via SHCI
    SHCI_CmdStatus_t status = SHCI_C2_FUS_StoreUsrKey(&param, slot_out);
    
    return status == SHCI_Success;
}
```

### Step 6: Load and Use Application Key

```c
bool iso7816_use_app_key(uint8_t slot, const uint8_t* iv, uint8_t* data, size_t len) {
    // Load key from enclave into AES engine
    // Note: When key is loaded from enclave, key registers are LOCKED
    //       Only FUS can unlock them via UnloadUsrKey
    if(!furi_hal_crypto_enclave_load_key(slot, iv)) {
        return false;
    }
    
    // Perform encryption/decryption
    bool success = furi_hal_crypto_encrypt(data, data, len);
    
    // MUST unload key when done
    furi_hal_crypto_enclave_unload_key(slot);
    
    return success;
}
```

---

## ISO7816 Key Storage Design

### Key Storage Structure

For ISO7816 applications, we need to store various types of keys:

```c
// Key types for ISO7816 applications
typedef enum {
    Iso7816KeyTypePin,           // PIN verification key
    Iso7816KeyTypeMac,           // Message Authentication Code key
    Iso7816KeyTypeEnc,           // Encryption key
    Iso7816KeyTypeKek,           // Key Encryption Key
    Iso7816KeyTypeSignature,     // Digital signature key
} Iso7816KeyType;

// Key metadata stored in filesystem
typedef struct {
    uint8_t slot;                // Enclave slot number
    Iso7816KeyType type;         // Key type
    uint8_t key_ref;             // ISO7816 key reference (P2)
    uint8_t algorithm;           // Algorithm identifier
    bool locked;                 // Whether key is permanently locked
    uint8_t retries;             // Usage retry counter (if applicable)
} Iso7816KeyEntry;

// Key storage in filesystem
typedef struct {
    Iso7816KeyEntry keys[32];    // Max 32 keys per application
    uint8_t key_count;
} Iso7816KeyStore;
```

### AES-GCM for Authenticated Encryption

For secure messaging in ISO7816, use AES-GCM:

```c
bool iso7816_secure_encrypt(
    uint8_t key_slot,
    const uint8_t* plaintext,
    size_t plaintext_len,
    const uint8_t* aad,          // Additional authenticated data (e.g., APDU header)
    size_t aad_len,
    uint8_t* ciphertext,
    uint8_t* tag                 // 16-byte authentication tag
) {
    uint8_t iv[12];
    furi_hal_random_fill_buf(iv, sizeof(iv));
    
    // Load key and get raw key data for GCM
    // Note: For GCM, we may need to use direct key loading
    // since enclave only supports CBC mode
    
    uint8_t key[32];
    // ... retrieve key ...
    
    FuriHalCryptoGCMState state = furi_hal_crypto_gcm_encrypt_and_tag(
        key,
        iv,
        aad,
        aad_len,
        plaintext,
        ciphertext,
        plaintext_len,
        tag
    );
    
    // Clear key from memory
    memset(key, 0, sizeof(key));
    
    return state == FuriHalCryptoGCMStateOk;
}
```

---

## API Reference

### Flipper Zero Crypto HAL Functions

| Function | Description |
|----------|-------------|
| `furi_hal_crypto_init()` | Initialize crypto subsystem |
| `furi_hal_crypto_enclave_verify()` | Check enclave key integrity |
| `furi_hal_crypto_enclave_ensure_key()` | Ensure key slot is populated |
| `furi_hal_crypto_enclave_store_key()` | Store key in enclave |
| `furi_hal_crypto_enclave_load_key()` | Load enclave key to AES engine |
| `furi_hal_crypto_enclave_unload_key()` | Unload key from AES engine |
| `furi_hal_crypto_encrypt()` | Encrypt data with loaded key |
| `furi_hal_crypto_decrypt()` | Decrypt data with loaded key |
| `furi_hal_crypto_ctr()` | AES-CTR encryption/decryption |
| `furi_hal_crypto_gcm()` | AES-GCM authenticated encryption |

### SHCI Commands (Low-Level)

| Command | Description |
|---------|-------------|
| `SHCI_C2_FUS_StoreUsrKey()` | Store user key in FUS enclave |
| `SHCI_C2_FUS_LoadUsrKey()` | Load user key into AES1 |
| `SHCI_C2_FUS_UnloadUsrKey()` | Unload user key from AES1 |
| `SHCI_C2_FUS_LockUsrKey()` | Permanently lock a key slot |

---

## Security Considerations

### Key Protection

1. **Never log key material** - Even in debug builds
2. **Clear keys from RAM** - Use `memset()` after use
3. **Use random IVs** - Never reuse initialization vectors
4. **Validate inputs** - Check all parameters before crypto operations

### Enclave Limitations

1. **Sequential only** - Slots must be used in order
2. **Append-only** - Keys cannot be deleted, only locked
3. **No export** - Keys cannot be extracted once stored
4. **AES-CBC only** - Hardware acceleration is CBC mode only

### Attack Vectors to Consider

1. **Side-channel attacks** - Timing, power analysis
2. **Debugger access** - Device is fully open
3. **Firmware modification** - No secure boot
4. **Physical access** - Flash can be dumped

### Recommendations

1. **Use AES-GCM** for authenticated encryption
2. **Implement key derivation** for session keys
3. **Add replay protection** with counters/nonces
4. **Log all key operations** for audit trail
5. **Implement key rotation** procedures

---

## Example: Complete Key Setup Flow

```c
// Initialize ISO7816 secure key storage
bool iso7816_crypto_init(void) {
    // 1. Verify enclave is intact
    uint8_t keys_nb, valid_keys_nb;
    if(!furi_hal_crypto_enclave_verify(&keys_nb, &valid_keys_nb)) {
        FURI_LOG_E("ISO7816", "Enclave verification failed");
        return false;
    }
    
    // 2. Ensure device unique key exists
    if(!furi_hal_crypto_enclave_ensure_key(FURI_HAL_CRYPTO_ENCLAVE_UNIQUE_KEY_SLOT)) {
        FURI_LOG_E("ISO7816", "Failed to ensure unique key");
        return false;
    }
    
    // 3. Check if ISO7816 KEK exists (slot 12)
    uint8_t empty_iv[16] = {0};
    if(!furi_hal_crypto_enclave_load_key(ISO7816_KEK_SLOT, empty_iv)) {
        // KEK doesn't exist, generate it
        FURI_LOG_I("ISO7816", "Generating new KEK...");
        
        uint8_t slot;
        if(!iso7816_generate_kek(&slot)) {
            return false;
        }
        
        // Verify slot is what we expected
        if(slot != ISO7816_KEK_SLOT) {
            FURI_LOG_W("ISO7816", "KEK stored in unexpected slot %u", slot);
        }
    } else {
        furi_hal_crypto_enclave_unload_key(ISO7816_KEK_SLOT);
        FURI_LOG_I("ISO7816", "KEK already exists in slot %u", ISO7816_KEK_SLOT);
    }
    
    FURI_LOG_I("ISO7816", "Crypto init complete");
    return true;
}
```

---

## References

- [STM32WB55 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0434-multiprotocol-wireless-32bit-mcu-armbased-cortexm4-with-fpu-bluetooth-lowenergy-and-802154-radio-solution-stmicroelectronics.pdf)
- [AN5185 - ST Firmware Upgrade Services (FUS)](https://www.st.com/resource/en/application_note/an5185-st-firmware-upgrade-services-for-stm32wb-series-stmicroelectronics.pdf)
- [Flipper Zero Crypto HAL Source](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/targets/f7/furi_hal/furi_hal_crypto.c)
- ISO/IEC 7816-4:2005 - Commands for interchange
- ISO/IEC 7816-8:2004 - Commands for security operations
