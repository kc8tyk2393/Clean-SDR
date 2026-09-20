#pragma once

#include <QMetaType>
#include <QString>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace brick2 {

inline constexpr double kMasterClockHz = 122880000.0;
inline constexpr quint16 kDiscoveryPort = 1024;
inline constexpr int kMaxReceivers = 2;
inline constexpr int kFftSize = 4096;
inline constexpr int kAudioRate = 48000;
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 6.28318530717958647692;

enum class Mode {
    LSB, USB, DSB, CWL, CWU, FM, AM, SAM, DIGL, DIGU, SPEC, DRM
};

enum class AgcMode { Off, Long, Slow, Med, Fast, Custom };
enum class DisplayMode {
    Spectrum, Panadapter, Scope, Phase, Waterfall, Histogram, Panafall, Panascope, Off
};
enum class FilterPreset { F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, Var1, Var2 };
enum class Band {
    B160, B80, B60, B40, B30, B20, B17, B15, B12, B10, B6, WWV, GEN, VHF
};
enum class MeterType { Signal, Alc, Mic, Eq, Comp, AlcG, AlcPk, Pwr, Swr, Fwd, Rev, Vac };
enum class ConnectionState { Idle, Discovering, Connecting, Running, Error };

struct RadioInfo {
    QString name = QStringLiteral("Brick2");
    QString mac;
    QString ip;
    quint16 port = kDiscoveryPort;
    int firmware = 0;
    int boardType = 2; // Hermes II / ANAN-10E
    int protocol = 2;
    int maxReceivers = 2;
    bool inUse = false;
    bool simulator = false;
};

inline QString modeName(Mode m)
{
    switch (m) {
    case Mode::LSB: return QStringLiteral("LSB");
    case Mode::USB: return QStringLiteral("USB");
    case Mode::DSB: return QStringLiteral("DSB");
    case Mode::CWL: return QStringLiteral("CWL");
    case Mode::CWU: return QStringLiteral("CWU");
    case Mode::FM: return QStringLiteral("FM");
    case Mode::AM: return QStringLiteral("AM");
    case Mode::SAM: return QStringLiteral("SAM");
    case Mode::DIGL: return QStringLiteral("DIGL");
    case Mode::DIGU: return QStringLiteral("DIGU");
    case Mode::SPEC: return QStringLiteral("SPEC");
    case Mode::DRM: return QStringLiteral("DRM");
    }
    return QStringLiteral("USB");
}

inline bool isLowerSideband(Mode m)
{
    return m == Mode::LSB || m == Mode::CWL || m == Mode::DIGL;
}

inline bool isCw(Mode m) { return m == Mode::CWL || m == Mode::CWU; }
inline bool isDigital(Mode m) { return m == Mode::DIGL || m == Mode::DIGU; }
inline bool isAm(Mode m) { return m == Mode::AM || m == Mode::SAM; }

inline quint32 frequencyToPhaseWord(double hz)
{
    return static_cast<quint32>(std::llround((4294967296.0 * hz) / kMasterClockHz));
}

inline QString formatFrequency(qint64 hz)
{
    const qint64 mhz = hz / 1'000'000;
    const qint64 khz = (hz / 1'000) % 1'000;
    const qint64 rest = hz % 1'000;
    return QStringLiteral("%1.%2.%3")
        .arg(mhz)
        .arg(khz, 3, 10, QChar('0'))
        .arg(rest, 3, 10, QChar('0'));
}

inline QString formatBandwidth(int hz)
{
    const int a = std::abs(hz);
    if (a >= 1000)
        return QStringLiteral("%1 kHz").arg(a / 1000.0, 0, 'f', 2);
    return QStringLiteral("%1 Hz").arg(a);
}

inline QString formatFilterRange(int low, int high)
{
    return QStringLiteral("%1 – %2 Hz").arg(low).arg(high);
}

struct FilterEdges {
    int low = -2700;
    int high = -150;
};

inline FilterEdges defaultFilter(Mode mode, FilterPreset p)
{
    const auto pair = [&](int a, int b) {
        FilterEdges e;
        if (isLowerSideband(mode)) {
            e.low = -b;
            e.high = -a;
        } else {
            e.low = a;
            e.high = b;
        }
        return e;
    };

    if (isCw(mode)) {
        switch (p) {
        case FilterPreset::F1: return pair(0, 1000);
        case FilterPreset::F2: return pair(0, 500);
        case FilterPreset::F3: return pair(0, 250);
        case FilterPreset::F4: return pair(0, 100);
        case FilterPreset::F5: return pair(0, 50);
        default: return pair(0, 500);
        }
    }
    if (isAm(mode) || mode == Mode::DSB || mode == Mode::FM) {
        const int hw = (p == FilterPreset::F1) ? 5000
                     : (p == FilterPreset::F2) ? 3000
                     : (p == FilterPreset::F3) ? 1600
                     : 8000;
        return {-hw, hw};
    }
    switch (p) {
    case FilterPreset::F1: return pair(50, 5500);
    case FilterPreset::F2: return pair(50, 3300);
    case FilterPreset::F3: return pair(100, 2900);
    case FilterPreset::F4: return pair(150, 2700);
    case FilterPreset::F5: return pair(150, 2400);
    case FilterPreset::F6: return pair(150, 2100);
    case FilterPreset::F7: return pair(150, 1800);
    case FilterPreset::F8: return pair(250, 1200);
    case FilterPreset::F9: return pair(250, 700);
    case FilterPreset::F10: return pair(250, 400);
    default: return pair(150, 2700);
    }
}

inline FilterEdges remapFilter(FilterEdges e, Mode mode)
{
    const int inner = std::min(std::abs(e.low), std::abs(e.high));
    const int outer = std::max(std::abs(e.low), std::abs(e.high));
    if (isAm(mode) || mode == Mode::DSB || mode == Mode::FM) {
        const int hw = std::max(outer, 800);
        return {-hw, hw};
    }
    FilterEdges out;
    if (isLowerSideband(mode)) {
        out.low = -outer;
        out.high = -inner;
    } else {
        out.low = inner;
        out.high = outer;
    }
    return out;
}

inline FilterEdges orderedFilter(int a, int b)
{
    FilterEdges e;
    e.low = std::min(a, b);
    e.high = std::max(a, b);
    return e;
}

inline void filterAudioCuts(FilterEdges e, int& inner, int& outer)
{
    inner = std::min(std::abs(e.low), std::abs(e.high));
    outer = std::max(std::abs(e.low), std::abs(e.high));
}

struct BandInfo {
    Band band;
    QString name;
    qint64 start;
    qint64 end;
    qint64 defaultHz;
    Mode defaultMode;
};

inline const std::array<BandInfo, 14>& bandTable()
{
    static const std::array<BandInfo, 14> t{{
        {Band::B160, QStringLiteral("160"), 1800000, 2000000, 1840000, Mode::LSB},
        {Band::B80, QStringLiteral("80"), 3500000, 4000000, 3600000, Mode::LSB},
        {Band::B60, QStringLiteral("60"), 5330000, 5405000, 5357000, Mode::USB},
        {Band::B40, QStringLiteral("40"), 7000000, 7300000, 7100000, Mode::LSB},
        {Band::B30, QStringLiteral("30"), 10100000, 10150000, 10120000, Mode::CWU},
        {Band::B20, QStringLiteral("20"), 14000000, 14350000, 14100000, Mode::USB},
        {Band::B17, QStringLiteral("17"), 18068000, 18168000, 18110000, Mode::USB},
        {Band::B15, QStringLiteral("15"), 21000000, 21450000, 21150000, Mode::USB},
        {Band::B12, QStringLiteral("12"), 24890000, 24990000, 24930000, Mode::USB},
        {Band::B10, QStringLiteral("10"), 28000000, 29700000, 28300000, Mode::USB},
        {Band::B6, QStringLiteral("6"), 50000000, 54000000, 50125000, Mode::USB},
        {Band::WWV, QStringLiteral("WWV"), 4990000, 5010000, 5000000, Mode::AM},
        {Band::GEN, QStringLiteral("GEN"), 100000, 61000000, 10000000, Mode::USB},
        {Band::VHF, QStringLiteral("VHF"), 50000000, 61000000, 50125000, Mode::USB},
    }};
    return t;
}

inline Band bandForFrequency(qint64 hz)
{
    for (const auto& b : bandTable()) {
        if (b.band == Band::GEN || b.band == Band::VHF || b.band == Band::WWV)
            continue;
        if (hz >= b.start && hz <= b.end)
            return b.band;
    }
    return Band::GEN;
}

inline qint64 snapTune(qint64 hz, qint64 step, int direction)
{
    if (step < 1)
        step = 1;
    const qint64 rem = ((hz % step) + step) % step;
    qint64 next = hz;
    if (direction > 0)
        next = (rem == 0) ? hz + step : hz + (step - rem);
    else if (direction < 0)
        next = (rem == 0) ? hz - step : hz - rem;
    return std::clamp(next, 100000LL, 61000000LL);
}

inline qint64 snapNearest(qint64 hz, qint64 step)
{
    if (step < 1)
        step = 1;
    const qint64 down = (hz / step) * step;
    const qint64 up = down + step;
    return std::clamp((hz - down < up - hz) ? down : up, 100000LL, 61000000LL);
}

inline quint32 alexFilterWord(qint64 rxHz, qint64 txHz, bool transmitting, bool pureSignal)
{
    quint32 filters = transmitting ? 0x08000000u : 0;
    if (transmitting && pureSignal)
        filters |= 0x00040000u;

    if (rxHz < 1'500'000)
        filters |= 0x1000;
    else if (rxHz < 6'500'000)
        filters |= 0x40;
    else if (rxHz < 9'500'000)
        filters |= 0x20;
    else if (rxHz < 13'000'000)
        filters |= 0x10;
    else if (rxHz < 20'000'000)
        filters |= 0x02;
    else if (rxHz < 50'000'000)
        filters |= 0x04;
    else
        filters |= 0x80;

    if (txHz > 35'600'000)
        filters |= 0x08;
    else if (txHz > 24'000'000)
        filters |= 0x04;
    else if (txHz > 16'500'000)
        filters |= 0x02;
    else if (txHz > 8'000'000)
        filters |= 0x10;
    else if (txHz > 5'000'000)
        filters |= 0x20;
    else
        filters |= 0x40;

    filters |= 0x01000000u; // TX ANT1
    return filters;
}

} // namespace brick2

Q_DECLARE_METATYPE(brick2::RadioInfo)
Q_DECLARE_METATYPE(brick2::Mode)
Q_DECLARE_METATYPE(brick2::Band)
