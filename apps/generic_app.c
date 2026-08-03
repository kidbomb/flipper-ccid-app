/**
 * @file generic_app.c
 * @brief Generic Test Application Implementation
 * 
 * A simple test application to demonstrate multi-app support.
 */

#include "generic_app.h"
#include "../iso7816/iso7816_response.h"
#include "../iso7816/iso7816_t0_apdu.h"
#include <furi.h>
#include <string.h>

#define TAG "GenericApp"

/*******************************************************************************
 * Generic App AID Definition
 ******************************************************************************/

static const uint8_t generic_app_aid[] = {GENERIC_APP_AID_BYTES};

/*******************************************************************************
 * Generic Application Context (Singleton)
 ******************************************************************************/

static GenericAppContext generic_context = {
    .selected = false,
    .command_count = 0,
    .fs = NULL,
};

/*******************************************************************************
 * Generic Application Callbacks
 ******************************************************************************/

/**
 * @brief Called when generic application is selected
 */
static void generic_app_on_select(void* context) {
    UNUSED(context);
    
    FURI_LOG_I(TAG, "Generic Test Application SELECTED");
    
    generic_context.selected = true;
    generic_context.command_count = 0;
}

/**
 * @brief Called when generic application is deselected
 */
static void generic_app_on_deselect(void* context) {
    UNUSED(context);
    
    FURI_LOG_I(TAG, "Generic Test Application DESELECTED (processed %lu commands)", 
        generic_context.command_count);
    
    generic_context.selected = false;
}

/**
 * @brief Generic app command handler
 * 
 * Handles application-specific commands.
 * Uses custom CLA=0x80 for proprietary commands.
 */
static void generic_app_command_handler(
    void* context,
    const void* command_ptr,
    void* response_ptr) {
    
    UNUSED(context);
    
    const ISO7816_Command_APDU* command = (const ISO7816_Command_APDU*)command_ptr;
    ISO7816_Response_APDU* response = (ISO7816_Response_APDU*)response_ptr;
    
    generic_context.command_count++;
    
    FURI_LOG_D(TAG, "Generic app command #%lu: CLA=%02X INS=%02X P1=%02X P2=%02X",
        generic_context.command_count,
        command->CLA, command->INS, command->P1, command->P2);
    
    // Handle proprietary commands (CLA=0x80)
    if(command->CLA == 0x80) {
        switch(command->INS) {
            case 0x01:  // GET APP INFO
                // Return app name and command count
                {
                    const char* app_name = "GenericTestApp";
                    uint8_t name_len = strlen(app_name);
                    
                    // Build response: name length + name + 4-byte command count
                    response->Data[0] = name_len;
                    memcpy(&response->Data[1], app_name, name_len);
                    response->Data[1 + name_len] = (generic_context.command_count >> 24) & 0xFF;
                    response->Data[2 + name_len] = (generic_context.command_count >> 16) & 0xFF;
                    response->Data[3 + name_len] = (generic_context.command_count >> 8) & 0xFF;
                    response->Data[4 + name_len] = generic_context.command_count & 0xFF;
                    response->DataLen = 5 + name_len;
                    
                    iso7816_set_response(response, ISO7816_RESPONSE_OK);
                    FURI_LOG_D(TAG, "GET APP INFO: returning %d bytes", response->DataLen);
                }
                break;
                
            case 0x02:  // ECHO
                // Echo back the command data
                if(command->Lc > 0) {
                    memcpy(response->Data, command->Data, command->Lc);
                    response->DataLen = command->Lc;
                } else {
                    response->DataLen = 0;
                }
                iso7816_set_response(response, ISO7816_RESPONSE_OK);
                FURI_LOG_D(TAG, "ECHO: returning %d bytes", response->DataLen);
                break;
                
            case 0x03:  // GET STATUS
                // Return simple status byte
                response->Data[0] = generic_context.selected ? 0x01 : 0x00;
                response->DataLen = 1;
                iso7816_set_response(response, ISO7816_RESPONSE_OK);
                break;
                
            default:
                FURI_LOG_W(TAG, "Unknown proprietary command INS=%02X", command->INS);
                iso7816_set_response(response, ISO7816_RESPONSE_INSTRUCTION_NOT_SUPPORTED);
                break;
        }
    } else {
        // Non-proprietary commands not handled by this app
        FURI_LOG_D(TAG, "Non-proprietary command, not handled by generic app");
        iso7816_set_response(response, ISO7816_RESPONSE_INSTRUCTION_NOT_SUPPORTED);
    }
}

/*******************************************************************************
 * Generic Application Definition
 ******************************************************************************/

static const Iso7816AppDefinition generic_app_definition = {
    .aid = {GENERIC_APP_AID_BYTES},
    .aid_len = GENERIC_APP_AID_LEN,
    .name = "TestApp",
    .handler = generic_app_command_handler,
    .on_select = generic_app_on_select,
    .on_deselect = generic_app_on_deselect,
    .context = &generic_context,
};

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

Iso7816File* generic_app_init(Iso7816FileSystem* fs) {
    if(!fs) {
        FURI_LOG_E(TAG, "Cannot init generic app: filesystem is NULL");
        return NULL;
    }
    
    generic_context.fs = fs;
    
    // Register application with the file system
    Iso7816File* app_df = iso7816_fs_register_app(
        fs,
        NULL,  // Parent = MF
        GENERIC_APP_DF_FID,
        &generic_app_definition
    );
    
    if(!app_df) {
        FURI_LOG_E(TAG, "Failed to register generic application");
        return NULL;
    }
    
    FURI_LOG_I(TAG, "Generic Test Application registered (DF=%04X, AID len=%d)", 
        GENERIC_APP_DF_FID, GENERIC_APP_AID_LEN);
    
    return app_df;
}

GenericAppContext* generic_app_get_context(void) {
    return &generic_context;
}

bool generic_app_is_selected(void) {
    return generic_context.selected;
}

const uint8_t* generic_app_get_aid(uint8_t* aid_len) {
    if(aid_len) {
        *aid_len = GENERIC_APP_AID_LEN;
    }
    return generic_app_aid;
}
