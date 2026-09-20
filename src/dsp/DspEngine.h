#pragma once

#include "core/Types.h"

#include <QMetaType>
#include <QObject>
#include <QVector>
#include <complex>
#include <deque>
#include <mutex>
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
    ~DspEngine() override;

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
    static constexpr int kRxCh = 0;
    static constexpr int kTxCh = 1;
    static constexpr int kDisp = 0;
    static constexpr int kBlock = 256;
    static constexpr int kFft = 4096;
    static constexpr int kPixels = 2048;

    void ensureChannels();
    void destroyChannels();
    void applyRxControls();
    void applyTxControls();
    void pushRxBlock();
    void pushTxBlock();
    void runSpectrum(float i, float q);
    int wdspMode(Mode m) const;

    RadioModel* m_model;
    SpectrumFrame m_frame;
    std::mutex m_lock;
    std::vector<double> m_rxIn;
    std::vector<double> m_rxOut;
    std::vector<double> m_txIn;
    std::vector<double> m_txOut;
    std::vector<std::complex<float>> m_fftIn;
    std::vector<std::complex<float>> m_fftOut;
    std::vector<float> m_window;
    std::vector<float> m_fftAvg;
    std::deque<float> m_micFifo;
    std::vector<std::complex<float>> m_txQueue;
    std::vector<qint16> m_spkQueue;
    double m_dcI = 0;
    double m_dcQ = 0;
    float m_voxEnv = 0;
    int m_hangSamples = 0;
    int m_openRate = 0;
    int m_rxFill = 0;
    int m_fftFill = 0;
    qint64 m_lastTuneHz = -1;
    bool m_open = false;
    bool m_anb = false;
    bool m_nob = false;
};

} // namespace brick2

Q_DECLARE_METATYPE(brick2::SpectrumFrame)
