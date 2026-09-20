#include "ui/ConsoleWidgets.h"

#include <QEvent>
#include <QFrame>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>

namespace brick2 {
namespace {

QFont lcdFont(int px, bool bold = true)
{
    QFont f(QStringLiteral("Consolas"));
    if (f.exactMatch() == false)
        f = QFont(QStringLiteral("Cascadia Mono"));
    f.setPixelSize(px);
    f.setBold(bold);
    f.setStyleHint(QFont::Monospace);
    return f;
}

void drawBar(QPainter& p, const QRect& r, float t, const QColor& hi)
{
    t = std::clamp(t, 0.f, 1.f);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 24, 30));
    p.drawRoundedRect(r, 3, 3);
    if (t <= 0.001f)
        return;
    QRect fill(r.x() + 1, r.y() + 1, int((r.width() - 2) * t), r.height() - 2);
    QLinearGradient g(fill.topLeft(), fill.topRight());
    g.setColorAt(0.0, QColor(46, 196, 132));
    g.setColorAt(0.55, QColor(240, 180, 41));
    g.setColorAt(1.0, hi);
    p.setBrush(g);
    p.drawRoundedRect(fill, 2, 2);
}

} // namespace

LcdVfo::LcdVfo(const QString& title, QWidget* parent)
    : QWidget(parent)
    , m_title(title)
{
    setMinimumSize(340, 132);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::ArrowCursor);
}

void LcdVfo::setState(const VfoState& vfo, bool split, bool tx)
{
    m_vfo = vfo;
    m_split = split;
    m_tx = tx;
    update();
}

qint64 LcdVfo::selectedStepHz() const
{
    return stepForDigit(m_selected);
}

qint64 LcdVfo::stepForDigit(int index) const
{
    static const qint64 steps[kDigitCount] = {
        10'000'000, 1'000'000, 100'000, 10'000, 1'000, 100, 10, 1
    };
    if (index < 0 || index >= kDigitCount)
        return 1;
    return steps[index];
}

void LcdVfo::layoutDigits()
{
    const QFont f = lcdFont(36);
    QFontMetrics fm(f);
    const int digitW = std::max(22, fm.horizontalAdvance(QLatin1Char('0')));
    const int gap = 3;
    const int dotW = std::max(10, fm.horizontalAdvance(QLatin1Char('.')));
    const int y = 28;
    const int h = 48;
    int x = 18;
    for (int i = 0; i < kDigitCount; ++i) {
        if (i == 2 || i == 5)
            x += dotW;
        m_digitRects[i] = QRect(x, y, digitW, h);
        m_upRects[i] = QRect(x, y - 12, digitW, 14);
        m_dnRects[i] = QRect(x, y + h - 2, digitW, 14);
        x += digitW + gap;
    }
}

int LcdVfo::digitAt(const QPoint& pos) const
{
    for (int i = 0; i < kDigitCount; ++i) {
        QRect hit = m_digitRects[i].adjusted(-2, -14, 2, 16);
        if (hit.contains(pos))
            return i;
    }
    return -1;
}

void LcdVfo::selectDigit(int index)
{
    if (index < 0 || index >= kDigitCount)
        return;
    m_selected = index;
    emit stepSelected(stepForDigit(index));
    update();
}

void LcdVfo::nudge(int direction)
{
    if (direction == 0)
        return;
    const qint64 next = std::clamp(m_vfo.frequency + direction * stepForDigit(m_selected),
                                   100000LL, 61000000LL);
    m_vfo.frequency = next;
    emit tuned(next);
    update();
}

void LcdVfo::paintEvent(QPaintEvent*)
{
    layoutDigits();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    QPainterPath path;
    path.addRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);
    p.fillPath(path, QColor(8, 10, 13));
    p.setPen(QPen(QColor(42, 51, 64), 1));
    p.drawPath(path);

    QLinearGradient glass(0, 0, 0, height());
    glass.setColorAt(0, QColor(255, 255, 255, 14));
    glass.setColorAt(0.35, QColor(255, 255, 255, 0));
    p.fillPath(path, glass);

    const QColor amber = m_tx ? QColor(255, 92, 76) : QColor(240, 180, 41);
    p.setPen(QColor(111, 123, 136));
    p.setFont(QFont(QStringLiteral("Segoe UI"), 8, QFont::DemiBold));
    p.drawText(16, 16, m_title.toUpper());

    const qint64 step = stepForDigit(m_selected);
    QString stepLabel = (step >= 1000000) ? QStringLiteral("%1 MHz").arg(step / 1000000)
                      : (step >= 1000)    ? QStringLiteral("%1 kHz").arg(step / 1000)
                                          : QStringLiteral("%1 Hz").arg(step);
    p.setPen(QColor(62, 200, 200));
    p.drawText(QRect(0, 6, width() - 16, 16), Qt::AlignRight, stepLabel);

    if (m_split) {
        p.setPen(QColor(255, 92, 76));
        p.drawText(width() / 2 - 20, 16, QStringLiteral("SPLIT"));
    }

    qint64 hz = m_vfo.frequency;
    int digits[kDigitCount];
    for (int i = kDigitCount - 1; i >= 0; --i) {
        digits[i] = int(hz % 10);
        hz /= 10;
    }

    p.setFont(lcdFont(36));
    for (int i = 0; i < kDigitCount; ++i) {
        const QRect r = m_digitRects[i];
        if (i == m_selected) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(240, 180, 41, 28));
            p.drawRoundedRect(r.adjusted(-3, -4, 3, 4), 4, 4);
            p.setBrush(amber);
            QPolygon up, dn;
            const int cx = r.center().x();
            up << QPoint(cx, r.top() - 8) << QPoint(cx - 6, r.top() + 2) << QPoint(cx + 6, r.top() + 2);
            dn << QPoint(cx, r.bottom() + 10) << QPoint(cx - 6, r.bottom()) << QPoint(cx + 6, r.bottom());
            p.drawPolygon(up);
            p.drawPolygon(dn);
        } else if (i == m_hover) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 18));
            p.drawRoundedRect(r.adjusted(-2, -2, 2, 2), 3, 3);
        }
        p.setPen(amber);
        p.drawText(r, Qt::AlignCenter, QString::number(digits[i]));
        if (i == 1 || i == 4) {
            p.setPen(QColor(amber.red(), amber.green(), amber.blue(), 160));
            p.drawText(QRect(r.right() + 1, r.top(), 12, r.height()), Qt::AlignCenter, QStringLiteral("."));
        }
    }

    p.setFont(QFont(QStringLiteral("Segoe UI"), 8, QFont::Bold));
    const auto pill = [&](int x, const QString& text, const QColor& bg, const QColor& fg) {
        const int w = 54;
        QRect r(x, height() - 22, w, 16);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(r, 3, 3);
        p.setPen(fg);
        p.drawText(r, Qt::AlignCenter, text);
        return x + w + 6;
    };
    int x = 16;
    x = pill(x, modeName(m_vfo.mode), QColor(30, 48, 52), QColor(62, 200, 200));
    QString bname = QStringLiteral("GEN");
    for (const auto& b : bandTable()) {
        if (b.band == m_vfo.band) {
            bname = b.name;
            break;
        }
    }
    x = pill(x, bname, QColor(36, 32, 18), amber);
    pill(x, QStringLiteral("click digit  ·  wheel / ▲▼"), QColor(22, 26, 32), QColor(139, 149, 161));
}

void LcdVfo::wheelEvent(QWheelEvent* event)
{
    const int d = digitAt(event->position().toPoint());
    if (d >= 0)
        selectDigit(d);
    nudge(event->angleDelta().y() > 0 ? 1 : -1);
}

void LcdVfo::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    const QPoint pos = event->pos();
    const int d = digitAt(pos);
    if (d < 0)
        return;
    selectDigit(d);
    if (m_upRects[d].adjusted(-4, -2, 4, 4).contains(pos) || pos.y() < m_digitRects[d].center().y() - 4)
        nudge(1);
    else if (m_dnRects[d].adjusted(-4, -4, 4, 2).contains(pos) || pos.y() > m_digitRects[d].center().y() + 4)
        nudge(-1);
}

void LcdVfo::mouseMoveEvent(QMouseEvent* event)
{
    const int d = digitAt(event->pos());
    if (d != m_hover) {
        m_hover = d;
        setCursor(d >= 0 ? Qt::SizeVerCursor : Qt::ArrowCursor);
        update();
    }
}

void LcdVfo::leaveEvent(QEvent*)
{
    m_hover = -1;
    setCursor(Qt::ArrowCursor);
    update();
}

void LcdVfo::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Left:
        selectDigit(std::max(0, m_selected - 1));
        break;
    case Qt::Key_Right:
        selectDigit(std::min(kDigitCount - 1, m_selected + 1));
        break;
    case Qt::Key_Up:
    case Qt::Key_Plus:
        nudge(1);
        break;
    case Qt::Key_Down:
    case Qt::Key_Minus:
        nudge(-1);
        break;
    default:
        QWidget::keyPressEvent(event);
        break;
    }
}

MeterRack::MeterRack(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(280, 118);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void MeterRack::setReadings(const MeterReadings& m, bool mox)
{
    m_m = m;
    m_mox = mox;
    update();
}

void MeterRack::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);
    p.fillPath(path, QColor(18, 22, 28));
    p.setPen(QPen(QColor(36, 44, 54), 1));
    p.drawPath(path);

    const auto row = [&](int y, const QString& name, float t, const QString& val) {
        p.setPen(QColor(111, 123, 136));
        p.setFont(QFont(QStringLiteral("Segoe UI"), 7, QFont::Bold));
        p.drawText(12, y + 10, name);
        drawBar(p, QRect(52, y + 2, width() - 128, 10), t, QColor(226, 61, 61));
        p.setPen(QColor(213, 221, 230));
        p.setFont(lcdFont(11, false));
        p.drawText(QRect(width() - 72, y - 1, 64, 16), Qt::AlignRight | Qt::AlignVCenter, val);
    };

    const float s = std::clamp((m_m.sMeter + 140.f) / 90.f, 0.f, 1.f);
    const float pwr = std::clamp(m_m.fwdWatts / 15.f, 0.f, 1.f);
    const float swr = std::clamp((m_m.swr - 1.f) / 2.f, 0.f, 1.f);
    const float alc = std::clamp(-m_m.alc / 20.f, 0.f, 1.f);

    row(10, QStringLiteral("SIG"), s, QStringLiteral("%1 dBm").arg(m_m.sMeter, 0, 'f', 0));
    row(32, QStringLiteral("PWR"), pwr, QStringLiteral("%1 W").arg(m_m.fwdWatts, 0, 'f', 1));
    row(54, QStringLiteral("SWR"), swr, QStringLiteral("%1 : 1").arg(m_m.swr, 0, 'f', 2));
    row(76, QStringLiteral("ALC"), alc, QStringLiteral("%1 dB").arg(m_m.alc, 0, 'f', 1));

    const auto led = [&](int x, const QString& name, bool on, const QColor& c) {
        p.setPen(Qt::NoPen);
        p.setBrush(on ? c : QColor(40, 46, 54));
        p.drawEllipse(QPoint(x, height() - 12), 4, 4);
        p.setPen(on ? c : QColor(111, 123, 136));
        p.setFont(QFont(QStringLiteral("Segoe UI"), 7, QFont::Bold));
        p.drawText(x + 8, height() - 8, name);
    };
    led(16, QStringLiteral("PTT"), m_m.ptt || m_mox, QColor(226, 61, 61));
    led(70, QStringLiteral("ADC"), !m_m.adcOverload, m_m.adcOverload ? QColor(226, 61, 61) : QColor(61, 214, 140));
    led(128, QStringLiteral("13.8V"), m_m.supplyVolts > 11.f, QColor(62, 200, 200));
    p.setPen(QColor(139, 149, 161));
    p.drawText(width() - 84, height() - 8, QStringLiteral("%1 V").arg(m_m.supplyVolts, 0, 'f', 1));
}

QFrame* makePanel(QWidget* parent, const QString& name)
{
    auto* f = new QFrame(parent);
    f->setObjectName(name);
    return f;
}

} // namespace brick2
