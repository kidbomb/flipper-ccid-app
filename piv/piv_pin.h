/**
 * @file piv_pin.h
 * @brief PIV PIN Management
 * 
 * Implements PIV-specific PIN handling per NIST SP 800-73-5.
 * 
 * PIV PIN Format:
 * - 6-8 numeric digits (ASCII 0x30-0x39)
 * - Padded with 0xFF to 8 bytes
 * 
 * Key References (Table 4 of SP 800-73-5):
 * - 0x80: PIV Card Application PIN
 * - 0x81: PIN Unblocking Key (PUK)
 * - 0x96: Primary Finger OCC (not implemented)
 * - 0x97: Secondary Finger OCC (not implemented)
 * - 0x98: Pairing Code (not implemented)
 */

#pragma once

#include "../iso7816/iso7816_filesystem.h"
#include "../iso7816/iso7816_t0_apdu.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * PIV PIN Constants
 ******************************************************************************/

/** PIV Card Application PIN reference */
#define PIV_PIN_REF                 0x80

/** PIN Unblocking Key (PUK) reference */
#define PIV_PUK_REF                 0x81

/** Global PIN reference (optional) */
#define PIV_GLOBAL_PIN_REF          0x00

/** PIV PIN minimum length (digits) */
#define PIV_PIN_MIN_LENGTH          6

/** PIV PIN maximum length (digits) */
#define PIV_PIN_MAX_LENGTH          8

/** PIV PIN padded length (always 8 bytes with 0xFF padding) */
#define PIV_PIN_PADDED_LENGTH       8

/** PIV PUK length (always 8 bytes) */
#define PIV_PUK_LENGTH              8

/** Default PIN retry limit */
#define PIV_PIN_MAX_RETRIES         10

/** Default PUK retry limit */
#define PIV_PUK_MAX_RETRIES         10

/** Default PIV PIN (123456) for demo/testing */
#define PIV_DEFAULT_PIN             "123456"
#define PIV_DEFAULT_PIN_LEN         6

/** Default PUK (12345678) for demo/testing */
#define PIV_DEFAULT_PUK             "12345678"
#define PIV_DEFAULT_PUK_LEN         8

/*******************************************************************************
 * PIV PIN Functions
 ******************************************************************************/

/**
 * @brief Initialize PIV PINs
 * 
 * Registers the PIV PIN (0x80) and PUK (0x81) with the file system.
 * Uses default values for demo/testing.
 * 
 * @param fs File system to register PINs with
 * @return true on success
 */
bool piv_pin_init(Iso7816FileSystem* fs);

/**
 * @brief Check if PIV PIN is currently verified
 * 
 * @param fs File system context
 * @return true if PIV PIN (0x80) is verified
 */
bool piv_pin_is_verified(Iso7816FileSystem* fs);

/**
 * @brief Clear PIV PIN verification status
 * 
 * Called when PIV application is deselected.
 * 
 * @param fs File system context
 */
void piv_pin_clear_verification(Iso7816FileSystem* fs);

/**
 * @brief Handle CHANGE REFERENCE DATA command (INS=0x24)
 * 
 * Allows changing PIN or PUK.
 * Command format: 00 24 00 <ref> <Lc> <old PIN> <new PIN>
 * 
 * @param fs File system context
 * @param command Command APDU
 * @param response Response APDU
 */
void piv_cmd_change_reference_data(
    Iso7816FileSystem* fs,
    const ISO7816_Command_APDU* command,
    ISO7816_Response_APDU* response);

/**
 * @brief Handle RESET RETRY COUNTER command (INS=0x2C)
 * 
 * Allows unblocking PIN using PUK.
 * Command format: 00 2C 00 <ref> <Lc> <PUK> <new PIN>
 * 
 * @param fs File system context
 * @param command Command APDU
 * @param response Response APDU
 */
void piv_cmd_reset_retry_counter(
    Iso7816FileSystem* fs,
    const ISO7816_Command_APDU* command,
    ISO7816_Response_APDU* response);

/**
 * @brief Validate PIN format per PIV specification
 * 
 * PIV PINs must be 6-8 numeric ASCII digits (0x30-0x39),
 * optionally padded with 0xFF to 8 bytes.
 * 
 * @param pin PIN data
 * @param len PIN length (including any padding)
 * @param actual_len Output: actual PIN length without padding (can be NULL)
 * @return true if valid PIV PIN format
 */
bool piv_pin_validate_format(const uint8_t* pin, uint16_t len, uint8_t* actual_len);

#ifdef __cplusplus
}
#endif
