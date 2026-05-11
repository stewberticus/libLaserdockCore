# 04 — Tier 3: Full ldCore Pipeline

The complete rendering + audio + hardware pipeline. Use when you need visualizations, audio reactivity, a filter chain, or multi-device management without writing your own render loop.

---

## Entry Point: `ldCore` Singleton

```
ldCore/include/ldCore/ldCore.h
```

```cpp
class ldCore : public QObject
{
    Q_OBJECT
public:
    // ── Initialization ────────────────────────────────────────────────────
    static void   initResources();           // call BEFORE QCoreApplication
    static ldCore *instance();               // singleton accessor

    void  setStorageDir(const QString &path);
    void  setResourceDir(const QString &path);
    void  initialize();                      // must call before any subsystem

    // ── Subsystem access ──────────────────────────────────────────────────
    ldHardwareManager        *hardwareManager();
    ldFilterManager          *filterManager();
    ldTaskManager            *taskManager();
    ldDataDispatcher         *dataDispatcher();
    ldVisualizationTask      *task();        // main visualization task worker

    ldAudioDecoder           *audioDecoder();
    ldSoundDataProvider      *soundDataProvider();

    // ── DAC rate ──────────────────────────────────────────────────────────
    void  setForcedDACRate(int rate);
    int   forcedDACRate() const;
};
```

### Initialization Sequence

```cpp
ldCore::initResources();                     // register Qt resources
QCoreApplication app(argc, argv);            // or QGuiApplication / QApplication

ldCore *core = ldCore::instance();
core->setStorageDir(QStandardPaths::writableLocation(...));
core->setResourceDir(QCoreApplication::applicationDirPath() + "/res");
core->initialize();

// Now safe to add hardware managers, set visualizers, etc.
```

---

## Visualization: `ldVisualizer`

```
ldCore/include/ldCore/Visualizations/ldVisualizer.h
```

Subclass this to create custom animations. `draw()` is called once per rendered frame.

```cpp
class ldVisualizer : public QObject, public ldShape
{
    Q_OBJECT
public:
    // ── Override these ────────────────────────────────────────────────────
    virtual void      draw() = 0;            // called every render frame
    virtual QString   visualizerName() const;
    virtual int       targetFPS() const { return 45; }
    virtual bool      is3d() const { return false; }
    virtual bool      isMusicAware() const { return false; }
    virtual void      updateWith(ldSoundData *pSoundData, float delta);

    // ── Provided ──────────────────────────────────────────────────────────
    ldRendererOpenlase  *m_renderer;         // draw into this
    ldMusicManager      *m_musicManager;     // audio analysis data

    void  start();
    void  stop();
    void  pause();

signals:
    void  activeChanged();
    void  pausedChanged();
};
```

### Minimal Custom Visualizer

```cpp
class CircleVisualizer : public ldVisualizer
{
    Q_OBJECT
public:
    QString visualizerName() const override { return "Circle"; }

    void draw() override
    {
        m_renderer->begin(OL_LINESTRIP);
        for (int i = 0; i <= 360; ++i) {
            float a = 2.0f * M_PI * i / 360.0f;
            m_renderer->vertex(std::cos(a), std::sin(a), C_WHITE);
        }
        m_renderer->end();
    }
};

// Install:
ldCore::instance()->task()->setVisualizer(new CircleVisualizer);
```

---

## Renderer: `ldRendererOpenlase`

```
ldCore/include/ldCore/Render/ldRendererOpenlase.h
```

Wraps the OpenLase rendering library. Coordinates are normalized: `-1.0 .. +1.0` on both axes. The internal pipeline maps these to `0 .. 4095` DAC units.

### Primitives

```cpp
m_renderer->begin(OL_POINTS);      // or OL_LINESTRIP, OL_LINELOOP
m_renderer->vertex(x, y, color);   // x, y in -1..+1; color = 0xAARRGGBB
m_renderer->vertex3(x, y, z, color); // 3D variant
m_renderer->end();

// Convenience shapes:
m_renderer->line(x1, y1, x2, y2, color);
m_renderer->rect(x1, y1, x2, y2, color);
m_renderer->dot(x, y, n_points, color);
```

### Transforms (apply before `begin`)

```cpp
m_renderer->pushMatrix();
m_renderer->translate(0.5f, 0.0f);
m_renderer->rotate(M_PI / 4.0f);
m_renderer->scale(0.5f, 0.5f);
// ... draw ...
m_renderer->popMatrix();
```

3D variants: `pushMatrix3 / popMatrix3 / rotate3X / rotate3Y / rotate3Z / translate3 / scale3 / perspective`.

### Color Constants

Defined in `ldCore/Helpers/`:

```cpp
C_WHITE    // 0xFFFFFFFF
C_RED      // 0xFFFF0000
C_GREEN    // 0xFF00FF00
C_BLUE     // 0xFF0000FF
C_BLACK    // 0xFF000000
```

---

## Filter Pipeline: `ldFilterManager`

```
ldCore/include/ldCore/Filter/ldFilterManager.h
```

Applied automatically between the renderer and hardware output. You set parameters; the pipeline runs without intervention.

```cpp
ldFilterManager *fm = ldCore::instance()->filterManager();

// Global scale (all devices):
fm->globalScaleFilter()->setScale(0.8f);

// Global power (brightness):
fm->globalPowerFilter()->setPower(0.75f);

// 2D rotation (degrees):
fm->rotateFilter()->setAngle(45.0f);

// Hue shift:
fm->hueFilter()->setHue(120);

// Strobe:
fm->strobeFilter()->setEnabled(true);
fm->strobeFilter()->setFrequency(10.0f);

// Color curve (per-channel):
fm->colorCurveFilter()->setRedCurve(...);

// Sound-reactive level filter:
fm->soundLevelFilter()->setEnabled(true);

// Per-device filter (for independent control):
ldHardwareFilter *hf = fm->getFilterById(device->id());
```

---

## Task & Render Loop: `ldVisualizationTask`

```
ldCore/include/ldCore/Task/ldVisualizationTask.h
```

Manages the render worker thread. Calls `visualizer->draw()` at `targetFPS()`.

```cpp
ldVisualizationTask *task = ldCore::instance()->task();

// Set visualizer:
task->setVisualizer(new CircleVisualizer, /*priority=*/0);

// 3D rotation convenience:
task->setRotX(30.0f);
task->setRotY(0.0f);
task->setRotZ(15.0f);

// Frame rate:
connect(task, &ldVisualizationTask::currentFpsChanged, this, [](int fps) {
    qDebug() << "FPS:" << fps;
});

// Access renderer directly (for manual frame submission):
ldRendererOpenlase *renderer = task->get_openlase();
```

---

## Audio: `ldSoundDataProvider` + `ldMusicManager`

```cpp
// Feed system audio or microphone to visualizers:
ldCore::instance()->soundDataProvider()->setEnabled(true);

// In ldVisualizer::updateWith():
void updateWith(ldSoundData *data, float delta) override
{
    float bass   = data->GetFFTValueForFrequency(80.0f);
    float treble = data->GetFFTValueForFrequency(8000.0f);
    m_radius = 0.3f + 0.5f * bass;
}
```

Audio decoding (file playback):

```cpp
ldCore::instance()->audioDecoder()->setFilePath("/path/to/track.mp3");
ldCore::instance()->audioDecoder()->play();
```

---

## Multi-Device Output

All initialized devices receive the same rendered frame unless you configure per-device filters or batch isolation.

```cpp
ldHardwareManager *hwMgr = ldCore::instance()->hardwareManager();

connect(hwMgr, &ldHardwareManager::deviceCountChanged, this, [hwMgr]() {
    for (auto *hw : hwMgr->devices()) {
        if (hw->isActiveAndInitialized()) {
            hw->setEnabled(true);
            hw->setActive(true);
        }
    }
});

// Force a specific DAC rate across all devices:
hwMgr->setForcedDACRate(30000);
```

---

## Adding a Network Manager to ldCore

```cpp
ldCore *core = ldCore::instance();
core->initialize();

// Register auth callbacks before manager starts discovering:
ldNetworkHardwareManager::setGenerateSecurityRequestCb(myGenFn);
ldNetworkHardwareManager::setAuthenticateSecurityCb(myAuthFn);

auto *netMgr = new ldNetworkHardwareManager(core->filterManager(), core);
core->hardwareManager()->addHardwareManager(netMgr);

// Periodic device check starts automatically via ldHardwareManager::initCheckTimer()
// or trigger manually:
core->hardwareManager()->checkDevices();
```

---

## Simulator (No Physical Device)

Renders a preview window instead of sending to hardware. Useful for development.

```cpp
// In ldCore initialization (before initialize()):
core->dataDispatcher()->setSimulatorEnabled(true);
```

The simulator renders via OpenGL and shows a window with the laser path drawn as a luminous trace. Requires Qt Gui component.
