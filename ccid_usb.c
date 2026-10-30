#include <furi.h>
#include <furi_hal_usb.h>

#include "ccid_protocol.h"
#include "ccid_usb.h"

#define CCID_TAG "usb_ccid"

/* USB string-descriptor indices. These mirror UsbDevManuf/UsbDevProduct/
 * UsbDevSerial from the firmware-internal furi_hal_usb_i.h, which is not part
 * of the app SDK, so they are hardcoded here. Keep in sync with the USB stack. */
#define CCID_STR_MANUF   (1)
#define CCID_STR_PRODUCT (2)
#define CCID_STR_SERIAL  (3)

#define USB_DEVICE_NO_CLASS    (0x0)
#define USB_DEVICE_NO_SUBCLASS (0x0)
#define USB_DEVICE_NO_PROTOCOL (0x0)

#define FIXED_CONTROL_ENDPOINT_SIZE (8)
#define IF_NUM_MAX                  (1)

#define CCID_VID_DEFAULT (0x1234)
#define CCID_PID_DEFAULT (0xABCD)
#define CCID_TOTAL_SLOTS (1)
#define CCID_SLOT_INDEX  (0)

#define CCID_DATABLOCK_SIZE \
    (4 + 1 + CCID_SHORT_APDU_SIZE + 1) //APDU Header + Lc + Short APDU size + Le

/** Size of the header every CCID bulk message starts with */
#define CCID_HEADER_SIZE (10)

/** Largest CCID bulk message in either direction, reported as dwMaxCCIDMessageLength */
#define CCID_MESSAGE_SIZE_MAX (CCID_HEADER_SIZE + CCID_DATABLOCK_SIZE)

#define ENDPOINT_DIR_IN        (0x80)
#define ENDPOINT_DIR_OUT       (0x00)
#define ENDPOINT_DIR_INTERRUPT (0x80)

#define INTERFACE_ID_CCID (0)

#define CCID_IN_EPADDR (ENDPOINT_DIR_IN + 2) //EP 2 IN

/** Endpoint address of the CCID data OUT endpoint, for host-to-device data transfers. */
#define CCID_OUT_EPADDR (ENDPOINT_DIR_OUT + 1) // EP 1 OUT

#define CCID_INTERRUPT_EPADDR (ENDPOINT_DIR_INTERRUPT + 3) // EP 3 INTERRUPT IN

/** Endpoint size in bytes of the CCID data being sent between IN and OUT endpoints. */
#define CCID_EPSIZE 64

/** How long to wait for the host to collect a packet before the response is dropped */
#define CCID_TX_TIMEOUT_MS (1000)

/* bError values of failed commands, CCID Rev 1.1 section 6.2.6 */
#define CCID_ERROR_CMDNOTSUPPORTED (0x00)
#define CCID_ERROR_BADLENGTH       (0x01) // offset of dwLength
#define CCID_ERROR_BADPROTOCOLNUM  (0x07) // offset of bProtocolNum
#define CCID_ERROR_ICCMUTE         (0xFE)

/* bmSlotICCState bits of RDR_to_PC_NotifySlotChange for slot 0 */
#define CCID_SLOTSTATE_ICCPRESENT (1 << 0)
#define CCID_SLOTSTATE_CHANGED    (1 << 1)

/* abProtocolDataStructure sizes of PC_to_RDR_SetParameters and RDR_to_PC_Parameters */
#define CCID_PARAMETERS_T0_SIZE (5)
#define CCID_PARAMETERS_T1_SIZE (7)

struct CcidIntfDescriptor {
    struct usb_interface_descriptor ccid;
    struct usb_ccid_descriptor ccid_desc;
    struct usb_endpoint_descriptor ccid_bulk_in;
    struct usb_endpoint_descriptor ccid_bulk_out;
    struct usb_endpoint_descriptor ccid_interrupt;
} FURI_PACKED;

struct CcidConfigDescriptor {
    struct usb_config_descriptor config;
    struct CcidIntfDescriptor intf_0;
} FURI_PACKED;

enum CCID_Features_Auto_t {
    CCID_Features_Auto_None = 0x0,
    CCID_Features_Auto_ParameterConfiguration = 0x2,
    CCID_Features_Auto_ICCActivation = 0x4,
    CCID_Features_Auto_VoltageSelection = 0x8,

    CCID_Features_Auto_ICCClockFrequencyChange = 0x10,
    CCID_Features_Auto_ICCBaudRateChange = 0x20,
    CCID_Features_Auto_ParameterNegotiation = 0x40,
    CCID_Features_Auto_PPS = 0x80,
};

enum CCID_Features_ExchangeLevel_t {
    CCID_Features_ExchangeLevel_TPDU = 0x00010000,
    CCID_Features_ExchangeLevel_ShortAPDU = 0x00020000,
    CCID_Features_ExchangeLevel_ShortExtendedAPDU = 0x00040000
};

typedef enum {
    WorkerEvtStop = (1 << 0),
    WorkerEvtRequest = (1 << 1),
    WorkerEvtSlotChange = (1 << 2),
} WorkerEvtFlags;

typedef struct ccid_bulk_message_header {
    uint8_t bMessageType;
    uint32_t dwLength;
    uint8_t bSlot;
    uint8_t bSeq;
} FURI_PACKED ccid_bulk_message_header_t;

/* Device descriptor */
static struct usb_device_descriptor ccid_device_desc = {
    .bLength = sizeof(struct usb_device_descriptor),
    .bDescriptorType = USB_DTYPE_DEVICE,
    .bcdUSB = VERSION_BCD(2, 0, 0),
    .bDeviceClass = USB_DEVICE_NO_CLASS,
    .bDeviceSubClass = USB_DEVICE_NO_SUBCLASS,
    .bDeviceProtocol = USB_DEVICE_NO_PROTOCOL,
    .bMaxPacketSize0 = FIXED_CONTROL_ENDPOINT_SIZE,
    .idVendor = CCID_VID_DEFAULT,
    .idProduct = CCID_PID_DEFAULT,
    .bcdDevice = VERSION_BCD(1, 0, 0),
    .iManufacturer = CCID_STR_MANUF,
    .iProduct = CCID_STR_PRODUCT,
    .iSerialNumber = NO_DESCRIPTOR,
    .bNumConfigurations = 1,
};

/* Device configuration descriptor*/
static const struct CcidConfigDescriptor ccid_cfg_desc = {
    .config =
        {
            .bLength = sizeof(struct usb_config_descriptor),
            .bDescriptorType = USB_DTYPE_CONFIGURATION,
            .wTotalLength = sizeof(struct CcidConfigDescriptor),
            .bNumInterfaces = 1,

            .bConfigurationValue = 1,
            .iConfiguration = NO_DESCRIPTOR,
            .bmAttributes = USB_CFG_ATTR_RESERVED | USB_CFG_ATTR_SELFPOWERED,
            .bMaxPower = USB_CFG_POWER_MA(500),
        },
    .intf_0 =
        {
            .ccid =
                {.bLength = sizeof(struct usb_interface_descriptor),
                 .bDescriptorType = USB_DTYPE_INTERFACE,

                 .bInterfaceNumber = INTERFACE_ID_CCID,
                 .bAlternateSetting = 0x00,
                 .bNumEndpoints = 3,

                 .bInterfaceClass = USB_CLASS_CCID,
                 .bInterfaceSubClass = 0,
                 .bInterfaceProtocol = 0,

                 .iInterface = NO_DESCRIPTOR

                },
            .ccid_desc =
                {.bLength = sizeof(struct usb_ccid_descriptor),
                 .bDescriptorType = USB_DTYPE_CCID_FUNCTIONAL,
                 .bcdCCID = CCID_CURRENT_SPEC_RELEASE_NUMBER,
                 .bMaxSlotIndex = 0x00,
                 .bVoltageSupport = CCID_VOLTAGESUPPORT_5V,
                 .dwProtocols = 0x03, //T0 and T1
                 .dwDefaultClock = 16000, //16MHz
                 .dwMaximumClock = 16000, //16MHz
                 .bNumClockSupported = 0,
                 .dwDataRate = 307200,
                 .dwMaxDataRate = 307200,
                 .bNumDataRatesSupported = 0,
                 .dwMaxIFSD = 254,
                 .dwSynchProtocols = 0,
                 .dwMechanical = 0,
                 .dwFeatures = CCID_Features_ExchangeLevel_ShortAPDU |
                               CCID_Features_Auto_ParameterConfiguration |
                               CCID_Features_Auto_ICCActivation |
                               CCID_Features_Auto_VoltageSelection,
                 .dwMaxCCIDMessageLength = CCID_MESSAGE_SIZE_MAX,
                 .bClassGetResponse = 0xff,
                 .bClassEnvelope = 0xff,
                 .wLcdLayout = 0,
                 .bPINSupport = 0,
                 .bMaxCCIDBusySlots = 1},
            .ccid_bulk_in =
                {.bLength = sizeof(struct usb_endpoint_descriptor),
                 .bDescriptorType = USB_DTYPE_ENDPOINT,
                 .bEndpointAddress = CCID_IN_EPADDR,
                 .bmAttributes = USB_EPTYPE_BULK,
                 .wMaxPacketSize = CCID_EPSIZE,
                 .bInterval = 0x05

                },
            .ccid_bulk_out =
                {.bLength = sizeof(struct usb_endpoint_descriptor),
                 .bDescriptorType = USB_DTYPE_ENDPOINT,
                 .bEndpointAddress = CCID_OUT_EPADDR,
                 .bmAttributes = USB_EPTYPE_BULK,
                 .wMaxPacketSize = CCID_EPSIZE,
                 .bInterval = 0x05},
            .ccid_interrupt =
                {.bLength = sizeof(struct usb_endpoint_descriptor),
                 .bDescriptorType = USB_DTYPE_ENDPOINT,
                 .bEndpointAddress = CCID_INTERRUPT_EPADDR,
                 .bmAttributes = USB_EPTYPE_INTERRUPT,
                 .wMaxPacketSize = CCID_EPSIZE,
                 .bInterval = 0x05},
        },
};

static void ccid_init(usbd_device* dev, FuriHalUsbInterface* intf, void* ctx);
static void ccid_deinit(usbd_device* dev);
static void ccid_on_wakeup(usbd_device* dev);
static void ccid_on_suspend(usbd_device* dev);
static int32_t ccid_worker(void* context);

FuriHalUsbInterface ccid_usb_interface = {
    .init = ccid_init,
    .deinit = ccid_deinit,
    .wakeup = ccid_on_wakeup,
    .suspend = ccid_on_suspend,

    .dev_descr = (struct usb_device_descriptor*)&ccid_device_desc,

    .str_manuf_descr = NULL,
    .str_prod_descr = NULL,
    .str_serial_descr = NULL,

    .cfg_descr = (void*)&ccid_cfg_desc,
};

static usbd_respond ccid_ep_config(usbd_device* dev, uint8_t cfg);
static usbd_respond ccid_control(usbd_device* dev, usbd_ctlreq* req, usbd_rqc_callback* callback);

typedef struct {
    bool connected;
    bool configured;
    bool smartcard_inserted;
    bool smartcard_powered;
    CcidUsbCallbacks* callbacks[CCID_TOTAL_SLOTS];
    void* cb_ctx[CCID_TOTAL_SLOTS];
    FuriThread* ccid_thread;
    FuriSemaphore* ccid_semaphore;
    usbd_device* usb_dev;

    uint8_t atr_protocol_num;
    uint8_t protocol_num;
    uint8_t protocol_data[CCID_PARAMETERS_T1_SIZE];

    /* Bulk-OUT message being assembled by the USB interrupt */
    uint16_t receive_buffer_data_index;
    uint32_t receive_length;
    uint8_t receive_buffer[CCID_MESSAGE_SIZE_MAX];

    /* Last complete message, handed over to the worker */
    bool request_valid;
    uint8_t request_buffer[CCID_MESSAGE_SIZE_MAX];

    uint8_t send_buffer[CCID_MESSAGE_SIZE_MAX];
    uint8_t notify_buffer[2];
} CcidUsb;

static CcidUsb* ccid_usb = NULL;

static void* ccid_set_string_descr(char* str) {
    furi_check(str);

    size_t len = strlen(str);
    struct usb_string_descriptor* dev_str_desc = malloc(len * 2 + 2);
    dev_str_desc->bLength = len * 2 + 2;
    dev_str_desc->bDescriptorType = USB_DTYPE_STRING;
    for(size_t i = 0; i < len; i++)
        dev_str_desc->wString[i] = str[i];

    return dev_str_desc;
}

static void ccid_reset_parameters(uint8_t protocol_num) {
    // ISO 7816-3 defaults: Fi/Di 372/1, WI 10 for T=0; LRC, BWI 4, CWI 13 and IFSC 254 for T=1
    static const uint8_t parameters_t0[CCID_PARAMETERS_T0_SIZE] = {0x11, 0x00, 0x00, 0x0A, 0x00};
    static const uint8_t parameters_t1[CCID_PARAMETERS_T1_SIZE] = {
        0x11, 0x10, 0x00, 0x4D, 0x00, 0xFE, 0x00};

    ccid_usb->protocol_num = protocol_num;
    if(protocol_num == 1) {
        memcpy(ccid_usb->protocol_data, parameters_t1, sizeof(parameters_t1));
    } else {
        memcpy(ccid_usb->protocol_data, parameters_t0, sizeof(parameters_t0));
    }
}

static void ccid_init(usbd_device* dev, FuriHalUsbInterface* intf, void* ctx) {
    UNUSED(intf);

    CcidUsbConfig* cfg = (CcidUsbConfig*)ctx;

    furi_check(ccid_usb == NULL);
    ccid_usb = malloc(sizeof(CcidUsb));

    ccid_usb->usb_dev = dev;
    ccid_usb->ccid_semaphore = furi_semaphore_alloc(1, 1);
    ccid_reset_parameters(0);

    ccid_usb_interface.dev_descr->iManufacturer = 0;
    ccid_usb_interface.dev_descr->iProduct = 0;
    ccid_usb_interface.str_manuf_descr = NULL;
    ccid_usb_interface.str_prod_descr = NULL;
    ccid_usb_interface.dev_descr->idVendor = CCID_VID_DEFAULT;
    ccid_usb_interface.dev_descr->idProduct = CCID_PID_DEFAULT;

    if(cfg != NULL) {
        ccid_usb_interface.dev_descr->idVendor = cfg->vid;
        ccid_usb_interface.dev_descr->idProduct = cfg->pid;

        if(cfg->manuf[0] != '\0') {
            ccid_usb_interface.str_manuf_descr = ccid_set_string_descr(cfg->manuf);
            ccid_usb_interface.dev_descr->iManufacturer = CCID_STR_MANUF;
        }

        if(cfg->product[0] != '\0') {
            ccid_usb_interface.str_prod_descr = ccid_set_string_descr(cfg->product);
            ccid_usb_interface.dev_descr->iProduct = CCID_STR_PRODUCT;
        }
    }

    // The worker has to exist before the host can send the first message
    ccid_usb->ccid_thread = furi_thread_alloc_ex("CcidWorker", 2048, ccid_worker, ctx);
    furi_thread_start(ccid_usb->ccid_thread);

    usbd_reg_config(dev, ccid_ep_config);
    usbd_reg_control(dev, ccid_control);

    usbd_connect(dev, true);
}

static void ccid_deinit(usbd_device* dev) {
    usbd_reg_config(dev, NULL);
    usbd_reg_control(dev, NULL);

    // No endpoint event may reach the state freed below
    usbd_reg_endpoint(dev, CCID_IN_EPADDR, NULL);
    usbd_reg_endpoint(dev, CCID_OUT_EPADDR, NULL);
    usbd_reg_endpoint(dev, CCID_INTERRUPT_EPADDR, NULL);
    ccid_usb->configured = false;

    furi_thread_flags_set(furi_thread_get_id(ccid_usb->ccid_thread), WorkerEvtStop);
    furi_thread_join(ccid_usb->ccid_thread);
    furi_thread_free(ccid_usb->ccid_thread);

    furi_semaphore_free(ccid_usb->ccid_semaphore);

    free(ccid_usb_interface.str_manuf_descr);
    free(ccid_usb_interface.str_prod_descr);
    ccid_usb_interface.str_manuf_descr = NULL;
    ccid_usb_interface.str_prod_descr = NULL;

    free(ccid_usb);

    ccid_usb = NULL;
}

static void ccid_on_wakeup(usbd_device* dev) {
    UNUSED(dev);
    furi_check(ccid_usb);

    ccid_usb->connected = true;
}

static void ccid_on_suspend(usbd_device* dev) {
    UNUSED(dev);
    furi_check(ccid_usb);

    ccid_usb->connected = false;
}

void ccid_usb_insert_smartcard(void) {
    furi_check(ccid_usb);

    if(!ccid_usb->smartcard_inserted) {
        // dwFeatures announces automatic activation of the ICC on insertion
        ccid_usb->smartcard_powered = true;
        ccid_usb->smartcard_inserted = true;
        furi_thread_flags_set(furi_thread_get_id(ccid_usb->ccid_thread), WorkerEvtSlotChange);
    }
}

void ccid_usb_remove_smartcard(void) {
    furi_check(ccid_usb);

    if(ccid_usb->smartcard_inserted) {
        ccid_usb->smartcard_inserted = false;
        ccid_usb->smartcard_powered = false;
        furi_thread_flags_set(furi_thread_get_id(ccid_usb->ccid_thread), WorkerEvtSlotChange);
    }
}

void ccid_usb_set_callbacks(CcidUsbCallbacks* cb, void* context) {
    furi_check(ccid_usb);

    ccid_usb->callbacks[CCID_SLOT_INDEX] = cb;
    ccid_usb->cb_ctx[CCID_SLOT_INDEX] = context;
}

static bool ccid_send_packet(uint8_t* data, uint8_t len) {
    furi_check(ccid_usb);

    if(!ccid_usb->configured || !ccid_usb->connected) return false;

    // Do not wait forever for a host that stopped reading: the worker would never see WorkerEvtStop
    if(furi_semaphore_acquire(ccid_usb->ccid_semaphore, CCID_TX_TIMEOUT_MS) != FuriStatusOk) {
        return false;
    }

    if(usbd_ep_write(ccid_usb->usb_dev, CCID_IN_EPADDR, data, len) < 0) {
        furi_semaphore_release(ccid_usb->ccid_semaphore);
        return false;
    }
    return true;
}

static void ccid_send_response(uint8_t* data, uint32_t len) {
    uint32_t data_to_send = len;
    uint32_t data_index = 0;
    while(data_to_send >= CCID_EPSIZE) {
        if(!ccid_send_packet(&data[data_index], CCID_EPSIZE)) return;
        data_to_send = data_to_send - CCID_EPSIZE;
        data_index = data_index + CCID_EPSIZE;
    }

    // A short packet ends the transfer, a zero length one if len is a multiple of CCID_EPSIZE
    ccid_send_packet(&data[data_index], data_to_send);
}

/* Response message type for each request, CCID Rev 1.1 section 6.1 */
static uint8_t ccid_response_type(uint8_t request_type) {
    switch(request_type) {
    case PC_TO_RDR_ICCPOWERON:
    case PC_TO_RDR_XFRBLOCK:
    case PC_TO_RDR_SECURE:
        return RDR_TO_PC_DATABLOCK;
    case PC_TO_RDR_GETPARAMETERS:
    case PC_TO_RDR_RESETPARAMETERS:
    case PC_TO_RDR_SETPARAMETERS:
        return RDR_TO_PC_PARAMETERS;
    case PC_TO_RDR_ESCAPE:
        return RDR_TO_PC_ESCAPE;
    case PC_TO_RDR_SETDATARATEANDCLOCKFREQUENCY:
        return RDR_TO_PC_DATARATEANDCLOCKFREQUENCY;
    default:
        return RDR_TO_PC_SLOTSTATUS;
    }
}

static uint8_t ccid_icc_status(void) {
    if(!ccid_usb->smartcard_inserted) {
        return CCID_ICCSTATUS_NOICCPRESENT;
    } else if(ccid_usb->smartcard_powered) {
        return CCID_ICCSTATUS_PRESENTANDACTIVE;
    } else {
        return CCID_ICCSTATUS_PRESENTANDINACTIVE;
    }
}

/* Protocol the card uses after the ATR: the one in TD1 (ISO 7816-3 section 8.2.3), T=0 if absent */
static uint8_t ccid_atr_protocol_num(const uint8_t* atr, uint32_t atr_len) {
    if(atr_len < 2 || !(atr[1] & 0x80)) return 0;

    uint32_t td1_index = 2 + __builtin_popcount(atr[1] & 0x70); // after TA1, TB1, TC1
    if(td1_index >= atr_len) return 0;

    return ((atr[td1_index] & 0x0F) == 1) ? 1 : 0;
}

/* Send a response whose abData is already in send_buffer */
static void ccid_send_message(
    const ccid_bulk_message_header_t* request,
    uint8_t message_type,
    uint8_t status,
    uint8_t error,
    uint8_t specific,
    uint32_t data_len) {
    // All RDR_to_PC bulk messages share this layout, only the meaning of byte 9 differs
    struct rdr_to_pc_data_block* response =
        (struct rdr_to_pc_data_block*)&ccid_usb->send_buffer; //-V641

    response->bMessageType = message_type;
    response->dwLength = data_len;
    response->bSlot = request->bSlot;
    response->bSeq = request->bSeq;
    response->bStatus = status;
    response->bError = error;
    response->bChainParameter = specific;

    ccid_send_response(ccid_usb->send_buffer, CCID_HEADER_SIZE + data_len);
}

static void
    ccid_send_parameters(const ccid_bulk_message_header_t* request, uint8_t status, uint8_t error) {
    struct rdr_to_pc_data_block* response =
        (struct rdr_to_pc_data_block*)&ccid_usb->send_buffer; //-V641
    uint32_t len = (ccid_usb->protocol_num == 1) ? CCID_PARAMETERS_T1_SIZE :
                                                   CCID_PARAMETERS_T0_SIZE;

    memcpy(response->abData, ccid_usb->protocol_data, len);
    ccid_send_message(request, RDR_TO_PC_PARAMETERS, status, error, ccid_usb->protocol_num, len);
}

static void ccid_process_request(void) {
    const ccid_bulk_message_header_t* request =
        (const ccid_bulk_message_header_t*)&ccid_usb->request_buffer; //-V641
    const uint8_t* request_data = &ccid_usb->request_buffer[CCID_HEADER_SIZE];
    struct rdr_to_pc_data_block* response =
        (struct rdr_to_pc_data_block*)&ccid_usb->send_buffer; //-V641
    CcidUsbCallbacks* callbacks = ccid_usb->callbacks[CCID_SLOT_INDEX];
    void* cb_ctx = ccid_usb->cb_ctx[CCID_SLOT_INDEX];
    uint8_t icc_status = ccid_icc_status();

    // Every request gets a response, otherwise the host stops talking to the reader
    if(request->bSlot != CCID_SLOT_INDEX) {
        ccid_send_message(
            request,
            ccid_response_type(request->bMessageType),
            CCID_COMMANDSTATUS_FAILED | CCID_ICCSTATUS_NOICCPRESENT,
            CCID_ERROR_SLOTNOTFOUND,
            0,
            0);
        return;
    }

    if(!ccid_usb->request_valid) {
        ccid_send_message(
            request,
            ccid_response_type(request->bMessageType),
            CCID_COMMANDSTATUS_FAILED | icc_status,
            CCID_ERROR_BADLENGTH,
            0,
            0);
        return;
    }

    switch(request->bMessageType) {
    case PC_TO_RDR_ICCPOWERON: {
        if(!ccid_usb->smartcard_inserted || callbacks == NULL) {
            ccid_send_message(
                request,
                RDR_TO_PC_DATABLOCK,
                CCID_COMMANDSTATUS_FAILED |
                    (ccid_usb->smartcard_inserted ? CCID_ICCSTATUS_PRESENTANDINACTIVE :
                                                    CCID_ICCSTATUS_NOICCPRESENT),
                CCID_ERROR_ICCMUTE,
                0,
                0);
            break;
        }

        uint32_t atr_len = 0;
        callbacks->icc_power_on_callback(response->abData, &atr_len, cb_ctx);
        furi_check(atr_len <= CCID_DATABLOCK_SIZE);

        ccid_usb->smartcard_powered = true;
        ccid_usb->atr_protocol_num = ccid_atr_protocol_num(response->abData, atr_len);
        ccid_reset_parameters(ccid_usb->atr_protocol_num);

        ccid_send_message(
            request,
            RDR_TO_PC_DATABLOCK,
            CCID_COMMANDSTATUS_PROCESSEDWITHOUTERROR | CCID_ICCSTATUS_PRESENTANDACTIVE,
            CCID_ERROR_NOERROR,
            0,
            atr_len);
        break;
    }
    case PC_TO_RDR_ICCPOWEROFF:
        ccid_usb->smartcard_powered = false;
        ccid_send_message(
            request,
            RDR_TO_PC_SLOTSTATUS,
            CCID_COMMANDSTATUS_PROCESSEDWITHOUTERROR | ccid_icc_status(),
            CCID_ERROR_NOERROR,
            0,
            0);
        break;
    case PC_TO_RDR_GETSLOTSTATUS:
    case PC_TO_RDR_ICCCLOCK:
    case PC_TO_RDR_T0APDU:
    case PC_TO_RDR_ABORT:
        // There is no ICC interface behind the slot: clock and T=0 settings have nothing to change,
        // and a request is complete before the next one is read, so there is nothing to abort
        ccid_send_message(
            request,
            RDR_TO_PC_SLOTSTATUS,
            CCID_COMMANDSTATUS_PROCESSEDWITHOUTERROR | icc_status,
            CCID_ERROR_NOERROR,
            0,
            0);
        break;
    case PC_TO_RDR_XFRBLOCK: {
        if(icc_status != CCID_ICCSTATUS_PRESENTANDACTIVE || callbacks == NULL) {
            ccid_send_message(
                request,
                RDR_TO_PC_DATABLOCK,
                CCID_COMMANDSTATUS_FAILED | icc_status,
                CCID_ERROR_ICCMUTE,
                0,
                0);
            break;
        }

        uint32_t response_len = 0;
        callbacks->xfr_datablock_callback(
            request_data, request->dwLength, response->abData, &response_len, cb_ctx);
        furi_check(response_len <= CCID_DATABLOCK_SIZE);

        ccid_send_message(
            request,
            RDR_TO_PC_DATABLOCK,
            CCID_COMMANDSTATUS_PROCESSEDWITHOUTERROR | CCID_ICCSTATUS_PRESENTANDACTIVE,
            CCID_ERROR_NOERROR,
            0,
            response_len);
        break;
    }
    case PC_TO_RDR_GETPARAMETERS:
        ccid_send_parameters(
            request, CCID_COMMANDSTATUS_PROCESSEDWITHOUTERROR | icc_status, CCID_ERROR_NOERROR);
        break;
    case PC_TO_RDR_RESETPARAMETERS:
        ccid_reset_parameters(ccid_usb->atr_protocol_num);
        ccid_send_parameters(
            request, CCID_COMMANDSTATUS_PROCESSEDWITHOUTERROR | icc_status, CCID_ERROR_NOERROR);
        break;
    case PC_TO_RDR_SETPARAMETERS: {
        const struct pc_to_rdr_set_parameters_t0* requestSetParameters =
            (const struct pc_to_rdr_set_parameters_t0*)request; //-V641
        uint8_t protocol_num = requestSetParameters->bProtocolNum;
        uint32_t len = (protocol_num == 1) ? CCID_PARAMETERS_T1_SIZE : CCID_PARAMETERS_T0_SIZE;

        if(protocol_num > 1) {
            ccid_send_parameters(
                request, CCID_COMMANDSTATUS_FAILED | icc_status, CCID_ERROR_BADPROTOCOLNUM);
        } else if(request->dwLength != len) {
            ccid_send_parameters(
                request, CCID_COMMANDSTATUS_FAILED | icc_status, CCID_ERROR_BADLENGTH);
        } else {
            // Parameters are applied automatically (see dwFeatures), take the ones the host asked for
            ccid_usb->protocol_num = protocol_num;
            memcpy(ccid_usb->protocol_data, request_data, len);
            ccid_send_parameters(
                request,
                CCID_COMMANDSTATUS_PROCESSEDWITHOUTERROR | icc_status,
                CCID_ERROR_NOERROR);
        }
        break;
    }
    case PC_TO_RDR_SETDATARATEANDCLOCKFREQUENCY: {
        // Clock and data rate are fixed, report the ones in use
        struct rdr_to_pc_data_rate_and_clock_frequency* responseDataRate =
            (struct rdr_to_pc_data_rate_and_clock_frequency*)&ccid_usb->send_buffer; //-V641
        responseDataRate->dwClockFrequency = ccid_cfg_desc.intf_0.ccid_desc.dwDefaultClock;
        responseDataRate->dwDataRate = ccid_cfg_desc.intf_0.ccid_desc.dwDataRate;

        ccid_send_message(
            request,
            RDR_TO_PC_DATARATEANDCLOCKFREQUENCY,
            CCID_COMMANDSTATUS_PROCESSEDWITHOUTERROR | icc_status,
            CCID_ERROR_NOERROR,
            0,
            sizeof(responseDataRate->dwClockFrequency) + sizeof(responseDataRate->dwDataRate));
        break;
    }
    default:
        // Escape, Secure, Mechanical and unknown messages: command not supported
        ccid_send_message(
            request,
            ccid_response_type(request->bMessageType),
            CCID_COMMANDSTATUS_FAILED | icc_status,
            CCID_ERROR_CMDNOTSUPPORTED,
            0,
            0);
        break;
    }
}

static void ccid_notify_slot_change(void) {
    if(!ccid_usb->configured || !ccid_usb->connected) return;

    ccid_usb->notify_buffer[0] = RDR_TO_PC_NOTIFYSLOTCHANGE;
    ccid_usb->notify_buffer[1] = CCID_SLOTSTATE_CHANGED |
                                 (ccid_usb->smartcard_inserted ? CCID_SLOTSTATE_ICCPRESENT : 0);

    // If the host has not read the previous notification yet, it will learn the state from GetSlotStatus
    usbd_ep_write(
        ccid_usb->usb_dev,
        CCID_INTERRUPT_EPADDR,
        ccid_usb->notify_buffer,
        sizeof(ccid_usb->notify_buffer));
}

static void ccid_in_ep_callback(usbd_device* dev, uint8_t event, uint8_t ep) {
    UNUSED(dev);
    UNUSED(ep);
    furi_check(ccid_usb);

    if(event == usbd_evt_eptx) {
        furi_semaphore_release(ccid_usb->ccid_semaphore);
    }
}

static void ccid_out_ep_callback(usbd_device* dev, uint8_t event, uint8_t ep) {
    UNUSED(ep);
    if(event != usbd_evt_eprx) return;
    furi_check(ccid_usb);

    uint8_t packet[CCID_EPSIZE];
    int32_t bytes_read = usbd_ep_read(dev, CCID_OUT_EPADDR, packet, CCID_EPSIZE);
    if(bytes_read <= 0) return;

    // Keep what fits and count the rest, so that an oversized message gets an error response
    uint32_t space = sizeof(ccid_usb->receive_buffer) - ccid_usb->receive_buffer_data_index;
    uint32_t bytes_kept = MIN((uint32_t)bytes_read, space);
    memcpy(&ccid_usb->receive_buffer[ccid_usb->receive_buffer_data_index], packet, bytes_kept);
    ccid_usb->receive_buffer_data_index += bytes_kept;
    ccid_usb->receive_length += bytes_read;

    bool short_packet = bytes_read < CCID_EPSIZE;
    if(ccid_usb->receive_buffer_data_index < CCID_HEADER_SIZE) {
        // Not even a header: nothing to respond to
        if(short_packet) {
            ccid_usb->receive_buffer_data_index = 0;
            ccid_usb->receive_length = 0;
        }
        return;
    }

    const ccid_bulk_message_header_t* message =
        (const ccid_bulk_message_header_t*)&ccid_usb->receive_buffer; //-V641
    uint32_t data_received = ccid_usb->receive_length - CCID_HEADER_SIZE;
    if(data_received < message->dwLength && !short_packet) return; // more packets to come

    // Hand the message over and start assembling the next one right away
    memcpy(
        ccid_usb->request_buffer, ccid_usb->receive_buffer, ccid_usb->receive_buffer_data_index);
    ccid_usb->request_valid = data_received == message->dwLength &&
                              message->dwLength <= CCID_DATABLOCK_SIZE;
    ccid_usb->receive_buffer_data_index = 0;
    ccid_usb->receive_length = 0;

    furi_thread_flags_set(furi_thread_get_id(ccid_usb->ccid_thread), WorkerEvtRequest);
}

static int32_t ccid_worker(void* context) {
    UNUSED(context);

    while(1) {
        furi_check(ccid_usb);
        uint32_t flags = furi_thread_flags_wait(
            WorkerEvtStop | WorkerEvtRequest | WorkerEvtSlotChange,
            FuriFlagWaitAny,
            FuriWaitForever);

        FURI_LOG_D(
            CCID_TAG,
            "Worker woke up: flags=0x%08lx smartcard_inserted=%d",
            flags,
            ccid_usb->smartcard_inserted);

        if(flags & WorkerEvtStop) {
            break;
        }
        if(flags & WorkerEvtSlotChange) {
            ccid_notify_slot_change();
        }
        if(flags & WorkerEvtRequest) {
            ccid_process_request();
        }
    }
    return 0;
}

/* Configure endpoints */
static usbd_respond ccid_ep_config(usbd_device* dev, uint8_t cfg) {
    furi_check(ccid_usb);

    switch(cfg) {
    case 0:
        /* deconfiguring device */
        ccid_usb->configured = false;
        usbd_ep_deconfig(dev, CCID_IN_EPADDR);
        usbd_ep_deconfig(dev, CCID_OUT_EPADDR);
        usbd_ep_deconfig(dev, CCID_INTERRUPT_EPADDR);
        usbd_reg_endpoint(dev, CCID_IN_EPADDR, 0);
        usbd_reg_endpoint(dev, CCID_OUT_EPADDR, 0);
        usbd_reg_endpoint(dev, CCID_INTERRUPT_EPADDR, 0);
        return usbd_ack;
    case 1:
        /* configuring device */
        usbd_ep_config(dev, CCID_IN_EPADDR, USB_EPTYPE_BULK, CCID_EPSIZE);
        usbd_ep_config(dev, CCID_OUT_EPADDR, USB_EPTYPE_BULK, CCID_EPSIZE);
        usbd_ep_config(dev, CCID_INTERRUPT_EPADDR, USB_EPTYPE_INTERRUPT, CCID_EPSIZE);
        usbd_reg_endpoint(dev, CCID_IN_EPADDR, ccid_in_ep_callback);
        usbd_reg_endpoint(dev, CCID_OUT_EPADDR, ccid_out_ep_callback);
        // Nothing to do when a notification is collected, but the slot may still hold the callback of
        // a class used before (callbacks survive bus resets), which would run on that event
        usbd_reg_endpoint(dev, CCID_INTERRUPT_EPADDR, NULL);

        // Whatever was in flight before a bus reset is gone
        ccid_usb->receive_buffer_data_index = 0;
        ccid_usb->receive_length = 0;
        if(furi_semaphore_get_count(ccid_usb->ccid_semaphore) == 0) {
            furi_semaphore_release(ccid_usb->ccid_semaphore);
        }

        // The host is talking to us, whether or not a wakeup event came before
        ccid_usb->connected = true;
        ccid_usb->configured = true;
        return usbd_ack;
    default:
        return usbd_fail;
    }
}

/* Control requests handler */
static usbd_respond ccid_control(usbd_device* dev, usbd_ctlreq* req, usbd_rqc_callback* callback) {
    UNUSED(callback);
    /* CDC control requests */
    if(((USB_REQ_RECIPIENT | USB_REQ_TYPE) & req->bmRequestType) ==
           (USB_REQ_INTERFACE | USB_REQ_CLASS) &&
       (req->wIndex == 0 || req->wIndex == 2)) {
        switch(req->bRequest) {
        case CCID_ABORT:
            // Requests are handled one at a time, PC_to_RDR_Abort that follows is answered as usual
            return usbd_ack;
        case CCID_GET_CLOCK_FREQUENCIES:
            dev->status.data_ptr = (void*)&(ccid_cfg_desc.intf_0.ccid_desc.dwDefaultClock);
            dev->status.data_count = sizeof(ccid_cfg_desc.intf_0.ccid_desc.dwDefaultClock);
            return usbd_ack;
        case CCID_GET_DATA_RATES:
            dev->status.data_ptr = (void*)&(ccid_cfg_desc.intf_0.ccid_desc.dwDataRate);
            dev->status.data_count = sizeof(ccid_cfg_desc.intf_0.ccid_desc.dwDataRate);
            return usbd_ack;
        default:
            return usbd_fail;
        }
    }
    return usbd_fail;
}
