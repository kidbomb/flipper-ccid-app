# CCID Test App — Interface Plan

Plan for evolving the CCID test app UI so it can exercise multi-application
smartcards and the smartcard insert/remove flow. No code has been written yet;
this document is the agreed design.

## Motivation

The app emulates a USB CCID smartcard reader with a virtual card that now hosts
multiple ISO7816 applications (PIV + a generic test app), registered in an
in-memory ISO7816 file system with a "default app" fallback mechanism.

The current UI is a flat two-item submenu (`Insert smartcard` /
`Remove smartcard`). It cannot:

- toggle which applications are present on the card,
- show live state (card presence, currently selected app),
- exercise the default-app fallthrough behaviour.

There is also a `ccid_test_app_render_callback` that is **dead code today** — it
is never attached to a `ViewPort`, so nothing renders it. It will be wired up as
the status view (Phase 2).

## Concept: three layers, two of them controllable at runtime

The CCID stack collapses three independent layers into one hardcoded path today.
The interface should expose them separately.

| Layer | Host-side event it produces | Control in this plan |
|---|---|---|
| **Reader** (USB CCID device) | pcscd reader added/removed; OpenSC reader hotplug | **Fixed** — plugged on app start, unplugged on exit. Not runtime-toggled. |
| **Card** (ICC in the reader slot) | pcscd card inserted/removed; ATR presented | **Menu-controlled** (insert / remove) |
| **Applications** (ISO7816 DFs) | SELECT AID resolves or fails; default-app fallthrough | **Menu-controlled** via a soft hidden flag |

### Decisions

- **Reader layer is not runtime-toggleable.** The USB CCID device is plugged
  when the app starts (`furi_hal_usb_set_config(&usb_ccid, ...)`) and unplugged
  when it exits (restore `usb_mode_prev`). This code already exists in
  `ccid_test_app()` and stays as-is.
- **Card layer stays menu-controlled** through the existing
  `furi_hal_usb_ccid_insert_smartcard()` / `furi_hal_usb_ccid_remove_smartcard()`
  HAL calls.
- **Applications use a soft "hidden" flag**, not hard deregistration. An app that
  is toggled off remains registered in the file system but is skipped by
  SELECT-by-AID resolution and hidden from the status view. This is simple,
  reversible, and avoids mutating the file-system tree at runtime.

## Phase 1 — Explicit runtime state

Track the controllable layers as explicit fields on `CcidTestApp`, as the single
source of truth for the UI:

- `bool card_inserted;` — mirrors the ICC slot state.
- Per-app `bool hidden;` (or an `enabled` flag) — see Phase 3.

Card insert/remove menu actions update `card_inserted` and call the matching HAL
function. The status view reads these fields; it never queries the HAL directly.

## Phase 2 — Restructure the UI into status + actions

Replace the flat submenu with a small view stack built from standard Flipper GUI
modules.

- **Status view** — wire up the currently-dead render callback to a `ViewPort`.
  Live-show:
  - `Card: inserted / removed`
  - `Selected app: PIV / TestApp / none`
  - (optional, Phase 4) last APDU INS + returned SW
- **Actions view** — use `VariableItemList` rather than `Submenu` so the current
  value renders inline next to each item:
  - `Card    [Inserted ▸ Removed]`
  - `Applications ▸` (opens the applications submenu)
  - `Reset card state`

## Phase 3 — Applications management (soft hidden flag)

Give the application layer real controls. Each registered app gets a soft flag
that hides it without removing it from the file-system tree.

- **Enable / disable each app** — toggling the flag. SELECT-by-AID resolution
  (`iso7816_fs_find_by_aid`) must skip hidden apps, so a disabled app makes
  `SELECT AID` fail and lets the default-app fallthrough be tested.
- **Set default app** — drive `iso7816_fs_set_default_app()` from the UI
  (PIV / generic / none) to exercise the default-app mechanism.
- **Reset app state** — clear per-app `selected` / `pin_verified` /
  `security_status` so a flow can be re-run without replugging the card.
- **Show live selection** — reuse `piv_app_is_selected()` /
  `generic_app_is_selected()`.

Implementation note: the hidden flag lives alongside the registered app
(e.g. on the app context or as a parallel field the SELECT lookup consults). The
DF stays in the tree; only visibility to AID resolution and the UI changes.

## Phase 4 — Last-APDU / activity feedback (optional, high value)

Capture the last command INS and returned status word from the
`xfr_datablock_callback` path and surface them on the status view. This makes the
Flipper its own debug readout while host tools drive it, avoiding a separate USB
sniffer for basic "did my SELECT land" checks.

## Phase 5 — Document the test matrix

Add a section pairing each UI action with the host command that verifies it:

- Card insert / remove → `pcsc_scan` (watch card inserted/removed events)
- App SELECT success / failure → `opensc-tool -n`, `opensc-tool -a`
- PIV present → `piv-tool`, `pkcs11-tool -L`
- Default-app fallthrough → disable PIV, confirm commands still resolve via the
  configured default
- Multi-app coexistence → enable both, SELECT each by AID

Key scenarios to script:

1. Card swap under a live reader (remove → insert) without touching the reader.
2. SELECT-AID success vs failure driven by the app hidden flag.
3. Default-app fallthrough with the primary app disabled.
4. Both apps enabled, selected in turn by AID.

## Out of scope

- Runtime reader (USB device) plug/unplug — intentionally fixed to app
  start/exit.
- Hard deregistration of apps from the file system — using the soft flag instead.
- Configurable ATR (card-level, not needed for app-selection testing).
