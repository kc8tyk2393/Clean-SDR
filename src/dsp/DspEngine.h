#pragma once

#include "core/Types.h"

#include <QMetaType>
#include <QObject>
#include <QVector>
#include <complex>
#include <deque>
#include <vector>

namespace brick2 {

class RadioModel;

struct SpectrumFrame {
    QVector<float> db;
    QVector<float> audio;
    QVector<float> scope;
    float sMeter = -130;
    float alc = -20;
    float mic = -20;
};

class DspEngine : public QObject {
    Q_OBJECT
public:
    explicit DspEngine(RadioModel* model, QObject* parent = nullptr);

    void processIq(int ddc, const QVector<float>& interleavedIq);
    void generateTx(int samples192k);
    void processMic(const QVector<float>& mic);
    QVector<std::complex<float>> takeTxIq();
    QVector<qint16> takeSpeaker();
    SpectrumFrame lastFrame() const { return m_frame; }

signals:
    void frameReady(const brick2::SpectrumFrame& frame);
    void txIqReady();
    void audioReady();

private:
    void rebuildFilters();
    void runSpectrum(const std::vector<std::complex<float>>& iq);
    void demodBlock(const std::vector<std::complex<float>>& iq, std::vector<float>& audio);
    float agc(float x);
    float noiseBlanker(float mag, float i, float q, float& oi, float& oq);
    void spectralNr(std::vector<float>& audio);
    std::complex<float> modulate(float mic);
    float popMic();
    std::complex<float> txSsb48(float audio);
    void buildTxFilters();

    RadioModel* m_model;
    SpectrumFrame m_frame;
    std::vector<float> m_lpf;
    std::vector<std::complex<float>> m_delay;
    std::vector<std::complex<float>> m_fftIn;
    std::vector<std::complex<float>> m_fftOut;
    std::vector<float> m_window;
    std::vector<float> m_nrNoise;
    std::deque<float> m_nrFifo;
    std::deque<float> m_binDly;
    std::vector<float> m_eqState;
    std::vector<std::complex<float>> m_txQueue;
    std::vector<qint16> m_spkQueue;
    std::deque<float> m_micFifo;
    std::vector<float> m_waterfallAcc;
    std::vector<std::complex<float>> m_txFir;
    std::vector<std::complex<float>> m_txDelay;
    std::complex<float> m_ssbPrev{0, 0};
    std::complex<float> m_ssbCurr{0, 0};
    float m_dcX = 0;
    float m_dcY = 0;
    float m_alcGain = 1.f;
    float m_lastMic = 0;
    int m_interp = 0;
    Mode m_lastTxMode = Mode::USB;
    double m_nco = 0;
    double m_tone1 = 0;
    double m_tone2 = 0;
    double m_fmPhase = 0;
    double m_pllPhase = 0;
    double m_pllFreq = 0;
    double m_bfo = 0;
    double m_shiftHz = 0;
    float m_agcGain = 1;
    float m_agcHang = 0;
    float m_dcI = 0;
    float m_dcQ = 0;
    float m_nbAvg = 0;
    float m_anfW[8] = {};
    float m_anfX[8] = {};
    float m_compEnv = 0;
    float m_voxEnv = 0;
    int m_hangSamples = 0;
    int m_fftFill = 0;
    int m_firPos = 0;
    int m_txPos = 0;
    int m_lastRate = 0;
    int m_lastLow = 0;
    int m_lastHigh = 0;
    int m_lastTxLow = -1;
    int m_lastTxHigh = -1;
    qint64 m_lastTuneHz = -1;
    Mode m_lastMode = Mode::USB;
};

} // namespace brick2

Q_DECLARE_METATYPE(brick2::SpectrumFrame)
