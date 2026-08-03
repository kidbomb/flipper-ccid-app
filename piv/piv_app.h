/**
 * @file piv_app.h
 * @brief PIV (Personal Identity Verification) Application
 * 
 * Implements NIST SP 800-73-5 Part 1: PIV Card Application.
 * This application can be selected via SELECT command using the PIV AID.
 * 
 * PIV AID: A0 00 00 03 08 00 00 10 00 01 00
 */

#pragma once

#include "../iso7816/iso7816_filesystem.h"
#include "../iso7816/iso7816_handler.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * PIV Application Identifier (AID)
 * 
 * Structure:
 *   A0 00 00 03 08  - NIST RID (Registered Identifier)
 *   00 00 10 00     - PIV PIX (Proprietary Identifier Extension)
 *   01 00           - Version 1.0
 ******************************************************************************/

/** PIV Application Identifier (full 11 bytes) */
#define PIV_AID_BYTES \
    0xA0, 0x00, 0x00, 0x03, 0x08, \
    0x00, 0x00, 0x10, 0x00, \
    0x01, 0x00

/** Length of PIV AID */
#define PIV_AID_LEN 11

/** PIV Application version */
#define PIV_VERSION_MAJOR 1
#define PIV_VERSION_MINOR 0

/** PIV DF file identifier (under MF) */
#define PIV_DF_FID 0x5000

/*******************************************************************************
 * PIV Application Context
 ******************************************************************************/

/** PIV application state */
typedef struct {
    /** Whether PIV app is currently selected */
    bool selected;
    
    /** Current security status */
    uint8_t security_status;
    
    /** File system reference (for accessing PIN storage, etc.) */
    Iso7816FileSystem* fs;
} PivAppContext;

/*******************************************************************************
 * PIV Application Functions
 ******************************************************************************/

/**
 * @brief Initialize the PIV application
 * 
 * Creates the PIV DF under MF and registers the application.
 * Should be called during ISO7816 handler initialization.
 * 
 * @param fs File system to register with
 * @return Pointer to PIV DF on success, NULL on failure
 */
Iso7816File* piv_app_init(Iso7816FileSystem* fs);

/**
 * @brief Get the PIV application context
 * 
 * @return Pointer to PIV context (valid after piv_app_init)
 */
PivAppContext* piv_app_get_context(void);

/**
 * @brief Check if PIV application is currently selected
 * 
 * @return true if PIV is selected
 */
bool piv_app_is_selected(void);

/**
 * @brief Get PIV AID
 * 
 * @param aid_len Output: length of AID
 * @return Pointer to PIV AID bytes
 */
const uint8_t* piv_app_get_aid(uint8_t* aid_len);

#ifdef __cplusplus
}
#endif
