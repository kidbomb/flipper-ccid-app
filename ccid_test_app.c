#include <stdint.h>
#include <furi.h>
#include <furi_hal.h>

#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/gui.h>

#include "iso7816/iso7816_handler.h"
#include "iso7816/iso7816_t0_apdu.h"
#include "iso7816/iso7816_atr.h"
#include "iso7816/iso7816_response.h"

#include "ccid_test_app_commands.h"

// Applications
#include "piv/piv_app.h"
#include "apps/generic_app.h"

typedef enum {
    EventTypeInput,
} EventType;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    FuriHalUsbCcidConfig ccid_cfg;
    Iso7816Handler* iso7816_handler;
    
    // Registered applications
    Iso7816File* piv_app_df;
    Iso7816File* generic_app_df;
} CcidTestApp;

typedef struct {
    union {
        InputEvent input;
    };
    EventType type;
} CcidTestAppEvent;

typedef enum {
    CcidTestAppViewSubmenu,
} CcidTestAppView;

typedef enum {
    CcidTestSubmenuIndexInsertSmartcard,
    CcidTestSubmenuIndexRemoveSmartcard
} SubmenuIndex;

static void ccid_test_submenu_callback(void* context, uint32_t index) {
    furi_assert(context);
    if(index == CcidTestSubmenuIndexInsertSmartcard) {
        furi_hal_usb_ccid_insert_smartcard();
    } else if(index == CcidTestSubmenuIndexRemoveSmartcard) {
        furi_hal_usb_ccid_remove_smartcard();
    }
}

static void ccid_test_app_render_callback(Canvas* canvas, void* ctx) {
    CcidTestApp* app = ctx;
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "CCID Test App");

    canvas_set_font(canvas, FontSecondary);
    
    // Show registered applications
    canvas_draw_str(canvas, 0, 25, "Applications:");
    
    if(app && app->piv_app_df) {
        if(piv_app_is_selected()) {
            canvas_draw_str(canvas, 8, 37, "> PIV (selected)");
        } else {
            canvas_draw_str(canvas, 8, 37, "  PIV");
        }
    }
    
    if(app && app->generic_app_df) {
        if(generic_app_is_selected()) {
            canvas_draw_str(canvas, 8, 49, "> TestApp (selected)");
        } else {
            canvas_draw_str(canvas, 8, 49, "  TestApp");
        }
    }

    canvas_draw_str(canvas, 0, 63, "Hold [back] to exit");
}

static void ccid_test_app_input_callback(InputEvent* input_event, void* ctx) {
    FuriMessageQueue* event_queue = ctx;

    CcidTestAppEvent event;
    event.type = EventTypeInput;
    event.input = *input_event;
    furi_message_queue_put(event_queue, &event, FuriWaitForever);

}

uint32_t ccid_test_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

CcidTestApp* ccid_test_app_alloc(void) {
    CcidTestApp* app = malloc(sizeof(CcidTestApp));

    //setup CCID USB
    // On linux: set VID PID using: /usr/lib/pcsc/drivers/ifd-ccid.bundle/Contents/Info.plist
    app->ccid_cfg.vid = 0x076B;
    app->ccid_cfg.pid = 0x3A21;

    app->iso7816_handler = iso7816_handler_alloc();
    app->iso7816_handler->iso7816_answer_to_reset = iso7816_answer_to_reset;
    app->iso7816_handler->iso7816_process_command = iso7816_process_command;

    // Register applications with the ISO7816 file system
    Iso7816FileSystem* fs = iso7816_handler_get_filesystem();
    
    // Register PIV application
    app->piv_app_df = piv_app_init(fs);
    if(app->piv_app_df) {
        FURI_LOG_I("CCID", "PIV application registered");
    } else {
        FURI_LOG_E("CCID", "Failed to register PIV application");
    }
    
    // Register generic test application
    app->generic_app_df = generic_app_init(fs);
    if(app->generic_app_df) {
        FURI_LOG_I("CCID", "Generic test application registered");
    } else {
        FURI_LOG_E("CCID", "Failed to register generic test application");
    }

    // Gui
    app->gui = furi_record_open(RECORD_GUI);


    // View dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    //viewport
    app->view_port = view_port_alloc();
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    view_port_draw_callback_set(app->view_port, ccid_test_app_render_callback, app);

    // Views
    app->submenu = submenu_alloc();
    submenu_add_item(
        app->submenu,
        "Insert smartcard",
        CcidTestSubmenuIndexInsertSmartcard,
        ccid_test_submenu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Remove smartcard",
        CcidTestSubmenuIndexRemoveSmartcard,
        ccid_test_submenu_callback,
        app);
    view_set_previous_callback(submenu_get_view(app->submenu), ccid_test_exit);
    view_dispatcher_add_view(
        app->view_dispatcher, CcidTestAppViewSubmenu, submenu_get_view(app->submenu));

    // Switch to menu
    view_dispatcher_switch_to_view(app->view_dispatcher, CcidTestAppViewSubmenu);
    return app;
}

void ccid_test_app_free(CcidTestApp* app) {
    furi_assert(app);

    // Free views
    view_dispatcher_remove_view(app->view_dispatcher, CcidTestAppViewSubmenu);
    view_dispatcher_free(app->view_dispatcher);
    submenu_free(app->submenu);

    // Close gui record
    furi_record_close(RECORD_GUI);
    app->gui = NULL;

    iso7816_handler_free(app->iso7816_handler);

    // Free rest
    free(app);
}

int32_t ccid_test_app(void* p) {
    UNUSED(p);

    //setup view
    CcidTestApp* app = ccid_test_app_alloc();

    FuriHalUsbInterface* usb_mode_prev = furi_hal_usb_get_config();
    furi_hal_usb_unlock();

    furi_check(furi_hal_usb_set_config(&usb_ccid, &app->ccid_cfg) == true);
    iso7816_handler_set_usb_ccid_callbacks();
    furi_hal_usb_ccid_insert_smartcard();

    view_dispatcher_run(app->view_dispatcher);

    //tear down USB
    iso7816_handler_reset_usb_ccid_callbacks();
    furi_hal_usb_set_config(usb_mode_prev, NULL);

    //teardown view
    ccid_test_app_free(app);
    return 0;
}
