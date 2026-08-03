/**
 * @file piv_cert.c
 * @brief PIV X.509 Certificate Storage Implementation
 * 
 * Provides demo X.509 certificates for PIV testing.
 * These are minimal self-signed certificates for development/testing only.
 * 
 * Production implementations should:
 * - Load certificates from secure storage
 * - Support certificate import via PUT DATA
 * - Validate certificate chains
 */

#include "piv_cert.h"
#include "piv_keys.h"
#include <furi.h>
#include <string.h>

#define TAG "PIV_Cert"

/*******************************************************************************
 * Demo X.509 Certificate for Card Authentication (0x9E)
 * 
 * This is a minimal self-signed X.509v3 certificate for testing.
 * Subject: CN=Flipper Zero PIV Card Auth
 * Key: ECC P-256 (matches demo key in piv_keys.c)
 * 
 * Certificate wrapped in PIV container format:
 *   53 <len>
 *     70 <len> <x509 cert>
 *     71 01 00 (uncompressed)
 *     FE 00 (no LRC)
 ******************************************************************************/

/*******************************************************************************
 * PIV Certificate Container Wrapper
 * 
 * Wraps X.509 cert in PIV container format:
 *   53 <len>
 *     70 <len> <x509 cert>
 *     71 01 00 (cert info: uncompressed)
 *     FE 00 (error detection: none)
 ******************************************************************************/

// Pre-computed container for Card Auth certificate
// 53 82 01 4C = Container tag + 2-byte length (332 bytes)
//   70 82 01 45 = Cert tag + 2-byte length (325 bytes)
//     <x509 cert>
//   71 01 00 = CertInfo tag, len 1, value 0 (uncompressed)
//   FE 00 = LRC tag, len 0
static const uint8_t demo_cert_container_card_auth[] = {
    // Certificate container (tag 0x53)
    0x53, 0x82, 0x01, 0x4C,  // Tag + 2-byte length (332 bytes content)
    
    // X.509 Certificate (tag 0x70)
    0x70, 0x82, 0x01, 0x45,  // Tag + 2-byte length (325 bytes)
    // Certificate data follows inline
    // SEQUENCE (Certificate)
    0x30, 0x82, 0x01, 0x45,
    // TBSCertificate SEQUENCE
    0x30, 0x82, 0x00, 0xEE,
    // Version [0]
    0xA0, 0x03, 0x02, 0x01, 0x02,
    // Serial Number
    0x02, 0x01, 0x01,
    // Signature Algorithm (ECDSA with SHA-256)
    0x30, 0x0A, 0x06, 0x08,
    0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02,
    // Issuer: CN=Flipper Zero CA
    0x30, 0x1B,
    0x31, 0x19, 0x30, 0x17,
    0x06, 0x03, 0x55, 0x04, 0x03,
    0x0C, 0x10,
    'F', 'l', 'i', 'p', 'p', 'e', 'r', ' ',
    'Z', 'e', 'r', 'o', ' ', 'C', 'A', 0x00,
    // Validity
    0x30, 0x1E,
    0x17, 0x0D, '2', '5', '0', '1', '0', '1', '0', '0', '0', '0', '0', '0', 'Z',
    0x17, 0x0D, '3', '5', '1', '2', '3', '1', '2', '3', '5', '9', '5', '9', 'Z',
    // Subject: CN=Flipper Zero Card Auth
    0x30, 0x23,
    0x31, 0x21, 0x30, 0x1F,
    0x06, 0x03, 0x55, 0x04, 0x03,
    0x0C, 0x18,
    'F', 'l', 'i', 'p', 'p', 'e', 'r', ' ',
    'Z', 'e', 'r', 'o', ' ', 'C', 'a', 'r',
    'd', ' ', 'A', 'u', 't', 'h', 0x00, 0x00,
    // SubjectPublicKeyInfo
    0x30, 0x59,
    0x30, 0x13,
    0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01,
    0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07,
    0x03, 0x42, 0x00,
    0x04,
    // Public key X
    0xE1, 0x84, 0x29, 0x8B, 0xFC, 0x4A, 0x32, 0x6B,
    0x45, 0x6E, 0x7B, 0xB2, 0x3E, 0x4A, 0x5E, 0x6D,
    0x7A, 0x8B, 0x9C, 0x0D, 0x1E, 0x2F, 0x3A, 0x4B,
    0x5C, 0x6D, 0x7E, 0x8F, 0x9A, 0x0B, 0x1C, 0x2D,
    // Public key Y
    0x3E, 0x4F, 0x5A, 0x6B, 0x7C, 0x8D, 0x9E, 0x0F,
    0x1A, 0x2B, 0x3C, 0x4D, 0x5E, 0x6F, 0x7A, 0x8B,
    0x9C, 0x0D, 0x1E, 0x2F, 0x3A, 0x4B, 0x5C, 0x6D,
    0x7E, 0x8F, 0x9A, 0x0B, 0x1C, 0x2D, 0x3E, 0x4F,
    // Extensions
    0xA3, 0x32,
    0x30, 0x30,
    0x30, 0x0E,
    0x06, 0x03, 0x55, 0x1D, 0x0F,
    0x01, 0x01, 0xFF,
    0x04, 0x04, 0x03, 0x02, 0x07, 0x80,
    0x30, 0x1E,
    0x06, 0x03, 0x55, 0x1D, 0x25,
    0x01, 0x01, 0x00,
    0x04, 0x14, 0x30, 0x12,
    0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0xC4, 0x0A, 0x03, 0x06,
    0x06, 0x04, 0x2B, 0x06, 0x01, 0x02,
    // Signature Algorithm
    0x30, 0x0A, 0x06, 0x08,
    0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02,
    // Signature (demo)
    0x03, 0x47, 0x00,
    0x30, 0x44,
    0x02, 0x20,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
    0x02, 0x20,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30,
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40,
    
    // Certificate Info (tag 0x71)
    0x71, 0x01, 0x00,  // Uncompressed
    
    // Error Detection Code (tag 0xFE)
    0xFE, 0x00,  // No error detection
};

/*******************************************************************************
 * Demo Certificate for PIV Authentication (0x9A)
 * Similar structure but different subject
 ******************************************************************************/

static const uint8_t demo_cert_container_piv_auth[] = {
    // Certificate container (tag 0x53)
    0x53, 0x82, 0x01, 0x4C,
    
    // X.509 Certificate (tag 0x70)
    0x70, 0x82, 0x01, 0x45,
    // Certificate
    0x30, 0x82, 0x01, 0x45,
    // TBSCertificate
    0x30, 0x82, 0x00, 0xEE,
    // Version
    0xA0, 0x03, 0x02, 0x01, 0x02,
    // Serial
    0x02, 0x01, 0x02,  // Serial = 2
    // Sig Alg
    0x30, 0x0A, 0x06, 0x08,
    0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02,
    // Issuer
    0x30, 0x1B,
    0x31, 0x19, 0x30, 0x17,
    0x06, 0x03, 0x55, 0x04, 0x03,
    0x0C, 0x10,
    'F', 'l', 'i', 'p', 'p', 'e', 'r', ' ',
    'Z', 'e', 'r', 'o', ' ', 'C', 'A', 0x00,
    // Validity
    0x30, 0x1E,
    0x17, 0x0D, '2', '5', '0', '1', '0', '1', '0', '0', '0', '0', '0', '0', 'Z',
    0x17, 0x0D, '3', '5', '1', '2', '3', '1', '2', '3', '5', '9', '5', '9', 'Z',
    // Subject: CN=Flipper Zero PIV Auth
    0x30, 0x23,
    0x31, 0x21, 0x30, 0x1F,
    0x06, 0x03, 0x55, 0x04, 0x03,
    0x0C, 0x18,
    'F', 'l', 'i', 'p', 'p', 'e', 'r', ' ',
    'Z', 'e', 'r', 'o', ' ', 'P', 'I', 'V',
    ' ', 'A', 'u', 't', 'h', 0x00, 0x00, 0x00,
    // SubjectPublicKeyInfo
    0x30, 0x59,
    0x30, 0x13,
    0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01,
    0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07,
    0x03, 0x42, 0x00,
    0x04,
    // Different public key for PIV Auth slot
    0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18,
    0x29, 0x3A, 0x4B, 0x5C, 0x6D, 0x7E, 0x8F, 0x90,
    0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18,
    0x29, 0x3A, 0x4B, 0x5C, 0x6D, 0x7E, 0x8F, 0x90,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00,
    // Extensions
    0xA3, 0x32,
    0x30, 0x30,
    0x30, 0x0E,
    0x06, 0x03, 0x55, 0x1D, 0x0F,
    0x01, 0x01, 0xFF,
    0x04, 0x04, 0x03, 0x02, 0x07, 0x80,
    0x30, 0x1E,
    0x06, 0x03, 0x55, 0x1D, 0x25,
    0x01, 0x01, 0x00,
    0x04, 0x14, 0x30, 0x12,
    0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0xC4, 0x0A, 0x03, 0x04,  // PIV Auth EKU
    0x06, 0x04, 0x2B, 0x06, 0x01, 0x01,
    // Signature Alg
    0x30, 0x0A, 0x06, 0x08,
    0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02,
    // Signature
    0x03, 0x47, 0x00,
    0x30, 0x44,
    0x02, 0x20,
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
    0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
    0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
    0x02, 0x20,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00,
    
    // Certificate Info
    0x71, 0x01, 0x00,
    // LRC
    0xFE, 0x00,
};

/*******************************************************************************
 * Certificate Storage
 ******************************************************************************/

static PivCertificate piv_certificates[PIV_CERT_SLOT_COUNT];
static bool piv_cert_initialized = false;

/*******************************************************************************
 * Tag to Slot Mapping
 ******************************************************************************/

static const struct {
    uint32_t tag;
    PivCertSlot slot;
    uint8_t key_ref;
    const char* name;
} cert_tag_map[] = {
    {PIV_CERT_TAG_PIV_AUTH,    PIV_CERT_SLOT_PIV_AUTH,    0x9A, "PIV Auth"},
    {PIV_CERT_TAG_CARD_AUTH,   PIV_CERT_SLOT_CARD_AUTH,   0x9E, "Card Auth"},
    {PIV_CERT_TAG_DIGITAL_SIG, PIV_CERT_SLOT_DIGITAL_SIG, 0x9C, "Digital Sig"},
    {PIV_CERT_TAG_KEY_MGMT,    PIV_CERT_SLOT_KEY_MGMT,    0x9D, "Key Mgmt"},
    {0, 0, 0, NULL}  // Sentinel
};

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

void piv_cert_init(void) {
    if(piv_cert_initialized) {
        return;
    }
    
    // Clear all slots
    memset(piv_certificates, 0, sizeof(piv_certificates));
    
    // Load Card Authentication certificate (0x9E)
    piv_certificates[PIV_CERT_SLOT_CARD_AUTH].tag = PIV_CERT_TAG_CARD_AUTH;
    piv_certificates[PIV_CERT_SLOT_CARD_AUTH].data = demo_cert_container_card_auth;
    piv_certificates[PIV_CERT_SLOT_CARD_AUTH].data_len = sizeof(demo_cert_container_card_auth);
    piv_certificates[PIV_CERT_SLOT_CARD_AUTH].present = true;
    
    // Load PIV Authentication certificate (0x9A)
    piv_certificates[PIV_CERT_SLOT_PIV_AUTH].tag = PIV_CERT_TAG_PIV_AUTH;
    piv_certificates[PIV_CERT_SLOT_PIV_AUTH].data = demo_cert_container_piv_auth;
    piv_certificates[PIV_CERT_SLOT_PIV_AUTH].data_len = sizeof(demo_cert_container_piv_auth);
    piv_certificates[PIV_CERT_SLOT_PIV_AUTH].present = true;
    
    // Digital Signature and Key Management certificates not present in demo
    piv_certificates[PIV_CERT_SLOT_DIGITAL_SIG].tag = PIV_CERT_TAG_DIGITAL_SIG;
    piv_certificates[PIV_CERT_SLOT_DIGITAL_SIG].present = false;
    
    piv_certificates[PIV_CERT_SLOT_KEY_MGMT].tag = PIV_CERT_TAG_KEY_MGMT;
    piv_certificates[PIV_CERT_SLOT_KEY_MGMT].present = false;
    
    piv_cert_initialized = true;
    
    FURI_LOG_I(TAG, "PIV certificates initialized");
    FURI_LOG_D(TAG, "  Card Auth (9E): %d bytes", 
        piv_certificates[PIV_CERT_SLOT_CARD_AUTH].data_len);
    FURI_LOG_D(TAG, "  PIV Auth (9A): %d bytes",
        piv_certificates[PIV_CERT_SLOT_PIV_AUTH].data_len);
}

bool piv_cert_get_by_tag(uint32_t tag, const uint8_t** data, uint16_t* data_len) {
    if(!piv_cert_initialized) {
        piv_cert_init();
    }
    
    for(size_t i = 0; cert_tag_map[i].name != NULL; i++) {
        if(cert_tag_map[i].tag == tag) {
            PivCertSlot slot = cert_tag_map[i].slot;
            
            if(!piv_certificates[slot].present) {
                FURI_LOG_W(TAG, "Certificate %s not present", cert_tag_map[i].name);
                return false;
            }
            
            *data = piv_certificates[slot].data;
            *data_len = piv_certificates[slot].data_len;
            
            FURI_LOG_D(TAG, "Found certificate %s (%d bytes)", 
                cert_tag_map[i].name, piv_certificates[slot].data_len);
            return true;
        }
    }
    
    FURI_LOG_W(TAG, "Unknown certificate tag 0x%06lX", (unsigned long)tag);
    return false;
}

bool piv_cert_get_by_key_ref(uint8_t key_ref, const uint8_t** data, uint16_t* data_len) {
    if(!piv_cert_initialized) {
        piv_cert_init();
    }
    
    for(size_t i = 0; cert_tag_map[i].name != NULL; i++) {
        if(cert_tag_map[i].key_ref == key_ref) {
            return piv_cert_get_by_tag(cert_tag_map[i].tag, data, data_len);
        }
    }
    
    FURI_LOG_W(TAG, "Unknown key reference 0x%02X", key_ref);
    return false;
}

bool piv_cert_is_cert_tag(uint32_t tag) {
    switch(tag) {
        case PIV_CERT_TAG_PIV_AUTH:
        case PIV_CERT_TAG_CARD_AUTH:
        case PIV_CERT_TAG_DIGITAL_SIG:
        case PIV_CERT_TAG_KEY_MGMT:
            return true;
        default:
            return false;
    }
}

int piv_cert_slot_from_key_ref(uint8_t key_ref) {
    for(size_t i = 0; cert_tag_map[i].name != NULL; i++) {
        if(cert_tag_map[i].key_ref == key_ref) {
            return (int)cert_tag_map[i].slot;
        }
    }
    return -1;
}
