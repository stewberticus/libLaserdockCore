# libLaserdockCore — Usage Vault

Reference documentation for the `libLaserdockCore` Qt/CMake library and its embedded `laserdocklib` network transport layer.

---

## Documents

- [[01-Architecture]] — Two-tier design, class map, data flow
- [[02-Tier1-Network-Device]] — `LaserdockNetworkDevice` (raw UDP; minimal Qt)
- [[03-Tier2-Hardware-Manager]] — `ldNetworkHardwareManager` + `ldNetworkHardware` (mid-level)
- [[04-Tier3-ldCore]] — Full `ldCore` singleton: visualizers, filters, tasks, audio
- [[05-Sample-Format]] — `LaserdockSample`, `ldVertex`, DAC coordinates, packet layout
- [[06-Build]] — CMake targets, options, platform notes

---

## Quick Reference

| Goal | Tier | Key class |
|------|------|-----------|
| Send UDP samples to a device you already know | Tier 1 | `LaserdockNetworkDevice` |
| Auto-discover + manage multiple devices | Tier 2 | `ldNetworkHardwareManager` |
| Full animation/audio/filter pipeline | Tier 3 | `ldCore` |

---

## Validated (2026-05-07)

Tier 1 (`LaserdockNetworkDevice`) was exercised end-to-end against physical hardware:
discovery → authentication → device info → output enable → 30 000 pps circle stream → clean exit.
See `demo/src/main.cpp` for the reference integration and the Obsidian research note
*LaserdockNetworkDevice Validation*.
