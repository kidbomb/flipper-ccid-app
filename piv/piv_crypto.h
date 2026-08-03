/**
 * @file piv_crypto.h
 * @brief PIV Cryptographic Operations
 * 
 * Implements GENERAL AUTHENTICATE command per NIST SP 800-73-5 Part 2.
 * 
 * GENERAL AUTHENTICATE (INS=0x87):
 * - Internal Authenticate: Card signs challenge from host
 * - External Authenticate: Card verifies signature from host  
 * - Mutual Authenticate: Bidirectional authentication
 * 
 * Command format: 00 87 <alg> <key ref> Lc <7C auth template> Le
 * 
 * Authentication Template (tag 0x7C):
 * - 80: Witness
 * - 81: Challenge
 * - 82: Response
 * - 85: Exponent (for key agreement)
 */

#pragma once

#include "../iso7816/iso7816_t0_apdu.h"
#include "../iso7816/iso7816_filesystem.h"
#include "piv_keys.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * GENERAL AUTHENTICATE Command Processing
 ******************************************************************************/

/**
 * @brief Handle GENERAL AUTHENTICATE command (INS=0x87)
 * 
 * Processes cryptographic authentication operations:
 * 1. Internal Authenticate - card signs challenge
 * 2. External Authenticate - card verifies response
 * 3. Challenge-Response protocols
 * 
 * Command: 00 87 <algorithm> <key ref> Lc <data> Le
 * 
 * @param fs File system context (for PIN verification state)
 * @param command Command APDU
 * @param response Response APDU
 */
void piv_cmd_general_authenticate(
    Iso7816FileSystem* fs,
    const ISO7816_Command_APDU* command,
    ISO7816_Response_APDU* response);

/**
 * @brief Perform ECC P-256 signing operation
 * 
 * Signs data using the specified key slot.
 * Uses mbedTLS ECDSA with SHA-256.
 * 
 * @param key_slot Key slot containing private key
 * @param data Data to sign (typically a hash)
 * @param data_len Length of data
 * @param signature Output signature buffer (64 bytes: R || S)
 * @param sig_len Output signature length
 * @return true on success
 */
bool piv_crypto_sign_p256(
    const PivKeySlot* key_slot,
    const uint8_t* data,
    size_t data_len,
    uint8_t* signature,
    size_t* sig_len);

/**
 * @brief Initialize PIV crypto module
 * 
 * Initializes mbedTLS contexts and key storage.
 */
void piv_crypto_init(void);

/**
 * @brief Deinitialize PIV crypto module
 * 
 * Cleans up mbedTLS contexts and clears sensitive data.
 */
void piv_crypto_deinit(void);

#ifdef __cplusplus
}
#endif
