# Host test suites

Python tests that drive the app from a computer, over USB, while it runs on the
Flipper. There is nothing to install on the Flipper beyond the app itself.

## Requirements

Install the Python dependencies from `requirements.txt`:

```
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt pytest
```

On Linux you also need a running `pcscd` and, for `ccid_test.py` only, the
libusb development package:

```
sudo apt install pcscd libusb-1.0-0-dev
```

macOS and Windows ship a smart card service already.

## Running

Open the app on the Flipper first. It takes over USB while it runs, so the
Flipper disappears as a serial device and reappears as a smart card reader.
Then, from this directory:

```
pytest -v
```

The suites below all talk to the card through PC/SC and can run together:

| Suite | Tests | Covers |
| --- | --- | --- |
| `piv_pin_test.py` | 20 | PIV PIN verification, retry counters, blocking, PUK |
| `select_test.py` | 16 | SELECT by AID and by file identifier |
| `piv_get_data_test.py` | 16 | PIV data containers over GET DATA |
| `piv_cert_test.py` | 16 | X.509 certificate storage and retrieval |
| `verify_test.py` | 15 | ISO7816 VERIFY, including the retry query form |
| `piv_crypto_test.py` | 15 | GENERAL AUTHENTICATE, ECDSA P-256 signing |
| `get_data_test.py` | 12 | ISO7816 GET DATA, tag ranges and error cases |
| `iso7816_test.py` | 10 | APDU layer, including extended APDUs |
| `app_select_test.py` | 6 | Switching between the PIV and generic applications |

`ccid_test.py` (41 tests) is different: it speaks raw USB with pyusb instead of
going through PC/SC, to exercise the CCID message layer itself, including RFU
and length validation. It needs exclusive access to the device, so `pcscd` has
to be stopped first, and the PC/SC suites cannot run while it is:

```
sudo systemctl stop pcscd pcscd.socket
pytest ccid_test.py -v
sudo systemctl start pcscd
```

## Useful values

- The PIV application PIN is `123456`, padded to 8 bytes with `FF`
- The PUK is `12345678`
- Key `0x9A` (PIV Authentication) requires the PIN; key `0x9E` (Card
  Authentication) does not; key `0x9C` (Digital Signature) requires it for
  every single use
- A card reset clears PIN verification, so re-powering the slot between tests
  puts the card back to a known state

## Known issues

- The six tests in `app_select_test.py` return `True` or `False` instead of
  asserting, so pytest reports them as passing regardless of what the card
  answers. Read their printed output to see the real result, or convert them to
  assertions.
- Running pytest leaves `__pycache__` here. That used to break the next app
  build, because the build globbed `*.c*` and matched the `.cpython-*.pyc`
  names. `application.fam` now pins the glob to `*.c`, so it is harmless.
