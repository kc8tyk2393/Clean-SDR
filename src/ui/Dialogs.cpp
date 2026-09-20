#include "ui/Dialogs.h"

#include "core/RadioModel.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace brick2 {

MemoryDialog::MemoryDialog(RadioModel* model, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Memories"));
    resize(420, 360);
    auto* lay = new QVBoxLayout(this);
    auto* list = new QListWidget;
    auto refresh = [=] {
        list->clear();
        for (const auto& m : model->memories)
            list->addItem(QStringLiteral("%1  %2  %3").arg(m.name, formatFrequency(m.frequency), modeName(m.mode)));
    };
    refresh();
    auto* name = new QLineEdit(QStringLiteral("CH"));
    auto* add = new QPushButton(QStringLiteral("Store VFO A"));
    connect(add, &QPushButton::clicked, this, [=] {
        MemoryChannel ch;
        ch.name = name->text();
        ch.frequency = model->vfoA.frequency;
        ch.mode = model->vfoA.mode;
        ch.filter = model->vfoA.filter;
        ch.band = model->vfoA.band;
        model->memories.push_back(ch);
        refresh();
    });
    auto* rec = new QPushButton(QStringLiteral("Recall"));
    connect(rec, &QPushButton::clicked, this, [=] {
        const int i = list->currentRow();
        if (i < 0 || i >= model->memories.size())
            return;
        const auto& m = model->memories[i];
        model->vfoA.frequency = m.frequency;
        model->vfoA.mode = m.mode;
        model->vfoA.filter = m.filter;
        model->vfoA.band = m.band;
        model->vfoA.edges = defaultFilter(m.mode, m.filter);
        emit model->frequencyChanged();
        emit model->modeChanged();
    });
    lay->addWidget(list);
    lay->addWidget(name);
    auto* row = new QHBoxLayout;
    row->addWidget(add);
    row->addWidget(rec);
    lay->addLayout(row);
}

EqualizerDialog::EqualizerDialog(RadioModel* model, bool tx, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tx ? QStringLiteral("TX Equalizer") : QStringLiteral("RX Equalizer"));
    auto* lay = new QHBoxLayout(this);
    static const int freqs[] = {32, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    auto& bands = tx ? model->tx.eq : model->rx1.rxEq;
    auto& on = tx ? model->tx.eqOn : model->rx1.rxEqOn;
    auto* enable = new QPushButton(QStringLiteral("On"));
    enable->setCheckable(true);
    enable->setChecked(on);
    connect(enable, &QPushButton::toggled, this, [&on](bool v) { on = v; });
    lay->addWidget(enable);
    for (int i = 0; i < 10; ++i) {
        auto* col = new QVBoxLayout;
        auto* s = new QSlider(Qt::Vertical);
        s->setRange(-12, 12);
        s->setValue(int(bands[i]));
        auto* lab = new QLabel(QString::number(freqs[i]));
        connect(s, &QSlider::valueChanged, this, [&bands, i](int v) { bands[i] = v; });
        col->addWidget(s);
        col->addWidget(lab);
        lay->addLayout(col);
    }
}

PureSignalDialog::PureSignalDialog(RadioModel* model, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Linearity — PureSignal"));
    auto* lay = new QVBoxLayout(this);
    auto* en = new QPushButton(QStringLiteral("PureSignal On"));
    en->setCheckable(true);
    en->setChecked(model->ps.enabled);
    connect(en, &QPushButton::toggled, this, [=](bool v) { model->ps.enabled = v; });
    auto* two = new QPushButton(QStringLiteral("Two Tone"));
    two->setCheckable(true);
    two->setChecked(model->ps.twoTone);
    connect(two, &QPushButton::toggled, this, [=](bool v) {
        model->ps.twoTone = v;
        if (v)
            model->tune = true;
        emit model->transmitChanged();
    });
    auto* info = new QLabel(QStringLiteral("Use the Brick2 feedback SMA for PA sample.\nAmpView updates while transmitting."));
    info->setWordWrap(true);
    lay->addWidget(en);
    lay->addWidget(two);
    lay->addWidget(info);
}

CwxDialog::CwxDialog(RadioModel* model, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("CWX"));
    resize(480, 240);
    auto* lay = new QVBoxLayout(this);
    m_edit = new QPlainTextEdit(model->cw.cwxBuffer);
    lay->addWidget(new QLabel(QStringLiteral("Keyboard CW — sent at the configured WPM when MOX/CW is active.")));
    lay->addWidget(m_edit);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, this, [=] {
        model->cw.cwxBuffer = m_edit->toPlainText();
        accept();
    });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(box);
}

QString CwxDialog::text() const
{
    return m_edit->toPlainText();
}

} // namespace brick2
