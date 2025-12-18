/**
 * @file piv_crypto.c
 * @brief PIV Cryptographic Operations Implementation
 * 
 * Implements GENERAL AUTHENTICATE command per NIST SP 800-73-5.
 * Uses mbedTLS for ECC P-256 cryptographic operations.
 */

#include "piv_crypto.h"
#include "piv_keys.h"
#include "piv_pin.h"
#include "../iso7816/iso7816_response.h"

#include <furi.h>
#include <furi_hal_random.h>
#include <string.h>

#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>
#include <mbedtls/error.h>

#define TAG "PIV_Crypto"

// Macro to check mbedTLS return values
#define MCHECK(expr) \
    do { \
        int _ret = (expr); \
        if(_ret != 0) { \
            FURI_LOG_E(TAG, "mbedTLS error: %d at %s:%d", _ret, __FILE__, __LINE__); \
            goto cleanup; \
        } \
    } while(0)

/*******************************************************************************
 * mbedTLS Random Callback
 ******************************************************************************/

static int piv_random_cb(void* ctx, unsigned char* buf, size_t len) {
    UNUSED(ctx);
    furi_hal_random_fill_buf(buf, len);
    return 0;
}

/*******************************************************************************
 * ECC P-256 Crypto Operations
 ******************************************************************************/

static mbedtls_ecp_group ecp_group;
static bool crypto_initialized = false;

void piv_crypto_init(void) {
    if(crypto_initialized) return;
    
    mbedtls_ecp_group_init(&ecp_group);
    int ret = mbedtls_ecp_group_load(&ecp_group, MBEDTLS_ECP_DP_SECP256R1);
    if(ret != 0) {
        FURI_LOG_E(TAG, "Failed to load ECP group: %d", ret);
        return;
    }
    
    // Initialize keys
    piv_keys_init();
    
    crypto_initialized = true;
    FURI_LOG_I(TAG, "PIV crypto initialized");
}

void piv_crypto_deinit(void) {
    if(!crypto_initialized) return;
    
    mbedtls_ecp_group_free(&ecp_group);
    crypto_initialized = false;
}

bool piv_crypto_sign_p256(
    const PivKeySlot* key_slot,
    const uint8_t* data,
    size_t data_len,
    uint8_t* signature,
    size_t* sig_len) {
    
    if(!crypto_initialized) {
        piv_crypto_init();
    }
    
    if(key_slot == NULL || key_slot->algorithm != PivKeyAlgEccP256) {
        FURI_LOG_E(TAG, "Invalid key slot or algorithm");
        return false;
    }
    
    mbedtls_mpi r, s, d;
    bool success = false;
    
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_mpi_init(&d);
    
    // Load private key
    MCHECK(mbedtls_mpi_read_binary(&d, key_slot->key.ecc_p256.private_key, 
                                    PIV_ECC_P256_PRIVATE_KEY_SIZE));
    
    // Sign the data
    // Note: For PIV, the input is typically already a hash
    MCHECK(mbedtls_ecdsa_sign(&ecp_group, &r, &s, &d, data, data_len, 
                               piv_random_cb, NULL));
    
    // Write R and S as raw 32-byte values
    MCHECK(mbedtls_mpi_write_binary(&r, signature, 32));
    MCHECK(mbedtls_mpi_write_binary(&s, signature + 32, 32));
    
    *sig_len = PIV_ECC_P256_SIGNATURE_SIZE;
    success = true;
    FURI_LOG_I(TAG, "ECC P-256 signature generated");
    
cleanup:
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&d);
    
    return success;
}

/*******************************************************************************
 * TLV Parsing Helpers
 ******************************************************************************/

/**
 * @brief Parse a TLV structure
 * 
 * @param data Input data
 * @param data_len Data length
 * @param tag Expected tag
 * @param value Output value pointer
 * @param value_len Output value length
 * @return Bytes consumed, or 0 on error
 */
static size_t parse_tlv(
    const uint8_t* data,
    size_t data_len,
    uint8_t tag,
    const uint8_t** value,
    size_t* value_len) {
    
    if(data_len < 2) return 0;
    
    if(data[0] != tag) return 0;
    
    size_t len_offset = 1;
    size_t len;
    
    if(data[1] < 0x80) {
        len = data[1];
        len_offset = 2;
    } else if(data[1] == 0x81) {
        if(data_len < 3) return 0;
        len = data[2];
        len_offset = 3;
    } else if(data[1] == 0x82) {
        if(data_len < 4) return 0;
        len = (data[2] << 8) | data[3];
        len_offset = 4;
    } else {
        return 0;
    }
    
    if(len_offset + len > data_len) return 0;
    
    *value = data + len_offset;
    *value_len = len;
    
    return len_offset + len;
}

/**
 * @brief Find a tag within data
 */
static bool find_tag(
    const uint8_t* data,
    size_t data_len,
    uint8_t tag,
    const uint8_t** value,
    size_t* value_len) {
    
    size_t offset = 0;
    
    while(offset < data_len) {
        if(data_len - offset < 2) return false;
        
        uint8_t current_tag = data[offset];
        size_t len_offset = 1;
        size_t len;
        
        if(data[offset + 1] < 0x80) {
            len = data[offset + 1];
            len_offset = 2;
        } else if(data[offset + 1] == 0x81) {
            if(offset + 2 >= data_len) return false;
            len = data[offset + 2];
            len_offset = 3;
        } else if(data[offset + 1] == 0x82) {
            if(offset + 3 >= data_len) return false;
            len = (data[offset + 2] << 8) | data[offset + 3];
            len_offset = 4;
        } else {
            return false;
        }
        
        if(offset + len_offset + len > data_len) return false;
        
        if(current_tag == tag) {
            *value = data + offset + len_offset;
            *value_len = len;
            return true;
        }
        
        offset += len_offset + len;
    }
    
    return false;
}

/**
 * @brief Build a TLV structure
 */
static size_t build_tlv(uint8_t* buffer, uint8_t tag, const uint8_t* value, size_t value_len) {
    size_t offset = 0;
    
    buffer[offset++] = tag;
    
    if(value_len < 0x80) {
        buffer[offset++] = value_len;
    } else if(value_len <= 0xFF) {
        buffer[offset++] = 0x81;
        buffer[offset++] = value_len;
    } else {
        buffer[offset++] = 0x82;
        buffer[offset++] = (value_len >> 8) & 0xFF;
        buffer[offset++] = value_len & 0xFF;
    }
    
    if(value != NULL && value_len > 0) {
        memcpy(buffer + offset, value, value_len);
        offset += value_len;
    }
    
    return offset;
}

/*******************************************************************************
 * GENERAL AUTHENTICATE Command Handler
 ******************************************************************************/

void piv_cmd_general_authenticate(
    Iso7816FileSystem* fs,
    const ISO7816_Command_APDU* command,
    ISO7816_Response_APDU* response) {
    
    if(!crypto_initialized) {
        piv_crypto_init();
    }
    
    uint8_t algorithm = command->P1;
    uint8_t key_ref = command->P2;
    
    FURI_LOG_I(TAG, "GENERAL AUTHENTICATE: alg=0x%02X key=0x%02X Lc=%u",
               algorithm, key_ref, command->Lc);
    
    // Get key slot
    PivKeySlot* key_slot = piv_keys_get_slot(key_ref);
    if(key_slot == NULL) {
        FURI_LOG_W(TAG, "Key not found: 0x%02X", key_ref);
        iso7816_set_response(response, ISO7816_RESPONSE_REF_DATA_NOT_FOUND);
        return;
    }
    
    // Verify algorithm matches key
    if((algorithm == PIV_ALG_ECC_P256 && key_slot->algorithm != PivKeyAlgEccP256) ||
       (algorithm == PIV_ALG_ECC_P384 && key_slot->algorithm != PivKeyAlgEccP384)) {
        FURI_LOG_W(TAG, "Algorithm mismatch: requested 0x%02X, key has %d",
                   algorithm, key_slot->algorithm);
        iso7816_set_response(response, ISO7816_RESPONSE_WRONG_DATA);
        return;
    }
    
    // Check if PIN is required
    if(key_slot->pin_required) {
        if(!piv_pin_is_verified(fs)) {
            FURI_LOG_W(TAG, "PIN verification required for key 0x%02X", key_ref);
            iso7816_set_response(response, ISO7816_RESPONSE_SECURITY_NOT_SATISFIED);
            return;
        }
        // Clear PIN verification for keys that require it for each use
        if(key_slot->pin_always) {
            piv_pin_clear_verification(fs);
        }
    }
    
    // Parse command data - must be Dynamic Authentication Template (7C)
    if(command->Lc < 4) {
        iso7816_set_response(response, ISO7816_RESPONSE_WRONG_LENGTH);
        return;
    }
    
    const uint8_t* template_data;
    size_t template_len;
    size_t consumed = parse_tlv(command->Data, command->Lc, 
                                 PIV_TAG_DYNAMIC_AUTH_TEMPLATE,
                                 &template_data, &template_len);
    
    if(consumed == 0) {
        FURI_LOG_W(TAG, "Missing 7C template");
        iso7816_set_response(response, ISO7816_RESPONSE_WRONG_DATA);
        return;
    }
    
    // Look for Challenge tag (81) - Internal Authenticate
    const uint8_t* challenge;
    size_t challenge_len;
    
    if(find_tag(template_data, template_len, PIV_TAG_CHALLENGE, &challenge, &challenge_len)) {
        // Internal Authenticate: Sign the challenge
        FURI_LOG_I(TAG, "Internal Authenticate: challenge len=%zu", challenge_len);
        
        if(algorithm == PIV_ALG_ECC_P256) {
            // For ECC, challenge should be a hash (32 bytes for SHA-256)
            // But PIV allows any length up to curve order
            if(challenge_len > 32) {
                FURI_LOG_W(TAG, "Challenge too long for P-256");
                iso7816_set_response(response, ISO7816_RESPONSE_WRONG_DATA);
                return;
            }
            
            uint8_t signature[PIV_ECC_P256_SIGNATURE_SIZE];
            size_t sig_len = 0;
            
            if(!piv_crypto_sign_p256(key_slot, challenge, challenge_len, 
                                      signature, &sig_len)) {
                FURI_LOG_E(TAG, "Signing failed");
                iso7816_set_response(response, ISO7816_RESPONSE_EXECUTION_ERROR);
                return;
            }
            
            // Build response: 7C <len> 82 <len> <signature>
            uint8_t inner_tlv[70];  // 82 + len + 64 bytes signature
            size_t inner_len = build_tlv(inner_tlv, PIV_TAG_RESPONSE, signature, sig_len);
            
            size_t resp_len = build_tlv(response->Data, PIV_TAG_DYNAMIC_AUTH_TEMPLATE,
                                         inner_tlv, inner_len);
            
            response->DataLen = resp_len;
            iso7816_set_response(response, ISO7816_RESPONSE_OK);
            
            FURI_LOG_I(TAG, "Internal Authenticate success, response len=%zu", resp_len);
            return;
        } else {
            // Unsupported algorithm
            iso7816_set_response(response, ISO7816_RESPONSE_FUNCTION_NOT_SUPPORTED);
            return;
        }
    }
    
    // Look for empty Response tag (82) with length 0 - request for challenge (External Auth)
    const uint8_t* resp_tag;
    size_t resp_tag_len;
    
    if(find_tag(template_data, template_len, PIV_TAG_RESPONSE, &resp_tag, &resp_tag_len)) {
        if(resp_tag_len == 0) {
            // External authenticate step 1: Generate challenge
            FURI_LOG_I(TAG, "External Authenticate: generating challenge");
            
            // Generate random challenge
            uint8_t random_challenge[16];
            furi_hal_random_fill_buf(random_challenge, sizeof(random_challenge));
            
            // TODO: Store challenge for verification
            // For now, just return the challenge
            
            uint8_t inner_tlv[20];
            size_t inner_len = build_tlv(inner_tlv, PIV_TAG_CHALLENGE, 
                                          random_challenge, sizeof(random_challenge));
            
            size_t resp_len = build_tlv(response->Data, PIV_TAG_DYNAMIC_AUTH_TEMPLATE,
                                         inner_tlv, inner_len);
            
            response->DataLen = resp_len;
            iso7816_set_response(response, ISO7816_RESPONSE_OK);
            return;
        }
    }
    
    // If we get here, the request format wasn't recognized
    FURI_LOG_W(TAG, "Unrecognized authentication template format");
    iso7816_set_response(response, ISO7816_RESPONSE_WRONG_DATA);
}
