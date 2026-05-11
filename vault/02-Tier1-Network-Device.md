# 02 — Tier 1: LaserdockNetworkDevice

The lowest-level network transport. Links only against `laserdocklib` (from `3rdparty/laserdocklib`), `Qt::Core`, and `Qt::Network`. No ldCore required.

**Validated 2026-05-07** — discovery, auth, device info, 30 000 pps circle stream all confirmed working against physical hardware. See `demo/src/main.cpp` for the full reference integration.

---

## Header

```
3rdparty/laserdocklib/include/laserdocklib/LaserDockNetworkDevice.h
```

---

## Class Summary

```cpp
class LaserdockNetworkDevice : public QObject
{
    Q_OBJECT
public:
    // ── Status & connection type ──────────────────────────────────────────
    enum Status { UNKNOWN, AUTHENTICATING, INITIALIZED };
    enum ConnectionType {
        CON_UNKNOWN = 0,
        CON_ETHERNET_SERVER,   // cube is DHCP server
        CON_WIFI_SERVER,       // cube is soft AP
        CON_ETHERNET_CLIENT,   // cube gets IP from host
        CON_WIFI_CLIENT        // cube joins host WiFi
    };

    // ── Static discovery helpers ─────────────────────────────────────────
    static bool ConfigDeviceAliveRequestSocket(QUdpSocket &skt);
    static bool RequestDeviceAlive(QUdpSocket &skt);
    static bool DeviceAliveResponseValid(const QByteArray &response_data);

    // ── Construction ─────────────────────────────────────────────────────
    explicit LaserdockNetworkDevice(QString ip_address, QObject *parent = nullptr);

    // ── Lifecycle ────────────────────────────────────────────────────────
    void     initialize();                 // sends GET_FULL_INFO, starts timers
    void     ResetStatus();                // returns device to UNKNOWN state

    // ── Control commands ─────────────────────────────────────────────────
    bool     enable_output();
    bool     disable_output();
    bool     set_dac_rate(uint32_t rate);
    bool     set_dac_buffer_thold_lvl(uint32_t level);
    bool     clear_ringbuffer();

    // ── Query ────────────────────────────────────────────────────────────
    Status          status() const;
    QString         get_ip_address() const;
    ConnectionType  get_connection_type();
    bool            get_disconnected();
    bool            get_output(bool *enabled);
    bool            dac_rate(uint32_t *rate);
    bool            max_dac_rate(uint32_t *rate);
    bool            battery_pct(uint8_t *value);
    bool            temperature_degc(int8_t *value);
    bool            version_major_number(uint32_t *major);
    bool            version_minor_number(uint32_t *minor);
    bool            ringbuffer_empty_sample_count(uint32_t *count);
    std::string     get_serial_number() const;

    // ── Sample streaming (thread-safe) ───────────────────────────────────
    bool  send_samples(LaserdockSample *samples, uint32_t count);

    // ── Flow control ─────────────────────────────────────────────────────
    void  set_max_udp_samples_per_xfer(uint samples);  // default 80
    void  set_max_udp_packets_per_xfer(uint packets);  // default 20

public slots:
    void  SecurityRequest(QByteArray request);      // send 0xb0 auth packet
    void  DeviceAuthenticated(bool state);          // tell device auth result

signals:
    void  DeviceDisconnected();
    void  DeviceReady();
    void  DeviceNeedsAuthenticating();
    void  SecurityResponseReceived(bool success, QByteArray response_data);

    // ── Info signals (fire when GET_FULL_INFO response arrives) ──────────
    void  FWMajorRevisionUpdated(int);
    void  FWMinorRevisionUpdated(int);
    void  DACRateUpdated(int);
    void  MaxDACRateUpdated(int);
    void  SampleBufferFreeUpdated(int buffer_free_samples);
    void  SampleBufferSizeUpdated(int buffer_size_samples);
    void  BatteryPercentUpdated(int battery_percent);
    void  TemperatureUpdated(int temperature_degc);
    void  ConnectionTypeUpdated(ConnectionType);
    void  SerialNumberUpdated(QString);
    void  ModelNameUpdated(QString);
    void  ModelNumberUpdated(int);
    void  OutputEnableUpdated(bool);
    void  InterlockEnabledUpdated(bool);
    void  TemperatureWarningUpdated(bool);
    void  OverTemperatureUpdated(bool);
    void  PacketErrorsUpdated(int);
    void  IPAddressUpdated(QString);
};
```

---

## UDP Ports (internal constants)

| Port | Purpose |
|------|---------|
| 45456 | Alive/discovery — `ConfigDeviceAliveRequestSocket` binds here |
| 45457 | Commands (control, auth, info queries) |
| 45458 | Sample data stream |

---

## Connection Lifecycle

```
1.  ConfigDeviceAliveRequestSocket(skt)   — bind port 45456, set SO_RCVBUF
2.  RequestDeviceAlive(skt)               — broadcast 0x27 to every IPv4 interface
3.  skt.readyRead()
      → DeviceAliveResponseValid(data)    — check: len==2, data[0]==0x27, data[1]==0
      → sender IP extracted
4.  new LaserdockNetworkDevice(ip, parent)
5.  connect signals (see below)
6.  device->initialize()                  — sends GET_FULL_INFO (0x77), starts timers

7.  DeviceNeedsAuthenticating()
      → SecurityRequest(k_security_req)   — prepends 0xb0, sends to cmd port

8.  SecurityResponseReceived(ok, data)
      → DeviceAuthenticated(true)         — moves status AUTHENTICATING → INITIALIZED
      → DeviceReady() emitted

9.  enable_output()
    set_dac_rate(30000)
    QTimer → send_samples(chunk, count)   — streaming loop

10. On exit:  disable_output()
              destructor sends two disable packets before closing sockets
```

---

## Signals to Connect (Minimum)

```cpp
connect(device, &LaserdockNetworkDevice::DeviceNeedsAuthenticating, this, [&]() {
    QByteArray req(reinterpret_cast<const char*>(k_security_req),
                   sizeof(k_security_req));
    device->SecurityRequest(req);
});

connect(device, &LaserdockNetworkDevice::SecurityResponseReceived,
        this, [&](bool /*ok*/, const QByteArray& /*data*/) {
    // Demo: unconditionally authenticate.
    // Production: validate data against the expected HMAC/SHA response.
    device->DeviceAuthenticated(true);
});

connect(device, &LaserdockNetworkDevice::DeviceReady,    this, &MyClass::onReady);
connect(device, &LaserdockNetworkDevice::DeviceDisconnected, this, &MyClass::onLost);
```

---

## Authentication Packet

The hardcoded fallback security request bytes (copy from `ldNetworkHardwareManager`):

```cpp
static const uint8_t k_security_req[] = {
    0x01,
    0xe0, 0x2e, 0x00, 0x00, 0x40, 0x9c, 0x00,
    0x00, 0x23, 0x27, 0x08, 0x00, 0x00, 0x00, 0xa1,
    0x21, 0x00, 0x00, 0xea, 0x35, 0x00, 0x00, 0x75,
    0x4f, 0x00, 0x00, 0x90, 0x1f, 0x00, 0x00, 0x40,
    0x39, 0x00, 0x00, 0x9c, 0x6d, 0x00, 0x00, 0xf2,
    0x2d, 0x00, 0x00, 0xa2, 0x6f, 0x00, 0x00, 0x73,
    0xc4
};
// Note: SecurityRequest() prepends the 0xb0 command byte; do NOT include it here.
```

`DeviceAuthenticated(true)` works against test hardware regardless of response content. A production integration should cryptographically verify the 35-byte response data.

---

## Streaming

`send_samples()` is thread-safe. It pushes into the device's internal `ldSharedQueue<LaserdockSample>`. The device's internal `QTimer` drains the queue and sends UDP packets on port 45458 at the rate the device can absorb (governed by `SampleBufferFreeUpdated` feedback).

```cpp
// Typical feed pattern (~150 samples every 5 ms at 30 000 pps)
m_feed_timer = new QTimer(this);
m_feed_timer->setInterval(5);
connect(m_feed_timer, &QTimer::timeout, this, [&]() {
    device->send_samples(circle_chunk.data(),
                         static_cast<uint32_t>(circle_chunk.size()));
});
m_feed_timer->start();
```

Internals: max 140 samples per UDP packet (default 80); max 20 packets per batch. Both are tunable via `set_max_udp_samples_per_xfer()` / `set_max_udp_packets_per_xfer()`.

---

## Comms Watchdog

The device starts a 4 000 ms no-comms timer after `initialize()`. If no UDP response arrives within that window, `DeviceDisconnected()` is emitted. This timer resets every time a response packet (GET_FULL_INFO reply, buffer-free reply, etc.) arrives.

---

## Internal Timers

| Timer | Interval | Purpose |
|-------|----------|---------|
| `m_timer` (info poll, inactive) | 250 ms | Sends GET_FULL_INFO to update battery/temp/buffer |
| `m_timer` (info poll, active) | 2 500 ms | Same, but less frequent while streaming |
| `m_nocommstimer` | 4 000 ms | Comms watchdog → `DeviceDisconnected()` |
| `m_security_timer` | 1 000 ms | Resends security request if no response |

---

## Discovery: Close the Ping Socket Before Constructing the Device

The discovery socket binds port 45456. `LaserdockNetworkDevice` constructor binds port 45457 (cmd). These are different ports, but as a precaution — and because you no longer need the discovery socket — close it before constructing the device:

```cpp
void onDiscoveryResponse() {
    const QString ip = dg.senderAddress().toString();
    m_ping_socket->close();      // ← close before constructing device
    connectToDevice(ip);
}
```

---

## CMake (Tier 1 only)

```cmake
# Link against just laserdocklib + Qt network — no ldCore required
target_link_libraries(my_target PRIVATE
    laserdocklib
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
)
```

The `laserdocklib` target is exported by `libLaserdockCore/3rdparty/laserdocklib/CMakeLists.txt`. It can also be built standalone — its `CMakeLists.txt` contains its own `project()` call.

See [[06-Build]] for the full option set.
