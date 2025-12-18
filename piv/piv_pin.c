/**
 * @file piv_pin.c
 * @brief PIV PIN Management Implementation
 * 
 * Implements PIV-specific PIN commands per NIST SP 800-73-5 Part 2.
 */

#include "piv_pin.h"
#include "../iso7816/iso7816_response.h"
#include "../iso7816/commands/iso7816_cmd_verify.h"
#include <furi.h>
#include <string.h>

#define TAG "PIV_PIN"

/*******************************************************************************
 * Internal Functions
 ******************************************************************************/

/**
 * @brief Find PIN by reference in filesystem
 */
static Iso7816PinData* find_pin(Iso7816FileSystem* fs, uint8_t ref) {
    if(!fs) return NULL;
    
    for(uint8_t i = 0; i < ISO7816_MAX_PIN_REFS; i++) {
        if(fs->pins[i].active && fs->pins[i].reference == ref) {
            return &fs->pins[i];
        }
    }
    return NULL;
}

/**
 * @brief Compare PIN with constant-time comparison
 */
static bool compare_pin_secure(
    const uint8_t* stored,
    uint8_t stored_len,
    const uint8_t* input,
    uint8_t input_len) {
    
    if(stored_len != input_len) return false;
    
    uint8_t result = 0;
    for(uint8_t i = 0; i < stored_len; i++) {
        result |= stored[i] ^ input[i];
    }
    return result == 0;
}

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

bool piv_pin_validate_format(const uint8_t* pin, uint16_t len, uint8_t* actual_len) {
    if(!pin || len == 0) return false;
    
    // Strip trailing 0xFF padding
    uint8_t real_len = len;
    while(real_len > 0 && pin[real_len - 1] == 0xFF) {
        real_len--;
    }
    
    // Check length: 6-8 digits
    if(real_len < PIV_PIN_MIN_LENGTH || real_len > PIV_PIN_MAX_LENGTH) {
        FURI_LOG_W(TAG, "PIN length %d out of range [%d-%d]", 
            real_len, PIV_PIN_MIN_LENGTH, PIV_PIN_MAX_LENGTH);
        return false;
    }
    
    // Check that all characters are ASCII digits 0-9
    for(uint8_t i = 0; i < real_len; i++) {
        if(pin[i] < '0' || pin[i] > '9') {
            FURI_LOG_W(TAG, "PIN contains non-digit at position %d: 0x%02X", i, pin[i]);
            return false;
        }
    }
    
    if(actual_len) {
        *actual_len = real_len;
    }
    
    return true;
}

bool piv_pin_init(Iso7816FileSystem* fs) {
    if(!fs) return false;
    
    // Register PIV PIN (0x80) with default value
    bool pin_ok = iso7816_register_pin(
        fs,
        PIV_PIN_REF,
        (const uint8_t*)PIV_DEFAULT_PIN,
        PIV_DEFAULT_PIN_LEN,
        PIV_PIN_MAX_RETRIES
    );
    
    if(!pin_ok) {
        FURI_LOG_E(TAG, "Failed to register PIV PIN (0x80)");
        return false;
    }
    
    // Register PUK (0x81) with default value
    bool puk_ok = iso7816_register_pin(
        fs,
        PIV_PUK_REF,
        (const uint8_t*)PIV_DEFAULT_PUK,
        PIV_DEFAULT_PUK_LEN,
        PIV_PUK_MAX_RETRIES
    );
    
    if(!puk_ok) {
        FURI_LOG_E(TAG, "Failed to register PUK (0x81)");
        return false;
    }
    
    FURI_LOG_I(TAG, "PIV PINs initialized (PIN=0x80, PUK=0x81)");
    return true;
}

bool piv_pin_is_verified(Iso7816FileSystem* fs) {
    Iso7816PinData* pin = find_pin(fs, PIV_PIN_REF);
    return pin && pin->verified;
}

void piv_pin_clear_verification(Iso7816FileSystem* fs) {
    Iso7816PinData* pin = find_pin(fs, PIV_PIN_REF);
    if(pin) {
        pin->verified = false;
        FURI_LOG_D(TAG, "PIV PIN verification cleared");
    }
}

void piv_cmd_change_reference_data(
    Iso7816FileSystem* fs,
    const ISO7816_Command_APDU* command,
    ISO7816_Response_APDU* response) {
    
    furi_assert(fs);
    furi_assert(command);
    furi_assert(response);
    
    response->DataLen = 0;
    
    uint8_t p1 = command->P1;
    uint8_t p2 = command->P2;
    
    FURI_LOG_D(TAG, "CHANGE REF DATA: P1=%02X P2=%02X Lc=%d", p1, p2, command->Lc);
    
    // P1 must be 0x00
    if(p1 != 0x00) {
        FURI_LOG_W(TAG, "Invalid P1=%02X (must be 00)", p1);
        iso7816_set_response(response, ISO7816_RESPONSE_WRONG_PARAMETERS_P1_P2);
        return;
    }
    
    // P2 is the key reference (0x80 for PIN, 0x81 for PUK)
    if(p2 != PIV_PIN_REF && p2 != PIV_PUK_REF) {
        FURI_LOG_W(TAG, "Invalid PIN reference: %02X", p2);
        iso7816_set_response(response, ISO7816_RESPONSE_REF_DATA_NOT_FOUND);
        return;
    }
    
    // Data format: <old value> <new value>
    // For PIN: 8 bytes old + 8 bytes new = 16 bytes
    // For PUK: 8 bytes old + 8 bytes new = 16 bytes
    if(command->Lc != 16) {
        FURI_LOG_W(TAG, "Wrong data length: %d (expected 16)", command->Lc);
        iso7816_set_response(response, ISO7816_RESPONSE_WRONG_LENGTH);
        return;
    }
    
    const uint8_t* old_value = command->Data;
    const uint8_t* new_value = command->Data + 8;
    
    // Find the PIN/PUK
    Iso7816PinData* pin = find_pin(fs, p2);
    if(!pin) {
        FURI_LOG_E(TAG, "PIN reference %02X not found", p2);
        iso7816_set_response(response, ISO7816_RESPONSE_REF_DATA_NOT_FOUND);
        return;
    }
    
    // Check if blocked
    if(pin->status == ISO7816_PIN_STATUS_BLOCKED) {
        FURI_LOG_W(TAG, "PIN %02X is blocked", p2);
        iso7816_set_response(response, ISO7816_RESPONSE_AUTH_BLOCKED);
        return;
    }
    
    // Validate old value format
    uint8_t old_len = 0;
    if(!piv_pin_validate_format(old_value, 8, &old_len)) {
        FURI_LOG_W(TAG, "Invalid old PIN format");
        iso7816_set_response(response, ISO7816_RESPONSE_WRONG_DATA);
        return;
    }
    
    // Validate new value format
    uint8_t new_len = 0;
    if(!piv_pin_validate_format(new_value, 8, &new_len)) {
        FURI_LOG_W(TAG, "Invalid new PIN format");
        iso7816_set_response(response, ISO7816_RESPONSE_WRONG_DATA);
        return;
    }
    
    // Verify old value
    if(!compare_pin_secure(pin->pin, pin->pin_len, old_value, old_len)) {
        // Wrong old PIN - decrement retry counter
        if(pin->retries_remaining > 0) {
            pin->retries_remaining--;
        }
        
        if(pin->retries_remaining == 0) {
            pin->status = ISO7816_PIN_STATUS_BLOCKED;
            FURI_LOG_W(TAG, "PIN %02X is now BLOCKED", p2);
            iso7816_set_response(response, ISO7816_RESPONSE_AUTH_BLOCKED);
            return;
        }
        
        FURI_LOG_W(TAG, "Wrong old PIN, %d retries left", pin->retries_remaining);
        response->SW1 = 0x63;
        response->SW2 = 0xC0 | (pin->retries_remaining & 0x0F);
        return;
    }
    
    // Update PIN
    memcpy(pin->pin, new_value, new_len);
    pin->pin_len = new_len;
    pin->retries_remaining = pin->max_retries;
    pin->verified = false;  // Require re-verification after change
    
    FURI_LOG_I(TAG, "PIN %02X changed successfully", p2);
    iso7816_set_response(response, ISO7816_RESPONSE_OK);
}

void piv_cmd_reset_retry_counter(
    Iso7816FileSystem* fs,
    const ISO7816_Command_APDU* command,
    ISO7816_Response_APDU* response) {
    
    furi_assert(fs);
    furi_assert(command);
    furi_assert(response);
    
    response->DataLen = 0;
    
    uint8_t p1 = command->P1;
    uint8_t p2 = command->P2;
    
    FURI_LOG_D(TAG, "RESET RETRY: P1=%02X P2=%02X Lc=%d", p1, p2, command->Lc);
    
    // P1 must be 0x00 (use PUK and set new PIN)
    if(p1 != 0x00) {
        FURI_LOG_W(TAG, "Invalid P1=%02X (must be 00)", p1);
        iso7816_set_response(response, ISO7816_RESPONSE_WRONG_PARAMETERS_P1_P2);
        return;
    }
    
    // P2 must be 0x80 (can only reset the PIV PIN, not PUK)
    if(p2 != PIV_PIN_REF) {
        FURI_LOG_W(TAG, "Can only reset PIN ref 0x80, got %02X", p2);
        iso7816_set_response(response, ISO7816_RESPONSE_WRONG_PARAMETERS_P1_P2);
        return;
    }
    
    // Data format: <PUK (8 bytes)> <new PIN (8 bytes)>
    if(command->Lc != 16) {
        FURI_LOG_W(TAG, "Wrong data length: %d (expected 16)", command->Lc);
        iso7816_set_response(response, ISO7816_RESPONSE_WRONG_LENGTH);
        return;
    }
    
    const uint8_t* puk_data = command->Data;
    const uint8_t* new_pin = command->Data + 8;
    
    // Find the PIN
    Iso7816PinData* pin = find_pin(fs, PIV_PIN_REF);
    if(!pin) {
        FURI_LOG_E(TAG, "PIV PIN not found");
        iso7816_set_response(response, ISO7816_RESPONSE_REF_DATA_NOT_FOUND);
        return;
    }
    
    // Find the PUK
    Iso7816PinData* puk = find_pin(fs, PIV_PUK_REF);
    if(!puk) {
        FURI_LOG_E(TAG, "PUK not found");
        iso7816_set_response(response, ISO7816_RESPONSE_REF_DATA_NOT_FOUND);
        return;
    }
    
    // Check if PUK is blocked
    if(puk->status == ISO7816_PIN_STATUS_BLOCKED) {
        FURI_LOG_W(TAG, "PUK is blocked - cannot reset PIN");
        iso7816_set_response(response, ISO7816_RESPONSE_AUTH_BLOCKED);
        return;
    }
    
    // Validate PUK format (8 digits)
    uint8_t puk_len = 0;
    // PUK is always exactly 8 digits, no padding
    for(uint8_t i = 0; i < 8; i++) {
        if(puk_data[i] >= '0' && puk_data[i] <= '9') {
            puk_len++;
        } else if(puk_data[i] == 0xFF) {
            break;  // Padding
        } else {
            FURI_LOG_W(TAG, "Invalid PUK character at %d: 0x%02X", i, puk_data[i]);
            iso7816_set_response(response, ISO7816_RESPONSE_WRONG_DATA);
            return;
        }
    }
    
    if(puk_len < 8) {
        FURI_LOG_W(TAG, "PUK too short: %d (expected 8)", puk_len);
        iso7816_set_response(response, ISO7816_RESPONSE_WRONG_DATA);
        return;
    }
    
    // Validate new PIN format
    uint8_t new_len = 0;
    if(!piv_pin_validate_format(new_pin, 8, &new_len)) {
        FURI_LOG_W(TAG, "Invalid new PIN format");
        iso7816_set_response(response, ISO7816_RESPONSE_WRONG_DATA);
        return;
    }
    
    // Verify PUK
    if(!compare_pin_secure(puk->pin, puk->pin_len, puk_data, puk_len)) {
        // Wrong PUK - decrement PUK retry counter
        if(puk->retries_remaining > 0) {
            puk->retries_remaining--;
        }
        
        if(puk->retries_remaining == 0) {
            puk->status = ISO7816_PIN_STATUS_BLOCKED;
            FURI_LOG_W(TAG, "PUK is now BLOCKED");
            iso7816_set_response(response, ISO7816_RESPONSE_AUTH_BLOCKED);
            return;
        }
        
        FURI_LOG_W(TAG, "Wrong PUK, %d retries left", puk->retries_remaining);
        response->SW1 = 0x63;
        response->SW2 = 0xC0 | (puk->retries_remaining & 0x0F);
        return;
    }
    
    // Reset PIN
    memcpy(pin->pin, new_pin, new_len);
    pin->pin_len = new_len;
    pin->retries_remaining = pin->max_retries;
    pin->status = ISO7816_PIN_STATUS_ACTIVE;  // Unblock
    pin->verified = false;
    
    FURI_LOG_I(TAG, "PIN reset successfully via PUK");
    iso7816_set_response(response, ISO7816_RESPONSE_OK);
}
