#pragma once

#include <QDialog>
#include <QPlainTextEdit>

namespace brick2 {

class RadioModel;

class MemoryDialog : public QDialog {
    Q_OBJECT
public:
    explicit MemoryDialog(RadioModel* model, QWidget* parent = nullptr);
};

class EqualizerDialog : public QDialog {
    Q_OBJECT
public:
    explicit EqualizerDialog(RadioModel* model, bool tx, QWidget* parent = nullptr);
};

class PureSignalDialog : public QDialog {
    Q_OBJECT
public:
    explicit PureSignalDialog(RadioModel* model, QWidget* parent = nullptr);
};

class CwxDialog : public QDialog {
    Q_OBJECT
public:
    explicit CwxDialog(RadioModel* model, QWidget* parent = nullptr);
    QString text() const;

private:
    QPlainTextEdit* m_edit;
};

} // namespace brick2
