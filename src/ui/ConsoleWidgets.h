#pragma once

#include "core/Types.h"
#include "core/RadioModel.h"

#include <QFrame>
#include <QWidget>

namespace brick2 {

class LcdVfo : public QWidget {
    Q_OBJECT
public:
    explicit LcdVfo(const QString& title, QWidget* parent = nullptr);
    void setState(const VfoState& vfo, bool split, bool tx);
    qint64 selectedStepHz() const;

signals:
    void tuned(qint64 frequencyHz);
    void stepSelected(qint64 stepHz);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    static constexpr int kDigitCount = 8;
    void layoutDigits();
    int digitAt(const QPoint& pos) const;
    qint64 stepForDigit(int index) const;
    void nudge(int direction);
    void selectDigit(int index);

    QString m_title;
    VfoState m_vfo;
    bool m_split = false;
    bool m_tx = false;
    int m_selected = 4; // 1 kHz
    int m_hover = -1;
    QRect m_digitRects[kDigitCount];
    QRect m_upRects[kDigitCount];
    QRect m_dnRects[kDigitCount];
};

class MeterRack : public QWidget {
    Q_OBJECT
public:
    explicit MeterRack(QWidget* parent = nullptr);
    void setReadings(const MeterReadings& m, bool mox);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    MeterReadings m_m;
    bool m_mox = false;
};

QFrame* makePanel(QWidget* parent, const QString& name = QStringLiteral("panel"));

} // namespace brick2
