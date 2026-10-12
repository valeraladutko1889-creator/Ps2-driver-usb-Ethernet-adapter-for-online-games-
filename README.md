# PS2 USB-to-SMAP Monolithic Driver

An experimental monolithic `.irx` driver for the PlayStation 2 that emulates the official Sony Network Adapter (`SMAP`) API and reroutes network traffic through a **USB-Ethernet adapter (ASIX AX88772 chipset)** over USB 1.1.

## Architecture
This project implements an **Asynchronous Ring Buffer** operating in a dedicated IOP background thread to overcome USB 1.1 polling latency, preventing net-enabled games from freezing or dropping connections due to network timeouts.

## How to Build
```bash
docker build -t ps2-usb-smap .
docker run --rm -v "$(pwd)":/src ps2-usb-smap
```

## How to Apply to Games
Use `patch_iso.py` to overwrite internal game paths with `mc0:SMAP`.
