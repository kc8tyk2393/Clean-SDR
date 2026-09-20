#pragma once

#include <QAudioSink>
#include <QAudioSource>
#include <QIODevice>
#include <QObject>
#include <QVector>
#include <mutex>

namespace brick2 {

class RadioModel;

class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(RadioModel* model, QObject* parent = nullptr);
    ~AudioEngine() override;

    void start();
    void stop();
    void pushSpeaker(const QVector<qint16>& interleavedStereo);
    QVector<float> pullMic(int count);

private:
    void setup();
    void closePair(QAudioSink*& sink, QIODevice*& io);
    void closePair(QAudioSource*& source, QIODevice*& io);

    RadioModel* m_model;
    QAudioSink* m_sink = nullptr;
    QAudioSink* m_vacSink = nullptr;
    QAudioSource* m_source = nullptr;
    QAudioSource* m_vacSource = nullptr;
    QIODevice* m_out = nullptr;
    QIODevice* m_vacOut = nullptr;
    QIODevice* m_in = nullptr;
    QIODevice* m_vacIn = nullptr;
    QByteArray m_play;
    QByteArray m_vacPlay;
    std::mutex m_mutex;
    int m_vacOutChannels = 2;
    int m_inChannels = 1;
};

} // namespace brick2
