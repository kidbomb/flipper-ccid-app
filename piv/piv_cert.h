/**
 * @file piv_cert.h
 * @brief PIV X.509 Certificate Storage
 * 
 * Manages PIV certificate containers per NIST SP 800-73-5:
 * - X.509 Certificate for PIV Authentication (0x5FC105)
 * - X.509 Certificate for Card Authentication (0x5FC101)
 * - X.509 Certificate for Digital Signature (0x5FC10A)
 * - X.509 Certificate for Key Management (0x5FC10B)
 * 
 * Certificate Format (BER-TLV wrapped):
 *   53 <len> <certificate data>
 *     70 <len> <X.509 certificate>
 *     71 01 <cert info byte>
 *     FE 00 (LRC)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * PIV Certificate Tags (3-byte BER-TLV tags)
 ******************************************************************************/

#define PIV_CERT_TAG_PIV_AUTH       0x5FC105  // PIV Authentication Key
#define PIV_CERT_TAG_CARD_AUTH      0x5FC101  // Card Authentication Key
#define PIV_CERT_TAG_DIGITAL_SIG    0x5FC10A  // Digital Signature Key
#define PIV_CERT_TAG_KEY_MGMT       0x5FC10B  // Key Management Key

/*******************************************************************************
 * PIV Certificate Container Internal Tags
 ******************************************************************************/

#define PIV_CERT_CONTAINER_TAG      0x53      // Certificate container
#define PIV_CERT_X509_TAG           0x70      // X.509 certificate
#define PIV_CERT_INFO_TAG           0x71      // Certificate info byte
#define PIV_CERT_MSCUID_TAG         0x72      // MSCUID (Microsoft)
#define PIV_CERT_LRC_TAG            0xFE      // Error detection code

/*******************************************************************************
 * Certificate Info Byte (Tag 0x71)
 * 
 * Bits 0-1: Compression indicator
 *   00 = Uncompressed
 *   01 = GZIP compressed
 * 
 * Bit 2: PKCS#11 certificate indicator
 *   0 = X.509 certificate
 *   1 = PKCS#11 certificate (deprecated)
 ******************************************************************************/

#define PIV_CERT_INFO_UNCOMPRESSED  0x00
#define PIV_CERT_INFO_GZIP          0x01

/*******************************************************************************
 * Maximum Certificate Sizes
 ******************************************************************************/

#define PIV_MAX_CERT_CONTAINER_SIZE 1857      // Per SP 800-73-5

/*******************************************************************************
 * Certificate Slot Identifiers
 ******************************************************************************/

typedef enum {
    PIV_CERT_SLOT_PIV_AUTH = 0,       // 0x9A key
    PIV_CERT_SLOT_CARD_AUTH,          // 0x9E key  
    PIV_CERT_SLOT_DIGITAL_SIG,        // 0x9C key
    PIV_CERT_SLOT_KEY_MGMT,           // 0x9D key
    PIV_CERT_SLOT_COUNT
} PivCertSlot;

/*******************************************************************************
 * Certificate Container Structure
 ******************************************************************************/

typedef struct {
    uint32_t tag;                     // BER-TLV tag
    const uint8_t* data;              // Full container (53 xx 70 xx <cert>...)
    uint16_t data_len;                // Container length
    bool present;                     // True if certificate is loaded
} PivCertificate;

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

/**
 * @brief Initialize PIV certificate storage
 * 
 * Loads demo certificates into memory. In production, would load from
 * secure storage.
 */
void piv_cert_init(void);

/**
 * @brief Get certificate by BER-TLV tag
 * 
 * @param tag Certificate tag (0x5FC101, 0x5FC105, etc.)
 * @param data Output: pointer to certificate container data
 * @param data_len Output: length of container
 * @return true if certificate found, false otherwise
 */
bool piv_cert_get_by_tag(uint32_t tag, const uint8_t** data, uint16_t* data_len);

/**
 * @brief Get certificate by key reference
 * 
 * @param key_ref Key reference (0x9A, 0x9C, 0x9D, 0x9E)
 * @param data Output: pointer to certificate container data
 * @param data_len Output: length of container
 * @return true if certificate found, false otherwise
 */
bool piv_cert_get_by_key_ref(uint8_t key_ref, const uint8_t** data, uint16_t* data_len);

/**
 * @brief Check if a tag is a certificate container tag
 * 
 * @param tag BER-TLV tag to check
 * @return true if tag is a certificate container
 */
bool piv_cert_is_cert_tag(uint32_t tag);

/**
 * @brief Get certificate slot from key reference
 * 
 * @param key_ref Key reference (0x9A, 0x9C, 0x9D, 0x9E)
 * @return Certificate slot or -1 if not found
 */
int piv_cert_slot_from_key_ref(uint8_t key_ref);

#ifdef __cplusplus
}
#endif
