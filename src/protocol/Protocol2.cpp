#include "protocol/Protocol2.h"

#include "core/RadioModel.h"

#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace brick2 {
namespace {

void putU16(QByteArray& b, int off, quint16 v)
{
    b[off] = char((v >> 8) & 0xFF);
    b[off + 1] = char(v & 0xFF);
}

void putU32(QByteArray& b, int off, quint32 v)
{
    b[off] = char((v >> 24) & 0xFF);
    b[off + 1] = char((v >> 16) & 0xFF);
    b[off + 2] = char((v >> 8) & 0xFF);
    b[off + 3] = char(v & 0xFF);
}

QString macString(const quint8* p)
{
    return QStringLiteral("%1:%2:%3:%4:%5:%6")
        .arg(p[0], 2, 16, QChar('0'))
        .arg(p[1], 2, 16, QChar('0'))
        .arg(p[2], 2, 16, QChar('0'))
        .arg(p[3], 2, 16, QChar('0'))
        .arg(p[4], 2, 16, QChar('0'))
        .arg(p[5], 2, 16, QChar('0'))
        .toUpper();
}

qint32 be24(const quint8* p)
{
    qint32 v = (qint32(qint8(p[0])) << 16) | (qint32(p[1]) << 8) | p[2];
    return v;
}

} // namespace

Protocol2Engine::Protocol2Engine(RadioModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
{
    bindSocket();
    connect(&m_socket, &QUdpSocket::readyRead, this, &Protocol2Engine::onReadyRead);

    auto* cmd = new QTimer(this);
    cmd->setInterval(50);
    connect(cmd, &QTimer::timeout, this, &Protocol2Engine::sendCommands);
    cmd->start();

    auto* disc = new QTimer(this);
    disc->setInterval(1500);
    connect(disc, &QTimer::timeout, this, [this] {
        if (m_discovering)
            sendDiscovery();
    });
    disc->start();

    auto* sim = new QTimer(this);
    sim->setInterval(10);
    connect(sim, &QTimer::timeout, this, &Protocol2Engine::simulatorTick);
    sim->start();

    m_audioBuf.fill(0, 260);
    m_txIqBuf.fill(0, 1444);
}

bool Protocol2Engine::bindSocket()
{
    m_socket.close();
    const auto flags = QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint;
    bool ok = m_socket.bind(QHostAddress::AnyIPv4, 1025, flags);
    if (!ok)
        ok = m_socket.bind(QHostAddress::AnyIPv4, 0, flags);
    m_localPort = m_socket.localPort();
    if (!ok) {
        m_lastNote = QStringLiteral("UDP bind failed: %1").arg(m_socket.errorString());
        emit discoveryStatus(m_lastNote);
    }
    return ok;
}

QString Protocol2Engine::ipv4String(const QHostAddress& addr) const
{
    QHostAddress v4(addr.toIPv4Address());
    if (v4.isNull() && addr.protocol() != QAbstractSocket::IPv4Protocol)
        return addr.toString();
    if (!v4.isNull())
        return v4.toString();
    return addr.toString();
}

void Protocol2Engine::startDiscovery()
{
    m_discovering = true;
    m_probes = 0;
    QVector<RadioInfo> keep;
    for (const auto& r : m_radios) {
        if (r.simulator)
            keep.push_back(r);
    }
    m_radios = keep;
    emit radiosChanged(m_radios);
    sendDiscovery();
}

void Protocol2Engine::stopDiscovery()
{
    m_discovering = false;
}

void Protocol2Engine::connectTo(const RadioInfo& info)
{
    m_info = info;
    m_running = true;
    m_seqGeneral = m_seqRx = m_seqTx = m_seqHp = 0;
    sendGeneral();
    sendRxSpecific();
    sendTxSpecific();
    sendHighPriority();
    emit connected(m_info);
}

void Protocol2Engine::disconnectRadio()
{
    if (m_running && !m_info.simulator) {
        const bool was = m_running;
        m_running = false;
        sendHighPriority();
        Q_UNUSED(was);
    }
    m_running = false;
    m_info = {};
    emit disconnected(QStringLiteral("Stopped"));
}

bool Protocol2Engine::connectFirstHardware()
{
    for (const auto& r : m_radios) {
        if (!r.simulator && !r.ip.isEmpty()) {
            connectTo(r);
            return true;
        }
    }
    return false;
}

void Protocol2Engine::connectToIp(const QString& ip)
{
    const QHostAddress addr(ip.trimmed());
    if (addr.isNull()) {
        m_lastNote = QStringLiteral("Invalid IP address");
        emit discoveryStatus(m_lastNote);
        return;
    }
    probeAddress(addr);
    RadioInfo info;
    info.name = QStringLiteral("Brick2 (manual)");
    info.ip = ipv4String(addr);
    info.protocol = 2;
    info.maxReceivers = 2;
    rememberRadio(info);
    connectTo(info);
}

void Protocol2Engine::startSimulator()
{
    RadioInfo sim;
    sim.name = QStringLiteral("Brick2 Simulator");
    sim.ip = QStringLiteral("127.0.0.1");
    sim.mac = QStringLiteral("02:42:52:4B:32:01");
    sim.firmware = 1;
    sim.simulator = true;
    sim.protocol = 2;
    m_radios = {sim};
    emit radiosChanged(m_radios);
    connectTo(sim);
}

void Protocol2Engine::pushControl()
{
    if (!m_running || m_info.simulator)
        return;
    const bool tx = m_model->isTransmitting();
    if (tx && !m_wasTx) {
        m_txIqBuf.fill(0, 1444);
        m_txIqIndex = 4;
        m_seqTxIq = 0;
    }
    if (!tx && m_wasTx && m_txIqIndex > 4)
        flushTxIq();
    m_wasTx = tx;
    sendHighPriority();
    sendTxSpecific();
}

void Protocol2Engine::sendAudio(qint16 left, qint16 right)
{
    if (!m_running || m_info.simulator)
        return;
    if (m_audioIndex + 4 > m_audioBuf.size())
        flushAudio();
    m_audioBuf[m_audioIndex++] = char((left >> 8) & 0xFF);
    m_audioBuf[m_audioIndex++] = char(left & 0xFF);
    m_audioBuf[m_audioIndex++] = char((right >> 8) & 0xFF);
    m_audioBuf[m_audioIndex++] = char(right & 0xFF);
    if (m_audioIndex >= 260)
        flushAudio();
}

void Protocol2Engine::sendTxIq(float i, float q)
{
    if (!m_running || m_info.simulator)
        return;
    const auto enc = [](float x) {
        const int v = int(std::clamp(x, -1.f, 1.f) * 8388607.f);
        return v;
    };
    if (m_txIqIndex + 6 > 1444)
        flushTxIq();
    const int ii = enc(i);
    const int qq = enc(q);
    m_txIqBuf[m_txIqIndex++] = char((ii >> 16) & 0xFF);
    m_txIqBuf[m_txIqIndex++] = char((ii >> 8) & 0xFF);
    m_txIqBuf[m_txIqIndex++] = char(ii & 0xFF);
    m_txIqBuf[m_txIqIndex++] = char((qq >> 16) & 0xFF);
    m_txIqBuf[m_txIqIndex++] = char((qq >> 8) & 0xFF);
    m_txIqBuf[m_txIqIndex++] = char(qq & 0xFF);
    if (m_txIqIndex >= 1444)
        flushTxIq();
}

void Protocol2Engine::flushAudio()
{
    putU32(m_audioBuf, 0, m_seqAudio++);
    const QHostAddress addr(m_info.ip);
    m_socket.writeDatagram(m_audioBuf, addr, 1028);
    m_audioBuf.fill(0, 260);
    m_audioIndex = 4;
}

void Protocol2Engine::flushTxIq()
{
    putU32(m_txIqBuf, 0, m_seqTxIq++);
    const QHostAddress addr(m_info.ip);
    m_socket.writeDatagram(m_txIqBuf, addr, 1029);
    m_txIqBuf.fill(0, 1444);
    m_txIqIndex = 4;
}

void Protocol2Engine::flushPendingTx()
{
    if (m_txIqIndex > 4)
        flushTxIq();
}

void Protocol2Engine::probeAddress(const QHostAddress& dest)
{
    QByteArray p2(60, 0);
    p2[4] = 0x02; // Protocol 2 discovery (piHPSDR / Thetis)
    m_socket.writeDatagram(p2, dest, kDiscoveryPort);

    // Metis probe with byte 4 != 0 so P2 gateware does not treat it as a General command
    QByteArray p1(63, 0);
    p1[0] = char(0xEF);
    p1[1] = char(0xFE);
    p1[2] = 0x02;
    p1[4] = 0x02;
    m_socket.writeDatagram(p1, dest, kDiscoveryPort);
}

void Protocol2Engine::sendDiscovery()
{
    ++m_probes;
    int targets = 0;
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning))
            continue;
        if (flags & QNetworkInterface::IsLoopBack)
            continue;
        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            if (entry.ip().isLoopback())
                continue;
            if (!entry.broadcast().isNull()) {
                probeAddress(entry.broadcast());
                ++targets;
            }
            probeAddress(entry.ip()); // some adapters need directed local probe
            ++targets;
        }
    }
    probeAddress(QHostAddress::Broadcast);
    ++targets;
    m_lastNote = QStringLiteral("Protocol 2 discovery on %1 target(s)  ·  probe %2")
                     .arg(targets)
                     .arg(m_probes);
    if (m_radios.isEmpty() && m_probes >= 2)
        m_lastNote += QStringLiteral("  ·  no reply yet — check LAN/firewall or enter IP");
    emit discoveryStatus(m_lastNote);
}

void Protocol2Engine::onReadyRead()
{
    while (m_socket.hasPendingDatagrams()) {
        const QNetworkDatagram d = m_socket.receiveDatagram();
        processPacket(d.data(), d.senderAddress(), quint16(d.senderPort()));
    }
}

void Protocol2Engine::sendCommands()
{
    if (!m_running || m_info.simulator)
        return;
    sendGeneral();
    sendRxSpecific();
    sendTxSpecific();
    sendHighPriority();
}

void Protocol2Engine::sendGeneral()
{
    QByteArray b(60, 0);
    putU32(b, 0, m_seqGeneral++);
    b[4] = 0;
    const quint16 pc = m_localPort ? m_localPort : quint16(m_socket.localPort());
    putU16(b, 5, pc);    // HP status → this socket
    putU16(b, 7, pc);    // mic → this socket
    putU16(b, 9, pc);    // wideband → this socket
    putU16(b, 11, 1025); // DDC command to radio
    putU16(b, 13, 1028); // RX audio to radio
    putU16(b, 15, 1029); // TX IQ to radio
    putU16(b, 17, pc);   // DDC0 IQ → this socket
    putU16(b, 19, 1026); // DUC command to radio
    putU16(b, 21, 1027); // HP command to radio
    b[23] = 0;
    putU16(b, 24, 512);
    b[26] = 16;
    b[27] = 0;
    b[28] = 32;
    b[37] = 0x08; // phase word, not Hz
    b[38] = 0x01; // hardware watchdog
    b[58] = char((m_model->paEnable ? 0x01 : 0) | (m_model->apollo ? 0x02 : 0));
    b[59] = 0x03; // Alex 0 + Alex 1
    m_socket.writeDatagram(b, QHostAddress(m_info.ip), 1024);
}

void Protocol2Engine::sendRxSpecific()
{
    QByteArray b(1444, 0);
    putU32(b, 0, m_seqRx++);
    const quint8 adcs = (m_info.boardType <= 2) ? 1 : 2;
    b[4] = char(adcs);
    const quint8 adcMask = adcs == 1 ? 0x01 : 0x07;
    if (m_model->rx1.dither)
        b[5] = char(adcMask);
    if (m_model->rx1.randomizer)
        b[6] = char(adcMask);
    quint8 ddc = 0x01;
    const int khz = m_model->sampleRate / 1000;
    b[17] = 0;
    putU16(b, 18, quint16(khz));
    b[22] = 24;
    if (m_model->rx2Enabled) {
        ddc = quint8(ddc | 0x02);
        b[23] = 0;
        putU16(b, 24, quint16(khz));
        b[28] = 24;
        if (m_model->rx2.dither)
            b[5] = char(b[5] | 0x02);
        if (m_model->rx2.randomizer)
            b[6] = char(b[6] | 0x02);
    }
    if (m_model->ps.enabled && m_model->isTransmitting()) {
        ddc = quint8(ddc | 0x03);
        b[1363] = 0x02;
        b[17 + 6] = 1;
        putU16(b, 18 + 6, 192);
        b[22 + 6] = 24;
    }
    b[7] = char(ddc);
    m_socket.writeDatagram(b, QHostAddress(m_info.ip), 1025);
}

void Protocol2Engine::sendTxSpecific()
{
    QByteArray b(60, 0);
    putU32(b, 0, m_seqTx++);
    b[4] = 1; // 1 DAC
    putU16(b, 14, quint16(std::max(1, m_model->sampleRate / 1000)));
    if (isCw(m_model->vfoA.mode)) {
        quint8 cw = 0x02;
        if (m_model->cw.iambic)
            cw = quint8(cw | (m_model->cw.modeB ? 0x28 : 0x08));
        if (m_model->cw.sidetoneGain != 0)
            cw = quint8(cw | 0x10);
        if (m_model->cw.breakIn)
            cw = quint8(cw | 0x80);
        b[5] = char(cw);
        b[6] = char(std::clamp(m_model->cw.sidetoneGain, 0, 127));
        putU16(b, 7, quint16(m_model->cw.sidetone));
        b[9] = char(std::clamp(m_model->cw.wpm, 1, 60));
        b[10] = 50;
        putU16(b, 11, 300);
        b[13] = char(std::clamp(std::min(8, 900 / std::max(1, m_model->cw.wpm)), 0, 255));
        b[17] = 9;
    }
    quint8 mic = 0;
    if (m_model->tx.lineIn)
        mic |= 0x01;
    if (m_model->tx.micBoost)
        mic |= 0x02;
    if (m_model->tx.micBias)
        mic |= 0x10;
    b[50] = char(mic);
    b[57] = 0;
    b[58] = 0;
    b[59] = 0;
    m_socket.writeDatagram(b, QHostAddress(m_info.ip), 1026);
}

void Protocol2Engine::sendHighPriority()
{
    QByteArray b(1444, 0);
    putU32(b, 0, m_seqHp++);
    quint8 run = m_running ? 0x01 : 0x00;
    if (m_model->isTransmitting())
        run |= 0x02;
    b[4] = char(run);

    const qint64 rxA = m_model->vfoA.frequency + (m_model->vfoA.ritOn ? m_model->vfoA.rit : 0);
    const qint64 rxB = m_model->vfoB.frequency + (m_model->vfoB.ritOn ? m_model->vfoB.rit : 0);
    putU32(b, 9, frequencyToPhaseWord(double(rxA)));
    if (m_model->rx2Enabled)
        putU32(b, 13, frequencyToPhaseWord(double(rxB)));

    const qint64 tx = m_model->txFrequency();
    putU32(b, 329, frequencyToPhaseWord(double(tx)));

    int power = m_model->tx.drive;
    if (m_model->isTransmitting() && m_model->tune && !m_model->tx.useDriveForTune)
        power = int(m_model->tx.drive / 100.0 * m_model->tx.tunePercent);
    b[345] = char(std::clamp(int(std::lround(power * 2.55)), 0, 255));
    b[1403] = m_model->rx1.lna ? 1 : 0;
    const quint8 att = quint8(std::clamp(m_model->rx1.stepAtt, 0, 31));
    b[1442] = char(att);
    b[1443] = char(att);
    const quint32 filters = alexFilterWord(rxA, tx, m_model->isTransmitting(), m_model->ps.enabled);
    putU32(b, 1428, filters);
    putU32(b, 1432, filters);
    m_socket.writeDatagram(b, QHostAddress(m_info.ip), 1027);
}

void Protocol2Engine::processPacket(const QByteArray& data, const QHostAddress& from, quint16 port)
{
    if (data.isEmpty())
        return;

    if (data.size() >= 50 && data.size() <= 80)
        parseDiscovery(data, from);

    if (!m_running)
        return;

    switch (port) {
    case 1024:
        break;
    case 1025:
        parseHighPriority(data);
        break;
    case 1026:
        parseMic(data);
        break;
    default:
        if (port >= 1035 && port <= 1042)
            parseIq(port - 1035, data);
        break;
    }
}

void Protocol2Engine::rememberRadio(RadioInfo info)
{
    if (info.ip.startsWith(QLatin1String("::ffff:")))
        info.ip = info.ip.mid(7);
    bool found = false;
    for (auto& r : m_radios) {
        if ((!info.mac.isEmpty() && r.mac == info.mac) || (r.ip == info.ip && r.mac == info.mac)) {
            r = info;
            found = true;
            break;
        }
        if (info.mac.isEmpty() && r.ip == info.ip) {
            r = info;
            found = true;
            break;
        }
    }
    if (!found)
        m_radios.push_back(info);
    emit radiosChanged(m_radios);
}

void Protocol2Engine::parseDiscovery(const QByteArray& data, const QHostAddress& from)
{
    if (data.size() < 12)
        return;
    const auto* p = reinterpret_cast<const quint8*>(data.constData());
    RadioInfo info;
    info.ip = ipv4String(from);
    info.maxReceivers = 2;

    const bool p2 = data.size() >= 14 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0
        && (p[4] == 0x02 || p[4] == 0x03);
    const bool p1 = p[0] == 0xEF && p[1] == 0xFE && (p[2] == 0x02 || p[2] == 0x03);

    if (p2) {
        info.protocol = 2;
        info.inUse = p[4] == 0x03;
        info.mac = macString(p + 5);
        info.boardType = p[11];
        info.firmware = p[13];
        info.name = QStringLiteral("Brick2 / Hermes (P2)");
        switch (info.boardType) {
        case 0: info.name = QStringLiteral("Atlas (P2)"); break;
        case 1: info.name = QStringLiteral("Hermes / Brick2 (P2)"); break;
        case 2: info.name = QStringLiteral("Hermes II / ANAN-10E / Brick2"); break;
        case 3: info.name = QStringLiteral("Angelia (P2)"); break;
        case 4: info.name = QStringLiteral("Orion (P2)"); break;
        case 5: info.name = QStringLiteral("Orion MkII (P2)"); break;
        default: break;
        }
    } else if (p1) {
        info.protocol = 2; // Brick2 still uses P2 streaming; Metis reply is identity only
        info.inUse = p[2] == 0x03;
        info.mac = macString(p + 3);
        info.firmware = p[9];
        info.boardType = p[10];
        info.name = QStringLiteral("HPSDR (Metis identity, P2 connect)");
    } else {
        return;
    }

    rememberRadio(info);
    m_lastNote = QStringLiteral("Found %1  ·  %2  ·  %3").arg(info.name, info.ip, info.mac);
    emit discoveryStatus(m_lastNote);
}

void Protocol2Engine::parseIq(int ddc, const QByteArray& data)
{
    if (data.size() < 16 + 6)
        return;
    const auto* p = reinterpret_cast<const quint8*>(data.constData());
    const int n = std::min(238, int((data.size() - 16) / 6));
    QVector<float> samples;
    samples.reserve(n * 2);
    constexpr float kScale = 1.f / 8388608.f;
    int b = 16;
    for (int i = 0; i < n; ++i) {
        samples.push_back(float(be24(p + b)) * kScale);
        samples.push_back(float(be24(p + b + 3)) * kScale);
        b += 6;
    }
    emit iqReceived(ddc, samples);
}

void Protocol2Engine::parseHighPriority(const QByteArray& data)
{
    if (data.size() < 51)
        return;
    const auto* p = reinterpret_cast<const quint8*>(data.constData());
    m_model->meters.ptt = p[4] & 0x01;
    m_model->meters.dot = (p[4] >> 1) & 0x01;
    m_model->meters.dash = (p[4] >> 2) & 0x01;
    m_model->meters.adcOverload = p[5] & 0x01;
    const int fwd = (p[14] << 8) | p[15];
    const int rev = (p[22] << 8) | p[23];
    const int volts = (p[49] << 8) | p[50];
    const auto cal = [](int v) {
        const double v1 = double(v) / 4095.0 * 3.3;
        return float((v1 * v1) / 0.095);
    };
    m_model->meters.fwdWatts = cal(fwd);
    m_model->meters.revWatts = cal(rev);
    if (m_model->meters.fwdWatts > 0.05f)
        m_model->meters.swr = float((1.0 + std::sqrt(m_model->meters.revWatts / m_model->meters.fwdWatts))
                                    / (1.0 - std::sqrt(std::min(0.99f, m_model->meters.revWatts / m_model->meters.fwdWatts))));
    m_model->meters.supplyVolts = float(volts) / 4095.f * 3.3f * 11.f;
    emit statusUpdated();
}

void Protocol2Engine::parseMic(const QByteArray& data)
{
    if (data.size() < 8 || data.size() == 60)
        return;
    QVector<float> mic;
    const auto* p = reinterpret_cast<const quint8*>(data.constData());
    for (int i = 4; i + 1 < data.size(); i += 2) {
        const qint16 s = qint16((p[i] << 8) | p[i + 1]);
        mic.push_back(float(s) / 32768.f);
    }
    emit micReceived(mic);
}

void Protocol2Engine::simulatorTick()
{
    if (!m_running || !m_info.simulator)
        return;

    const int n = std::max(1, m_model->sampleRate / 100);
    QVector<float> iq(n * 2);
    static double phase = 0;
    static double noisePhase = 0;
    const double bin = kTwoPi * 1000.0 / m_model->sampleRate;
    auto* rng = QRandomGenerator::global();
    for (int i = 0; i < n; ++i) {
        const float noise = float(rng->generateDouble() * 2.0 - 1.0) * 0.002f;
        const float sig = float(0.04 * std::cos(phase));
        const float sigq = float(0.04 * std::sin(phase));
        const float bc = float(0.015 * std::cos(noisePhase * 3.7));
        iq[i * 2] = sig + noise + bc;
        iq[i * 2 + 1] = sigq + noise * 0.9f;
        phase += bin;
        noisePhase += bin * 0.37;
    }
    emit iqReceived(0, iq);

    m_model->meters.sMeter = -73.f;
    m_model->meters.supplyVolts = 13.6f;
    if (m_model->isTransmitting()) {
        m_model->meters.fwdWatts = float(m_model->tx.drive) * 0.15f;
        m_model->meters.swr = 1.2f;
        m_model->meters.alc = 3.f;
    } else {
        m_model->meters.fwdWatts = 0;
        m_model->meters.alc = 0.f;
    }
    emit statusUpdated();
}

} // namespace brick2
