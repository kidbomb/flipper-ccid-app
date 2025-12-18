/**
 * @file piv_keys.h
 * @brief PIV Key References and Storage
 * 
 * Defines PIV asymmetric key references per NIST SP 800-73-5 Table 5.
 * Provides key storage structures for cryptographic operations.
 * 
 * Key References:
 * - 0x9A: PIV Authentication Key
 * - 0x9B: PIV Card Application Administration Key
 * - 0x9C: Digital Signature Key
 * - 0x9D: Key Management Key
 * - 0x9E: Card Authentication Key
 * - 0x82-0x95: Retired Key Management Keys (1-20)
 * 
 * Supported Algorithms (Table 6):
 * - 0x07: RSA 2048
 * - 0x11: ECC P-256 (secp256r1)
 * - 0x14: ECC P-384 (secp384r1)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * PIV Key References (Table 5 of SP 800-73-5)
 ******************************************************************************/

/** PIV Secure Messaging Key - for VCI establishment */
#define PIV_KEY_SM                      0x04

/** PIV Authentication Key - cardholder + card authentication */
#define PIV_KEY_PIV_AUTH                0x9A

/** PIV Card Application Administration Key - admin operations */
#define PIV_KEY_CARD_ADMIN              0x9B

/** Digital Signature Key - document signing (non-repudiation) */
#define PIV_KEY_DIGITAL_SIG             0x9C

/** Key Management Key - encryption/decryption */
#define PIV_KEY_KEY_MGMT                0x9D

/** Card Authentication Key - card-only authentication */
#define PIV_KEY_CARD_AUTH               0x9E

/** Retired Key Management Keys 1-20 (0x82-0x95) */
#define PIV_KEY_RETIRED_1               0x82
#define PIV_KEY_RETIRED_20              0x95

/*******************************************************************************
 * PIV Cryptographic Algorithm Identifiers (Table 6)
 ******************************************************************************/

/** 3-key Triple DES - ECB */
#define PIV_ALG_3DES_ECB                0x00

/** 3-key Triple DES - CBC */
#define PIV_ALG_3DES_CBC                0x03

/** RSA 2048 */
#define PIV_ALG_RSA_2048                0x07

/** AES-128 CBC */
#define PIV_ALG_AES_128_CBC             0x08

/** AES-192 CBC */
#define PIV_ALG_AES_192_CBC             0x0A

/** AES-256 CBC */
#define PIV_ALG_AES_256_CBC             0x0C

/** ECC P-256 (secp256r1, NIST P-256) */
#define PIV_ALG_ECC_P256                0x11

/** ECC P-384 (secp384r1, NIST P-384) */
#define PIV_ALG_ECC_P384                0x14

/** RSA 3072 */
#define PIV_ALG_RSA_3072                0x05

/** CS2 cipher suite (SM) */
#define PIV_ALG_CS2                     0x27

/** CS7 cipher suite (SM) */
#define PIV_ALG_CS7                     0x2E

/*******************************************************************************
 * Key Sizes
 ******************************************************************************/

/** ECC P-256 private key size (32 bytes) */
#define PIV_ECC_P256_PRIVATE_KEY_SIZE   32

/** ECC P-256 public key size (65 bytes uncompressed: 04 || X || Y) */
#define PIV_ECC_P256_PUBLIC_KEY_SIZE    65

/** ECC P-256 signature size (64 bytes: R || S) */
#define PIV_ECC_P256_SIGNATURE_SIZE     64

/** RSA 2048 modulus size (256 bytes) */
#define PIV_RSA_2048_KEY_SIZE           256

/*******************************************************************************
 * PIV Key Structure
 ******************************************************************************/

/** Algorithm type for key */
typedef enum {
    PivKeyAlgNone = 0,
    PivKeyAlgEccP256 = PIV_ALG_ECC_P256,
    PivKeyAlgEccP384 = PIV_ALG_ECC_P384,
    PivKeyAlgRsa2048 = PIV_ALG_RSA_2048,
    PivKeyAlgRsa3072 = PIV_ALG_RSA_3072,
} PivKeyAlgorithm;

/** ECC P-256 Key Pair */
typedef struct {
    uint8_t private_key[PIV_ECC_P256_PRIVATE_KEY_SIZE];  // 32 bytes
    uint8_t public_key[PIV_ECC_P256_PUBLIC_KEY_SIZE];    // 65 bytes (04 || X || Y)
} PivEccP256KeyPair;

/** PIV Key Slot */
typedef struct {
    uint8_t reference;              // Key reference (0x9A, 0x9B, etc.)
    PivKeyAlgorithm algorithm;      // Algorithm identifier
    bool present;                   // Key is loaded
    bool pin_required;              // PIN verification required for use
    bool pin_always;                // PIN required for each use (vs session)
    union {
        PivEccP256KeyPair ecc_p256;
        // Add RSA when needed
    } key;
} PivKeySlot;

/*******************************************************************************
 * GENERAL AUTHENTICATE Dynamic Authentication Template Tags
 ******************************************************************************/

/** Dynamic Authentication Template (outer wrapper) */
#define PIV_TAG_DYNAMIC_AUTH_TEMPLATE   0x7C

/** Witness (encrypted challenge from card) */
#define PIV_TAG_WITNESS                 0x80

/** Challenge (random data from host or card) */
#define PIV_TAG_CHALLENGE               0x81

/** Response (signed/encrypted response) */
#define PIV_TAG_RESPONSE                0x82

/** Exponentiation (for key agreement) */
#define PIV_TAG_EXPONENT                0x85

/*******************************************************************************
 * Function Declarations
 ******************************************************************************/

/**
 * @brief Initialize PIV key slots
 * 
 * Sets up default key slots. For demo, loads test keys.
 */
void piv_keys_init(void);

/**
 * @brief Get a key slot by reference
 * 
 * @param reference Key reference (e.g., 0x9A)
 * @return Pointer to key slot, or NULL if not found
 */
PivKeySlot* piv_keys_get_slot(uint8_t reference);

/**
 * @brief Check if key exists
 * 
 * @param reference Key reference
 * @return true if key is present
 */
bool piv_keys_exists(uint8_t reference);

/**
 * @brief Check if key requires PIN
 * 
 * @param reference Key reference
 * @return true if PIN verification is required
 */
bool piv_keys_requires_pin(uint8_t reference);

/**
 * @brief Get algorithm for a key
 * 
 * @param reference Key reference
 * @return Algorithm identifier, or 0 if key not found
 */
PivKeyAlgorithm piv_keys_get_algorithm(uint8_t reference);

#ifdef __cplusplus
}
#endif
