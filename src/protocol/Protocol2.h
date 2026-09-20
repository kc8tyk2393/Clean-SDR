#pragma once

#include "core/Types.h"

#include <QHostAddress>
#include <QObject>
#include <QUdpSocket>
#include <QVector>
#include <cstdint>

namespace brick2 {

class RadioModel;

class Protocol2Engine : public QObject {
    Q_OBJECT
public:
    explicit Protocol2Engine(RadioModel* model, QObject* parent = nullptr);

    void startDiscovery();
    void stopDiscovery();
    void connectTo(const RadioInfo& info);
    void disconnectRadio();
    void startSimulator();
    bool isRunning() const { return m_running; }
    bool isSimulator() const { return m_info.simulator; }
    RadioInfo currentRadio() const { return m_info; }
    QVector<RadioInfo> discovered() const { return m_radios; }
    bool connectFirstHardware();
    void connectToIp(const QString& ip);
    void pushControl();
    QString lastDiscoveryNote() const { return m_lastNote; }

    void sendAudio(qint16 left, qint16 right);
    void sendTxIq(float i, float q);

signals:
    void radiosChanged(const QVector<RadioInfo>& radios);
    void connected(const RadioInfo& info);
    void disconnected(const QString& reason);
    void discoveryStatus(const QString& message);
    void iqReceived(int ddc, const QVector<float>& interleavedIq);
    void micReceived(const QVector<float>& samples);
    void statusUpdated();

private:
    void sendDiscovery();
    void probeAddress(const QHostAddress& dest);
    bool bindSocket();
    QString ipv4String(const QHostAddress& addr) const;
    void rememberRadio(RadioInfo info);
    void onReadyRead();
    void sendCommands();
    void sendGeneral();
    void sendRxSpecific();
    void sendTxSpecific();
    void sendHighPriority();
    void processPacket(const QByteArray& data, const QHostAddress& from, quint16 port);
    void parseDiscovery(const QByteArray& data, const QHostAddress& from);
    void parseIq(int ddc, const QByteArray& data);
    void parseHighPriority(const QByteArray& data);
    void parseMic(const QByteArray& data);
    void simulatorTick();
    void flushAudio();
    void flushTxIq();

    RadioModel* m_model;
    QUdpSocket m_socket;
    QVector<RadioInfo> m_radios;
    RadioInfo m_info;
    bool m_running = false;
    bool m_discovering = false;
    quint32 m_seqGeneral = 0;
    quint32 m_seqRx = 0;
    quint32 m_seqTx = 0;
    quint32 m_seqHp = 0;
    quint32 m_seqAudio = 0;
    quint32 m_seqTxIq = 0;
    QByteArray m_audioBuf;
    QByteArray m_txIqBuf;
    int m_audioIndex = 4;
    int m_txIqIndex = 4;
    QString m_lastNote;
    int m_probes = 0;
    bool m_wasTx = false;
};

} // namespace brick2
