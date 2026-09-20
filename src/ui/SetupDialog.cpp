#include "ui/SetupDialog.h"

#include "audio/AudioEngine.h"
#include "core/RadioModel.h"

#include <QAudioDevice>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>


namespace brick2 {

SetupDialog::SetupDialog(RadioModel* model, AudioEngine* audio, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Setup — Brick2 / Protocol 2"));
    resize(780, 640);
    auto* tabs = new QTabWidget(this);
    auto* root = new QVBoxLayout(this);
    root->addWidget(tabs);

    auto addForm = [&](const QString& name) {
        auto* w = new QWidget;
        auto* f = new QFormLayout(w);
        tabs->addTab(w, name);
        return f;
    };

    {
        auto* f = addForm(QStringLiteral("General"));
        auto* rate = new QComboBox;
        for (int r : {48000, 96000, 192000, 384000})
            rate->addItem(QString::number(r), r);
        rate->setCurrentIndex(rate->findData(model->sampleRate));
        connect(rate, &QComboBox::currentIndexChanged, this, [=] {
            model->sampleRate = rate->currentData().toInt();
            emit model->stateChanged();
        });
        auto* step = new QComboBox;
        for (int s : {1, 10, 50, 100, 250, 500, 1000, 2500, 5000, 9000})
            step->addItem(QString::number(s) + QStringLiteral(" Hz"), s);
        step->setCurrentIndex(step->findData(model->tuneStepHz));
        connect(step, &QComboBox::currentIndexChanged, this, [=] {
            model->tuneStepHz = step->currentData().toInt();
            emit model->stateChanged();
        });
        auto* apollo = new QCheckBox(QStringLiteral("Apollo SWR/PWR (Brick2)"));
        apollo->setChecked(model->apollo);
        connect(apollo, &QCheckBox::toggled, this, [=](bool v) { model->apollo = v; });
        auto* pa = new QCheckBox(QStringLiteral("Enable PA"));
        pa->setChecked(model->paEnable);
        connect(pa, &QCheckBox::toggled, this, [=](bool v) { model->paEnable = v; });
        auto* rx2 = new QCheckBox(QStringLiteral("Enable RX2"));
        rx2->setChecked(model->rx2Enabled);
        connect(rx2, &QCheckBox::toggled, this, [=](bool v) {
            model->rx2Enabled = v;
            emit model->stateChanged();
        });
        f->addRow(QStringLiteral("Radio model"), new QLabel(QStringLiteral("Brick2 as ANAN-10E / Hermes II")));
        f->addRow(QStringLiteral("Protocol"), new QLabel(QStringLiteral("OpenHPSDR Protocol 2 (UDP)")));
        f->addRow(QStringLiteral("Sample rate"), rate);
        f->addRow(QStringLiteral("Tune step"), step);
        f->addRow(apollo);
        f->addRow(pa);
        f->addRow(rx2);
    }

    {
        auto* f = addForm(QStringLiteral("Audio / VAC"));

        auto fillInputs = [](QComboBox* box, const QString& selected) {
            box->clear();
            box->addItem(QStringLiteral("System default"), QString());
            int idx = 0;
            for (const auto& d : QMediaDevices::audioInputs()) {
                const QString id = QString::fromUtf8(d.id());
                box->addItem(d.description(), id);
                if (!selected.isEmpty() && id == selected)
                    idx = box->count() - 1;
            }
            box->setCurrentIndex(idx);
        };
        auto fillOutputs = [](QComboBox* box, const QString& selected) {
            box->clear();
            box->addItem(QStringLiteral("System default"), QString());
            int idx = 0;
            for (const auto& d : QMediaDevices::audioOutputs()) {
                const QString id = QString::fromUtf8(d.id());
                box->addItem(d.description(), id);
                if (!selected.isEmpty() && id == selected)
                    idx = box->count() - 1;
            }
            box->setCurrentIndex(idx);
        };

        auto* vacEn = new QCheckBox(QStringLiteral("VAC1 enable"));
        vacEn->setChecked(model->vac.vac1);
        connect(vacEn, &QCheckBox::toggled, this, [model, audio](bool v) {
            model->vac.vac1 = v;
            if (audio)
                audio->start();
        });
        f->addRow(vacEn);

        auto* vacIn = new QComboBox;
        vacIn->setMinimumWidth(360);
        fillInputs(vacIn, model->vac.vac1In);
        connect(vacIn, &QComboBox::currentIndexChanged, this, [model, audio, vacIn](int) {
            model->vac.vac1In = vacIn->currentData().toString();
            if (audio && model->vac.vac1)
                audio->start();
        });
        f->addRow(QStringLiteral("VAC1 TX input"), vacIn);

        auto* vacOut = new QComboBox;
        vacOut->setMinimumWidth(360);
        fillOutputs(vacOut, model->vac.vac1Out);
        connect(vacOut, &QComboBox::currentIndexChanged, this, [model, audio, vacOut](int) {
            model->vac.vac1Out = vacOut->currentData().toString();
            if (audio && model->vac.vac1)
                audio->start();
        });
        f->addRow(QStringLiteral("VAC1 RX output"), vacOut);

        auto* stereo = new QCheckBox(QStringLiteral("VAC1 stereo"));
        stereo->setChecked(model->vac.vac1Stereo);
        connect(stereo, &QCheckBox::toggled, this, [model, audio](bool v) {
            model->vac.vac1Stereo = v;
            if (audio && model->vac.vac1)
                audio->start();
        });
        f->addRow(stereo);

        auto* refresh = new QPushButton(QStringLiteral("Refresh devices"));
        connect(refresh, &QPushButton::clicked, this, [model, vacIn, vacOut, fillInputs, fillOutputs] {
            fillInputs(vacIn, model->vac.vac1In);
            fillOutputs(vacOut, model->vac.vac1Out);
        });
        f->addRow(refresh);

        auto* rxGain = new QSlider(Qt::Horizontal);
        rxGain->setRange(-40, 40);
        rxGain->setValue(model->vac.vac1GainRx);
        connect(rxGain, &QSlider::valueChanged, this, [model](int v) { model->vac.vac1GainRx = v; });
        f->addRow(QStringLiteral("VAC1 RX gain dB"), rxGain);

        auto* txGain = new QSlider(Qt::Horizontal);
        txGain->setRange(-40, 40);
        txGain->setValue(model->vac.vac1GainTx);
        connect(txGain, &QSlider::valueChanged, this, [model](int v) { model->vac.vac1GainTx = v; });
        f->addRow(QStringLiteral("VAC1 TX gain dB"), txGain);

        auto* lat = new QSpinBox;
        lat->setRange(20, 500);
        lat->setValue(model->vac.vac1Latency);
        connect(lat, &QSpinBox::valueChanged, this, [model, audio](int v) {
            model->vac.vac1Latency = v;
            if (audio && model->vac.vac1)
                audio->start();
        });
        f->addRow(QStringLiteral("VAC1 latency ms"), lat);

        auto* vac2 = new QCheckBox(QStringLiteral("VAC2 enable"));
        vac2->setChecked(model->vac.vac2);
        connect(vac2, &QCheckBox::toggled, this, [model](bool v) { model->vac.vac2 = v; });
        f->addRow(vac2);
        auto* autoDig = new QCheckBox(QStringLiteral("Auto-enable VAC on DIGU/DIGL"));
        autoDig->setChecked(model->vac.autoEnableDigital);
        connect(autoDig, &QCheckBox::toggled, this, [model](bool v) { model->vac.autoEnableDigital = v; });
        f->addRow(autoDig);
        f->addRow(new QLabel(QStringLiteral(
            "When VAC1 is on, RX audio goes to VAC1 RX output and TX audio is taken from VAC1 TX input.")));
    }

    {
        auto* f = addForm(QStringLiteral("Display"));
        auto* mode = new QComboBox;
        const char* names[] = {"Spectrum", "Panadapter", "Scope", "Phase", "Waterfall",
                               "Histogram", "Panafall", "Panascope", "Off"};
        for (int i = 0; i < 9; ++i)
            mode->addItem(QString::fromLatin1(names[i]), i);
        mode->setCurrentIndex(int(model->display.mode));
        connect(mode, &QComboBox::currentIndexChanged, this, [=](int i) {
            model->display.mode = DisplayMode(i);
        });
        auto* pal = new QComboBox;
        pal->addItems({QStringLiteral("enhanced"), QStringLiteral("classic"), QStringLiteral("speclab")});
        pal->setCurrentText(model->display.palette);
        connect(pal, &QComboBox::currentTextChanged, this, [=](const QString& t) { model->display.palette = t; });
        auto* avg = new QSpinBox;
        avg->setRange(1, 20);
        avg->setValue(model->display.avg);
        connect(avg, &QSpinBox::valueChanged, this, [=](int v) { model->display.avg = v; });
        f->addRow(QStringLiteral("Display mode"), mode);
        f->addRow(QStringLiteral("Waterfall palette"), pal);
        f->addRow(QStringLiteral("Average"), avg);

        auto* panFps = new QSlider(Qt::Horizontal);
        panFps->setRange(5, 60);
        panFps->setValue(model->display.fps);
        auto* panFpsVal = new QLabel(QStringLiteral("%1 fps").arg(model->display.fps));
        connect(panFps, &QSlider::valueChanged, this, [model, panFpsVal](int v) {
            model->display.fps = v;
            panFpsVal->setText(QStringLiteral("%1 fps").arg(v));
        });
        auto* panFpsRow = new QWidget;
        auto* panFpsLay = new QHBoxLayout(panFpsRow);
        panFpsLay->setContentsMargins(0, 0, 0, 0);
        panFpsLay->addWidget(panFps, 1);
        panFpsLay->addWidget(panFpsVal);
        f->addRow(QStringLiteral("Panadapter speed"), panFpsRow);

        auto* wfFps = new QSlider(Qt::Horizontal);
        wfFps->setRange(2, 60);
        wfFps->setValue(model->display.waterfallFps);
        auto* wfFpsVal = new QLabel(QStringLiteral("%1 fps").arg(model->display.waterfallFps));
        connect(wfFps, &QSlider::valueChanged, this, [model, wfFpsVal](int v) {
            model->display.waterfallFps = v;
            wfFpsVal->setText(QStringLiteral("%1 fps").arg(v));
        });
        auto* wfFpsRow = new QWidget;
        auto* wfFpsLay = new QHBoxLayout(wfFpsRow);
        wfFpsLay->setContentsMargins(0, 0, 0, 0);
        wfFpsLay->addWidget(wfFps, 1);
        wfFpsLay->addWidget(wfFpsVal);
        f->addRow(QStringLiteral("Waterfall speed"), wfFpsRow);

        auto colorBtn = [this](QColor* color) {
            auto* b = new QPushButton;
            b->setMinimumHeight(26);
            const auto paint = [b, color] {
                b->setStyleSheet(QStringLiteral("background:%1; border:1px solid #4a5668; min-height:22px;")
                                     .arg(color->name()));
            };
            paint();
            QObject::connect(b, &QPushButton::clicked, this, [this, color, paint, b] {
                const QColor c = QColorDialog::getColor(*color, this, QStringLiteral("Panadapter color"));
                if (c.isValid()) {
                    *color = c;
                    paint();
                }
            });
            return b;
        };
        f->addRow(QStringLiteral("Panadapter line"), colorBtn(&model->display.panLine));
        f->addRow(QStringLiteral("Panadapter fill"), colorBtn(&model->display.panFill));

        auto* fillA = new QSlider(Qt::Horizontal);
        fillA->setRange(0, 255);
        fillA->setValue(model->display.panFillAlpha);
        connect(fillA, &QSlider::valueChanged, this, [model](int v) { model->display.panFillAlpha = v; });
        f->addRow(QStringLiteral("Fill opacity"), fillA);

        auto* bright = new QSlider(Qt::Horizontal);
        bright->setRange(20, 220);
        bright->setValue(model->display.panBrightness);
        connect(bright, &QSlider::valueChanged, this, [model](int v) { model->display.panBrightness = v; });
        f->addRow(QStringLiteral("Panadapter brightness"), bright);

        auto* grad = new QSlider(Qt::Horizontal);
        grad->setRange(30, 250);
        grad->setValue(model->display.panGradient);
        connect(grad, &QSlider::valueChanged, this, [model](int v) { model->display.panGradient = v; });
        f->addRow(QStringLiteral("Gradient (low=contrast, high=soft)"), grad);

        auto* gmid = new QSlider(Qt::Horizontal);
        gmid->setRange(15, 85);
        gmid->setValue(model->display.panGradientMid);
        connect(gmid, &QSlider::valueChanged, this, [model](int v) { model->display.panGradientMid = v; });
        f->addRow(QStringLiteral("Gradient midpoint"), gmid);

        auto* wfB = new QSlider(Qt::Horizontal);
        wfB->setRange(20, 220);
        wfB->setValue(model->display.wfBrightness);
        connect(wfB, &QSlider::valueChanged, this, [model](int v) { model->display.wfBrightness = v; });
        f->addRow(QStringLiteral("Waterfall brightness"), wfB);

        auto* smin = new QSpinBox;
        smin->setRange(-160, -40);
        smin->setValue(model->display.spectrumMin);
        connect(smin, &QSpinBox::valueChanged, this, [model](int v) { model->display.spectrumMin = v; });
        auto* smax = new QSpinBox;
        smax->setRange(-80, 0);
        smax->setValue(model->display.spectrumMax);
        connect(smax, &QSpinBox::valueChanged, this, [model](int v) { model->display.spectrumMax = v; });
        f->addRow(QStringLiteral("Spectrum floor dB"), smin);
        f->addRow(QStringLiteral("Spectrum ceiling dB"), smax);

        auto* grid = new QCheckBox(QStringLiteral("Grid"));
        grid->setChecked(model->display.grid);
        connect(grid, &QCheckBox::toggled, this, [model](bool v) { model->display.grid = v; });
        auto* fill = new QCheckBox(QStringLiteral("Fill panadapter"));
        fill->setChecked(model->display.fill);
        connect(fill, &QCheckBox::toggled, this, [model](bool v) { model->display.fill = v; });
        f->addRow(grid);
        f->addRow(fill);
    }

    {
        auto* f = addForm(QStringLiteral("DSP RX"));
        auto* agc = new QComboBox;
        agc->addItems({QStringLiteral("Off"), QStringLiteral("Long"), QStringLiteral("Slow"),
                       QStringLiteral("Med"), QStringLiteral("Fast"), QStringLiteral("Custom")});
        agc->setCurrentIndex(int(model->rx1.agc));
        connect(agc, &QComboBox::currentIndexChanged, this, [=](int i) { model->rx1.agc = AgcMode(i); });
        auto* hang = new QSpinBox;
        hang->setRange(0, 4000);
        hang->setValue(model->rx1.agcHang);
        connect(hang, &QSpinBox::valueChanged, this, [=](int v) { model->rx1.agcHang = v; });
        f->addRow(QStringLiteral("AGC"), agc);
        f->addRow(QStringLiteral("AGC hang ms"), hang);

        auto* rxBw = new QLabel;
        auto updateRxBw = [model, rxBw] {
            const auto e = model->vfoA.edges;
            rxBw->setText(QStringLiteral("%1   (%2)")
                              .arg(formatBandwidth(std::abs(e.high - e.low)), formatFilterRange(e.low, e.high)));
        };
        auto* rxLo = new QSpinBox;
        rxLo->setRange(-8000, 8000);
        rxLo->setSuffix(QStringLiteral(" Hz"));
        rxLo->setValue(model->vfoA.edges.low);
        auto* rxHi = new QSpinBox;
        rxHi->setRange(-8000, 8000);
        rxHi->setSuffix(QStringLiteral(" Hz"));
        rxHi->setValue(model->vfoA.edges.high);
        const auto applyRx = [model, rxLo, rxHi, updateRxBw] {
            model->applyRxFilter(rxLo->value(), rxHi->value(), true);
            updateRxBw();
        };
        connect(rxLo, &QSpinBox::valueChanged, this, applyRx);
        connect(rxHi, &QSpinBox::valueChanged, this, applyRx);
        updateRxBw();
        f->addRow(QStringLiteral("RX low"), rxLo);
        f->addRow(QStringLiteral("RX high"), rxHi);
        f->addRow(QStringLiteral("RX bandwidth"), rxBw);
        f->addRow(new QLabel(QStringLiteral("NR / NR2 / ANF / NB / NB2 / SNB are on the console DSP bar.")));
    }

    {
        auto* f = addForm(QStringLiteral("Transmit"));
        auto* low = new QSpinBox;
        low->setRange(0, 1000);
        low->setValue(model->tx.txFilterLow);
        auto* high = new QSpinBox;
        high->setRange(1000, 8000);
        high->setValue(model->tx.txFilterHigh);
        auto* cessb = new QCheckBox(QStringLiteral("CESSB"));
        cessb->setChecked(model->tx.cessb);
        connect(cessb, &QCheckBox::toggled, this, [=](bool v) { model->tx.cessb = v; });
        auto* lev = new QCheckBox(QStringLiteral("Leveler"));
        lev->setChecked(model->tx.lev);
        connect(lev, &QCheckBox::toggled, this, [=](bool v) { model->tx.lev = v; });
        auto* cfc = new QCheckBox(QStringLiteral("CFC compressor"));
        cfc->setChecked(model->tx.cfc);
        connect(cfc, &QCheckBox::toggled, this, [=](bool v) { model->tx.cfc = v; });
        auto* tune = new QSpinBox;
        tune->setRange(1, 100);
        tune->setValue(model->tx.tunePercent);
        connect(tune, &QSpinBox::valueChanged, this, [=](int v) { model->tx.tunePercent = v; });
        auto* txBw = new QLabel;
        auto updateTxBw = [model, txBw] {
            const int bw = std::max(0, model->tx.txFilterHigh - model->tx.txFilterLow);
            txBw->setText(QStringLiteral("%1   (%2)")
                              .arg(formatBandwidth(bw),
                                   formatFilterRange(model->tx.txFilterLow, model->tx.txFilterHigh)));
        };
        connect(low, &QSpinBox::valueChanged, this, [=](int v) {
            model->applyTxFilter(v, high->value());
            updateTxBw();
        });
        connect(high, &QSpinBox::valueChanged, this, [=](int v) {
            model->applyTxFilter(low->value(), v);
            updateTxBw();
        });
        updateTxBw();
        f->addRow(QStringLiteral("TX filter low"), low);
        f->addRow(QStringLiteral("TX filter high"), high);
        f->addRow(QStringLiteral("TX bandwidth"), txBw);
        auto* sync = new QCheckBox(QStringLiteral("Sync RX and TX filter size"));
        sync->setChecked(model->filterSync);
        connect(sync, &QCheckBox::toggled, this, [=](bool v) { model->setFilterSync(v); });
        f->addRow(sync);
        f->addRow(cessb);
        f->addRow(lev);
        f->addRow(cfc);
        f->addRow(QStringLiteral("Tune power %"), tune);
    }

    {
        auto* f = addForm(QStringLiteral("CW"));
        auto* wpm = new QSpinBox;
        wpm->setRange(5, 60);
        wpm->setValue(model->cw.wpm);
        connect(wpm, &QSpinBox::valueChanged, this, [=](int v) { model->cw.wpm = v; });
        auto* st = new QSpinBox;
        st->setRange(300, 1000);
        st->setValue(model->cw.sidetone);
        connect(st, &QSpinBox::valueChanged, this, [=](int v) { model->cw.sidetone = v; });
        auto* iambic = new QCheckBox(QStringLiteral("Iambic"));
        iambic->setChecked(model->cw.iambic);
        connect(iambic, &QCheckBox::toggled, this, [=](bool v) { model->cw.iambic = v; });
        auto* modeB = new QCheckBox(QStringLiteral("Mode B"));
        modeB->setChecked(model->cw.modeB);
        connect(modeB, &QCheckBox::toggled, this, [=](bool v) { model->cw.modeB = v; });
        auto* brk = new QCheckBox(QStringLiteral("Semi break-in"));
        brk->setChecked(model->cw.breakIn);
        connect(brk, &QCheckBox::toggled, this, [=](bool v) { model->cw.breakIn = v; });
        auto* delay = new QSpinBox;
        delay->setRange(0, 2000);
        delay->setValue(model->cw.breakInDelay);
        connect(delay, &QSpinBox::valueChanged, this, [=](int v) { model->cw.breakInDelay = v; });
        f->addRow(QStringLiteral("WPM"), wpm);
        f->addRow(QStringLiteral("Sidetone Hz"), st);
        f->addRow(iambic);
        f->addRow(modeB);
        f->addRow(brk);
        f->addRow(QStringLiteral("Break-in delay ms"), delay);
    }

    {
        auto* f = addForm(QStringLiteral("CAT / TCI"));
        auto* en = new QCheckBox(QStringLiteral("Enable CAT TCP server"));
        en->setChecked(model->cat.enabled);
        connect(en, &QCheckBox::toggled, this, [=](bool v) { model->cat.enabled = v; });
        auto* port = new QSpinBox;
        port->setRange(1, 65535);
        port->setValue(model->cat.port);
        connect(port, &QSpinBox::valueChanged, this, [=](int v) { model->cat.port = quint16(v); });
        auto* zz = new QCheckBox(QStringLiteral("Thetis ZZ commands"));
        zz->setChecked(model->cat.thetisZz);
        connect(zz, &QCheckBox::toggled, this, [=](bool v) { model->cat.thetisZz = v; });
        f->addRow(en);
        f->addRow(QStringLiteral("TCP port"), port);
        f->addRow(zz);
        f->addRow(new QLabel(QStringLiteral("Kenwood FA/FB/MD/TX/RX/PC/IF plus ZZFA/ZZMD/ZZDM/ZZTX/ZZSP.")));
    }

    {
        auto* f = addForm(QStringLiteral("PureSignal"));
        auto* en = new QCheckBox(QStringLiteral("Enable PureSignal (feedback on Brick2 PS port)"));
        en->setChecked(model->ps.enabled);
        connect(en, &QCheckBox::toggled, this, [=](bool v) { model->ps.enabled = v; });
        auto* aa = new QCheckBox(QStringLiteral("Auto attenuate"));
        aa->setChecked(model->ps.autoAttenuate);
        connect(aa, &QCheckBox::toggled, this, [=](bool v) { model->ps.autoAttenuate = v; });
        f->addRow(en);
        f->addRow(aa);
        f->addRow(new QLabel(QStringLiteral("Two-tone and AmpView are on the Linearity window.")));
    }

    {
        auto* f = addForm(QStringLiteral("PA / Alex / Apollo"));
        f->addRow(new QLabel(QStringLiteral("Brick2: 15 W PA, 7 LPF, step ATT 0–31 dB, LNA, SWR bridge.")));
        f->addRow(new QLabel(QStringLiteral("Alex filter words follow Hermes / ANAN-10E Protocol 2 mapping.")));
        f->addRow(new QLabel(QStringLiteral("External PA: BCD / Xiegu band voltage / Hardrock UART are hardware pins.")));
        auto* att = new QSpinBox;
        att->setRange(0, 31);
        att->setValue(model->rx1.stepAtt);
        connect(att, &QSpinBox::valueChanged, this, [=](int v) { model->rx1.stepAtt = v; });
        f->addRow(QStringLiteral("RX step attenuator dB"), att);
        auto* lna = new QCheckBox(QStringLiteral("LNA"));
        lna->setChecked(model->rx1.lna);
        connect(lna, &QCheckBox::toggled, this, [=](bool v) { model->rx1.lna = v; });
        f->addRow(lna);
    }
}

} // namespace brick2
