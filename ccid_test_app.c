#include <stdint.h>
#include <furi.h>
#include <furi_hal.h>

#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/variable_item_list.h>
#include <gui/gui.h>

#include "iso7816/iso7816_handler.h"
#include "iso7816/iso7816_t0_apdu.h"
#include "iso7816/iso7816_atr.h"
#include "iso7816/iso7816_response.h"

#include "ccid_usb.h"
#include "ccid_test_app_commands.h"

// Applications
#include "piv/piv_app.h"
#include "apps/generic_app.h"

/** How often the status view is refreshed (ms), so the selected-app readout
 *  stays live while a host tool drives the card. */
#define CCID_TEST_STATUS_TICK_PERIOD 250

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;

    // Views
    View* status_view;
    VariableItemList* actions;
    VariableItem* card_item;

    CcidUsbConfig ccid_cfg;
    Iso7816Handler* iso7816_handler;

    // Runtime state (single source of truth for the UI)
    bool card_inserted;

    // Registered applications
    Iso7816File* piv_app_df;
    Iso7816File* generic_app_df;
} CcidTestApp;

typedef enum {
    CcidTestAppViewStatus,
    CcidTestAppViewActions,
} CcidTestAppView;

typedef enum {
    CcidTestActionIndexCard,
    CcidTestActionIndexReset,
} CcidTestActionIndex;

/** Model backing the status view; carries the app pointer to the draw callback,
 *  which only receives the model (not the view context). */
typedef struct {
    CcidTestApp* app;
} CcidTestStatusModel;

/*******************************************************************************
 * Card (ICC slot) control
 ******************************************************************************/

static void ccid_test_card_insert(CcidTestApp* app) {
    ccid_usb_insert_smartcard();
    app->card_inserted = true;
}

static void ccid_test_card_remove(CcidTestApp* app) {
    ccid_usb_remove_smartcard();
    app->card_inserted = false;
}

/** Re-power the virtual card and clear per-app state so a flow can be re-run
 *  without unplugging the reader. */
static void ccid_test_reset_card(CcidTestApp* app) {
    PivAppContext* piv = piv_app_get_context();
    if(piv) {
        piv->selected = false;
        piv->pin_verified = false;
        piv->security_status = 0;
    }

    GenericAppContext* generic = generic_app_get_context();
    if(generic) {
        generic->selected = false;
        generic->command_count = 0;
    }

    // Force the host to re-power the slot (fresh ATR)
    ccid_usb_remove_smartcard();
    ccid_usb_insert_smartcard();
    app->card_inserted = true;

    if(app->card_item) {
        variable_item_set_current_value_index(app->card_item, 1);
        variable_item_set_current_value_text(app->card_item, "Inserted");
    }
}

/*******************************************************************************
 * Status view
 ******************************************************************************/

static const char* ccid_test_selected_app_name(void) {
    if(piv_app_is_selected()) return "PIV";
    if(generic_app_is_selected()) return "TestApp";
    return "none";
}

static void ccid_test_status_draw_callback(Canvas* canvas, void* model) {
    CcidTestStatusModel* status = model;
    CcidTestApp* app = status ? status->app : NULL;

    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "CCID Test App");

    canvas_set_font(canvas, FontSecondary);

    canvas_draw_str(canvas, 0, 26, "Card:");
    canvas_draw_str(canvas, 40, 26, (app && app->card_inserted) ? "Inserted" : "Removed");

    canvas_draw_str(canvas, 0, 38, "App:");
    canvas_draw_str(canvas, 40, 38, ccid_test_selected_app_name());

    canvas_draw_str(canvas, 0, 62, "[OK] menu   [back] exit");
}

static bool ccid_test_status_input_callback(InputEvent* event, void* context) {
    CcidTestApp* app = context;
    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        view_dispatcher_switch_to_view(app->view_dispatcher, CcidTestAppViewActions);
        return true;
    }
    return false;
}

/*******************************************************************************
 * Actions view (VariableItemList)
 ******************************************************************************/

static void ccid_test_card_change_callback(VariableItem* item) {
    CcidTestApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, index ? "Inserted" : "Removed");
    if(index) {
        ccid_test_card_insert(app);
    } else {
        ccid_test_card_remove(app);
    }
}

static void ccid_test_actions_enter_callback(void* context, uint32_t index) {
    CcidTestApp* app = context;
    if(index == CcidTestActionIndexReset) {
        ccid_test_reset_card(app);
    }
}

/*******************************************************************************
 * Navigation
 ******************************************************************************/

static uint32_t ccid_test_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t ccid_test_return_to_status(void* context) {
    UNUSED(context);
    return CcidTestAppViewStatus;
}

/*******************************************************************************
 * Tick (live status refresh)
 ******************************************************************************/

static void ccid_test_tick_callback(void* context) {
    CcidTestApp* app = context;
    // Commit the model to trigger a redraw; state is read live in the draw callback.
    with_view_model(
        app->status_view, CcidTestStatusModel * model, { UNUSED(model); }, true);
}

/*******************************************************************************
 * Lifecycle
 ******************************************************************************/

CcidTestApp* ccid_test_app_alloc(void) {
    CcidTestApp* app = malloc(sizeof(CcidTestApp));

    //setup CCID USB
    // On linux: set VID PID using: /usr/lib/pcsc/drivers/ifd-ccid.bundle/Contents/Info.plist
    app->ccid_cfg.vid = 0x076B;
    app->ccid_cfg.pid = 0x3A21;

    // Card is inserted on startup (see ccid_test_app())
    app->card_inserted = true;

    app->iso7816_handler = iso7816_handler_alloc();
    app->iso7816_handler->iso7816_answer_to_reset = iso7816_answer_to_reset;
    app->iso7816_handler->iso7816_process_command = iso7816_process_command;

    // Register applications with the ISO7816 file system
    Iso7816FileSystem* fs = iso7816_handler_get_filesystem();

    // Register PIV application
    app->piv_app_df = piv_app_init(fs);
    if(app->piv_app_df) {
        FURI_LOG_I("CCID", "PIV application registered");

        // Set PIV as the default application
        // This allows PIV commands to work even before explicit SELECT AID
        if(iso7816_fs_set_default_app(fs, app->piv_app_df)) {
            FURI_LOG_I("CCID", "PIV set as default application");
        }
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
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, ccid_test_tick_callback, CCID_TEST_STATUS_TICK_PERIOD);

    // Status view (home)
    app->status_view = view_alloc();
    view_allocate_model(
        app->status_view, ViewModelTypeLockFree, sizeof(CcidTestStatusModel));
    with_view_model(
        app->status_view, CcidTestStatusModel * model, { model->app = app; }, false);
    view_set_context(app->status_view, app);
    view_set_draw_callback(app->status_view, ccid_test_status_draw_callback);
    view_set_input_callback(app->status_view, ccid_test_status_input_callback);
    view_set_previous_callback(app->status_view, ccid_test_exit);
    view_dispatcher_add_view(app->view_dispatcher, CcidTestAppViewStatus, app->status_view);

    // Actions view
    app->actions = variable_item_list_alloc();
    app->card_item = variable_item_list_add(
        app->actions, "Card", 2, ccid_test_card_change_callback, app);
    variable_item_set_current_value_index(app->card_item, app->card_inserted ? 1 : 0);
    variable_item_set_current_value_text(
        app->card_item, app->card_inserted ? "Inserted" : "Removed");
    variable_item_list_add(app->actions, "Reset card state", 1, NULL, app);
    variable_item_list_set_enter_callback(
        app->actions, ccid_test_actions_enter_callback, app);
    view_set_previous_callback(
        variable_item_list_get_view(app->actions), ccid_test_return_to_status);
    view_dispatcher_add_view(
        app->view_dispatcher, CcidTestAppViewActions, variable_item_list_get_view(app->actions));

    // Switch to home
    view_dispatcher_switch_to_view(app->view_dispatcher, CcidTestAppViewStatus);
    return app;
}

void ccid_test_app_free(CcidTestApp* app) {
    furi_assert(app);

    // Free views
    view_dispatcher_remove_view(app->view_dispatcher, CcidTestAppViewStatus);
    view_dispatcher_remove_view(app->view_dispatcher, CcidTestAppViewActions);
    view_dispatcher_free(app->view_dispatcher);
    view_free(app->status_view);
    variable_item_list_free(app->actions);

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

    furi_check(furi_hal_usb_set_config(&ccid_usb_interface, &app->ccid_cfg) == true);
    iso7816_handler_set_usb_ccid_callbacks();
    ccid_usb_insert_smartcard();

    view_dispatcher_run(app->view_dispatcher);

    //tear down USB
    iso7816_handler_reset_usb_ccid_callbacks();
    furi_hal_usb_set_config(usb_mode_prev, NULL);

    //teardown view
    ccid_test_app_free(app);
    return 0;
}
