#pragma once

#include "core/Types.h"

#include <QColor>
#include <QObject>
#include <QVector>
#include <array>
#include <complex>

namespace brick2 {

struct VfoState {
    qint64 frequency = 14100000;
    Mode mode = Mode::USB;
    FilterPreset filter = FilterPreset::F4;
    FilterEdges edges;
    qint64 rit = 0;
    bool ritOn = false;
    bool lock = false;
    Band band = Band::B20;
};

struct MemoryChannel {
    QString name;
    qint64 frequency = 14100000;
    Mode mode = Mode::USB;
    FilterPreset filter = FilterPreset::F4;
    Band band = Band::B20;
};

struct RxDspSettings {
    AgcMode agc = AgcMode::Med;
    int agcGain = 80;
    int agcHang = 500;
    int agcThresh = -80;
    int agcSlope = 0;
    int agcDecay = 500;
    bool nr = false;
    bool nr2 = false;
    bool anf = false;
    bool nb = false;
    bool nb2 = false;
    bool snb = false;
    bool binaural = false;
    bool mute = false;
    bool squelch = false;
    int squelchThresh = -80;
    int afGain = 30;
    int rfGain = 0;
    int pan = 50;
    int stepAtt = 0;
    bool lna = true;
    bool dither = true;
    bool randomizer = true;
    std::array<double, 10> rxEq{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    bool rxEqOn = false;
};

struct TxDspSettings {
    int micGain = 35;
    int drive = 50;
    int tunePercent = 100;
    bool useDriveForTune = true;
    bool comp = false;
    int compLevel = 3;
    bool cessb = false;
    bool vox = false;
    int voxHang = 250;
    int voxThresh = 20;
    bool dexp = false;
    bool eqOn = false;
    std::array<double, 10> eq{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    bool lev = false;
    bool cfc = false;
    bool txFilter = true;
    int txFilterLow = 200;
    int txFilterHigh = 2900;
    bool micBoost = false;
    bool lineIn = false;
    bool phono = false;
};

struct CwSettings {
    int wpm = 22;
    int sidetone = 600;
    int sidetoneGain = 40;
    bool iambic = true;
    bool modeB = false;
    bool breakIn = true;
    int breakInDelay = 300;
    bool reversePaddles = false;
    bool showCwFreq = true;
    QString cwxBuffer;
};

struct VacSettings {
    bool vac1 = false;
    bool vac2 = false;
    bool vac1Stereo = true;
    bool vac2Stereo = true;
    int vac1GainRx = 0;
    int vac1GainTx = 0;
    int vac1Latency = 120;
    QString vac1In;
    QString vac1Out;
    bool autoEnableDigital = true;
};

struct DisplaySettings {
    DisplayMode mode = DisplayMode::Panafall;
    int avg = 4;
    int waterfallMin = -130;
    int waterfallMax = -40;
    int spectrumMin = -140;
    int spectrumMax = -20;
    int fps = 30;
    int waterfallFps = 15;
    int zoom = 1;
    double pan = 0.5;
    bool grid = true;
    bool peakHold = false;
    bool fill = true;
    int fftSize = kFftSize;
    QString palette = QStringLiteral("enhanced");
    QColor panLine{180, 245, 245};
    QColor panFill{62, 200, 200};
    int panFillAlpha = 150;
    int panBrightness = 100;
    int panGradient = 100;
    int panGradientMid = 55;
    int wfBrightness = 100;
};

struct CatSettings {
    bool enabled = false;
    quint16 port = 4532;
    bool kenwood = true;
    bool thetisZz = true;
    bool pttInhibit = false;
};

struct PureSignalSettings {
    bool enabled = false;
    bool autoAttenuate = true;
    bool twoTone = false;
    int feedbackGain = 0;
    int corrections = 0;
    double ampRms = 0;
};

struct MeterReadings {
    float sMeter = -130;
    float alc = -20;
    float mic = -20;
    float eq = -20;
    float comp = -20;
    float fwdWatts = 0;
    float revWatts = 0;
    float swr = 1;
    float supplyVolts = 0;
    float paTemp = 0;
    bool adcOverload = false;
    bool ptt = false;
    bool dot = false;
    bool dash = false;
};

class RadioModel : public QObject {
    Q_OBJECT
public:
    explicit RadioModel(QObject* parent = nullptr);

    VfoState vfoA;
    VfoState vfoB;
    RxDspSettings rx1;
    RxDspSettings rx2;
    TxDspSettings tx;
    CwSettings cw;
    VacSettings vac;
    DisplaySettings display;
    CatSettings cat;
    PureSignalSettings ps;
    MeterReadings meters;

    bool rx2Enabled = false;
    bool split = false;
    bool mox = false;
    bool tune = false;
    bool voxOpen = false;
    bool dup = false;
    bool vfoSwapPending = false;
    bool multiRx = false;
    bool diversity = false;
    bool apollo = true;
    bool paEnable = true;
    int sampleRate = 192000;
    int rxCount = 1;
    int tuneStepHz = 100;
    int txAnt = 0;
    int rxAnt = 0;
    qint64 xit = 0;
    bool xitOn = false;
    bool filterSync = false;
    QString profile = QStringLiteral("Default");
    QVector<MemoryChannel> memories;
    std::array<qint64, 14> bandStack{};

    qint64 txFrequency() const;
    FilterEdges currentFilter() const;
    bool isTransmitting() const;
    QString transmitLabel() const;
    void applyFilterPreset(FilterPreset p);
    void applyRxFilter(int low, int high, bool asVar = true);
    void applyTxFilter(int low, int high);
    void setFilterSync(bool on);
    void syncTxFromRx();
    void syncRxFromTx();
    void setBand(Band band);
    void loadSettings();
    void saveSettings() const;

signals:
    void stateChanged();
    void frequencyChanged();
    void modeChanged();
    void transmitChanged();
    void dspChanged();
};

} // namespace brick2
