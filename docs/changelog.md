## v1.0

First release outside the Flipper Zero firmware repository, where this app
shipped as a debug app.

- Emulates a USB CCID smart card reader holding a virtual ISO7816 T=0 card
- Simulates card insertion and removal from the app menu
- Answers test APDUs on class 0x01, including an echo command for payloads of up
  to 255 bytes
- Carries its own USB CCID implementation, so it no longer depends on CCID
  support being built into the firmware
