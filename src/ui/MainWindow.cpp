#include "ui/MainWindow.h"

#include "audio/AudioEngine.h"
#include "cat/CatEngine.h"
#include "core/RadioModel.h"
#include "dsp/DspEngine.h"
#include "protocol/Protocol2.h"
#include "ui/ConsoleWidgets.h"
#include "ui/Dialogs.h"
#include "ui/PanafallWidget.h"
#include "ui/SetupDialog.h"
#include "ui/Theme.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace brick2 {

MainWindow::MainWindow()
    : m_model(new RadioModel(this))
    , m_proto(new Protocol2Engine(m_model, this))
    , m_dsp(new DspEngine(m_model, this))
    , m_audio(new AudioEngine(m_model, this))
    , m_cat(new CatEngine(m_model, this))
{
    m_model->loadSettings();
    setWindowTitle(QStringLiteral("Brick2SDR"));
    resize(1480, 940);
    setStyleSheet(theme::styleSheet());
    buildMenus();
    buildUi();

    connect(m_proto, &Protocol2Engine::iqReceived, m_dsp, &DspEngine::processIq);
    connect(m_proto, &Protocol2Engine::micReceived, m_dsp, &DspEngine::processMic);
    connect(m_dsp, &DspEngine::frameReady, this, [this](const SpectrumFrame& f) {
        m_display->setFrame(f);
        m_model->meters.sMeter = f.sMeter;
        m_model->meters.mic = f.mic;
        refreshMeters();
    });
    connect(m_dsp, &DspEngine::audioReady, this, [this] {
        m_audio->pushSpeaker(m_dsp->takeSpeaker());
        const auto tx = m_dsp->takeTxIq();
        for (const auto& s : tx)
            m_proto->sendTxIq(s.real(), s.imag());
    });
    connect(m_display, &PanafallWidget::clickTune, this, [this](qint64 hz) {
        m_model->setFrequency(hz, false, true);
    });
    connect(m_model, &RadioModel::frequencyChanged, this, [this] {
        refreshVfo();
        m_display->update();
        m_proto->pushControl();
    });
    connect(m_model, &RadioModel::stateChanged, this, [this] {
        if (m_lcdA)
            m_lcdA->setStepHz(m_model->tuneStepHz);
        if (m_lcdB)
            m_lcdB->setStepHz(m_model->tuneStepHz);
    });
    connect(m_model, &RadioModel::modeChanged, this, [this] { refreshVfo(); });
    connect(m_proto, &Protocol2Engine::connected, this, [this](const RadioInfo& info) {
        m_link->setText(info.simulator ? QStringLiteral("SIMULATOR")
                                       : QStringLiteral("ONLINE  ·  %1").arg(info.ip));
        m_status->setText(QStringLiteral("Protocol 2  ·  %1  ·  %2  ·  MAC %3  ·  FW %4")
                              .arg(info.name, info.ip, info.mac)
                              .arg(info.firmware));
        m_audio->start();
    });
    connect(m_proto, &Protocol2Engine::radiosChanged, this, [this](const QVector<RadioInfo>& radios) {
        const QString current = m_radioBox->currentData().toString();
        m_radioBox->clear();
        for (const auto& r : radios) {
            if (r.simulator)
                continue;
            m_radioBox->addItem(QStringLiteral("%1  ·  %2").arg(r.ip, r.mac.isEmpty() ? r.name : r.mac), r.ip);
        }
        const int idx = m_radioBox->findData(current);
        if (idx >= 0)
            m_radioBox->setCurrentIndex(idx);
        if (m_radioBox->count() == 0)
            m_radioBox->addItem(QStringLiteral("No radios yet"));
    });
    connect(m_proto, &Protocol2Engine::discoveryStatus, this, [this](const QString& msg) {
        m_status->setText(msg);
    });
    connect(m_proto, &Protocol2Engine::disconnected, this, [this](const QString&) {
        m_link->setText(QStringLiteral("STANDBY"));
    });

    connect(m_model, &RadioModel::transmitChanged, this, [this] { applyTransmit(); });

    auto* micTimer = new QTimer(this);
    micTimer->setInterval(20);
    connect(micTimer, &QTimer::timeout, this, [this] {
        m_dsp->processMic(m_audio->pullMic(960));
        if (m_model->isTransmitting()) {
            m_dsp->generateTx(3840);
            const auto tx = m_dsp->takeTxIq();
            for (const auto& s : tx)
                m_proto->sendTxIq(s.real(), s.imag());
        } else {
            m_model->meters.alc = 0.f;
        }
        const auto spk = m_dsp->takeSpeaker();
        if (!spk.isEmpty() && (!m_model->isTransmitting() || m_model->dup)) {
            for (int i = 0; i + 1 < spk.size(); i += 2)
                m_proto->sendAudio(spk[i], spk[i + 1]);
        }
        if (m_keyStatus)
            m_keyStatus->setText(m_model->transmitLabel());
        refreshMeters();
    });
    micTimer->start();

    refreshVfo();
    m_audio->start();
    m_proto->startDiscovery();
    m_status->setText(QStringLiteral("Discovering OpenHPSDR Protocol 2 radios…"));
}

MainWindow::~MainWindow()
{
    m_model->saveSettings();
    m_proto->disconnectRadio();
    m_audio->stop();
}

void MainWindow::buildMenus()
{
    auto* file = menuBar()->addMenu(QStringLiteral("&File"));
    file->addAction(QStringLiteral("Setup…"), this, [this] {
        SetupDialog dlg(m_model, m_audio, this);
        dlg.exec();
        refreshVfo();
        m_audio->start();
        if (m_model->cat.enabled)
            m_cat->start();
        else
            m_cat->stop();
    });
    file->addSeparator();
    file->addAction(QStringLiteral("Exit"), this, &QWidget::close);

    auto* radio = menuBar()->addMenu(QStringLiteral("&Radio"));
    radio->addAction(QStringLiteral("Discover"), this, [this] { m_proto->startDiscovery(); });
    radio->addAction(QStringLiteral("Connect Brick2"), this, [this] {
        if (!m_proto->connectFirstHardware())
            m_status->setText(QStringLiteral("No Protocol 2 Brick2 found"));
    });
    radio->addAction(QStringLiteral("Start Simulator"), this, [this] { m_proto->startSimulator(); });
    radio->addAction(QStringLiteral("Stop"), this, [this] { m_proto->disconnectRadio(); });

    auto* dsp = menuBar()->addMenu(QStringLiteral("&DSP"));
    dsp->addAction(QStringLiteral("RX Equalizer"), this, [this] { EqualizerDialog(m_model, false, this).exec(); });
    dsp->addAction(QStringLiteral("TX Equalizer"), this, [this] { EqualizerDialog(m_model, true, this).exec(); });
    dsp->addAction(QStringLiteral("Linearity (PureSignal)"), this, [this] { PureSignalDialog(m_model, this).exec(); });

    auto* cw = menuBar()->addMenu(QStringLiteral("&CW"));
    cw->addAction(QStringLiteral("CWX"), this, [this] { CwxDialog(m_model, this).exec(); });

    auto* mem = menuBar()->addMenu(QStringLiteral("&Memory"));
    mem->addAction(QStringLiteral("Memories…"), this, [this] { MemoryDialog(m_model, this).exec(); });

    auto* help = menuBar()->addMenu(QStringLiteral("&Help"));
    help->addAction(QStringLiteral("About Brick2SDR"), this, [this] {
        QMessageBox::information(this, QStringLiteral("Brick2SDR"),
                                 QStringLiteral("Brick2SDR  ·  Qt6 console for the Brick2\n"
                                                "Profile: ANAN-10E / Hermes II  ·  OpenHPSDR Protocol 2"));
    });
}

QPushButton* MainWindow::key(const QString& text)
{
    auto* b = new QPushButton(text);
    b->setCursor(Qt::PointingHandCursor);
    b->setCheckable(true);
    b->setAutoExclusive(false);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

QPushButton* MainWindow::latch(const QString& text, bool* flag, const char* role)
{
    auto* b = key(text);
    b->setChecked(*flag);
    b->setProperty("role", QByteArray(role));
    connect(b, &QPushButton::toggled, this, [this, b, flag](bool v) {
        *flag = v;
        b->style()->unpolish(b);
        b->style()->polish(b);
        emit m_model->dspChanged();
        emit m_model->stateChanged();
        if (flag == &m_model->mox || flag == &m_model->tune || flag == &m_model->ps.twoTone
            || flag == &m_model->dup)
            emit m_model->transmitChanged();
        if (flag == &m_model->vac.vac1)
            m_audio->start();
        refreshVfo();
    });
    return b;
}

QWidget* MainWindow::sliderColumn(const QString& name, int minv, int maxv, int* value)
{
    auto* w = new QWidget;
    auto* box = new QVBoxLayout(w);
    box->setContentsMargins(4, 4, 4, 4);
    box->setSpacing(4);
    auto* lab = new QLabel(name);
    lab->setObjectName(QStringLiteral("section"));
    lab->setAlignment(Qt::AlignCenter);
    auto* s = new QSlider(Qt::Vertical);
    s->setRange(minv, maxv);
    s->setValue(*value);
    s->setInvertedAppearance(false);
    s->setMinimumHeight(90);
    auto* val = new QLabel(QString::number(*value));
    val->setObjectName(QStringLiteral("value"));
    val->setAlignment(Qt::AlignCenter);
    connect(s, &QSlider::valueChanged, this, [value, val](int v) {
        *value = v;
        val->setText(QString::number(v));
    });
    box->addWidget(lab);
    box->addWidget(s, 1, Qt::AlignHCenter);
    box->addWidget(val);
    return w;
}

void MainWindow::buildUi()
{
    auto* central = new QWidget;
    setCentralWidget(central);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(12, 10, 12, 8);
    root->setSpacing(10);

    auto* header = makePanel(central, QStringLiteral("header"));
    auto* headerLay = new QHBoxLayout(header);
    headerLay->setContentsMargins(16, 10, 12, 10);
    auto* brandCol = new QVBoxLayout;
    brandCol->setSpacing(0);
    auto* brand = new QLabel(QStringLiteral("BRICK2SDR"));
    brand->setObjectName(QStringLiteral("brand"));
    auto* sub = new QLabel(QStringLiteral("OPENHPSDR PROTOCOL 2  ·  ANAN-10E"));
    sub->setObjectName(QStringLiteral("brandSub"));
    brandCol->addWidget(brand);
    brandCol->addWidget(sub);
    m_link = new QLabel(QStringLiteral("STANDBY"));
    m_link->setObjectName(QStringLiteral("brandSub"));
    m_radioBox = new QComboBox;
    m_radioBox->setMinimumWidth(220);
    m_radioBox->addItem(QStringLiteral("No radios yet"));
    m_ipEdit = new QLineEdit;
    m_ipEdit->setPlaceholderText(QStringLiteral("Brick2 IP"));
    m_ipEdit->setFixedWidth(130);
    auto* disc = new QPushButton(QStringLiteral("DISCOVER"));
    disc->setProperty("role", "ghost");
    disc->setFocusPolicy(Qt::NoFocus);
    connect(disc, &QPushButton::clicked, this, [this] { m_proto->startDiscovery(); });
    auto* conn = new QPushButton(QStringLiteral("CONNECT"));
    conn->setProperty("role", "ghost");
    conn->setFocusPolicy(Qt::NoFocus);
    connect(conn, &QPushButton::clicked, this, [this] {
        const QString typed = m_ipEdit->text().trimmed();
        if (!typed.isEmpty()) {
            m_proto->connectToIp(typed);
            return;
        }
        const QString ip = m_radioBox->currentData().toString();
        if (!ip.isEmpty()) {
            m_proto->connectToIp(ip);
            return;
        }
        m_proto->startDiscovery();
        QTimer::singleShot(800, this, [this] {
            if (!m_proto->connectFirstHardware())
                m_status->setText(QStringLiteral("No Brick2 reply — enter its IP and press CONNECT"));
        });
    });
    auto* start = new QPushButton(QStringLiteral("START"));
    start->setProperty("role", "power");
    start->setFocusPolicy(Qt::NoFocus);
    connect(start, &QPushButton::clicked, this, [this] {
        if (!m_ipEdit->text().trimmed().isEmpty()) {
            m_proto->connectToIp(m_ipEdit->text().trimmed());
            return;
        }
        if (!m_proto->connectFirstHardware())
            m_proto->startSimulator();
    });
    auto* stop = new QPushButton(QStringLiteral("STOP"));
    stop->setProperty("role", "ghost");
    stop->setFocusPolicy(Qt::NoFocus);
    connect(stop, &QPushButton::clicked, this, [this] { m_proto->disconnectRadio(); });
    headerLay->addLayout(brandCol);
    headerLay->addStretch();
    headerLay->addWidget(m_link);
    headerLay->addSpacing(8);
    headerLay->addWidget(m_radioBox);
    headerLay->addWidget(m_ipEdit);
    headerLay->addWidget(disc);
    headerLay->addWidget(conn);
    headerLay->addWidget(start);
    headerLay->addWidget(stop);
    root->addWidget(header);

    auto* top = new QHBoxLayout;
    top->setSpacing(10);
    m_lcdA = new LcdVfo(QStringLiteral("VFO A"));
    m_lcdB = new LcdVfo(QStringLiteral("VFO B"));
    m_meters = new MeterRack;
    connect(m_lcdA, &LcdVfo::tuned, this, [this](qint64 hz) { m_model->setFrequency(hz); });
    connect(m_lcdA, &LcdVfo::stepSelected, this, [this](qint64 step) {
        m_model->tuneStepHz = int(std::max<qint64>(1, step));
        m_lcdB->setStepHz(step);
    });
    connect(m_lcdB, &LcdVfo::tuned, this, [this](qint64 hz) { m_model->setFrequency(hz, true); });
    connect(m_lcdB, &LcdVfo::stepSelected, this, [this](qint64 step) {
        m_model->tuneStepHz = int(std::max<qint64>(1, step));
        m_lcdA->setStepHz(step);
    });
    top->addWidget(m_lcdA, 3);
    top->addWidget(m_meters, 3);
    top->addWidget(m_lcdB, 3);
    root->addLayout(top);

    auto* bezel = makePanel(central, QStringLiteral("bezel"));
    auto* bezelLay = new QVBoxLayout(bezel);
    bezelLay->setContentsMargins(6, 6, 6, 6);
    m_display = new PanafallWidget(m_model);
    m_display->setMinimumHeight(360);
    bezelLay->addWidget(m_display);
    root->addWidget(bezel, 1);

    auto keyRow = [](QButtonGroup* g, QLayout* lay) {
        g->setExclusive(true);
        lay->setSpacing(4);
    };

    auto* bandsBox = makePanel(central);
    auto* bandsLay = new QVBoxLayout(bandsBox);
    bandsLay->setContentsMargins(10, 8, 10, 8);
    auto* bandTitle = new QLabel(QStringLiteral("BAND"));
    bandTitle->setObjectName(QStringLiteral("section"));
    bandsLay->addWidget(bandTitle);
    auto* bands = new QHBoxLayout;
    m_bands = new QButtonGroup(this);
    keyRow(m_bands, bands);
    int bi = 0;
    for (const auto& b : bandTable()) {
        if (b.band == Band::VHF)
            continue;
        auto* btn = key(b.name);
        m_bands->addButton(btn, int(b.band));
        connect(btn, &QPushButton::clicked, this, [this, band = b.band] {
            m_model->setBand(band);
            refreshVfo();
        });
        bands->addWidget(btn);
        ++bi;
        Q_UNUSED(bi);
    }
    bandsLay->addLayout(bands);
    root->addWidget(bandsBox);

    auto* mid = new QHBoxLayout;
    mid->setSpacing(10);

    auto* modeBox = makePanel(central);
    auto* modeLay = new QVBoxLayout(modeBox);
    modeLay->setContentsMargins(10, 8, 10, 8);
    auto* mt = new QLabel(QStringLiteral("MODE"));
    mt->setObjectName(QStringLiteral("section"));
    modeLay->addWidget(mt);
    auto* modes = new QHBoxLayout;
    m_modes = new QButtonGroup(this);
    keyRow(m_modes, modes);
    for (Mode m : {Mode::LSB, Mode::USB, Mode::DSB, Mode::CWL, Mode::CWU, Mode::FM, Mode::AM, Mode::SAM,
                   Mode::DIGL, Mode::DIGU, Mode::SPEC, Mode::DRM}) {
        auto* btn = key(modeName(m));
        m_modes->addButton(btn, int(m));
        connect(btn, &QPushButton::clicked, this, [this, m] { setMode(m); });
        modes->addWidget(btn);
    }
    modeLay->addLayout(modes);

    auto* ft = new QLabel(QStringLiteral("FILTER"));
    ft->setObjectName(QStringLiteral("section"));
    modeLay->addWidget(ft);
    auto* filters = new QHBoxLayout;
    m_filters = new QButtonGroup(this);
    keyRow(m_filters, filters);
    for (int i = 1; i <= 10; ++i) {
        auto* btn = key(QStringLiteral("F%1").arg(i));
        m_filters->addButton(btn, i - 1);
        connect(btn, &QPushButton::clicked, this, [this, i] { m_model->applyFilterPreset(FilterPreset(i - 1)); });
        filters->addWidget(btn);
    }
    auto* var1 = key(QStringLiteral("VAR"));
    m_filters->addButton(var1, int(FilterPreset::Var1));
    connect(var1, &QPushButton::clicked, this, [this] { m_model->applyFilterPreset(FilterPreset::Var1); });
    filters->addWidget(var1);
    auto* sync = key(QStringLiteral("SYNC"));
    sync->setChecked(m_model->filterSync);
    sync->setToolTip(QStringLiteral("Keep RX and TX filter size the same"));
    connect(sync, &QPushButton::toggled, this, [this](bool on) { m_model->setFilterSync(on); });
    m_syncBtn = sync;
    filters->addWidget(sync);
    modeLay->addLayout(filters);

    auto tag = [](const QString& t) {
        auto* l = new QLabel(t);
        l->setObjectName(QStringLiteral("section"));
        return l;
    };
    auto* rxAdj = new QHBoxLayout;
    rxAdj->setSpacing(6);
    m_rxBw = new QLabel;
    m_rxBw->setObjectName(QStringLiteral("bwReadout"));
    m_rxLow = filterSpin(-8000, 8000, m_model->vfoA.edges.low);
    m_rxHigh = filterSpin(-8000, 8000, m_model->vfoA.edges.high);
    connect(m_rxLow, &QSpinBox::valueChanged, this, [this] { applyRxFromUi(); });
    connect(m_rxHigh, &QSpinBox::valueChanged, this, [this] { applyRxFromUi(); });
    rxAdj->addWidget(m_rxBw);
    rxAdj->addWidget(tag(QStringLiteral("LOW")));
    rxAdj->addWidget(m_rxLow);
    rxAdj->addWidget(tag(QStringLiteral("HIGH")));
    rxAdj->addWidget(m_rxHigh);
    rxAdj->addStretch(1);
    modeLay->addLayout(rxAdj);

    auto* txAdj = new QHBoxLayout;
    txAdj->setSpacing(6);
    m_txBw = new QLabel;
    m_txBw->setObjectName(QStringLiteral("bwReadout"));
    m_txLow = filterSpin(0, 8000, m_model->tx.txFilterLow);
    m_txHigh = filterSpin(50, 8000, m_model->tx.txFilterHigh);
    connect(m_txLow, &QSpinBox::valueChanged, this, [this] { applyTxFromUi(); });
    connect(m_txHigh, &QSpinBox::valueChanged, this, [this] { applyTxFromUi(); });
    txAdj->addWidget(m_txBw);
    txAdj->addWidget(tag(QStringLiteral("LOW")));
    txAdj->addWidget(m_txLow);
    txAdj->addWidget(tag(QStringLiteral("HIGH")));
    txAdj->addWidget(m_txHigh);
    txAdj->addStretch(1);
    modeLay->addLayout(txAdj);
    mid->addWidget(modeBox, 3);

    auto* dspBox = makePanel(central);
    auto* dspLay = new QVBoxLayout(dspBox);
    dspLay->setContentsMargins(10, 8, 10, 8);
    auto* dt = new QLabel(QStringLiteral("DSP"));
    dt->setObjectName(QStringLiteral("section"));
    dspLay->addWidget(dt);
    auto* dsp = new QHBoxLayout;
    dsp->setSpacing(4);
    dsp->addWidget(latch(QStringLiteral("NR"), &m_model->rx1.nr));
    dsp->addWidget(latch(QStringLiteral("NR2"), &m_model->rx1.nr2));
    dsp->addWidget(latch(QStringLiteral("ANF"), &m_model->rx1.anf));
    dsp->addWidget(latch(QStringLiteral("NB"), &m_model->rx1.nb));
    dsp->addWidget(latch(QStringLiteral("NB2"), &m_model->rx1.nb2));
    dsp->addWidget(latch(QStringLiteral("SNB"), &m_model->rx1.snb));
    dsp->addWidget(latch(QStringLiteral("BIN"), &m_model->rx1.binaural));
    dsp->addWidget(latch(QStringLiteral("SQL"), &m_model->rx1.squelch));
    dsp->addWidget(latch(QStringLiteral("MUTE"), &m_model->rx1.mute));
    dsp->addWidget(latch(QStringLiteral("VAC"), &m_model->vac.vac1));
    dsp->addWidget(latch(QStringLiteral("RX2"), &m_model->rx2Enabled));
    dspLay->addLayout(dsp);
    auto* at = new QLabel(QStringLiteral("AGC"));
    at->setObjectName(QStringLiteral("section"));
    dspLay->addWidget(at);
    auto* agcRow = new QHBoxLayout;
    m_agc = new QButtonGroup(this);
    keyRow(m_agc, agcRow);
    int ai = 0;
    for (auto [label, mode] : {std::pair<const char*, AgcMode>{"OFF", AgcMode::Off},
                               {"LONG", AgcMode::Long},
                               {"SLOW", AgcMode::Slow},
                               {"MED", AgcMode::Med},
                               {"FAST", AgcMode::Fast}}) {
        auto* b = key(QString::fromLatin1(label));
        m_agc->addButton(b, int(mode));
        connect(b, &QPushButton::clicked, this, [this, mode] { m_model->rx1.agc = mode; });
        agcRow->addWidget(b);
        ++ai;
        Q_UNUSED(ai);
    }
    dspLay->addLayout(agcRow);
    mid->addWidget(dspBox, 2);
    root->addLayout(mid);

    auto* bottom = new QHBoxLayout;
    bottom->setSpacing(10);

    auto* txBox = makePanel(central);
    auto* txLay = new QVBoxLayout(txBox);
    txLay->setContentsMargins(10, 8, 10, 8);
    auto* txt = new QLabel(QStringLiteral("KEYING  ·  BRICK2 PTT"));
    txt->setObjectName(QStringLiteral("section"));
    txLay->addWidget(txt);
    auto* keying = new QHBoxLayout;
    keying->setSpacing(8);
    m_moxBtn = latch(QStringLiteral("MOX"), &m_model->mox, "tx");
    m_moxBtn->setMinimumSize(100, 42);
    m_voxBtn = latch(QStringLiteral("VOX"), &m_model->tx.vox);
    m_voxBtn->setMinimumSize(100, 42);
    m_twoToneBtn = latch(QStringLiteral("TWO TONE"), &m_model->ps.twoTone, "tx");
    m_twoToneBtn->setMinimumSize(120, 42);
    connect(m_moxBtn, &QPushButton::toggled, this, [this](bool on) {
        if (on && m_twoToneBtn) {
            m_twoToneBtn->blockSignals(true);
            m_twoToneBtn->setChecked(false);
            m_twoToneBtn->blockSignals(false);
            m_model->ps.twoTone = false;
        }
        applyTransmit();
    });
    connect(m_voxBtn, &QPushButton::toggled, this, [this] { applyTransmit(); });
    connect(m_twoToneBtn, &QPushButton::toggled, this, [this](bool on) {
        if (on) {
            m_model->tune = false;
            if (m_moxBtn) {
                m_moxBtn->blockSignals(true);
                m_moxBtn->setChecked(false);
                m_moxBtn->blockSignals(false);
                m_model->mox = false;
            }
        }
        applyTransmit();
    });
    m_keyStatus = new QLabel(QStringLiteral("RX"));
    m_keyStatus->setObjectName(QStringLiteral("brand"));
    m_keyStatus->setMinimumWidth(110);
    keying->addWidget(m_moxBtn);
    keying->addWidget(m_voxBtn);
    keying->addWidget(m_twoToneBtn);
    keying->addWidget(m_keyStatus);
    keying->addStretch();
    txLay->addLayout(keying);

    auto* tx = new QHBoxLayout;
    tx->setSpacing(4);
    auto* tun = latch(QStringLiteral("TUN"), &m_model->tune, "tx");
    connect(tun, &QPushButton::toggled, this, [this](bool on) {
        if (on)
            m_model->ps.twoTone = false;
        applyTransmit();
    });
    tx->addWidget(tun);
    tx->addWidget(latch(QStringLiteral("COMP"), &m_model->tx.comp));
    tx->addWidget(latch(QStringLiteral("CESSB"), &m_model->tx.cessb));
    tx->addWidget(latch(QStringLiteral("DUP"), &m_model->dup));
    tx->addWidget(latch(QStringLiteral("SPLIT"), &m_model->split));
    tx->addWidget(latch(QStringLiteral("PS"), &m_model->ps.enabled));
    auto addAct = [&](const QString& t, auto fn) {
        auto* b = new QPushButton(t);
        b->setProperty("role", "ghost");
        b->setFocusPolicy(Qt::NoFocus);
        connect(b, &QPushButton::clicked, this, fn);
        tx->addWidget(b);
    };
    addAct(QStringLiteral("A → B"), [this] {
        m_model->vfoB = m_model->vfoA;
        refreshVfo();
    });
    addAct(QStringLiteral("A ← B"), [this] {
        m_model->vfoA = m_model->vfoB;
        refreshVfo();
        emit m_model->frequencyChanged();
    });
    addAct(QStringLiteral("A ⇄ B"), [this] {
        std::swap(m_model->vfoA, m_model->vfoB);
        refreshVfo();
        emit m_model->frequencyChanged();
    });
    txLay->addLayout(tx);

    auto* dispRow = new QHBoxLayout;
    auto* dl = new QLabel(QStringLiteral("DISPLAY"));
    dl->setObjectName(QStringLiteral("section"));
    auto* disp = new QComboBox;
    disp->addItems({QStringLiteral("Spectrum"), QStringLiteral("Panadapter"), QStringLiteral("Scope"),
                    QStringLiteral("Phase"), QStringLiteral("Waterfall"), QStringLiteral("Histogram"),
                    QStringLiteral("Panafall"), QStringLiteral("Panascope")});
    disp->setCurrentIndex(int(m_model->display.mode));
    connect(disp, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_model->display.mode = DisplayMode(i);
        m_display->update();
    });
    dispRow->addWidget(dl);
    dispRow->addWidget(disp, 1);
    txLay->addLayout(dispRow);
    bottom->addWidget(txBox, 3);

    auto* sliders = makePanel(central);
    auto* sLay = new QHBoxLayout(sliders);
    sLay->setContentsMargins(8, 6, 8, 6);
    sLay->addWidget(sliderColumn(QStringLiteral("AF"), 0, 100, &m_model->rx1.afGain));
    sLay->addWidget(sliderColumn(QStringLiteral("RF"), -20, 20, &m_model->rx1.rfGain));
    sLay->addWidget(sliderColumn(QStringLiteral("AGC"), 0, 120, &m_model->rx1.agcGain));
    sLay->addWidget(sliderColumn(QStringLiteral("ATT"), 0, 31, &m_model->rx1.stepAtt));
    sLay->addWidget(sliderColumn(QStringLiteral("MIC"), 0, 70, &m_model->tx.micGain));
    sLay->addWidget(sliderColumn(QStringLiteral("DRV"), 0, 100, &m_model->tx.drive));
    sLay->addWidget(sliderColumn(QStringLiteral("SQL"), -160, 0, &m_model->rx1.squelchThresh));
    bottom->addWidget(sliders, 2);
    root->addLayout(bottom);

    m_status = new QLabel;
    statusBar()->addWidget(m_status, 1);
}

void MainWindow::applyTransmit()
{
    m_proto->pushControl();
    refreshVfo();
    refreshMeters();
    if (m_keyStatus)
        m_keyStatus->setText(m_model->transmitLabel());
}

void MainWindow::setMode(Mode m)
{
    m_model->vfoA.mode = m;
    if (m_model->vfoA.filter == FilterPreset::Var1 || m_model->vfoA.filter == FilterPreset::Var2)
        m_model->vfoA.edges = remapFilter(m_model->vfoA.edges, m);
    else
        m_model->vfoA.edges = defaultFilter(m, m_model->vfoA.filter);
    if (m_model->filterSync)
        m_model->syncTxFromRx();
    if (m_model->vac.autoEnableDigital) {
        const bool on = isDigital(m);
        if (m_model->vac.vac1 != on) {
            m_model->vac.vac1 = on;
            m_audio->start();
        }
    }
    emit m_model->modeChanged();
    refreshVfo();
}

void MainWindow::refreshVfo()
{
    m_lcdA->setState(m_model->vfoA, false, m_model->isTransmitting());
    m_lcdB->setState(m_model->vfoB, m_model->split, false);
    m_lcdA->setStepHz(m_model->tuneStepHz);
    m_lcdB->setStepHz(m_model->tuneStepHz);
    const auto e = m_model->vfoA.edges;
    const int rxHz = std::abs(e.high - e.low);
    const int txHz = std::max(0, m_model->tx.txFilterHigh - m_model->tx.txFilterLow);
    if (m_rxBw)
        m_rxBw->setText(QStringLiteral("RX  %1").arg(formatBandwidth(rxHz)));
    if (m_txBw)
        m_txBw->setText(QStringLiteral("TX  %1").arg(formatBandwidth(txHz)));
    syncFilterSpins();
    refreshSelection();
    refreshMeters();
}

QSpinBox* MainWindow::filterSpin(int minv, int maxv, int value)
{
    auto* s = new QSpinBox;
    s->setObjectName(QStringLiteral("filterHz"));
    s->setRange(minv, maxv);
    s->setSingleStep(10);
    s->setAccelerated(true);
    s->setSuffix(QStringLiteral(" Hz"));
    s->setValue(value);
    s->setKeyboardTracking(false);
    s->setMinimumWidth(108);
    return s;
}

void MainWindow::syncFilterSpins()
{
    auto setSpin = [](QSpinBox* s, int v) {
        if (!s || s->value() == v)
            return;
        s->blockSignals(true);
        s->setValue(v);
        s->blockSignals(false);
    };
    setSpin(m_rxLow, m_model->vfoA.edges.low);
    setSpin(m_rxHigh, m_model->vfoA.edges.high);
    setSpin(m_txLow, m_model->tx.txFilterLow);
    setSpin(m_txHigh, m_model->tx.txFilterHigh);
}

void MainWindow::applyRxFromUi()
{
    if (!m_rxLow || !m_rxHigh)
        return;
    m_model->applyRxFilter(m_rxLow->value(), m_rxHigh->value(), true);
}

void MainWindow::applyTxFromUi()
{
    if (!m_txLow || !m_txHigh)
        return;
    m_model->applyTxFilter(m_txLow->value(), m_txHigh->value());
}

void MainWindow::refreshSelection()
{
    if (auto* b = m_bands->button(int(m_model->vfoA.band)))
        b->setChecked(true);
    if (auto* b = m_modes->button(int(m_model->vfoA.mode)))
        b->setChecked(true);
    if (auto* b = m_filters->button(int(m_model->vfoA.filter)))
        b->setChecked(true);
    if (auto* b = m_agc->button(int(m_model->rx1.agc)))
        b->setChecked(true);
    if (m_syncBtn) {
        m_syncBtn->blockSignals(true);
        m_syncBtn->setChecked(m_model->filterSync);
        m_syncBtn->blockSignals(false);
    }
}

void MainWindow::refreshMeters()
{
    m_meters->setReadings(m_model->meters, m_model->isTransmitting());
}

bool MainWindow::isTypingWidget() const
{
    const QWidget* w = QApplication::focusWidget();
    return qobject_cast<const QAbstractSpinBox*>(w) || qobject_cast<const QLineEdit*>(w)
        || qobject_cast<const QComboBox*>(w) || qobject_cast<const QSlider*>(w);
}

void MainWindow::applyTuneKey(int direction, int stepMul)
{
    const bool vfoB = QApplication::focusWidget() == m_lcdB;
    m_model->tuneBy(direction, std::max(1, m_model->tuneStepHz) * std::max(1, stepMul), vfoB);
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (isTypingWidget()) {
        QMainWindow::keyPressEvent(event);
        return;
    }
    switch (event->key()) {
    case Qt::Key_Up:
    case Qt::Key_Plus:
        applyTuneKey(1);
        break;
    case Qt::Key_Down:
    case Qt::Key_Minus:
        applyTuneKey(-1);
        break;
    case Qt::Key_PageUp:
        applyTuneKey(1, 10);
        break;
    case Qt::Key_PageDown:
        applyTuneKey(-1, 10);
        break;
    default:
        QMainWindow::keyPressEvent(event);
        break;
    }
}

} // namespace brick2
