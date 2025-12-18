/**
 * @file generic_app.h
 * @brief Generic Test Application
 * 
 * A simple test application with an arbitrary AID for testing
 * multi-application support in the ISO7816 stack.
 * 
 * This application demonstrates:
 * - Application registration with custom AID
 * - SELECT command handling
 * - Application-specific command processing
 * 
 * Generic App AID: F0 54 45 53 54 41 50 50 (8 bytes - "TESTAPP" with F0 prefix)
 */

#pragma once

#include "../iso7816/iso7816_filesystem.h"
#include "../iso7816/iso7816_handler.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Generic Application Identifier (AID)
 * 
 * Using proprietary AID format (starts with 'F'):
 *   F0             - Proprietary category
 *   54 45 53 54    - "TEST"
 *   41 50 50       - "APP"
 ******************************************************************************/

/** Generic Application Identifier (8 bytes) */
#define GENERIC_APP_AID_BYTES \
    0xF0, 0x54, 0x45, 0x53, 0x54, 0x41, 0x50, 0x50

/** Length of Generic App AID */
#define GENERIC_APP_AID_LEN 8

/** Generic App DF file identifier (under MF) */
#define GENERIC_APP_DF_FID 0x7F01

/*******************************************************************************
 * Generic Application Context
 ******************************************************************************/

/** Generic application state */
typedef struct {
    /** Whether app is currently selected */
    bool selected;
    
    /** Counter for demo purposes */
    uint32_t command_count;
    
    /** File system reference */
    Iso7816FileSystem* fs;
} GenericAppContext;

/*******************************************************************************
 * Generic Application Functions
 ******************************************************************************/

/**
 * @brief Initialize the generic test application
 * 
 * Creates the application DF under MF and registers it.
 * 
 * @param fs File system to register with
 * @return Pointer to application DF on success, NULL on failure
 */
Iso7816File* generic_app_init(Iso7816FileSystem* fs);

/**
 * @brief Get the generic application context
 * 
 * @return Pointer to context (valid after generic_app_init)
 */
GenericAppContext* generic_app_get_context(void);

/**
 * @brief Check if generic application is currently selected
 * 
 * @return true if app is selected
 */
bool generic_app_is_selected(void);

/**
 * @brief Get generic app AID
 * 
 * @param aid_len Output: length of AID
 * @return Pointer to AID bytes
 */
const uint8_t* generic_app_get_aid(uint8_t* aid_len);

#ifdef __cplusplus
}
#endif
