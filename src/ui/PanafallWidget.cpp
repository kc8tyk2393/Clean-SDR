#include "ui/PanafallWidget.h"

#include "core/RadioModel.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace brick2 {

PanafallWidget::PanafallWidget(RadioModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
{
    setMinimumHeight(280);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setAutoFillBackground(false);
    m_clock.start();
}

void PanafallWidget::setFrame(const SpectrumFrame& frame)
{
    if (!m_clock.isValid())
        m_clock.start();
    const qint64 now = m_clock.elapsed();
    const int panMs = std::max(8, 1000 / std::clamp(m_model->display.fps, 5, 60));
    const int wfMs = std::max(8, 1000 / std::clamp(m_model->display.waterfallFps, 2, 60));
    bool dirty = false;
    if (now - m_lastPanMs >= panMs) {
        m_frame = frame;
        m_lastPanMs = now;
        dirty = true;
    } else {
        m_frame.sMeter = frame.sMeter;
        m_frame.audio = frame.audio;
        m_frame.scope = frame.scope;
        if (m_frame.db.isEmpty())
            m_frame.db = frame.db;
    }
    if (now - m_lastWfMs >= wfMs) {
        scrollWaterfall(frame.db);
        m_lastWfMs = now;
        dirty = true;
    }
    if (dirty)
        update();
}

void PanafallWidget::scrollWaterfall(const QVector<float>& db)
{
    const int wfH = std::max(80, height() / 2);
    if (m_waterfall.width() != width() || m_waterfall.height() != wfH) {
        m_waterfall = QImage(std::max(1, width()), wfH, QImage::Format_RGB32);
        m_waterfall.fill(QColor(7, 8, 10));
    }
    if (db.isEmpty() || m_waterfall.isNull())
        return;
    memmove(m_waterfall.scanLine(1), m_waterfall.scanLine(0),
            qsizetype(m_waterfall.bytesPerLine()) * (m_waterfall.height() - 1));
    auto* line = reinterpret_cast<QRgb*>(m_waterfall.scanLine(0));
    const int w = m_waterfall.width();
    const int n = std::max(1, int(db.size()));
    const double zoom = std::max(1, m_model->display.zoom);
    const int span = std::max(1, int(n / zoom));
    const int slack = std::max(0, n - span);
    const int start = std::clamp(int(slack * m_model->display.pan), 0, slack);
    for (int x = 0; x < w; ++x) {
        const int i = start + int(double(x) / w * span);
        line[x] = waterfallColor(db[std::clamp(i, 0, n - 1)]).rgb();
    }
}

void PanafallWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(7, 8, 10));

    const bool panafall = m_model->display.mode == DisplayMode::Panafall
        || m_model->display.mode == DisplayMode::Panascope;
    const int panH = (m_model->display.mode == DisplayMode::Waterfall) ? 0
                     : panafall ? int(height() * 0.52) : height();
    const int scaleH = 18;
    const int plotH = std::max(0, panH - scaleH);
    const int wfY = panH;

    if (!m_frame.db.isEmpty() && plotH > 0
        && m_model->display.mode != DisplayMode::Off
        && m_model->display.mode != DisplayMode::Waterfall) {
        const auto v = specView();
        const float mn = float(m_model->display.spectrumMin);
        const float mx = float(m_model->display.spectrumMax);
        const qint64 cf = v.center;
        const auto hzToX = [&](qint64 hz) { return xAtHz(hz); };

        if (m_model->display.grid) {
            p.setFont(QFont(QStringLiteral("Segoe UI"), 8));
            for (int db = -140; db <= -20; db += 20) {
                const float y = (1.f - (float(db) - mn) / (mx - mn)) * float(plotH);
                p.setPen(QPen(QColor(36, 44, 54), 1, Qt::DotLine));
                p.drawLine(0, int(y), width(), int(y));
                p.setPen(QColor(90, 102, 116));
                p.drawText(8, int(y) - 3, QString::number(db));
            }
            const double bw = double(v.rate) * v.span / std::max(1, v.n);
            const double step = bw > 200000 ? 50000 : bw > 80000 ? 20000 : 10000;
            const qint64 first = qint64(std::floor((cf - bw / 2) / step) * step);
            for (qint64 f = first; f < cf + bw / 2; f += qint64(step)) {
                const int x = hzToX(f);
                p.setPen(QPen(QColor(36, 44, 54), 1));
                p.drawLine(x, 0, x, plotH);
            }
        }

        const auto pass = remapFilter(m_model->vfoA.edges, m_model->vfoA.mode);
        const int x0 = hzToX(cf + pass.low);
        const int x1 = hzToX(cf + pass.high);
        QColor filt = m_model->display.panFill;
        filt.setAlpha(55);
        p.fillRect(QRect(std::min(x0, x1), 0, std::max(2, std::abs(x1 - x0)), plotH), filt);
        QColor filtEdge = m_model->display.panFill;
        filtEdge.setAlpha(200);
        p.setPen(QPen(filtEdge, 1.5));
        p.drawLine(x0, 0, x0, plotH);
        p.drawLine(x1, 0, x1, plotH);

        QPolygonF poly;
        poly << QPointF(0, plotH);
        for (int x = 0; x < width(); ++x) {
            const int i = v.start + int(double(x) / width() * v.span);
            const float db = m_frame.db[std::clamp(i, 0, v.n - 1)];
            const float y = (1.f - (db - mn) / (mx - mn)) * float(plotH);
            poly << QPointF(x, std::clamp(y, 0.f, float(plotH)));
        }
        poly << QPointF(width(), plotH);
        const float bright = std::clamp(m_model->display.panBrightness, 10, 250) / 100.f;
        const float grad = std::clamp(m_model->display.panGradient, 20, 300) / 100.f;
        const float mid = std::clamp(m_model->display.panGradientMid, 10, 90) / 100.f;
        auto shade = [&](int alpha) {
            QColor c = m_model->display.panFill;
            c.setRed(std::clamp(int(c.red() * bright), 0, 255));
            c.setGreen(std::clamp(int(c.green() * bright), 0, 255));
            c.setBlue(std::clamp(int(c.blue() * bright), 0, 255));
            c.setAlpha(std::clamp(int(alpha * std::pow(0.5, 1.0 / grad) * 2.0), 0, 255));
            return c;
        };
        if (m_model->display.fill) {
            const int a = std::clamp(m_model->display.panFillAlpha, 0, 255);
            QLinearGradient g(0, 0, 0, plotH);
            g.setColorAt(0, shade(a));
            g.setColorAt(std::clamp(mid, 0.05f, 0.95f), shade(a / 3));
            g.setColorAt(1, shade(std::max(6, a / 18)));
            p.setBrush(g);
        } else {
            p.setBrush(Qt::NoBrush);
        }
        QColor line = m_model->display.panLine;
        line.setRed(std::clamp(int(line.red() * bright), 0, 255));
        line.setGreen(std::clamp(int(line.green() * bright), 0, 255));
        line.setBlue(std::clamp(int(line.blue() * bright), 0, 255));
        p.setPen(QPen(line, 1.25));
        p.drawPolygon(poly);

        const int xc = hzToX(cf);
        p.setPen(QPen(QColor(240, 180, 41), 1.4));
        p.drawLine(xc, 0, xc, plotH);

        p.fillRect(0, plotH, width(), scaleH, QColor(10, 12, 16));
        p.setPen(QColor(139, 149, 161));
        p.setFont(QFont(QStringLiteral("Segoe UI"), 8));
        const double bw = double(v.rate) * v.span / std::max(1, v.n);
        const double step = bw > 200000 ? 50000 : bw > 80000 ? 20000 : 10000;
        const qint64 first = qint64(std::floor((cf - bw / 2) / step) * step);
        for (qint64 f = first; f < cf + bw / 2; f += qint64(step)) {
            const int x = hzToX(f);
            p.drawText(x + 4, plotH + 13, QString::number(f / 1000.0, 'f', 1));
        }
    }

    if ((panafall || m_model->display.mode == DisplayMode::Waterfall) && !m_waterfall.isNull()) {
        p.drawImage(QRect(0, wfY, width(), height() - wfY), m_waterfall);
        p.fillRect(0, wfY, width(), 1, QColor(36, 44, 54));
    }

    if (m_model->display.mode == DisplayMode::Scope && !m_frame.scope.isEmpty()) {
        p.setPen(QPen(QColor(61, 214, 140), 1.2));
        QPolygonF s;
        for (int i = 0; i < m_frame.scope.size(); ++i) {
            const float x = float(i) / m_frame.scope.size() * width();
            const float y = height() / 2.f - m_frame.scope[i] * height() / 3.f;
            s << QPointF(x, y);
        }
        p.drawPolyline(s);
    }

    const auto e = remapFilter(m_model->vfoA.edges, m_model->vfoA.mode);
    const int rxHz = std::abs(e.high - e.low);
    const int txHz = std::max(0, m_model->tx.txFilterHigh - m_model->tx.txFilterLow);
    p.setPen(QColor(240, 180, 41));
    p.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
    p.drawText(12, 16,
               QStringLiteral("%1 Hz   %2   zoom %3×")
                   .arg(m_model->sampleRate)
                   .arg(modeName(m_model->vfoA.mode))
                   .arg(m_model->display.zoom));
    p.setPen(QColor(126, 230, 230));
    p.drawText(12, 32,
               QStringLiteral("RX %1  (%2)     TX %3  (%4)")
                   .arg(formatBandwidth(rxHz), formatFilterRange(e.low, e.high), formatBandwidth(txHz),
                        formatFilterRange(m_model->tx.txFilterLow, m_model->tx.txFilterHigh)));
}

void PanafallWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    emit clickTune(hzAt(int(event->position().x())));
}

void PanafallWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::RightButton)
        emit clickTune(hzAt(int(event->position().x())));
}

void PanafallWidget::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        m_model->display.zoom = std::clamp(m_model->display.zoom + (event->angleDelta().y() > 0 ? 1 : -1), 1, 16);
        update();
        return;
    }
    const int dir = event->angleDelta().y() > 0 ? 1 : -1;
    m_model->tuneBy(dir);
}

void PanafallWidget::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Up:
    case Qt::Key_Plus:
        m_model->tuneBy(1);
        break;
    case Qt::Key_Down:
    case Qt::Key_Minus:
        m_model->tuneBy(-1);
        break;
    case Qt::Key_PageUp:
        m_model->tuneBy(1, std::max(1, m_model->tuneStepHz) * 10);
        break;
    case Qt::Key_PageDown:
        m_model->tuneBy(-1, std::max(1, m_model->tuneStepHz) * 10);
        break;
    default:
        QWidget::keyPressEvent(event);
        break;
    }
}

void PanafallWidget::resizeEvent(QResizeEvent*)
{
    m_waterfall = QImage();
}

PanafallWidget::SpecView PanafallWidget::specView() const
{
    SpecView v;
    v.n = std::max(1, int(m_frame.db.size()));
    v.rate = std::max(1, m_model->sampleRate);
    v.center = m_model->vfoA.frequency;
    const double zoom = std::max(1, m_model->display.zoom);
    v.span = std::max(1, int(v.n / zoom));
    const int slack = std::max(0, v.n - v.span);
    v.start = std::clamp(int(slack * m_model->display.pan), 0, slack);
    return v;
}

qint64 PanafallWidget::hzAt(int x) const
{
    const auto v = specView();
    const double t = double(x) / std::max(1, width());
    const double bin = v.start + t * v.span;
    return v.center + qint64(std::llround((bin - v.n / 2.0) * v.rate / v.n));
}

int PanafallWidget::xAtHz(qint64 hz) const
{
    const auto v = specView();
    const double bin = v.n / 2.0 + double(hz - v.center) * v.n / v.rate;
    return int(std::lround((bin - v.start) / v.span * width()));
}

QColor PanafallWidget::waterfallColor(float db) const
{
    const float mn = float(m_model->display.waterfallMin);
    const float mx = float(m_model->display.waterfallMax);
    float t = std::clamp((db - mn) / (mx - mn), 0.f, 1.f);
    const float grad = std::clamp(m_model->display.panGradient, 20, 300) / 100.f;
    t = std::pow(t, 1.f / grad);
    const float bright = std::clamp(m_model->display.wfBrightness, 10, 250) / 100.f;
    auto lit = [&](QColor c) {
        int h, s, v, a;
        c.getHsv(&h, &s, &v, &a);
        v = std::clamp(int(v * bright), 0, 255);
        c.setHsv(h, s, v, a);
        return c;
    };
    if (m_model->display.palette == QStringLiteral("classic"))
        return lit(QColor::fromHsvF(0.66 * (1.f - t), 0.85, 0.18 + 0.82 * t));
    if (m_model->display.palette == QStringLiteral("speclab"))
        return lit(QColor::fromRgbF(t, t * t, std::sqrt(t)));
    const float r = std::clamp(-0.15f + 2.1f * t, 0.f, 1.f);
    const float g = std::clamp(-0.4f + 1.8f * t, 0.f, 0.85f);
    const float b = std::clamp(0.12f + 0.35f * (1.f - t) + 0.7f * t * t, 0.f, 1.f);
    return lit(QColor::fromRgbF(r, g, b));
}

} // namespace brick2
