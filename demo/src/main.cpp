// ld_network_demo — proves out LaserdockNetworkDevice ethernet connectivity.
//
// Stages:
//   1. Parse --ip <addr> CLI arg. If absent, broadcast-discover devices on LAN.
//   2. Connect to the discovered/specified device; handle authentication.
//   3. On DeviceReady: print device info, enable output, stream a white circle
//      for 5 seconds at 30 000 pps, then disable output and exit 0.
//
// Build (from libLaserdockCore/build-macos-arm64):
//   cmake .. -DLD_CORE_BUILD_NETWORK_DEMO=ON -DLD_CORE_BUILD_EXAMPLE=OFF \
//             -DLD_CORE_BUILD_TESTS=OFF -DQTDIR=/opt/homebrew/opt/qt -G Ninja
//   ninja ld_network_demo
//
// Run:
//   ./demo/ld_network_demo                     # broadcast discovery
//   ./demo/ld_network_demo --ip 192.168.1.100  # direct connect

#include <laserdocklib/LaserDockNetworkDevice.h>
#include <laserdocklib/LaserdockSample.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QTimer>
#include <QUdpSocket>

#include <QtDebug>
#include <cmath>
#include <cstdio>

// ---------------------------------------------------------------------------
// Demo constants
// ---------------------------------------------------------------------------
static constexpr uint32_t k_dac_rate          = 30000; // samples per second
static constexpr int      k_samples_per_tick  = 150;   // sent each timer tick
static constexpr int      k_tick_ms           = 5;     // feed timer interval (ms)
static constexpr int      k_stream_seconds    = 5;     // total streaming duration
static constexpr int      k_discover_ms       = 5000;  // discovery timeout
static constexpr uint16_t k_center            = 2048;  // 0..4095 midpoint
static constexpr uint16_t k_radius            = 1500;  // circle radius in DAC units

// ---------------------------------------------------------------------------
// Hardcoded security request payload (matches ldNetworkHardwareManager fallback).
// SecurityRequest() prepends the 0xB0 command byte, so the data starts at 0x01.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Pre-compute circle samples once.
// LaserdockSample: rg = (green << 8) | red, b lower byte = blue, x/y 0..4095.
// White circle at centre 2048, radius 1500.
// ---------------------------------------------------------------------------
static std::vector<LaserdockSample> buildCircle(int point_count = 360)
{
    std::vector<LaserdockSample> pts;
    pts.reserve(static_cast<size_t>(point_count));
    for (int i = 0; i < point_count; ++i) {
        const double angle = 2.0 * M_PI * i / point_count;
        LaserdockSample s{};
        s.x  = static_cast<uint16_t>(k_center + static_cast<int>(k_radius * std::cos(angle)));
        s.y  = static_cast<uint16_t>(k_center + static_cast<int>(k_radius * std::sin(angle)));
        s.rg = 0xFFFF; // red = 0xFF (low byte), green = 0xFF (high byte)
        s.b  = 0x00FF; // blue = 0xFF (low byte), reserved = 0
        pts.push_back(s);
    }
    return pts;
}

// ---------------------------------------------------------------------------

class NetworkDemo : public QObject
{
    Q_OBJECT
public:
    explicit NetworkDemo(const QString& ip_override, QObject* parent = nullptr)
        : QObject(parent)
        , m_ip_override(ip_override)
        , m_circle(buildCircle())
    {}

    void start()
    {
        if (!m_ip_override.isEmpty()) {
            qInfo("[demo] Direct-connect mode, target IP: %s", qPrintable(m_ip_override));
            connectToDevice(m_ip_override);
        } else {
            qInfo("[demo] Discovery mode — broadcasting on all IPv4 interfaces ...");
            startDiscovery();
        }
    }

private slots:
    // ------------------------------------------------------------------
    // Discovery
    // ------------------------------------------------------------------
    void startDiscovery()
    {
        m_ping_socket = new QUdpSocket(this);
        if (!LaserdockNetworkDevice::ConfigDeviceAliveRequestSocket(*m_ping_socket)) {
            qCritical("[demo] Failed to configure discovery socket — cannot continue.");
            QCoreApplication::exit(1);
            return;
        }
        connect(m_ping_socket, &QUdpSocket::readyRead, this, &NetworkDemo::onDiscoveryResponse);

        LaserdockNetworkDevice::RequestDeviceAlive(*m_ping_socket);
        qInfo("[demo] Alive ping sent. Waiting up to %d ms for a response ...", k_discover_ms);

        m_discover_timeout = new QTimer(this);
        m_discover_timeout->setSingleShot(true);
        connect(m_discover_timeout, &QTimer::timeout, this, [this]() {
            qCritical("[demo] No LaserCube device found on the network within %d ms.", k_discover_ms);
            QCoreApplication::exit(1);
        });
        m_discover_timeout->start(k_discover_ms);
    }

    void onDiscoveryResponse()
    {
        while (m_ping_socket->hasPendingDatagrams()) {
            const QNetworkDatagram dg = m_ping_socket->receiveDatagram();
            if (LaserdockNetworkDevice::DeviceAliveResponseValid(dg.data())) {
                const QString ip = dg.senderAddress().toString();
                qInfo("[demo] Device found at %s", qPrintable(ip));

                m_discover_timeout->stop();
                // Stop listening on the alive socket before the device constructor
                // binds its own command socket on the same port.
                m_ping_socket->close();

                connectToDevice(ip);
                return;
            }
        }
    }

    // ------------------------------------------------------------------
    // Device lifecycle
    // ------------------------------------------------------------------
    void connectToDevice(const QString& ip)
    {
        qInfo("[demo] Connecting to device @ %s ...", qPrintable(ip));
        m_device = new LaserdockNetworkDevice(ip, this);

        connect(m_device, &LaserdockNetworkDevice::DeviceNeedsAuthenticating,
                this, &NetworkDemo::onAuthRequest);

        connect(m_device, &LaserdockNetworkDevice::SecurityResponseReceived,
                this, &NetworkDemo::onAuthResponse);

        connect(m_device, &LaserdockNetworkDevice::DeviceReady,
                this, &NetworkDemo::onDeviceReady);

        connect(m_device, &LaserdockNetworkDevice::DeviceDisconnected,
                this, &NetworkDemo::onDeviceDisconnected);

        // Info signals — printed when received
        connect(m_device, &LaserdockNetworkDevice::FWMajorRevisionUpdated, this,
                [](int v){ qInfo("[demo]   FW major : %d", v); });
        connect(m_device, &LaserdockNetworkDevice::FWMinorRevisionUpdated, this,
                [](int v){ qInfo("[demo]   FW minor : %d", v); });
        connect(m_device, &LaserdockNetworkDevice::DACRateUpdated, this,
                [](int v){ qInfo("[demo]   DAC rate : %d pps", v); });
        connect(m_device, &LaserdockNetworkDevice::BatteryPercentUpdated, this,
                [](int v){ qInfo("[demo]   Battery  : %d%%", v); });
        connect(m_device, &LaserdockNetworkDevice::TemperatureUpdated, this,
                [](int v){ qInfo("[demo]   Temp     : %d°C", v); });
        connect(m_device, &LaserdockNetworkDevice::ConnectionTypeUpdated, this,
                [](LaserdockNetworkDevice::ConnectionType ct){
                    const char* names[] = {
                        "Unknown", "Ethernet-server", "WiFi-server",
                        "Ethernet-client", "WiFi-client"
                    };
                    const int idx = static_cast<int>(ct);
                    qInfo("[demo]   Conn type: %s",
                          (idx >= 0 && idx < 5) ? names[idx] : "?");
                });
        connect(m_device, &LaserdockNetworkDevice::SerialNumberUpdated, this,
                [](const QString& s){ qInfo("[demo]   Serial   : %s", qPrintable(s)); });
        connect(m_device, &LaserdockNetworkDevice::ModelNameUpdated, this,
                [](const QString& s){ qInfo("[demo]   Model    : %s", qPrintable(s)); });

        m_device->initialize();
        qInfo("[demo] initialize() sent — waiting for DeviceReady ...");
    }

    void onAuthRequest()
    {
        qInfo("[demo] Device requesting authentication — sending fallback security request.");
        const QByteArray req(reinterpret_cast<const char*>(k_security_req),
                             static_cast<int>(sizeof(k_security_req)));
        m_device->SecurityRequest(req);
    }

    void onAuthResponse(bool success, const QByteArray& data)
    {
        qInfo("[demo] Security response received (success=%s, %lld bytes) — calling DeviceAuthenticated(true).",
              success ? "true" : "false", static_cast<long long>(data.size()));
        // Demo: unconditionally authenticate. A production path would validate
        // the response data via the m_authSecRespCb callback.
        m_device->DeviceAuthenticated(true);
    }

    void onDeviceReady()
    {
        qInfo("[demo] *** Device READY ***");
        qInfo("[demo] Device info signals will continue to print as they arrive.");
        qInfo("[demo] Enabling laser output ...");
        m_device->enable_output();
        m_device->set_dac_rate(k_dac_rate);

        qInfo("[demo] Streaming white circle for %d seconds ...", k_stream_seconds);

        // Feed timer: pushes sample blocks into the device's internal queue.
        m_feed_timer = new QTimer(this);
        connect(m_feed_timer, &QTimer::timeout, this, &NetworkDemo::feedSamples);
        m_feed_timer->start(k_tick_ms);

        // Stop timer: halts streaming and exits after the demo duration.
        m_stop_timer = new QTimer(this);
        m_stop_timer->setSingleShot(true);
        connect(m_stop_timer, &QTimer::timeout, this, &NetworkDemo::onStreamDone);
        m_stop_timer->start(k_stream_seconds * 1000);
    }

    void feedSamples()
    {
        const auto count = static_cast<uint32_t>(m_circle.size());
        if (count == 0)
            return;

        // Slice k_samples_per_tick samples from the circle (wraps around).
        std::vector<LaserdockSample> chunk;
        chunk.reserve(k_samples_per_tick);
        for (int i = 0; i < k_samples_per_tick; ++i) {
            chunk.push_back(m_circle[m_circle_pos % count]);
            ++m_circle_pos;
        }
        m_device->send_samples(chunk.data(), static_cast<uint32_t>(chunk.size()));
    }

    void onStreamDone()
    {
        m_feed_timer->stop();
        qInfo("[demo] Stream complete. Disabling laser output.");
        m_device->disable_output();

        // Small delay to let the disable command reach the device before exit.
        QTimer::singleShot(200, this, []() {
            qInfo("[demo] Done — exiting cleanly.");
            QCoreApplication::exit(0);
        });
    }

    void onDeviceDisconnected()
    {
        qCritical("[demo] Device disconnected unexpectedly.");
        QCoreApplication::exit(1);
    }

private:
    QString                       m_ip_override;
    QUdpSocket*                   m_ping_socket    {nullptr};
    QTimer*                       m_discover_timeout{nullptr};
    LaserdockNetworkDevice*       m_device         {nullptr};
    QTimer*                       m_feed_timer     {nullptr};
    QTimer*                       m_stop_timer     {nullptr};
    std::vector<LaserdockSample>  m_circle;
    size_t                        m_circle_pos     {0};
};

// ---------------------------------------------------------------------------

#include "main.moc"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("ld_network_demo");
    app.setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "LaserCube network connectivity demo.\n"
        "Discovers or connects to a LaserCube over ethernet/WiFi and streams "
        "a white circle for 5 seconds.");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption ipOpt(
        QStringList() << "ip",
        "Connect directly to the device at <address> (skip broadcast discovery).",
        "address");
    parser.addOption(ipOpt);
    parser.process(app);

    const QString ip = parser.value(ipOpt);

    auto* demo = new NetworkDemo(ip, &app);
    QTimer::singleShot(0, demo, &NetworkDemo::start);

    return app.exec();
}
