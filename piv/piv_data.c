/**
 * @file piv_data.c
 * @brief PIV Data Container Implementation
 * 
 * Implements PIV data containers with demo/test data.
 * Real implementations would load from secure storage.
 */

#include "piv_data.h"
#include "piv_app.h"
#include <furi.h>
#include <string.h>

#define TAG "PIV_Data"

/*******************************************************************************
 * Demo Data: Discovery Object
 * 
 * Format per SP 800-73-5:
 *   7E LL                   - Discovery Object tag
 *     4F 0B <PIV AID>       - Application ID
 *     5F2F 02 <PIN policy>  - PIN Usage Policy
 * 
 * PIN Policy bytes:
 *   Byte 1: 0x40 = PIV Card Application PIN (ref 0x80) is the only PIN
 *   Byte 2: 0x00 = Primary finger OCC not available
 ******************************************************************************/

static const uint8_t demo_discovery_object[] = {
    // Discovery Object (tag 0x7E)
    0x7E, 0x12,  // Tag + length (18 bytes content)
    
    // PIV Card Application AID
    0x4F, 0x0B,  // AID tag + length
    PIV_AID_BYTES,  // Full PIV AID (11 bytes)
    
    // PIN Usage Policy
    0x5F, 0x2F, 0x02,  // PIN Policy tag + length
    0x40,              // Only PIV PIN (0x80) is available
    0x00               // No OCC (biometric) support
};

/*******************************************************************************
 * Demo Data: Card Capability Container (CCC)
 * 
 * Minimal CCC for GSC-IS compatibility.
 * Format per SP 800-73-5 Table 8.
 ******************************************************************************/

static const uint8_t demo_ccc[] = {
    // Card Identifier (mandatory)
    0xF0, 0x15,  // Tag + length (21 bytes)
    0xA0, 0x00, 0x00, 0x01, 0x16,  // GSC-RID
    0xFF,                          // Manufacturer ID (test)
    0x02,                          // Card type: JavaCard
    // Unique card ID (14 bytes) - demo value
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    
    // Capability Container version number (mandatory)
    0xF1, 0x01, 0x21,  // Version 2.1
    
    // Capability Grammar version number (mandatory)
    0xF2, 0x01, 0x21,  // Version 2.1
    
    // Applications CardURL (optional - empty)
    0xF3, 0x00,
    
    // PKCS#15 (optional - not supported)
    0xF4, 0x01, 0x00,
    
    // Registered Data Model number (mandatory)
    0xF5, 0x01, 0x10,  // PIV data model
    
    // Access Control Rule Table (optional - empty)
    0xF6, 0x00,
    
    // Card APDUs (deprecated - empty)
    0xF7, 0x00,
    
    // Redirection Tag (optional - empty)
    0xFA, 0x00,
    
    // Capability Tuples (deprecated - empty)
    0xFB, 0x00,
    
    // Status Tuples (deprecated - empty)
    0xFC, 0x00,
    
    // Next CCC (optional - empty)
    0xFD, 0x00,
    
    // Error Detection Code (mandatory)
    0xFE, 0x00  // No error detection in demo
};

/*******************************************************************************
 * Demo Data: Card Holder Unique Identifier (CHUID)
 * 
 * Minimal CHUID for testing.
 * Contains FASC-N, GUID, Expiration Date, and Error Detection.
 ******************************************************************************/

static const uint8_t demo_chuid[] = {
    // Buffer Length (optional)
    // 0xEE, ...
    
    // FASC-N (mandatory, 25 bytes)
    0x30, 0x19,  // Tag + length
    // Demo FASC-N: All zeros (invalid but recognizable for testing)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,
    
    // Organizational Identifier (optional)
    // 0x32, ...
    
    // DUNS (optional)
    // 0x33, ...
    
    // GUID (mandatory, 16 bytes UUID)
    0x34, 0x10,  // Tag + length
    // Demo UUID: 12345678-1234-5678-1234-567812345678
    0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x56, 0x78,
    0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x56, 0x78,
    
    // Expiration Date (mandatory, YYYYMMDD format)
    0x35, 0x08,  // Tag + length
    '2', '0', '3', '0', '1', '2', '3', '1',  // 2030-12-31
    
    // Cardholder UUID (optional)
    // 0x36, ...
    
    // Issuer Asymmetric Signature (conditional - omitted for demo)
    // In production, this would contain a CMS signature
    // 0x3E, ...
    
    // Error Detection Code (mandatory)
    0xFE, 0x00  // Empty for demo
};

/*******************************************************************************
 * PIV Data Context (Singleton)
 ******************************************************************************/

static PivDataContext piv_data_ctx = {
    .initialized = false,
};

/*******************************************************************************
 * Container Lookup Table
 ******************************************************************************/

typedef struct {
    uint32_t tag;
    const uint8_t* data;
    uint16_t data_len;
    bool requires_pin;
    const char* name;
} PivContainerEntry;

// Static container entries (populated at runtime)
static PivContainerEntry piv_containers[] = {
    {PIV_TAG_DISCOVERY, demo_discovery_object, sizeof(demo_discovery_object), false, "Discovery"},
    {PIV_TAG_CCC, demo_ccc, sizeof(demo_ccc), false, "CCC"},
    {PIV_TAG_CHUID, demo_chuid, sizeof(demo_chuid), false, "CHUID"},
    // Add more containers as implemented
    // {PIV_TAG_CERT_PIV_AUTH, NULL, 0, false, "PIV Auth Cert"},
    // {PIV_TAG_FINGERPRINTS, NULL, 0, true, "Fingerprints"},  // PIN required
    // {PIV_TAG_FACIAL_IMAGE, NULL, 0, true, "Facial Image"},  // PIN required
    {0, NULL, 0, false, NULL}  // Sentinel
};

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

void piv_data_init(void) {
    if(piv_data_ctx.initialized) {
        return;
    }
    
    // Copy demo data to context (allows modification in future)
    memcpy(piv_data_ctx.discovery_data, demo_discovery_object, sizeof(demo_discovery_object));
    piv_data_ctx.discovery_len = sizeof(demo_discovery_object);
    
    memcpy(piv_data_ctx.ccc_data, demo_ccc, sizeof(demo_ccc));
    piv_data_ctx.ccc_len = sizeof(demo_ccc);
    
    memcpy(piv_data_ctx.chuid_data, demo_chuid, sizeof(demo_chuid));
    piv_data_ctx.chuid_len = sizeof(demo_chuid);
    
    piv_data_ctx.initialized = true;
    
    FURI_LOG_I(TAG, "PIV data containers initialized");
    FURI_LOG_D(TAG, "  Discovery: %d bytes", piv_data_ctx.discovery_len);
    FURI_LOG_D(TAG, "  CCC: %d bytes", piv_data_ctx.ccc_len);
    FURI_LOG_D(TAG, "  CHUID: %d bytes", piv_data_ctx.chuid_len);
}

PivDataContext* piv_data_get_context(void) {
    return &piv_data_ctx;
}

bool piv_data_get_by_tag(
    uint32_t tag,
    const uint8_t** data,
    uint16_t* data_len,
    bool* requires_pin) {
    
    if(!piv_data_ctx.initialized) {
        piv_data_init();
    }
    
    // Search for container
    for(size_t i = 0; piv_containers[i].tag != 0; i++) {
        if(piv_containers[i].tag == tag) {
            if(piv_containers[i].data == NULL) {
                // Container defined but not implemented
                FURI_LOG_W(TAG, "Container %s not implemented", piv_containers[i].name);
                return false;
            }
            
            *data = piv_containers[i].data;
            *data_len = piv_containers[i].data_len;
            if(requires_pin) {
                *requires_pin = piv_containers[i].requires_pin;
            }
            
            FURI_LOG_D(TAG, "Found container %s (%d bytes)", 
                piv_containers[i].name, piv_containers[i].data_len);
            return true;
        }
    }
    
    FURI_LOG_W(TAG, "Container not found for tag 0x%06lX", (unsigned long)tag);
    return false;
}

bool piv_data_parse_tag(
    const uint8_t* data,
    uint16_t data_len,
    uint32_t* tag) {
    
    if(!data || !tag) {
        return false;
    }
    
    // PIV GET DATA format: 5C LL <tag bytes>
    // 5C = Tag List tag
    // LL = length of tag (1, 2, or 3 bytes)
    // tag bytes follow
    
    if(data_len < 2) {
        FURI_LOG_E(TAG, "Data too short for tag parsing");
        return false;
    }
    
    if(data[0] != 0x5C) {
        FURI_LOG_E(TAG, "Expected 5C (Tag List), got %02X", data[0]);
        return false;
    }
    
    uint8_t tag_len = data[1];
    
    if(data_len < (uint16_t)(2 + tag_len)) {
        FURI_LOG_E(TAG, "Data too short: need %d, have %d", 2 + tag_len, data_len);
        return false;
    }
    
    // Parse tag based on length
    switch(tag_len) {
        case 1:
            *tag = data[2];
            break;
        case 2:
            *tag = ((uint32_t)data[2] << 8) | data[3];
            break;
        case 3:
            *tag = ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 8) | data[4];
            break;
        default:
            FURI_LOG_E(TAG, "Unsupported tag length: %d", tag_len);
            return false;
    }
    
    FURI_LOG_D(TAG, "Parsed tag: 0x%06lX (len=%d)", (unsigned long)*tag, tag_len);
    return true;
}

bool piv_data_is_valid_tag(uint32_t tag) {
    // Check against known PIV tags
    switch(tag) {
        case PIV_TAG_CCC:
        case PIV_TAG_CHUID:
        case PIV_TAG_CERT_PIV_AUTH:
        case PIV_TAG_FINGERPRINTS:
        case PIV_TAG_SECURITY_OBJECT:
        case PIV_TAG_FACIAL_IMAGE:
        case PIV_TAG_CERT_CARD_AUTH:
        case PIV_TAG_CERT_DIGITAL_SIG:
        case PIV_TAG_CERT_KEY_MGMT:
        case PIV_TAG_PRINTED_INFO:
        case PIV_TAG_DISCOVERY:
        case PIV_TAG_KEY_HISTORY:
            return true;
        default:
            return false;
    }
}
