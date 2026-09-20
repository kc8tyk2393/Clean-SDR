#pragma once

#include "core/Types.h"

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

namespace brick2 {

class RadioModel;

class CatEngine : public QObject {
    Q_OBJECT
public:
    explicit CatEngine(RadioModel* model, QObject* parent = nullptr);
    void start();
    void stop();

private:
    void onNewConnection();
    void onReadyRead();
    QByteArray handle(const QByteArray& cmd);
    QByteArray zz(const QByteArray& cmd);

    RadioModel* m_model;
    QTcpServer m_server;
    QList<QTcpSocket*> m_clients;
    QByteArray m_acc;
};

} // namespace brick2
