#pragma once

#include "core/Types.h"
#include "dsp/DspEngine.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>

namespace brick2 {

class RadioModel;
class Protocol2Engine;
class DspEngine;
class AudioEngine;
class CatEngine;
class PanafallWidget;
class LcdVfo;
class MeterRack;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void buildUi();
    void buildMenus();
    void refreshVfo();
    void refreshMeters();
    void refreshSelection();
    void setMode(Mode m);
    void applyTransmit();
    void applyRxFromUi();
    void applyTxFromUi();
    void syncFilterSpins();
    void applyTuneKey(int direction, int stepMul = 1);
    bool isTypingWidget() const;
    QSpinBox* filterSpin(int minv, int maxv, int value);
    QPushButton* key(const QString& text);
    QPushButton* latch(const QString& text, bool* flag, const char* role = "dsp");
    QWidget* sliderColumn(const QString& name, int minv, int maxv, int* value);

    RadioModel* m_model;
    Protocol2Engine* m_proto;
    DspEngine* m_dsp;
    AudioEngine* m_audio;
    CatEngine* m_cat;
    PanafallWidget* m_display = nullptr;
    LcdVfo* m_lcdA = nullptr;
    LcdVfo* m_lcdB = nullptr;
    MeterRack* m_meters = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_link = nullptr;
    QComboBox* m_radioBox = nullptr;
    QLineEdit* m_ipEdit = nullptr;
    QPushButton* m_moxBtn = nullptr;
    QPushButton* m_voxBtn = nullptr;
    QPushButton* m_twoToneBtn = nullptr;
    QLabel* m_keyStatus = nullptr;
    QLabel* m_rxBw = nullptr;
    QLabel* m_txBw = nullptr;
    QSpinBox* m_rxLow = nullptr;
    QSpinBox* m_rxHigh = nullptr;
    QSpinBox* m_txLow = nullptr;
    QSpinBox* m_txHigh = nullptr;
    QButtonGroup* m_bands = nullptr;
    QButtonGroup* m_modes = nullptr;
    QButtonGroup* m_filters = nullptr;
    QButtonGroup* m_agc = nullptr;
    QPushButton* m_syncBtn = nullptr;
};

} // namespace brick2
