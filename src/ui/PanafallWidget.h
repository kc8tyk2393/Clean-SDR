#pragma once

#include "core/Types.h"
#include "dsp/DspEngine.h"

#include <QElapsedTimer>
#include <QWidget>

namespace brick2 {

class RadioModel;

class PanafallWidget : public QWidget {
    Q_OBJECT
public:
    explicit PanafallWidget(RadioModel* model, QWidget* parent = nullptr);
    void setFrame(const SpectrumFrame& frame);

signals:
    void clickTune(qint64 hz);
    void filterDrag(int low, int high);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct SpecView {
        int n = 0;
        int span = 1;
        int start = 0;
        int rate = 192000;
        qint64 center = 0;
    };
    SpecView specView() const;
    qint64 hzAt(int x) const;
    int xAtHz(qint64 hz) const;
    QColor waterfallColor(float db) const;
    void scrollWaterfall(const QVector<float>& db);

    RadioModel* m_model;
    SpectrumFrame m_frame;
    QImage m_waterfall;
    QElapsedTimer m_clock;
    qint64 m_lastPanMs = 0;
    qint64 m_lastWfMs = 0;
    bool m_draggingFilter = false;
};

} // namespace brick2
