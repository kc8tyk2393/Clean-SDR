#include "core/RadioModel.h"

#include <QSettings>

namespace brick2 {

RadioModel::RadioModel(QObject* parent)
    : QObject(parent)
{
    vfoA.edges = defaultFilter(vfoA.mode, vfoA.filter);
    vfoB.edges = defaultFilter(vfoB.mode, vfoB.filter);
    vfoB.frequency = 14200000;
    for (int i = 0; i < static_cast<int>(bandTable().size()); ++i)
        bandStack[static_cast<size_t>(i)] = bandTable()[static_cast<size_t>(i)].defaultHz;
}

qint64 RadioModel::txFrequency() const
{
    qint64 f = split ? vfoB.frequency : vfoA.frequency;
    if (xitOn)
        f += xit;
    if (isCw(vfoA.mode)) {
        if (vfoA.mode == Mode::CWU)
            f += cw.sidetone;
        else
            f -= cw.sidetone;
    }
    return f;
}

FilterEdges RadioModel::currentFilter() const
{
    return vfoA.edges;
}

bool RadioModel::isTransmitting() const
{
    return mox || tune || voxOpen || ps.twoTone;
}

QString RadioModel::transmitLabel() const
{
    if (ps.twoTone)
        return QStringLiteral("TWO TONE");
    if (tune)
        return QStringLiteral("TUNE");
    if (mox)
        return QStringLiteral("MOX");
    if (voxOpen)
        return QStringLiteral("VOX");
    return QStringLiteral("RX");
}

void RadioModel::applyFilterPreset(FilterPreset p)
{
    vfoA.filter = p;
    if (p != FilterPreset::Var1 && p != FilterPreset::Var2)
        vfoA.edges = defaultFilter(vfoA.mode, p);
    if (filterSync)
        syncTxFromRx();
    emit modeChanged();
    emit stateChanged();
}

void RadioModel::applyRxFilter(int low, int high, bool asVar)
{
    vfoA.edges = orderedFilter(low, high);
    if (asVar)
        vfoA.filter = FilterPreset::Var1;
    if (filterSync)
        syncTxFromRx();
    emit modeChanged();
    emit stateChanged();
}

void RadioModel::applyTxFilter(int low, int high)
{
    const auto e = orderedFilter(low, high);
    tx.txFilterLow = e.low;
    tx.txFilterHigh = e.high;
    if (filterSync)
        syncRxFromTx();
    emit modeChanged();
    emit stateChanged();
}

void RadioModel::setFilterSync(bool on)
{
    filterSync = on;
    if (on)
        syncTxFromRx();
    emit modeChanged();
    emit stateChanged();
}

void RadioModel::syncTxFromRx()
{
    int inner = 0;
    int outer = 0;
    filterAudioCuts(vfoA.edges, inner, outer);
    if (isAm(vfoA.mode) || vfoA.mode == Mode::DSB || vfoA.mode == Mode::FM) {
        tx.txFilterLow = 0;
        tx.txFilterHigh = std::max(outer, 800);
        return;
    }
    tx.txFilterLow = inner;
    tx.txFilterHigh = std::max(outer, inner + 50);
}

void RadioModel::syncRxFromTx()
{
    vfoA.edges = remapFilter(FilterEdges{tx.txFilterLow, tx.txFilterHigh}, vfoA.mode);
    vfoA.filter = FilterPreset::Var1;
}

void RadioModel::setBand(Band band)
{
    const int idx = static_cast<int>(vfoA.band);
    if (idx >= 0 && idx < static_cast<int>(bandStack.size()))
        bandStack[static_cast<size_t>(idx)] = vfoA.frequency;

    vfoA.band = band;
    for (const auto& b : bandTable()) {
        if (b.band == band) {
            const int nidx = static_cast<int>(band);
            vfoA.frequency = bandStack[static_cast<size_t>(nidx)];
            vfoA.mode = b.defaultMode;
            if (vfoA.filter == FilterPreset::Var1 || vfoA.filter == FilterPreset::Var2)
                vfoA.edges = remapFilter(vfoA.edges, vfoA.mode);
            else
                vfoA.edges = defaultFilter(vfoA.mode, vfoA.filter);
            if (filterSync)
                syncTxFromRx();
            break;
        }
    }
    emit frequencyChanged();
    emit modeChanged();
    emit stateChanged();
}

void RadioModel::loadSettings()
{
    QSettings s(QStringLiteral("Brick2SDR"), QStringLiteral("Console"));
    vfoA.frequency = s.value(QStringLiteral("vfoA"), vfoA.frequency).toLongLong();
    vfoB.frequency = s.value(QStringLiteral("vfoB"), vfoB.frequency).toLongLong();
    vfoA.mode = static_cast<Mode>(s.value(QStringLiteral("mode"), static_cast<int>(vfoA.mode)).toInt());
    vfoA.filter = static_cast<FilterPreset>(s.value(QStringLiteral("filter"), static_cast<int>(vfoA.filter)).toInt());
    vfoA.edges.low = s.value(QStringLiteral("rxLow"), vfoA.edges.low).toInt();
    vfoA.edges.high = s.value(QStringLiteral("rxHigh"), vfoA.edges.high).toInt();
    tx.txFilterLow = s.value(QStringLiteral("txLow"), tx.txFilterLow).toInt();
    tx.txFilterHigh = s.value(QStringLiteral("txHigh"), tx.txFilterHigh).toInt();
    filterSync = s.value(QStringLiteral("filterSync"), filterSync).toBool();
    sampleRate = s.value(QStringLiteral("sampleRate"), sampleRate).toInt();
    rx1.afGain = s.value(QStringLiteral("af"), rx1.afGain).toInt();
    tx.drive = s.value(QStringLiteral("drive"), tx.drive).toInt();
    tx.micGain = s.value(QStringLiteral("mic"), tx.micGain).toInt();
    vac.vac1 = s.value(QStringLiteral("vac1"), vac.vac1).toBool();
    vac.vac1In = s.value(QStringLiteral("vac1In"), vac.vac1In).toString();
    vac.vac1Out = s.value(QStringLiteral("vac1Out"), vac.vac1Out).toString();
    vac.vac1Stereo = s.value(QStringLiteral("vac1Stereo"), vac.vac1Stereo).toBool();
    vac.vac1GainRx = s.value(QStringLiteral("vac1GainRx"), vac.vac1GainRx).toInt();
    vac.vac1GainTx = s.value(QStringLiteral("vac1GainTx"), vac.vac1GainTx).toInt();
    vac.vac1Latency = s.value(QStringLiteral("vac1Latency"), vac.vac1Latency).toInt();
    cat.enabled = s.value(QStringLiteral("cat"), false).toBool();
    cat.port = static_cast<quint16>(s.value(QStringLiteral("catPort"), 4532).toUInt());
    display.mode = static_cast<DisplayMode>(
        s.value(QStringLiteral("display"), static_cast<int>(display.mode)).toInt());
    display.palette = s.value(QStringLiteral("palette"), display.palette).toString();
    display.panLine = s.value(QStringLiteral("panLine"), display.panLine).value<QColor>();
    display.panFill = s.value(QStringLiteral("panFill"), display.panFill).value<QColor>();
    display.panFillAlpha = s.value(QStringLiteral("panFillAlpha"), display.panFillAlpha).toInt();
    display.panBrightness = s.value(QStringLiteral("panBright"), display.panBrightness).toInt();
    display.panGradient = s.value(QStringLiteral("panGrad"), display.panGradient).toInt();
    display.panGradientMid = s.value(QStringLiteral("panGradMid"), display.panGradientMid).toInt();
    display.wfBrightness = s.value(QStringLiteral("wfBright"), display.wfBrightness).toInt();
    display.fps = s.value(QStringLiteral("panFps"), display.fps).toInt();
    display.waterfallFps = s.value(QStringLiteral("wfFps"), display.waterfallFps).toInt();
    display.spectrumMin = s.value(QStringLiteral("specMin"), display.spectrumMin).toInt();
    display.spectrumMax = s.value(QStringLiteral("specMax"), display.spectrumMax).toInt();
    display.waterfallMin = s.value(QStringLiteral("wfMin"), display.waterfallMin).toInt();
    display.waterfallMax = s.value(QStringLiteral("wfMax"), display.waterfallMax).toInt();
    if (vfoA.filter != FilterPreset::Var1 && vfoA.filter != FilterPreset::Var2
        && !s.contains(QStringLiteral("rxLow")))
        vfoA.edges = defaultFilter(vfoA.mode, vfoA.filter);
}

void RadioModel::saveSettings() const
{
    QSettings s(QStringLiteral("Brick2SDR"), QStringLiteral("Console"));
    s.setValue(QStringLiteral("vfoA"), vfoA.frequency);
    s.setValue(QStringLiteral("vfoB"), vfoB.frequency);
    s.setValue(QStringLiteral("mode"), static_cast<int>(vfoA.mode));
    s.setValue(QStringLiteral("filter"), static_cast<int>(vfoA.filter));
    s.setValue(QStringLiteral("rxLow"), vfoA.edges.low);
    s.setValue(QStringLiteral("rxHigh"), vfoA.edges.high);
    s.setValue(QStringLiteral("txLow"), tx.txFilterLow);
    s.setValue(QStringLiteral("txHigh"), tx.txFilterHigh);
    s.setValue(QStringLiteral("filterSync"), filterSync);
    s.setValue(QStringLiteral("sampleRate"), sampleRate);
    s.setValue(QStringLiteral("af"), rx1.afGain);
    s.setValue(QStringLiteral("drive"), tx.drive);
    s.setValue(QStringLiteral("mic"), tx.micGain);
    s.setValue(QStringLiteral("vac1"), vac.vac1);
    s.setValue(QStringLiteral("vac1In"), vac.vac1In);
    s.setValue(QStringLiteral("vac1Out"), vac.vac1Out);
    s.setValue(QStringLiteral("vac1Stereo"), vac.vac1Stereo);
    s.setValue(QStringLiteral("vac1GainRx"), vac.vac1GainRx);
    s.setValue(QStringLiteral("vac1GainTx"), vac.vac1GainTx);
    s.setValue(QStringLiteral("vac1Latency"), vac.vac1Latency);
    s.setValue(QStringLiteral("cat"), cat.enabled);
    s.setValue(QStringLiteral("catPort"), cat.port);
    s.setValue(QStringLiteral("display"), static_cast<int>(display.mode));
    s.setValue(QStringLiteral("palette"), display.palette);
    s.setValue(QStringLiteral("panLine"), display.panLine);
    s.setValue(QStringLiteral("panFill"), display.panFill);
    s.setValue(QStringLiteral("panFillAlpha"), display.panFillAlpha);
    s.setValue(QStringLiteral("panBright"), display.panBrightness);
    s.setValue(QStringLiteral("panGrad"), display.panGradient);
    s.setValue(QStringLiteral("panGradMid"), display.panGradientMid);
    s.setValue(QStringLiteral("wfBright"), display.wfBrightness);
    s.setValue(QStringLiteral("panFps"), display.fps);
    s.setValue(QStringLiteral("wfFps"), display.waterfallFps);
    s.setValue(QStringLiteral("specMin"), display.spectrumMin);
    s.setValue(QStringLiteral("specMax"), display.spectrumMax);
    s.setValue(QStringLiteral("wfMin"), display.waterfallMin);
    s.setValue(QStringLiteral("wfMax"), display.waterfallMax);
}

} // namespace brick2
