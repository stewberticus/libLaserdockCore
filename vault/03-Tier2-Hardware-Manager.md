# 03 — Tier 2: ldNetworkHardwareManager + ldNetworkHardware

Sits on top of `LaserdockNetworkDevice` (Tier 1). Adds automatic discovery, authentication callbacks, multi-device lifecycle, and integration with `ldFilterManager`. Requires `ldCore`.

---

## Headers

```
ldCore/include/ldCore/Hardware/ldNetworkHardwareManager.h
ldCore/include/ldCore/Hardware/ldNetworkHardware.h
ldCore/include/ldCore/Hardware/ldHardware.h
ldCore/include/ldCore/Hardware/ldHardwareInfo.h
ldCore/include/ldCore/Hardware/ldHardwareManager.h
```

---

## ldNetworkHardwareManager

```cpp
class ldNetworkHardwareManager : public ldAbstractHardwareManager
{
    Q_OBJECT
public:
    explicit ldNetworkHardwareManager(ldFilterManager *filterManager,
                                      QObject *parent = nullptr);

    // ── Identification ────────────────────────────────────────────────────
    QString  hwType() const override;          // "Network"
    QString  managerName() const override;     // "Network Hardware Manager"

    // ── Device list ───────────────────────────────────────────────────────
    uint                        deviceCount() const override;
    std::vector<ldHardware*>    devices() const override;

    // ── Discovery (called by ldHardwareManager periodically) ─────────────
    void  deviceCheck() override;              // broadcasts alive ping

    // ── Active state ──────────────────────────────────────────────────────
    void  setConnectedDevicesActive(bool active) override;

    // ── Authentication callbacks (set before first deviceCheck()) ─────────
    static void setGenerateSecurityRequestCb(
        ldGenerateSecurityRequestCallbackFunc fn);
    static void setAuthenticateSecurityCb(
        ldAuthenticateSecurityResponseCallbackFunc fn);

    // ── Debug helpers ─────────────────────────────────────────────────────
    void  debugAddDevice() override;
    void  debugRemoveDevice() override;
    void  removeDevice(const QString &hwId) override;

signals:
    void  deviceCountChanged(uint count);
};
```

### Threading

The manager runs in its own `QThread` (`m_managerworkerthread`). All device objects are created on that thread. Do not access them directly from the Qt main thread without going through signals/slots or a mutex.

### Security Callbacks

```cpp
// Provide before any discovery occurs:
ldNetworkHardwareManager::setGenerateSecurityRequestCb([]() -> QByteArray {
    // return your security request payload (without the 0xb0 prefix)
    return QByteArray(reinterpret_cast<const char*>(k_security_req),
                      sizeof(k_security_req));
});

ldNetworkHardwareManager::setAuthenticateSecurityCb([](QByteArray response) -> bool {
    // validate the 35-byte response from the device
    return mySecurityLib.validate(response);
});
```

If neither callback is set, the manager uses the hardcoded fallback security request bytes and calls `DeviceAuthenticated(true)` unconditionally — same as the Tier 1 demo.

---

## ldNetworkHardware

Per-device wrapper. Created internally by `ldNetworkHardwareManager`; you receive it via `devices()` or the `deviceCountChanged` signal.

```cpp
class ldNetworkHardware : public ldHardware
{
    Q_OBJECT
public:
    // ── Identity ──────────────────────────────────────────────────────────
    QString  id() const override;         // serial number
    QString  hwType() const override;     // "Network"
    QString  address() const override;    // IP address

    // ── DAC rate ──────────────────────────────────────────────────────────
    int   getDacRate() const override;
    int   getMaximumDacRate() const override;
    void  setDacRate(int rate) const override;
    void  setDacBufferTHold(int level) const override;

    // ── Sample transmission (main data path) ──────────────────────────────
    bool  send_samples(uint startIndex, uint count) override;
        // Reads from the hardware's internal m_compressed_buffer[startIndex..+count]
        // Converts ldCompressedSample → LaserdockSample and calls device->send_samples()
        // Returns false + emits deviceDisconnected() on failure

    bool  send_samples(LaserdockSample *samples, unsigned int size);
        // Lower-level overload for direct buffer access

    // ── Buffer status ─────────────────────────────────────────────────────
    int   get_full_count() override;      // remote buffer occupancy

    // ── Access underlying device ──────────────────────────────────────────
    const device_params &params() const;  // params.device → LaserdockNetworkDevice*

    // ── Active state ──────────────────────────────────────────────────────
    void  setActive(bool active) override; // calls enable_output / disable_output

    // ── Lifecycle ─────────────────────────────────────────────────────────
    void  initialize();
    void  ResetStatus();

signals:
    void  deviceDisconnected();
};
```

---

## ldHardware (Base)

Abstract interface shared by USB and network devices.

```cpp
class ldHardware : public QObject
{
    Q_OBJECT
public:
    enum class Status { UNKNOWN, INITIALIZED };

    virtual QString  id() const = 0;
    virtual QString  hwType() const = 0;
    virtual QString  address() const = 0;

    // ── Buffer management (called by ldDataDispatcher) ────────────────────
    void  setFrame(uint index, size_t count);
    void  setSample(uint index, const ldVertex &sample);
    //    These fill m_compressed_buffer from the rendered ldVertexFrame.

    // ── Filter attachment ─────────────────────────────────────────────────
    void  setFilter(ldHardwareFilter *filter);
    ldHardwareFilter  *filter() const;

    // ── Batch (multi-device sync) ─────────────────────────────────────────
    void  setBatch(ldHardwareBatch *batch);
    ldHardwareBatch  *batch() const;

    // ── State ─────────────────────────────────────────────────────────────
    Status  status() const;
    bool    isEnabled() const;      void setEnabled(bool en);
    bool    isActive() const;       virtual void setActive(bool active);
    bool    isActiveAndInitialized() const;

    // ── Info ──────────────────────────────────────────────────────────────
    ldHardwareInfo  *info() const;   // read-only device properties

    virtual bool  send_samples(uint startIndex, uint count) = 0;

    std::vector<ldCompressedSample>  m_compressed_buffer;

signals:
    void  batchChanged();
    void  statusChanged();
    void  enabledChanged();
};
```

---

## ldHardwareInfo

Read-only property bag, populated automatically from `LaserdockNetworkDevice` info signals.

```cpp
class ldHardwareInfo : public ldPropertyObject
{
    // All exposed as Q_PROPERTY (readable from QML):
    bool    hasValidInfo;
    int     fwMajor, fwMinor;
    int     dacRate, maxDacRate;
    int     batteryPercent;
    int     modelNumber;
    QString modelName;
    int     temperatureDegC;
    int     connectionType;         // LaserdockNetworkDevice::ConnectionType int value
    int     bufferSize, bufferFree;
    int     overTemperature;
    int     temperatureWarning;
    int     interlockEnabled;
    int     packetErrors;
    QString address;                // IP for network devices
};
```

Access via `hardware->info()`.

---

## ldHardwareManager (Top-Level Aggregator)

Used in full ldCore context. Aggregates USB and network managers under one interface.

```cpp
class ldHardwareManager : public QObject
{
    Q_OBJECT
public:
    int                               getDeviceCount() const;
    std::vector<ldHardware*>          devices() const;
    std::vector<ldAbstractHardwareManager*>  hardwareManagers() const;

    void  addHardwareManager(ldAbstractHardwareManager *mgr);
    void  removeDevice(ldHardware *hw);

    void  setForcedDACRate(int rate);   // overrides per-device DAC rate
    int   getForcedDACRate() const;

    void  checkDevices();               // triggers deviceCheck() on all managers
    void  initCheckTimer();             // starts periodic device check

    void  setConnectedDevicesActive(bool active);

signals:
    void  deviceCountChanged(uint count);
    void  forcedDacRateChanged(int rate);
};
```

---

## Typical Tier 2 Setup

```cpp
// Must be called before deviceCheck() fires
ldNetworkHardwareManager::setGenerateSecurityRequestCb(myGenFn);
ldNetworkHardwareManager::setAuthenticateSecurityCb(myAuthFn);

auto *filterMgr   = new ldFilterManager(this);
auto *networkMgr  = new ldNetworkHardwareManager(filterMgr, this);

connect(networkMgr, &ldNetworkHardwareManager::deviceCountChanged,
        this, [networkMgr](uint count) {
    qInfo("Devices: %u", count);
    for (auto *hw : networkMgr->devices()) {
        qInfo("  %s @ %s", qPrintable(hw->hwType()), qPrintable(hw->address()));
        hw->setActive(true);
    }
});

// Discovery fires automatically; or trigger manually:
networkMgr->deviceCheck();
```

---

## Ownership

`ldNetworkHardwareManager` owns all `ldNetworkHardware` objects it creates (stored in `m_initializingnetworkHardwares` and `m_networkHardwares` as `unique_ptr`). Device lifetime is tied to the manager. Never delete a device pointer you get from `devices()`.
