/**
 * @file piv_app.c
 * @brief PIV Application Implementation
 * 
 * Implements the PIV Card Application as per NIST SP 800-73-5.
 */

#include "piv_app.h"
#include "piv_data.h"
#include "../iso7816/iso7816_response.h"
#include "../iso7816/iso7816_t0_apdu.h"
#include <furi.h>
#include <string.h>

#define TAG "PIV_App"

/*******************************************************************************
 * PIV AID Definition
 ******************************************************************************/

static const uint8_t piv_aid[] = {PIV_AID_BYTES};

/*******************************************************************************
 * PIV Application Context (Singleton)
 ******************************************************************************/

static PivAppContext piv_context = {
    .selected = false,
    .pin_verified = false,
    .security_status = 0,
    .fs = NULL,
};

/*******************************************************************************
 * PIV Application Callbacks
 ******************************************************************************/

/**
 * @brief Called when PIV application is selected
 */
static void piv_app_on_select(void* context) {
    UNUSED(context);
    
    FURI_LOG_I(TAG, "PIV Application SELECTED");
    
    piv_context.selected = true;
    piv_context.pin_verified = false;  // Reset PIN state on each selection
    piv_context.security_status = 0;
}

/**
 * @brief Called when PIV application is deselected
 */
static void piv_app_on_deselect(void* context) {
    UNUSED(context);
    
    FURI_LOG_I(TAG, "PIV Application DESELECTED");
    
    piv_context.selected = false;
    piv_context.pin_verified = false;
    piv_context.security_status = 0;
}

/**
 * @brief Handle PIV GET DATA command (INS=0xCB)
 * 
 * PIV uses INS=0xCB (odd) for BER-TLV encoded data.
 * Command format: 00 CB 3F FF Lc 5C LL <tag> 00
 * 
 * @param command Command APDU
 * @param response Response APDU
 * @return true if handled, false if not a PIV GET DATA
 */
static bool piv_handle_get_data(
    const ISO7816_Command_APDU* command,
    ISO7816_Response_APDU* response) {
    
    // Validate P1-P2: 3F FF means "current DF, data in command"
    if(command->P1 != 0x3F || command->P2 != 0xFF) {
        FURI_LOG_W(TAG, "GET DATA: unsupported P1=%02X P2=%02X", 
            command->P1, command->P2);
        iso7816_set_response(response, ISO7816_RESPONSE_WRONG_PARAMETERS_P1_P2);
        return true;
    }
    
    // Parse tag from data field
    if(command->Lc < 3) {
        FURI_LOG_E(TAG, "GET DATA: data too short (Lc=%d)", command->Lc);
        iso7816_set_response(response, ISO7816_RESPONSE_WRONG_LENGTH);
        return true;
    }
    
    uint32_t tag = 0;
    if(!piv_data_parse_tag(command->Data, command->Lc, &tag)) {
        FURI_LOG_E(TAG, "GET DATA: failed to parse tag");
        iso7816_set_response(response, ISO7816_RESPONSE_NC_TLV_INCONSISTENT);
        return true;
    }
    
    // Check if valid PIV tag
    if(!piv_data_is_valid_tag(tag)) {
        FURI_LOG_W(TAG, "GET DATA: unknown tag 0x%06lX", (unsigned long)tag);
        iso7816_set_response(response, ISO7816_RESPONSE_FILE_NOT_FOUND);
        return true;
    }
    
    // Get data for this tag
    const uint8_t* data = NULL;
    uint16_t data_len = 0;
    bool requires_pin = false;
    
    if(!piv_data_get_by_tag(tag, &data, &data_len, &requires_pin)) {
        FURI_LOG_W(TAG, "GET DATA: container not found for tag 0x%06lX", (unsigned long)tag);
        iso7816_set_response(response, ISO7816_RESPONSE_FILE_NOT_FOUND);
        return true;
    }
    
    // Check PIN if required
    if(requires_pin && !piv_context.pin_verified) {
        FURI_LOG_W(TAG, "GET DATA: PIN required for tag 0x%06lX", (unsigned long)tag);
        iso7816_set_response(response, ISO7816_RESPONSE_SECURITY_NOT_SATISFIED);
        return true;
    }
    
    // Check if response fits in buffer (max CCID extended APDU size = 2048)
    // Note: sizeof(response->Data) is 0 because it's a flexible array member
    #define PIV_MAX_RESPONSE_SIZE 2048
    if(data_len > PIV_MAX_RESPONSE_SIZE) {
        FURI_LOG_E(TAG, "GET DATA: data too large (%d bytes)", data_len);
        // Could implement chaining here, but for now return error
        iso7816_set_response(response, ISO7816_RESPONSE_WRONG_LENGTH);
        return true;
    }
    
    // Copy data to response
    memcpy(response->Data, data, data_len);
    response->DataLen = data_len;
    iso7816_set_response(response, ISO7816_RESPONSE_OK);
    
    FURI_LOG_I(TAG, "GET DATA: returned %d bytes for tag 0x%06lX", 
        data_len, (unsigned long)tag);
    
    return true;
}

/**
 * @brief PIV command handler
 * 
 * Handles PIV-specific commands when the application is selected.
 * Standard ISO7816 commands (SELECT, GET DATA, VERIFY) are handled
 * by the base handler first.
 */
static void piv_app_command_handler(
    void* context,
    const void* command_ptr,
    void* response_ptr) {
    
    UNUSED(context);
    
    const ISO7816_Command_APDU* command = (const ISO7816_Command_APDU*)command_ptr;
    ISO7816_Response_APDU* response = (ISO7816_Response_APDU*)response_ptr;
    
    FURI_LOG_D(TAG, "PIV command: CLA=%02X INS=%02X P1=%02X P2=%02X",
        command->CLA, command->INS, command->P1, command->P2);
    
    // Handle PIV-specific commands
    switch(command->INS) {
        case 0xCB:  // PIV GET DATA (odd INS for BER-TLV)
            piv_handle_get_data(command, response);
            break;
            
        case 0x87:  // GENERAL AUTHENTICATE
            FURI_LOG_W(TAG, "GENERAL AUTHENTICATE not yet implemented");
            iso7816_set_response(response, ISO7816_RESPONSE_FUNCTION_NOT_SUPPORTED);
            break;
            
        case 0x24:  // CHANGE REFERENCE DATA
            FURI_LOG_W(TAG, "CHANGE REFERENCE DATA not yet implemented");
            iso7816_set_response(response, ISO7816_RESPONSE_FUNCTION_NOT_SUPPORTED);
            break;
            
        case 0x2C:  // RESET RETRY COUNTER
            FURI_LOG_W(TAG, "RESET RETRY COUNTER not yet implemented");
            iso7816_set_response(response, ISO7816_RESPONSE_FUNCTION_NOT_SUPPORTED);
            break;
            
        case 0x47:  // GENERATE ASYMMETRIC KEY PAIR
            FURI_LOG_W(TAG, "GENERATE KEY PAIR not yet implemented");
            iso7816_set_response(response, ISO7816_RESPONSE_FUNCTION_NOT_SUPPORTED);
            break;
            
        case 0xDB:  // PUT DATA
            FURI_LOG_W(TAG, "PUT DATA not yet implemented");
            iso7816_set_response(response, ISO7816_RESPONSE_FUNCTION_NOT_SUPPORTED);
            break;
            
        default:
            // Unknown command for PIV
            FURI_LOG_D(TAG, "Unknown PIV command INS=%02X", command->INS);
            iso7816_set_response(response, ISO7816_RESPONSE_INSTRUCTION_NOT_SUPPORTED);
            break;
    }
}

/*******************************************************************************
 * PIV Application Definition
 ******************************************************************************/

static const Iso7816AppDefinition piv_app_definition = {
    .aid = {PIV_AID_BYTES},
    .aid_len = PIV_AID_LEN,
    .name = "PIV",
    .handler = piv_app_command_handler,
    .on_select = piv_app_on_select,
    .on_deselect = piv_app_on_deselect,
    .context = &piv_context,
};

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

Iso7816File* piv_app_init(Iso7816FileSystem* fs) {
    if(!fs) {
        FURI_LOG_E(TAG, "Cannot init PIV: filesystem is NULL");
        return NULL;
    }
    
    piv_context.fs = fs;
    
    // Initialize PIV data containers
    piv_data_init();
    
    // Register PIV application with the file system
    // This creates a DF under MF with the PIV AID
    Iso7816File* piv_df = iso7816_fs_register_app(
        fs,
        NULL,  // Parent = MF
        PIV_DF_FID,
        &piv_app_definition
    );
    
    if(!piv_df) {
        FURI_LOG_E(TAG, "Failed to register PIV application");
        return NULL;
    }
    
    FURI_LOG_I(TAG, "PIV Application registered (DF=%04X, AID len=%d)", 
        PIV_DF_FID, PIV_AID_LEN);
    
    return piv_df;
}

PivAppContext* piv_app_get_context(void) {
    return &piv_context;
}

bool piv_app_is_selected(void) {
    return piv_context.selected;
}

const uint8_t* piv_app_get_aid(uint8_t* aid_len) {
    if(aid_len) {
        *aid_len = PIV_AID_LEN;
    }
    return piv_aid;
}
