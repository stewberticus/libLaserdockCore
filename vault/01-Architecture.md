# 01 — Architecture

## Repository Layout

```
libLaserdockCore/
  3rdparty/
    laserdocklib/           ← Tier 1: low-level UDP transport
      include/laserdocklib/
        LaserDockNetworkDevice.h   main network device class
        LaserdockSample.h          wire sample struct
        ldSharedQueue.h            thread-safe queue template
      src/
        LaserDockNetworkDevice.cpp
  ldCore/                   ← Tier 2 + Tier 3: hardware manager + full pipeline
    include/ldCore/
      ldCore.h                     singleton entry point
      Hardware/                    device abstraction
      Filter/                      frame processing
      Render/                      OpenLase renderer wrapper
      Task/                        worker thread management
      Visualizations/              ldVisualizer animation base
      Sound/                       audio decode + FFT
      Data/                        frame buffer + dispatcher
      Shape/                       geometric primitives
      Helpers/                     math, color, transform utils
    src/
  demo/                     ← Reference Tier 1 integration
    src/main.cpp
  example/                  ← Qt Quick GUI example (full Tier 3)
```

---

## Three Usage Tiers

### Tier 1 — Raw Network Device (`laserdocklib`)

```
QCoreApplication
  └─ Your QObject
       └─ LaserdockNetworkDevice   (QObject, owns 3 QUdpSocket)
            ├─ discovery socket    port 45456 — alive ping/response
            ├─ command socket      port 45457 — control commands
            └─ data socket         port 45458 — sample stream
```

You own the event loop, the discovery logic, and the authentication handling. The device manages its own internal `ldSharedQueue<LaserdockSample>` and drains it to UDP on a `QTimer`.

**Dependencies:** `Qt::Core`, `Qt::Network`  
**Reference:** [[02-Tier1-Network-Device]], `demo/src/main.cpp`

---

### Tier 2 — Hardware Manager (`ldNetworkHardwareManager`)

```
ldNetworkHardwareManager   (runs in its own QThread)
  ├─ QUdpSocket m_pingskt  continuous broadcast discovery
  └─ [0..N] ldNetworkHardware
               └─ LaserdockNetworkDevice   (Tier 1 device, owned here)
```

The manager owns discovery, authentication callbacks, device lifecycle, and comms-timeout cleanup. You interact at the `ldNetworkHardware` level: call `send_samples()`, read `ldHardwareInfo`, react to `deviceCountChanged`.

**Dependencies:** `ldCore` (for `ldFilterManager` passed to constructor)  
**Reference:** [[03-Tier2-Hardware-Manager]]

---

### Tier 3 — Full Pipeline (`ldCore`)

```
ldCore (singleton)
  ├─ ldHardwareManager        manages USB + network managers
  ├─ ldFilterManager          per-device + global filter chains
  ├─ ldTaskManager            worker thread lifecycle
  ├─ ldVisualizationTask      drives ldVisualizer at fixed rate
  │    └─ ldRendererOpenlase  OpenLase drawing API (-1..+1 coords)
  ├─ ldSoundDataProvider      audio input + FFT
  ├─ ldDataDispatcher         routes rendered frames → hardware
  └─ ldSimulator              optional OpenGL preview
```

You subclass `ldVisualizer`, override `draw()` (and optionally `updateWith()`), and hand the instance to `ldVisualizationTask::setVisualizer()`. Everything else is automatic.

**Dependencies:** `ldCore` shared lib + full Qt stack (Core, Network, Gui, Multimedia, Quick if example)  
**Reference:** [[04-Tier3-ldCore]]

---

## Data Flow (Tier 3)

```
ldVisualizer::draw()
  └─ ldRendererOpenlase (openlase -1..+1 coords)
       └─ ldFrameBuffer (vertex list)
            └─ ldFilterManager::processFrame1/2()
                 └─ ldHardware::setFrame() / setSample()
                      └─ compress to ldCompressedSample[]
                           └─ ldNetworkHardware::send_samples()
                                └─ LaserdockNetworkDevice::send_samples()
                                     └─ QUdpSocket → device port 45458
```

---

## Thread Model

| Thread | Owner | Responsibility |
|--------|-------|----------------|
| Qt main thread | Caller | `ldCore` init, signal connections, UI |
| `ldNetworkHardwareManager` worker | `ldNetworkHardwareManager` | Discovery, auth, device lifecycle |
| `ldTaskWorker` | `ldTaskManager` | Render loop — calls `update()` on each task |
| Internal device timers | `LaserdockNetworkDevice` | Info polling, sample drain, comms watchdog |

`LaserdockNetworkDevice::send_samples()` is thread-safe: it pushes into `ldSharedQueue` which uses `std::mutex`. The device's internal timer drains the queue on the Qt event loop.

---

## Key Headers

| Header | Tier | Purpose |
|--------|------|---------|
| `laserdocklib/LaserDockNetworkDevice.h` | 1 | Network device, discovery, streaming |
| `laserdocklib/LaserdockSample.h` | 1 | Wire sample struct |
| `ldCore/Hardware/ldNetworkHardwareManager.h` | 2 | Auto-discovery manager |
| `ldCore/Hardware/ldNetworkHardware.h` | 2 | Per-device wrapper |
| `ldCore/Hardware/ldHardwareInfo.h` | 2 | Read-only device status |
| `ldCore/ldCore.h` | 3 | Singleton entry point |
| `ldCore/Visualizations/ldVisualizer.h` | 3 | Custom animation base class |
| `ldCore/Filter/ldFilterManager.h` | 3 | Frame processing chain |
| `ldCore/Render/ldRendererOpenlase.h` | 3 | OpenLase drawing API |
