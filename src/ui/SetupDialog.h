#pragma once

#include <QDialog>

namespace brick2 {

class RadioModel;
class AudioEngine;

class SetupDialog : public QDialog {
    Q_OBJECT
public:
    explicit SetupDialog(RadioModel* model, AudioEngine* audio, QWidget* parent = nullptr);
};

} // namespace brick2
