# 05 — Sample Format & Coordinates

Everything about how laser samples are represented at each layer of the stack.

---

## Wire Format: `LaserdockSample`

```
3rdparty/laserdocklib/include/laserdocklib/LaserdockSample.h
```

```cpp
struct LaserdockSample
{
    uint16_t rg;   // low byte  = red   (0x00–0xFF)
                   // high byte = green (0x00–0xFF)
    uint16_t b;    // low byte  = blue  (0x00–0xFF)
                   // high byte = reserved / unused
    uint16_t x;    // 0–4095 (left → right)
    uint16_t y;    // 0–4095 (bottom → top on most firmware)
};
// sizeof(LaserdockSample) == 8 bytes
```

This is the struct you pass to `LaserdockNetworkDevice::send_samples()`.

### Color Packing

```cpp
// White
s.rg = 0xFFFF;   // red = 0xFF, green = 0xFF
s.b  = 0x00FF;   // blue = 0xFF

// Red only
s.rg = 0x0000 | (0xFF);   // red = 0xFF, green = 0x00
s.b  = 0x0000;

// From separate R, G, B bytes:
s.rg = static_cast<uint16_t>(green << 8) | red;
s.b  = blue;   // high byte unused — leave 0
```

---

## DAC Coordinate System

| Axis | Range | Notes |
|------|-------|-------|
| X | 0 – 4095 | 0 = left, 4095 = right |
| Y | 0 – 4095 | 0 = bottom, 4095 = top (firmware ≥ 1.8) |
| Centre | 2048, 2048 | |

```cpp
// Centre + circle radius 1500:
static constexpr uint16_t k_center = 2048;
static constexpr uint16_t k_radius = 1500;

s.x = static_cast<uint16_t>(k_center + static_cast<int>(k_radius * std::cos(angle)));
s.y = static_cast<uint16_t>(k_center + static_cast<int>(k_radius * std::sin(angle)));
```

Clamp to `0..4095` if your geometry can exceed the range. Overflow wraps silently and produces mirror artefacts.

---

## UDP Packet Layout (Data Port 45458)

Each packet sent to port 45458 has a 4-byte header followed by raw `LaserdockSample` structs.

```
Byte 0:    0xA9  — LASERCUBE_SAMPLE_DATA_ID
Byte 1:    0x00  — reserved
Byte 2:    sequence number (uint8_t, rolling per packet)
Byte 3:    frame counter (uint8_t, rolling per logical frame)
Bytes 4+:  N × 10 bytes of sample data
```

Wait — the on-wire sample in the packet is **10 bytes**, not 8. The device firmware expects the `LaserdockSample` fields in this order:

```
Bytes 0–1:  x    (uint16_t LE)
Bytes 2–3:  y    (uint16_t LE)
Bytes 4–5:  rg   (uint16_t LE — low=red, high=green)
Bytes 6–7:  b    (uint16_t LE — low=blue)
Bytes 8–9:  padding / zeroed
```

`LaserdockNetworkDevice` handles this packing internally. You never construct the packet header yourself; just call `send_samples()`.

**Max samples per packet:** 140 (default send batch is 80).  
**Max packets per batch call:** 20 (ESP32 UDP stack limit).

---

## Internal Format: `ldCompressedSample`

Used inside `ldCore` between the render pipeline and `ldHardware::send_samples()`. You do not interact with this type directly.

```cpp
// ldHardware stores:
std::vector<ldCompressedSample>  m_compressed_buffer;

// ldNetworkHardware::send_samples(startIndex, count):
//   → reinterpret_cast<LaserdockSample*>(&m_compressed_buffer[startIndex])
//   → calls device->send_samples(ptr, count)
```

The buffer is filled by `ldHardware::setSample(index, ldVertex)` during frame dispatch.

---

## Render-Layer Format: `ldVertex`

Used by `ldRendererOpenlase`. Coordinates in `−1.0 .. +1.0`. Color is `0xAARRGGBB`.

```cpp
struct ldVertex
{
    float    x, y, z;      // -1.0 to +1.0
    uint32_t color;        // 0xAARRGGBB; AA = alpha (not used by laser)
};
```

The render pipeline maps `(-1, -1)` → DAC `(0, 0)` and `(+1, +1)` → DAC `(4095, 4095)`. The `ldFilterManager` applies scale, rotation, and colour corrections before this mapping.

---

## Rate and Timing

| Parameter | Typical value | Notes |
|-----------|--------------|-------|
| DAC rate | 30 000 pps | Samples per second emitted by the galvo DAC |
| Max DAC rate | 40 000 pps | Firmware-dependent; read via `max_dac_rate()` |
| Buffer size | ~5 000 samples | Varies by firmware |
| Target fill | ~33% of buffer | `ldNetworkHardwareManager` default; prevents underrun |
| Packet interval | 5 ms feed timer | Matches 30 000 pps / 150 samples per tick |
| Info poll (inactive) | 250 ms | `GET_FULL_INFO` to update battery/temp/buffer |
| Info poll (active) | 2 500 ms | Reduced frequency while streaming |

### Feeding at 30 000 pps

```
30 000 samples/sec ÷ 200 ticks/sec (5 ms timer) = 150 samples per tick
```

Push 150 samples every 5 ms. The device's internal queue absorbs bursts; the UDP drain is paced by the timer.

---

## Blank Samples

Blank (dark) transitions move the beam without emitting light. Set all colour channels to zero:

```cpp
LaserdockSample blank{};
blank.x  = target_x;
blank.y  = target_y;
blank.rg = 0x0000;
blank.b  = 0x0000;
```

The galvo still moves to `(x, y)` at full speed. Use a sequence of blank samples to bridge between contours, matching the number of samples to the desired transit distance at the current DAC rate.

---

## Sample Rate vs Frame Rate

The DAC rate (30 000 pps) and the render/cook frame rate (~60 fps) are completely independent:

- 30 000 pps → ~833 samples per 16.7 ms frame
- Plan your geometry so each logical frame contains enough samples to fill that budget
- `ldCore`'s task worker handles this decoupling internally
- In Tier 1 (manual), maintain your own feed timer independent of any display clock
