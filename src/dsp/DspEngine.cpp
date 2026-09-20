#include "dsp/DspEngine.h"

#include "core/RadioModel.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace brick2 {
namespace {

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

std::vector<std::complex<float>> designBandpass(int taps, int rate, int low, int high)
{
    taps |= 1;
    std::vector<std::complex<float>> h(taps);
    const double nyq = rate / 2.0;
    const double f1 = std::clamp(low / nyq, -0.99, 0.99);
    const double f2 = std::clamp(high / nyq, -0.99, 0.99);
    const int m = taps / 2;
    for (int n = 0; n < taps; ++n) {
        const int k = n - m;
        const double win = 0.54 - 0.46 * std::cos(kTwoPi * n / (taps - 1));
        double re = 0;
        double im = 0;
        if (k == 0) {
            re = f2 - f1;
        } else {
            re = (std::sin(kPi * f2 * k) - std::sin(kPi * f1 * k)) / (kPi * k);
            im = (std::cos(kPi * f1 * k) - std::cos(kPi * f2 * k)) / (kPi * k);
        }
        h[n] = {float(re * win), float(im * win)};
    }
    return h;
}

std::vector<float> designRealLowpass(int taps, int rate, int cutoffHz)
{
    taps |= 1;
    std::vector<float> h(taps);
    const double nyq = rate / 2.0;
    const double f = std::clamp(cutoffHz / nyq, 0.002, 0.95);
    const int m = taps / 2;
    for (int n = 0; n < taps; ++n) {
        const int k = n - m;
        const double win = 0.54 - 0.46 * std::cos(kTwoPi * n / (taps - 1));
        const double re = (k == 0) ? f : std::sin(kPi * f * k) / (kPi * k);
        h[n] = float(re * win);
    }
    return h;
}

} // namespace

DspEngine::DspEngine(RadioModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
{
    m_fftIn.assign(kFftSize, {});
    m_fftOut.assign(kFftSize, {});
    m_window.resize(kFftSize);
    m_nrNoise.assign(kFftSize / 2 + 1, 1e-6f);
    m_waterfallAcc.assign(kFftSize, -140.f);
    m_eqState.assign(40, 0.f);
    for (int i = 0; i < kFftSize; ++i)
        m_window[i] = float(0.5 - 0.5 * std::cos(kTwoPi * i / (kFftSize - 1)));
    rebuildFilters();
    buildTxFilters();
}

void DspEngine::buildTxFilters()
{
    const int lo = std::max(80, m_model->tx.txFilterLow);
    const int hi = std::clamp(m_model->tx.txFilterHigh, lo + 200, 5000);
    const Mode mode = m_model->vfoA.mode;
    int f1 = lo;
    int f2 = hi;
    if (isLowerSideband(mode)) {
        f1 = -hi;
        f2 = -lo;
    } else if (isAm(mode) || mode == Mode::DSB || mode == Mode::FM) {
        f1 = -hi;
        f2 = hi;
    }
    m_txFir = designBandpass(97, kAudioRate, f1, f2);
    m_txDelay.assign(m_txFir.size(), {});
    m_txPos = 0;
    m_lastTxLow = lo;
    m_lastTxHigh = hi;
    m_lastTxMode = mode;
}

void DspEngine::rebuildFilters()
{
    const auto e = m_model->vfoA.edges;
    const Mode mode = m_model->vfoA.mode;
    int cut = std::max(200, std::abs(e.high - e.low) / 2);
    m_shiftHz = 0.5 * (e.low + e.high);
    if (isAm(mode) || mode == Mode::DSB || mode == Mode::FM) {
        m_shiftHz = 0;
        cut = std::max(std::abs(e.low), std::abs(e.high));
    }
    m_lpf = designRealLowpass(97, m_model->sampleRate, cut);
    m_delay.assign(m_lpf.size(), {});
    m_firPos = 0;
    m_lastRate = m_model->sampleRate;
    m_lastLow = e.low;
    m_lastHigh = e.high;
    m_lastMode = mode;
}

void DspEngine::processIq(int ddc, const QVector<float>& interleavedIq)
{
    if (ddc != 0 || interleavedIq.size() < 2)
        return;
    const auto e = m_model->vfoA.edges;
    if (m_lastRate != m_model->sampleRate || m_lastLow != e.low || m_lastHigh != e.high
        || m_lastMode != m_model->vfoA.mode)
        rebuildFilters();
    if (m_lastTuneHz != m_model->vfoA.frequency) {
        m_lastTuneHz = m_model->vfoA.frequency;
        std::fill(m_waterfallAcc.begin(), m_waterfallAcc.end(), -140.f);
        m_fftFill = 0;
    }

    std::vector<std::complex<float>> buf;
    buf.reserve(interleavedIq.size() / 2);
    for (int i = 0; i + 1 < interleavedIq.size(); i += 2)
        buf.push_back({interleavedIq[i], interleavedIq[i + 1]});
    const float rf = std::pow(10.f, m_model->rx1.rfGain / 20.f);
    for (auto& s : buf) {
        m_dcI = m_dcI * 0.999f + s.real() * 0.001f;
        m_dcQ = m_dcQ * 0.999f + s.imag() * 0.001f;
        s -= std::complex<float>(m_dcI, m_dcQ);
        s *= rf;
        if (m_model->rx1.nb || m_model->rx1.nb2 || m_model->rx1.snb) {
            float oi = s.real(), oq = s.imag();
            noiseBlanker(std::abs(s), s.real(), s.imag(), oi, oq);
            s = {oi, oq};
        }
    }

    runSpectrum(buf);

    std::vector<float> audio;
    demodBlock(buf, audio);

    if (m_model->rx1.nr || m_model->rx1.nr2)
        spectralNr(audio);

    if (m_model->rx1.anf) {
        for (auto& x : audio) {
            float yhat = 0.f;
            for (int t = 0; t < 8; ++t)
                yhat += m_anfW[t] * m_anfX[t];
            const float e = x - yhat;
            for (int t = 7; t > 0; --t)
                m_anfX[t] = m_anfX[t - 1];
            m_anfX[0] = x;
            const float mu = 0.04f;
            for (int t = 0; t < 8; ++t)
                m_anfW[t] += mu * e * m_anfX[t];
            x = e;
        }
    }

    const float af = m_model->rx1.afGain / 100.f;
    m_frame.audio.resize(int(audio.size()));
    m_frame.scope.resize(std::min(512, int(audio.size())));
    for (int i = 0; i < int(audio.size()); ++i) {
        float y = audio[i] * af;
        if (m_model->rx1.squelch && m_frame.sMeter < float(m_model->rx1.squelchThresh))
            y = 0;
        if (m_model->rx1.mute || (m_model->isTransmitting() && !m_model->dup))
            y = 0;
        y = std::clamp(y, -1.f, 1.f);
        m_frame.audio[i] = y;
        float r = y;
        if (m_model->rx1.binaural) {
            m_binDly.push_back(y);
            if (m_binDly.size() > 16) {
                r = m_binDly.front();
                m_binDly.pop_front();
            }
        }
        m_spkQueue.push_back(qint16(y * 32000.f));
        m_spkQueue.push_back(qint16(r * 32000.f));
        if (i < m_frame.scope.size())
            m_frame.scope[i] = y;
    }

    emit frameReady(m_frame);
    emit audioReady();
}

void DspEngine::processMic(const QVector<float>& mic)
{
    const bool wasOpen = m_model->voxOpen;
    float peak = 0;
    for (float x : mic) {
        const float dcIn = x;
        m_dcY = dcIn - m_dcX + 0.995f * m_dcY;
        m_dcX = dcIn;
        x = m_dcY;
        x *= m_model->tx.micGain / 70.f;
        m_voxEnv = m_voxEnv * 0.995f + std::abs(x) * 0.005f;
        if (m_model->tx.comp) {
            m_compEnv = std::max(std::abs(x), m_compEnv * 0.999f);
            const float t = std::pow(10.f, -m_model->tx.compLevel / 20.f);
            if (m_compEnv > t)
                x *= t / m_compEnv;
        }
        peak = std::max(peak, std::abs(x));
        m_micFifo.push_back(x);
        if (m_micFifo.size() > 8192)
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

float DspEngine::popMic()
{
    if (m_micFifo.empty())
        return 0.f;
    const float x = m_micFifo.front();
    m_micFifo.pop_front();
    return x;
}

std::complex<float> DspEngine::txSsb48(float audio)
{
    if (m_txFir.empty())
        return {audio, 0.f};
    const int n = int(m_txFir.size());
    m_txPos = (m_txPos + 1) % n;
    m_txDelay[size_t(m_txPos)] = {audio, 0.f};
    std::complex<float> acc{0.f, 0.f};
    int j = m_txPos;
    for (int t = 0; t < n; ++t) {
        acc += m_txFir[size_t(t)] * m_txDelay[size_t(j)];
        if (--j < 0)
            j = n - 1;
    }
    return acc * 2.2f;
}

void DspEngine::generateTx(int samples192k)
{
    if (!m_model->isTransmitting() || samples192k <= 0)
        return;
    const Mode mode = m_model->vfoA.mode;
    const int lo = std::max(80, m_model->tx.txFilterLow);
    const int hi = std::clamp(m_model->tx.txFilterHigh, lo + 200, 5000);
    if (m_txFir.empty() || lo != m_lastTxLow || hi != m_lastTxHigh || mode != m_lastTxMode)
        buildTxFilters();

    const double dt48 = kTwoPi / double(kAudioRate);

    for (int n = 0; n < samples192k; ++n) {
        if ((m_interp % 4) == 0) {
            float audio = 0.f;
            if (m_model->ps.twoTone) {
                audio = 0.42f * float(std::sin(m_tone1) + std::sin(m_tone2));
                m_tone1 += dt48 * 700.0;
                m_tone2 += dt48 * 1900.0;
            } else if (m_model->tune) {
                audio = 0.72f * float(std::sin(m_nco));
                m_nco += dt48 * 1000.0;
            } else {
                audio = popMic();
            }
            audio = std::tanh(audio);
            m_ssbPrev = m_ssbCurr;
            if (mode == Mode::AM) {
                const float a = (1.f + 0.85f * audio) * 0.55f;
                m_ssbCurr = {a, 0.f};
            } else if (mode == Mode::FM) {
                m_nco += double(audio) * 0.55;
                m_ssbCurr = {float(std::cos(m_nco)), float(std::sin(m_nco))};
            } else {
                m_ssbCurr = txSsb48(audio);
            }
        }
        const float t = float(m_interp % 4) / 4.f;
        const float w = t * t * (3.f - 2.f * t);
        std::complex<float> s = m_ssbPrev * (1.f - w) + m_ssbCurr * w;
        const float mag = std::abs(s);
        if (mag > 0.94f)
            m_alcGain *= 0.94f / mag;
        else
            m_alcGain += (1.f - m_alcGain) * 0.0005f;
        m_alcGain = std::clamp(m_alcGain, 0.2f, 1.f);
        s *= m_alcGain;
        m_model->meters.alc = 20.f * std::log10(std::max(0.05f, m_alcGain));
        m_txQueue.push_back(s);
        ++m_interp;
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

void DspEngine::runSpectrum(const std::vector<std::complex<float>>& iq)
{
    for (auto s : iq) {
        if (m_fftFill < kFftSize) {
            m_fftIn[m_fftFill] = s * m_window[m_fftFill];
            ++m_fftFill;
        }
        if (m_fftFill >= kFftSize) {
            m_fftOut = m_fftIn;
            fftRadix2(m_fftOut, false);
            m_frame.db.resize(kFftSize);
            const float avg = std::max(1, m_model->display.avg);
            float peak = 1e-12f;
            for (int i = 0; i < kFftSize; ++i) {
                const int src = (i + kFftSize / 2) % kFftSize;
                const float mag = std::norm(m_fftOut[src]) + 1e-20f;
                const float db = 10.f * std::log10(mag) - 90.f;
                m_waterfallAcc[i] += (db - m_waterfallAcc[i]) / avg;
                m_frame.db[i] = m_waterfallAcc[i];
                peak = std::max(peak, mag);
            }
            m_frame.sMeter = 10.f * std::log10(peak) - 80.f;
            m_model->meters.sMeter = m_frame.sMeter;
            m_fftFill = kFftSize / 4;
            std::rotate(m_fftIn.begin(), m_fftIn.begin() + 3 * kFftSize / 4, m_fftIn.end());
        }
    }
}

void DspEngine::demodBlock(const std::vector<std::complex<float>>& iq, std::vector<float>& audio)
{
    const int decim = std::max(1, m_model->sampleRate / kAudioRate);
    const int n = int(m_lpf.size());
    if (n <= 0)
        return;
    audio.reserve(iq.size() / decim + 1);
    const double dphi = kTwoPi * m_shiftHz / double(std::max(1, m_model->sampleRate));
    int idx = 0;
    for (auto s : iq) {
        s *= std::exp(std::complex<float>(0.f, float(-m_bfo)));
        m_bfo += dphi;
        if (m_bfo > kTwoPi)
            m_bfo -= kTwoPi;
        else if (m_bfo < -kTwoPi)
            m_bfo += kTwoPi;

        m_firPos = (m_firPos + 1) % n;
        m_delay[size_t(m_firPos)] = s;
        if ((++idx % decim) != 0)
            continue;

        std::complex<float> acc{0.f, 0.f};
        int j = m_firPos;
        for (int t = 0; t < n; ++t) {
            acc += m_lpf[size_t(t)] * m_delay[size_t(j)];
            if (--j < 0)
                j = n - 1;
        }

        float y = 0;
        const Mode m = m_model->vfoA.mode;
        if (m == Mode::AM)
            y = std::abs(acc);
        else if (m == Mode::SAM) {
            const auto err = acc * std::exp(std::complex<float>(0.f, float(-m_pllPhase)));
            m_pllFreq += 0.001 * err.imag();
            m_pllPhase += m_pllFreq + 0.05 * err.imag();
            y = err.real();
        } else if (m == Mode::FM) {
            const float ph = std::arg(acc);
            float d = ph - float(m_fmPhase);
            while (d > float(kPi))
                d -= float(kTwoPi);
            while (d < -float(kPi))
                d += float(kTwoPi);
            m_fmPhase = ph;
            y = d * 4.f;
        } else {
            y = acc.real() * 2.f;
        }

        audio.push_back(agc(y));
    }
}

float DspEngine::agc(float x)
{
    if (m_model->rx1.agc == AgcMode::Off)
        return x * (m_model->rx1.agcGain / 50.f);
    const float mag = std::abs(x) + 1e-9f;
    const float target = 0.2f;
    const float decay = (m_model->rx1.agc == AgcMode::Fast)     ? 0.02f
                        : (m_model->rx1.agc == AgcMode::Med)    ? 0.005f
                        : (m_model->rx1.agc == AgcMode::Slow)   ? 0.001f
                                                                : 0.0003f;
    if (mag * m_agcGain > target) {
        m_agcGain *= 0.92f;
        m_agcHang = float(m_model->rx1.agcHang);
    } else if (m_agcHang > 0) {
        m_agcHang -= 1;
    } else {
        m_agcGain += (target / mag - m_agcGain) * decay;
    }
    m_agcGain = std::clamp(m_agcGain, 0.5f, 400.f);
    return x * m_agcGain;
}

float DspEngine::noiseBlanker(float mag, float i, float q, float& oi, float& oq)
{
    m_nbAvg = m_nbAvg * 0.995f + mag * 0.005f;
    const float thr = m_nbAvg * (m_model->rx1.nb2 ? 2.2f : m_model->rx1.snb ? 3.5f : 5.5f);
    if (mag > thr && (m_model->rx1.nb || m_model->rx1.nb2 || m_model->rx1.snb)) {
        oi = 0;
        oq = 0;
        return 0;
    }
    oi = i;
    oq = q;
    return mag;
}

void DspEngine::spectralNr(std::vector<float>& audio)
{
    if (audio.empty())
        return;
    m_nrFifo.insert(m_nrFifo.end(), audio.begin(), audio.end());
    const int n = 128;
    if (int(m_nrFifo.size()) < n)
        return;
    if (int(m_nrNoise.size()) < n / 2 + 1)
        m_nrNoise.assign(n / 2 + 1, 1e-6f);

    std::vector<float> work(m_nrFifo.begin(), m_nrFifo.end());
    std::vector<std::complex<float>> spec(n);
    const float strength = m_model->rx1.nr2 ? 2.2f : 1.25f;
    for (int off = 0; off + n <= int(work.size()); off += n / 2) {
        for (int i = 0; i < n; ++i) {
            const float w = float(0.5 - 0.5 * std::cos(kTwoPi * i / (n - 1)));
            spec[i] = {work[off + i] * w, 0.f};
        }
        fftRadix2(spec, false);
        for (int i = 0; i < n / 2; ++i) {
            const float p = std::norm(spec[i]);
            m_nrNoise[i] += (p - m_nrNoise[i]) * 0.05f;
            const float g = std::max(0.02f, 1.f - strength * m_nrNoise[i] / (p + 1e-12f));
            spec[i] *= g;
            if (i)
                spec[n - i] = std::conj(spec[i]);
        }
        fftRadix2(spec, true);
        for (int i = 0; i < n / 2 && off + i < int(work.size()); ++i)
            work[off + i] = spec[i].real();
    }
    const int start = std::max(0, int(work.size()) - int(audio.size()));
    for (int i = 0; i < int(audio.size()); ++i)
        audio[i] = work[start + i];
    const int keep = n;
    if (int(m_nrFifo.size()) > keep)
        m_nrFifo.erase(m_nrFifo.begin(), m_nrFifo.end() - keep);
}

std::complex<float> DspEngine::modulate(float mic)
{
    return txSsb48(mic);
}

} // namespace brick2
