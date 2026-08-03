# CCID

Turns your Flipper Zero into a USB smart card reader. While the app is open, the
Flipper enumerates on the host as a CCID class device holding a virtual ISO7816
card, and answers a small set of test APDUs.

This is a development and testing tool. It emulates a card that responds to
commands, it does not read or clone physical smart cards.

## What it does

- Enumerates as a standard USB CCID reader, so the host's stock smart card stack
  picks it up with no extra drivers
- Presents a virtual card with the minimum valid Answer To Reset, 3B 00
- Answers a handful of test APDUs over protocol T=0
- Simulates card insertion and removal from the app menu, so you can exercise how
  host software reacts to a card appearing and disappearing

## Usage

1. Connect the Flipper to your computer with a USB-C cable
2. Open the app. A new smart card reader appears on the host, and a card is
  inserted automatically
3. Talk to it with any PC/SC client, for example pcsc_scan, opensc-tool, or the
  test client bundled in this repository
4. Use **Insert smartcard** and **Remove smartcard** from the menu to simulate
  card events
5. Press Back to exit. The Flipper's previous USB mode is restored

On Linux you need pcscd running. On macOS and Windows the smart card service is
already part of the system.

## Supported APDUs

All test commands use class 0x01. Every one of them returns status 90 00 on
success.

- **INS 0x01** takes no data and returns none. Example: 01 01 00 00
- **INS 0x02** takes no data and returns two bytes, 62 63. Example: 01 02 00 00 02
- **INS 0x03** takes two bytes and returns none. Example: 01 03 00 00 02 CA FE
- **INS 0x04** echoes back whatever you send it, up to 255 bytes. Example:
  01 04 00 00 02 CA FE 02

An unknown instruction returns 6D 00. Any class other than 0x01 returns 6E 00.
Wrong parameters return 6A 00, and a bad length returns 67 00.

## Test client

The client directory holds a PC/SC test script that runs through every supported
APDU and reports pass or fail for each. It needs pyscard, listed in
client/requirements.txt. Run it with the app open on the Flipper, and pick your
Flipper from the list of readers it prints.

## Notes

- The reader reports vendor 0x076B and product 0x3A21, an OmniKey identifier, so
  that stock CCID drivers bind to it without extra configuration. If your host
  needs the pair added explicitly, on Linux it goes in the Info.plist inside
  ifd-ccid.bundle
- The app takes over USB while it runs, so the serial console is unavailable
  until you exit
- APDUs are limited to the short form, 255 bytes of payload

## License

Released under the GNU General Public License v3.0, the license of the
Flipper Zero firmware this app was originally developed in. See the LICENSE file.

The CCID class definitions in ccid_protocol.h are from the Lightweight USB device
Stack for STM32 microcontrollers and stay under the Apache License 2.0.
