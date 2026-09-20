#include "dsp/DspEngine.h"

#include "core/RadioModel.h"

#include <QDir>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" {
#include "wdsp.h"
}

namespace brick2 {

namespace {

constexpr int kRxSAv = 1;
constexpr int kTxMicPk = 0;
constexpr int kTxAlcPk = 13;
constexpr int kTxAlcGain = 15;

void fftRadix2(std::vector<std::complex<float>>& a, bool inverse)
{
    const int n = int(a.size());
    int j = 0;
    for (int i = 1; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = (inverse ? kTwoPi : -kTwoPi) / float(len);
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.f, 0.f);
            for (int k = 0; k < len / 2; ++k) {
                auto u = a[i + k];
                auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse) {
        for (auto& x : a)
            x /= float(n);
    }
}

} // namespace

DspEngine::DspEngine(RadioModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
{
    m_rxIn.assign(size_t(kBlock) * 2, 0.0);
    m_rxOut.assign(size_t(kBlock) * 8, 0.0);
    m_txIn.assign(size_t(kBlock) * 2, 0.0);
    m_txOut.assign(size_t(kBlock) * 8, 0.0);
    m_fftIn.assign(kFft, {});
    m_fftOut.assign(kFft, {});
    m_window.resize(kFft);
    m_fftAvg.assign(kFft, -140.f);
    m_frame.db.fill(-140.f, kFft);
    for (int i = 0; i < kFft; ++i)
        m_window[size_t(i)] = float(0.5 - 0.5 * std::cos(kTwoPi * i / (kFft - 1)));

    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(data);
    const QByteArray path = QDir::toNativeSeparators(data + QLatin1Char('/')).toLocal8Bit();
    WDSPwisdom(const_cast<char*>(path.constData()));

    ensureChannels();
}

DspEngine::~DspEngine()
{
    destroyChannels();
}

int DspEngine::wdspMode(Mode m) const
{
    switch (m) {
    case Mode::LSB: return 0;
    case Mode::USB: return 1;
    case Mode::DSB: return 2;
    case Mode::CWL: return 3;
    case Mode::CWU: return 4;
    case Mode::FM: return 5;
    case Mode::AM: return 6;
    case Mode::DIGU: return 7;
    case Mode::SPEC: return 8;
    case Mode::DIGL: return 9;
    case Mode::SAM: return 10;
    case Mode::DRM: return 11;
    }
    return 1;
}

void DspEngine::ensureChannels()
{
    const int rate = std::max(48000, m_model->sampleRate);
    if (m_open && m_openRate == rate)
        return;
    destroyChannels();

    OpenChannel(kRxCh, kBlock, kBlock, rate, kAudioRate, kAudioRate, 0, 1, 0.010, 0.025, 0.000, 0.010, 1);
    OpenChannel(kTxCh, kBlock, kBlock, kAudioRate, kAudioRate, rate, 1, 0, 0.010, 0.025, 0.000, 0.010, 1);
    SetChannelState(kRxCh, 1, 0);

    create_anbEXT(0, 0, kBlock, double(rate), 0.0001, 0.0001, 0.0001, 0.0001, 0.05);
    create_nobEXT(0, 0, 0, kBlock, double(rate), 0.0001, 0.0001, 0.0001, 0.0001, 0.05);
    m_anb = true;
    m_nob = true;

    int ok = 0;
    XCreateAnalyzer(kDisp, &ok, 262144, 1, 1, nullptr);
    int flp[1] = {0};
    const int overlap = kFft - kBlock;
    const int maxW = kFft * 4;
    SetAnalyzer(kDisp, 1, 1, 1, flp, kFft, kBlock, 6, 0.0, overlap, 0, 0.0, 0.0, kPixels, 1, 0, 0.0, 0.0,
                maxW);
    SetDisplaySampleRate(kDisp, rate);
    SetDisplayDetectorMode(kDisp, 0, 0);
    SetDisplayAverageMode(kDisp, 0, 3);
    SetDisplayNumAverage(kDisp, 0, 4);

    m_openRate = rate;
    m_open = true;
    applyRxControls();
    applyTxControls();
}

void DspEngine::destroyChannels()
{
    if (!m_open)
        return;
    SetChannelState(kRxCh, 0, 1);
    SetChannelState(kTxCh, 0, 1);
    CloseChannel(kRxCh);
    CloseChannel(kTxCh);
    if (m_anb)
        destroy_anbEXT(0);
    if (m_nob)
        destroy_nobEXT(0);
    DestroyAnalyzer(kDisp);
    m_anb = m_nob = m_open = false;
}

void DspEngine::applyRxControls()
{
    if (!m_open)
        return;
    const auto e = remapFilter(m_model->vfoA.edges, m_model->vfoA.mode);
    SetRXAMode(kRxCh, wdspMode(m_model->vfoA.mode));
    RXASetPassband(kRxCh, e.low, e.high);
    SetRXAAGCMode(kRxCh, int(m_model->rx1.agc));
    SetRXAAGCHang(kRxCh, m_model->rx1.agcHang);
    SetRXAAGCTop(kRxCh, 60.0 + m_model->rx1.agcGain * 0.6);
    SetRXAANRRun(kRxCh, m_model->rx1.nr ? 1 : 0);
    SetRXAEMNRRun(kRxCh, m_model->rx1.nr2 ? 1 : 0);
    SetRXAANFRun(kRxCh, m_model->rx1.anf ? 1 : 0);
    SetRXASNBARun(kRxCh, m_model->rx1.snb ? 1 : 0);
    SetEXTANBRun(0, m_model->rx1.nb ? 1 : 0);
    SetEXTNOBRun(0, m_model->rx1.nb2 ? 1 : 0);
    SetRXAPanelSelect(kRxCh, 2);
    SetRXAPanelBinaural(kRxCh, m_model->rx1.binaural ? 1 : 0);
    SetRXAPanelCopy(kRxCh, m_model->rx1.binaural ? 0 : 1);
    const double af = m_model->rx1.mute ? 0.0 : std::max(0.05, m_model->rx1.afGain / 50.0);
    SetRXAPanelGain1(kRxCh, af);
    SetRXAAMSQRun(kRxCh, m_model->rx1.squelch ? 1 : 0);
    SetRXAAMSQThreshold(kRxCh, double(m_model->rx1.squelchThresh));
}

void DspEngine::applyTxControls()
{
    if (!m_open)
        return;
    const Mode mode = m_model->vfoA.mode;
    SetTXAMode(kTxCh, wdspMode(mode));
    const double lo = m_model->tx.txFilterLow;
    const double hi = m_model->tx.txFilterHigh;
    if (isLowerSideband(mode))
        SetTXABandpassFreqs(kTxCh, -hi, -lo);
    else if (isAm(mode) || mode == Mode::DSB || mode == Mode::FM)
        SetTXABandpassFreqs(kTxCh, -hi, hi);
    else
        SetTXABandpassFreqs(kTxCh, lo, hi);
    SetTXAALCSt(kTxCh, 1);
    SetTXACompressorRun(kTxCh, m_model->tx.comp ? 1 : 0);
    SetTXALevelerSt(kTxCh, m_model->tx.lev ? 1 : 0);
    SetTXACFCOMPRun(kTxCh, m_model->tx.cfc ? 1 : 0);
    SetTXAPanelGain1(kTxCh, m_model->tx.micGain / 70.0);

    if (m_model->ps.twoTone) {
        SetTXAPreGenMode(kTxCh, 1);
        SetTXAPreGenRun(kTxCh, 1);
    } else if (m_model->tune) {
        SetTXAPreGenMode(kTxCh, 0);
        SetTXAPreGenToneFreq(kTxCh, 1000.0);
        SetTXAPreGenToneMag(kTxCh, 0.5);
        SetTXAPreGenRun(kTxCh, 1);
    } else {
        SetTXAPreGenRun(kTxCh, 0);
    }

    const int on = m_model->isTransmitting() ? 1 : 0;
    SetChannelState(kTxCh, on, 0);
    if (!m_model->dup)
        SetChannelState(kRxCh, on ? 0 : 1, 0);
    else
        SetChannelState(kRxCh, 1, 0);
}

void DspEngine::processIq(int ddc, const QVector<float>& interleavedIq)
{
    if (ddc != 0 || interleavedIq.size() < 2)
        return;
    std::lock_guard<std::mutex> lock(m_lock);
    ensureChannels();
    applyRxControls();

    const float rf = std::pow(10.f, m_model->rx1.rfGain / 20.f);
    if (m_lastTuneHz != m_model->vfoA.frequency) {
        m_lastTuneHz = m_model->vfoA.frequency;
        std::fill(m_fftAvg.begin(), m_fftAvg.end(), -140.f);
        m_fftFill = 0;
    }
    for (int i = 0; i + 1 < interleavedIq.size(); i += 2) {
        double i0 = double(interleavedIq[i]) * double(rf);
        double q0 = double(interleavedIq[i + 1]) * double(rf);
        m_dcI = m_dcI * 0.999 + i0 * 0.001;
        m_dcQ = m_dcQ * 0.999 + q0 * 0.001;
        i0 -= m_dcI;
        q0 -= m_dcQ;
        runSpectrum(float(i0), float(q0));
        const int p = m_rxFill;
        m_rxIn[size_t(p) * 2] = i0;
        m_rxIn[size_t(p) * 2 + 1] = q0;
        ++m_rxFill;
        if (m_rxFill >= kBlock)
            pushRxBlock();
    }

    m_frame.sMeter = float(GetRXAMeter(kRxCh, kRxSAv));
    m_model->meters.sMeter = m_frame.sMeter;
    if (!m_model->isTransmitting())
        m_model->meters.alc = 0;
    emit frameReady(m_frame);
    emit audioReady();
}

void DspEngine::pushRxBlock()
{
    if (m_anb)
        xanbEXT(0, m_rxIn.data(), m_rxIn.data());
    if (m_nob)
        xnobEXT(0, m_rxIn.data(), m_rxIn.data());
    Spectrum0(1, kDisp, 0, 0, m_rxIn.data());

    int err = 0;
    fexchange0(kRxCh, m_rxIn.data(), m_rxOut.data(), &err);

    const int outN = kBlock * kAudioRate / std::max(1, m_openRate);
    m_frame.audio.resize(outN);
    m_frame.scope.resize(std::min(512, outN));
    const bool mute = m_model->rx1.mute || (m_model->isTransmitting() && !m_model->dup);
    for (int i = 0; i < outN; ++i) {
        float y = mute ? 0.f : float(m_rxOut[size_t(i) * 2]);
        float r = mute ? 0.f : float(m_rxOut[size_t(i) * 2 + 1]);
        y = std::clamp(y, -1.f, 1.f);
        r = std::clamp(r, -1.f, 1.f);
        m_frame.audio[i] = y;
        m_spkQueue.push_back(qint16(y * 32000.f));
        m_spkQueue.push_back(qint16(r * 32000.f));
        if (i < m_frame.scope.size())
            m_frame.scope[i] = y;
    }
    m_rxFill = 0;
    std::fill(m_rxIn.begin(), m_rxIn.begin() + kBlock * 2, 0.0);
}

void DspEngine::runSpectrum(float i, float q)
{
    m_fftIn[size_t(m_fftFill)] = std::complex<float>(i, -q) * m_window[size_t(m_fftFill)];
    ++m_fftFill;
    if (m_fftFill < kFft)
        return;
    m_fftOut = m_fftIn;
    fftRadix2(m_fftOut, false);
    m_frame.db.resize(kFft);
    for (int n = 0; n < kFft; ++n) {
        const auto z = m_fftOut[size_t((n + kFft / 2) % kFft)];
        const float p = 20.f * std::log10(std::abs(z) / float(kFft) + 1e-12f);
        m_fftAvg[size_t(n)] = m_fftAvg[size_t(n)] * 0.72f + p * 0.28f;
        m_frame.db[n] = m_fftAvg[size_t(n)];
    }
    m_fftFill = 0;
}

void DspEngine::processMic(const QVector<float>& mic)
{
    const bool wasOpen = m_model->voxOpen;
    float peak = 0;
    for (float x : mic) {
        x *= m_model->tx.micGain / 70.f;
        m_voxEnv = m_voxEnv * 0.995f + std::abs(x) * 0.005f;
        peak = std::max(peak, std::abs(x));
        m_micFifo.push_back(x);
        if (m_micFifo.size() > 16384)
            m_micFifo.pop_front();
    }
    m_frame.mic = peak > 1e-6f ? 20.f * std::log10(peak) : -40.f;

    if (m_model->tx.vox) {
        if (m_voxEnv > m_model->tx.voxThresh / 200.f)
            m_hangSamples = std::max(1, m_model->tx.voxHang) * 48;
        if (m_hangSamples > 0) {
            m_hangSamples = std::max(0, m_hangSamples - int(mic.size()));
            m_model->voxOpen = true;
        } else {
            m_model->voxOpen = false;
        }
    } else {
        m_model->voxOpen = false;
        m_hangSamples = 0;
    }
    if (wasOpen != m_model->voxOpen)
        emit m_model->transmitChanged();
}

void DspEngine::pushTxBlock()
{
    for (int i = 0; i < kBlock; ++i) {
        float a = 0.f;
        if (!m_micFifo.empty() && !m_model->tune && !m_model->ps.twoTone) {
            a = m_micFifo.front();
            m_micFifo.pop_front();
        }
        m_txIn[size_t(i) * 2] = double(a);
        m_txIn[size_t(i) * 2 + 1] = 0.0;
    }
    int err = 0;
    fexchange0(kTxCh, m_txIn.data(), m_txOut.data(), &err);
    const int outN = kBlock * std::max(1, m_openRate) / kAudioRate;
    for (int i = 0; i < outN; ++i) {
        m_txQueue.push_back({float(m_txOut[size_t(i) * 2]), float(m_txOut[size_t(i) * 2 + 1])});
    }
    m_model->meters.alc = float(GetTXAMeter(kTxCh, kTxAlcGain));
    m_frame.alc = float(GetTXAMeter(kTxCh, kTxAlcPk));
    m_frame.mic = float(GetTXAMeter(kTxCh, kTxMicPk));
}

void DspEngine::generateTx(int samples192k)
{
    if (!m_model->isTransmitting() || samples192k <= 0)
        return;
    std::lock_guard<std::mutex> lock(m_lock);
    ensureChannels();
    applyTxControls();
    const int outN = kBlock * std::max(1, m_openRate) / kAudioRate;
    int produced = 0;
    while (produced < samples192k) {
        pushTxBlock();
        produced += outN;
    }
    emit txIqReady();
}

QVector<std::complex<float>> DspEngine::takeTxIq()
{
    QVector<std::complex<float>> out(m_txQueue.begin(), m_txQueue.end());
    m_txQueue.clear();
    return out;
}

QVector<qint16> DspEngine::takeSpeaker()
{
    QVector<qint16> out(m_spkQueue.begin(), m_spkQueue.end());
    m_spkQueue.clear();
    return out;
}

} // namespace brick2
