/**
 * @file piv_data.h
 * @brief PIV Data Container Definitions
 * 
 * Defines PIV data objects as per NIST SP 800-73-5 Part 1.
 * This includes Container IDs, BER-TLV Tags, and data structures.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * PIV Container IDs (for internal file system use)
 ******************************************************************************/

#define PIV_CONTAINER_CCC               0xDB00  // Card Capability Container
#define PIV_CONTAINER_CHUID             0x3000  // Card Holder Unique ID
#define PIV_CONTAINER_CERT_PIV_AUTH     0x0101  // X.509 Cert for PIV Auth
#define PIV_CONTAINER_FINGERPRINTS      0x6010  // Cardholder Fingerprints
#define PIV_CONTAINER_SECURITY_OBJECT   0x9000  // Security Object
#define PIV_CONTAINER_FACIAL_IMAGE      0x6030  // Cardholder Facial Image
#define PIV_CONTAINER_CERT_CARD_AUTH    0x0500  // X.509 Cert for Card Auth
#define PIV_CONTAINER_CERT_DIGITAL_SIG  0x0100  // X.509 Cert for Digital Sig
#define PIV_CONTAINER_CERT_KEY_MGMT     0x0102  // X.509 Cert for Key Mgmt
#define PIV_CONTAINER_PRINTED_INFO      0x3001  // Printed Information
#define PIV_CONTAINER_DISCOVERY         0x6050  // Discovery Object
#define PIV_CONTAINER_KEY_HISTORY       0x6060  // Key History Object

/*******************************************************************************
 * PIV BER-TLV Tags (3-byte tags used in GET DATA command)
 * 
 * These tags are used as P1-P2 or in the data field for PIV GET DATA (INS=0xCB)
 ******************************************************************************/

#define PIV_TAG_CCC                     0x5FC107
#define PIV_TAG_CHUID                   0x5FC102
#define PIV_TAG_CERT_PIV_AUTH           0x5FC105
#define PIV_TAG_FINGERPRINTS            0x5FC103
#define PIV_TAG_SECURITY_OBJECT         0x5FC106
#define PIV_TAG_FACIAL_IMAGE            0x5FC108
#define PIV_TAG_CERT_CARD_AUTH          0x5FC101
#define PIV_TAG_CERT_DIGITAL_SIG        0x5FC10A
#define PIV_TAG_CERT_KEY_MGMT           0x5FC10B
#define PIV_TAG_PRINTED_INFO            0x5FC109
#define PIV_TAG_DISCOVERY               0x7E
#define PIV_TAG_KEY_HISTORY             0x5FC10C

/*******************************************************************************
 * PIV Container Maximum Sizes (per SP 800-73-5)
 ******************************************************************************/

#define PIV_MAX_CCC_SIZE                170
#define PIV_MAX_CHUID_SIZE              2881
#define PIV_MAX_CERT_SIZE               1857
#define PIV_MAX_FINGERPRINTS_SIZE       4006
#define PIV_MAX_SECURITY_OBJECT_SIZE    1336
#define PIV_MAX_FACIAL_IMAGE_SIZE       12710
#define PIV_MAX_PRINTED_INFO_SIZE       245
#define PIV_MAX_DISCOVERY_SIZE          19
#define PIV_MAX_KEY_HISTORY_SIZE        128

/*******************************************************************************
 * PIV Internal TLV Tags (within data objects)
 ******************************************************************************/

// Card Capability Container (CCC) internal tags
#define PIV_CCC_TAG_CARD_ID             0xF0
#define PIV_CCC_TAG_CAPABILITY_CONT_VER 0xF1
#define PIV_CCC_TAG_CAPABILITY_GRAMMAR  0xF2
#define PIV_CCC_TAG_APP_CARD_URL        0xF3
#define PIV_CCC_TAG_PKCS15              0xF4
#define PIV_CCC_TAG_REG_DATA_MODEL_NUM  0xF5
#define PIV_CCC_TAG_ACR_TABLE           0xF6
#define PIV_CCC_TAG_CARD_APDUS          0xF7
#define PIV_CCC_TAG_REDIRECTION_TAG     0xFA
#define PIV_CCC_TAG_CAPABILITY_TUPLES   0xFB
#define PIV_CCC_TAG_STATUS_TUPLES       0xFC
#define PIV_CCC_TAG_NEXT_CCC            0xFD
#define PIV_CCC_TAG_ERROR_DETECTION     0xFE

// CHUID internal tags
#define PIV_CHUID_TAG_BUFFER_LENGTH     0xEE
#define PIV_CHUID_TAG_FASC_N            0x30
#define PIV_CHUID_TAG_ORG_ID            0x32
#define PIV_CHUID_TAG_DUNS              0x33
#define PIV_CHUID_TAG_GUID              0x34
#define PIV_CHUID_TAG_EXPIRATION_DATE   0x35
#define PIV_CHUID_TAG_CARDHOLDER_UUID   0x36
#define PIV_CHUID_TAG_ISSUER_SIGNATURE  0x3E
#define PIV_CHUID_TAG_ERROR_DETECTION   0xFE

// Discovery Object internal tags
#define PIV_DISC_TAG_AID                0x4F
#define PIV_DISC_TAG_PIN_POLICY         0x5F2F

/*******************************************************************************
 * PIV Data Object Structure
 * 
 * Represents a single PIV data container with its metadata
 ******************************************************************************/

typedef struct {
    uint32_t tag;               // BER-TLV tag (e.g., 0x5FC102 for CHUID)
    uint16_t container_id;      // Container ID (e.g., 0x3000)
    const uint8_t* data;        // Pointer to data
    uint16_t data_len;          // Current data length
    uint16_t max_size;          // Maximum allowed size
    bool requires_pin;          // True if PIN verification required for read
} PivDataObject;

/*******************************************************************************
 * PIV Data Context
 * 
 * Manages all PIV data containers
 ******************************************************************************/

typedef struct {
    // Discovery Object (always present, 19 bytes max)
    uint8_t discovery_data[PIV_MAX_DISCOVERY_SIZE];
    uint16_t discovery_len;
    
    // Card Capability Container (170 bytes max)
    uint8_t ccc_data[PIV_MAX_CCC_SIZE];
    uint16_t ccc_len;
    
    // Card Holder Unique Identifier (2881 bytes max, but we use smaller demo)
    uint8_t chuid_data[256];  // Demo size
    uint16_t chuid_len;
    
    // Initialization status
    bool initialized;
} PivDataContext;

/*******************************************************************************
 * PIV Data Functions
 ******************************************************************************/

/**
 * @brief Initialize PIV data containers with default values
 * 
 * Sets up the Discovery Object, CCC, and CHUID with demo data.
 */
void piv_data_init(void);

/**
 * @brief Get PIV data context
 * 
 * @return Pointer to PIV data context
 */
PivDataContext* piv_data_get_context(void);

/**
 * @brief Get data object by BER-TLV tag
 * 
 * @param tag BER-TLV tag (e.g., 0x5FC102 for CHUID)
 * @param data Output: pointer to data buffer
 * @param data_len Output: length of data
 * @param requires_pin Output: whether PIN is required (can be NULL)
 * @return true if object found, false otherwise
 */
bool piv_data_get_by_tag(
    uint32_t tag,
    const uint8_t** data,
    uint16_t* data_len,
    bool* requires_pin);

/**
 * @brief Parse tag from GET DATA command data field
 * 
 * PIV GET DATA uses format: 5C LL TT TT TT
 * where 5C is tag list tag, LL is length, TT TT TT is the 3-byte tag
 * 
 * @param data Data field from GET DATA command
 * @param data_len Length of data field
 * @param tag Output: parsed tag
 * @return true if valid tag found, false otherwise
 */
bool piv_data_parse_tag(
    const uint8_t* data,
    uint16_t data_len,
    uint32_t* tag);

/**
 * @brief Check if a tag is a valid PIV container tag
 * 
 * @param tag BER-TLV tag to check
 * @return true if valid PIV tag
 */
bool piv_data_is_valid_tag(uint32_t tag);

#ifdef __cplusplus
}
#endif
