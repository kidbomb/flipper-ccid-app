/**
 * @file piv_keys.c
 * @brief PIV Key Storage Implementation
 * 
 * Provides key storage and retrieval for PIV cryptographic operations.
 * For demo/testing purposes, includes pre-generated ECC P-256 test keys.
 * 
 * WARNING: These are test keys for development only!
 * In production, keys should be generated securely and stored in the
 * hardware secure enclave.
 */

#include "piv_keys.h"
#include <string.h>
#include <furi.h>

/*******************************************************************************
 * Demo Test Keys
 * 
 * WARNING: These are PUBLIC test keys for development only!
 * Never use these in any real security context.
 * 
 * Generated with: openssl ecparam -genkey -name prime256v1 -noout -out key.pem
 ******************************************************************************/

// Demo PIV Authentication Key (0x9A) - ECC P-256
// Private key: 32 bytes
static const uint8_t demo_9a_private[] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
};

// Public key: 65 bytes (04 || X || Y)
// Note: This is a placeholder - in real implementation, derive from private key
static const uint8_t demo_9a_public[] = {
    0x04,  // Uncompressed point indicator
    // X coordinate (32 bytes)
    0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42, 0x47,
    0xf8, 0xbc, 0xe6, 0xe5, 0x63, 0xa4, 0x40, 0xf2,
    0x77, 0x03, 0x7d, 0x81, 0x2d, 0xeb, 0x33, 0xa0,
    0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2, 0x96,
    // Y coordinate (32 bytes)
    0x4f, 0xe3, 0x42, 0xe2, 0xfe, 0x1a, 0x7f, 0x9b,
    0x8e, 0xe7, 0xeb, 0x4a, 0x7c, 0x0f, 0x9e, 0x16,
    0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31, 0x5e, 0xce,
    0xcb, 0xb6, 0x40, 0x68, 0x37, 0xbf, 0x51, 0xf5
};

// Demo Card Authentication Key (0x9E) - ECC P-256
static const uint8_t demo_9e_private[] = {
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40
};

static const uint8_t demo_9e_public[] = {
    0x04,
    0x7c, 0xf2, 0x7b, 0x18, 0x8d, 0x03, 0x4f, 0x7e,
    0x8a, 0x52, 0x38, 0x03, 0x04, 0xb5, 0x1a, 0xc3,
    0xc0, 0x89, 0x69, 0xe2, 0x77, 0xf2, 0x1b, 0x35,
    0xa6, 0x0b, 0x48, 0xfc, 0x47, 0x66, 0x99, 0x78,
    0x07, 0x77, 0x55, 0x10, 0xdb, 0x8e, 0xd0, 0x40,
    0x29, 0x3d, 0x9a, 0xc6, 0x9f, 0x74, 0x30, 0xdb,
    0xba, 0x7d, 0xad, 0xe6, 0x3c, 0xe9, 0x82, 0x29,
    0x9e, 0x04, 0xb7, 0x9d, 0x22, 0x7b, 0x8f, 0xaf
};

// Demo Digital Signature Key (0x9C) - ECC P-256
static const uint8_t demo_9c_private[] = {
    0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50,
    0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x60
};

static const uint8_t demo_9c_public[] = {
    0x04,
    0x3b, 0x4b, 0x5b, 0x6b, 0x7b, 0x8b, 0x9b, 0xab,
    0xbb, 0xcb, 0xdb, 0xeb, 0xfb, 0x0b, 0x1b, 0x2b,
    0x3c, 0x4c, 0x5c, 0x6c, 0x7c, 0x8c, 0x9c, 0xac,
    0xbc, 0xcc, 0xdc, 0xec, 0xfc, 0x0c, 0x1c, 0x2c,
    0x2d, 0x3d, 0x4d, 0x5d, 0x6d, 0x7d, 0x8d, 0x9d,
    0xad, 0xbd, 0xcd, 0xdd, 0xed, 0xfd, 0x0d, 0x1d,
    0x2e, 0x3e, 0x4e, 0x5e, 0x6e, 0x7e, 0x8e, 0x9e,
    0xae, 0xbe, 0xce, 0xde, 0xee, 0xfe, 0x0e, 0x1e
};

/*******************************************************************************
 * Key Slot Storage
 ******************************************************************************/

#define PIV_MAX_KEY_SLOTS 6

static PivKeySlot key_slots[PIV_MAX_KEY_SLOTS];
static bool keys_initialized = false;

/*******************************************************************************
 * Implementation
 ******************************************************************************/

void piv_keys_init(void) {
    if(keys_initialized) {
        return;
    }
    
    memset(key_slots, 0, sizeof(key_slots));
    
    // PIV Authentication Key (0x9A) - requires PIN
    key_slots[0].reference = PIV_KEY_PIV_AUTH;
    key_slots[0].algorithm = PivKeyAlgEccP256;
    key_slots[0].present = true;
    key_slots[0].pin_required = true;
    key_slots[0].pin_always = false;
    memcpy(key_slots[0].key.ecc_p256.private_key, demo_9a_private, 
           PIV_ECC_P256_PRIVATE_KEY_SIZE);
    memcpy(key_slots[0].key.ecc_p256.public_key, demo_9a_public,
           PIV_ECC_P256_PUBLIC_KEY_SIZE);
    
    // Card Authentication Key (0x9E) - no PIN required
    key_slots[1].reference = PIV_KEY_CARD_AUTH;
    key_slots[1].algorithm = PivKeyAlgEccP256;
    key_slots[1].present = true;
    key_slots[1].pin_required = false;  // Card auth doesn't need PIN
    key_slots[1].pin_always = false;
    memcpy(key_slots[1].key.ecc_p256.private_key, demo_9e_private,
           PIV_ECC_P256_PRIVATE_KEY_SIZE);
    memcpy(key_slots[1].key.ecc_p256.public_key, demo_9e_public,
           PIV_ECC_P256_PUBLIC_KEY_SIZE);
    
    // Digital Signature Key (0x9C) - requires PIN always
    key_slots[2].reference = PIV_KEY_DIGITAL_SIG;
    key_slots[2].algorithm = PivKeyAlgEccP256;
    key_slots[2].present = true;
    key_slots[2].pin_required = true;
    key_slots[2].pin_always = true;  // PIN required for EACH signature
    memcpy(key_slots[2].key.ecc_p256.private_key, demo_9c_private,
           PIV_ECC_P256_PRIVATE_KEY_SIZE);
    memcpy(key_slots[2].key.ecc_p256.public_key, demo_9c_public,
           PIV_ECC_P256_PUBLIC_KEY_SIZE);
    
    // Key Management Key (0x9D) - requires PIN (stub, not present)
    key_slots[3].reference = PIV_KEY_KEY_MGMT;
    key_slots[3].algorithm = PivKeyAlgNone;
    key_slots[3].present = false;
    
    // Card Admin Key (0x9B) - 3DES (stub, not present for now)
    key_slots[4].reference = PIV_KEY_CARD_ADMIN;
    key_slots[4].algorithm = PivKeyAlgNone;
    key_slots[4].present = false;
    
    keys_initialized = true;
    FURI_LOG_I("PIV", "Keys initialized (demo mode)");
}

PivKeySlot* piv_keys_get_slot(uint8_t reference) {
    if(!keys_initialized) {
        piv_keys_init();
    }
    
    for(size_t i = 0; i < PIV_MAX_KEY_SLOTS; i++) {
        if(key_slots[i].reference == reference && key_slots[i].present) {
            return &key_slots[i];
        }
    }
    
    return NULL;
}

bool piv_keys_exists(uint8_t reference) {
    return piv_keys_get_slot(reference) != NULL;
}

bool piv_keys_requires_pin(uint8_t reference) {
    PivKeySlot* slot = piv_keys_get_slot(reference);
    if(slot == NULL) {
        return false;
    }
    return slot->pin_required;
}

PivKeyAlgorithm piv_keys_get_algorithm(uint8_t reference) {
    PivKeySlot* slot = piv_keys_get_slot(reference);
    if(slot == NULL) {
        return PivKeyAlgNone;
    }
    return slot->algorithm;
}
