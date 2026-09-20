#include "audio/AudioEngine.h"

#include "core/RadioModel.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QMediaDevices>
#include <algorithm>
#include <cmath>

namespace brick2 {
namespace {

QAudioDevice outputById(const QString& id)
{
    if (id.isEmpty())
        return QMediaDevices::defaultAudioOutput();
    for (const auto& d : QMediaDevices::audioOutputs()) {
        if (QString::fromUtf8(d.id()) == id)
            return d;
    }
    return QMediaDevices::defaultAudioOutput();
}

QAudioDevice inputById(const QString& id)
{
    if (id.isEmpty())
        return QMediaDevices::defaultAudioInput();
    for (const auto& d : QMediaDevices::audioInputs()) {
        if (QString::fromUtf8(d.id()) == id)
            return d;
    }
    return QMediaDevices::defaultAudioInput();
}

QAudioFormat pcm48(int channels)
{
    QAudioFormat fmt;
    fmt.setSampleRate(kAudioRate);
    fmt.setChannelCount(channels);
    fmt.setSampleFormat(QAudioFormat::Int16);
    return fmt;
}

void writeQueue(QIODevice* io, QByteArray& queue, const QByteArray& chunk)
{
    if (!io)
        return;
    queue.append(chunk);
    const qint64 n = io->write(queue);
    if (n > 0)
        queue.remove(0, int(n));
    if (queue.size() > kAudioRate * 8)
        queue.remove(0, queue.size() - kAudioRate * 4);
}

} // namespace

AudioEngine::AudioEngine(RadioModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
{
}

AudioEngine::~AudioEngine()
{
    stop();
}

void AudioEngine::start()
{
    stop();
    setup();
}

void AudioEngine::closePair(QAudioSink*& sink, QIODevice*& io)
{
    if (sink) {
        sink->stop();
        delete sink;
        sink = nullptr;
    }
    io = nullptr;
}

void AudioEngine::closePair(QAudioSource*& source, QIODevice*& io)
{
    if (source) {
        source->stop();
        delete source;
        source = nullptr;
    }
    io = nullptr;
}

void AudioEngine::stop()
{
    closePair(m_sink, m_out);
    closePair(m_vacSink, m_vacOut);
    closePair(m_source, m_in);
    closePair(m_vacSource, m_vacIn);
    m_play.clear();
    m_vacPlay.clear();
}

void AudioEngine::setup()
{
    const QAudioFormat spk = pcm48(2);
    const QAudioFormat mic = pcm48(1);
    const int buf = kAudioRate * 4;

    const QAudioDevice speakers = QMediaDevices::defaultAudioOutput();
    if (!speakers.isNull()) {
        m_sink = new QAudioSink(speakers, spk, this);
        m_sink->setBufferSize(buf);
        m_out = m_sink->start();
    }

    const QAudioDevice microphone = QMediaDevices::defaultAudioInput();
    if (!microphone.isNull()) {
        m_source = new QAudioSource(microphone, mic, this);
        m_source->setBufferSize(buf);
        m_in = m_source->start();
        m_inChannels = 1;
    }

    if (!m_model->vac.vac1)
        return;

    const int vacCh = m_model->vac.vac1Stereo ? 2 : 1;
    m_vacOutChannels = vacCh;
    const auto vacSpk = pcm48(vacCh);
    const QAudioDevice vacOut = outputById(m_model->vac.vac1Out);
    if (!vacOut.isNull() && vacOut != speakers) {
        m_vacSink = new QAudioSink(vacOut, vacSpk, this);
        m_vacSink->setBufferSize(std::max(buf, kAudioRate * 4 * std::clamp(m_model->vac.vac1Latency, 20, 500) / 1000));
        m_vacOut = m_vacSink->start();
    } else if (!vacOut.isNull()) {
        m_vacOut = m_out;
        m_vacOutChannels = 2;
    }

    const QAudioDevice vacIn = inputById(m_model->vac.vac1In);
    if (!vacIn.isNull()) {
        m_vacSource = new QAudioSource(vacIn, mic, this);
        m_vacSource->setBufferSize(buf);
        m_vacIn = m_vacSource->start();
        m_inChannels = 1;
    }
}

void AudioEngine::pushSpeaker(const QVector<qint16>& interleavedStereo)
{
    std::lock_guard lock(m_mutex);
    const QByteArray raw(reinterpret_cast<const char*>(interleavedStereo.constData()),
                         interleavedStereo.size() * int(sizeof(qint16)));
    writeQueue(m_out, m_play, raw);

    if (!m_model->vac.vac1 || !m_vacOut)
        return;

    const float g = std::pow(10.f, m_model->vac.vac1GainRx / 20.f);
    QByteArray packed;
    packed.resize(interleavedStereo.size() / 2 * m_vacOutChannels * int(sizeof(qint16)));
    auto* dst = reinterpret_cast<qint16*>(packed.data());
    int o = 0;
    for (int i = 0; i + 1 < interleavedStereo.size(); i += 2) {
        const float l = std::clamp(interleavedStereo[i] / 32768.f * g, -1.f, 1.f);
        const float r = std::clamp(interleavedStereo[i + 1] / 32768.f * g, -1.f, 1.f);
        if (m_vacOutChannels >= 2) {
            dst[o++] = qint16(l * 32767.f);
            dst[o++] = qint16(r * 32767.f);
        } else {
            dst[o++] = qint16(((l + r) * 0.5f) * 32767.f);
        }
    }
    packed.resize(o * int(sizeof(qint16)));
    if (m_vacOut == m_out)
        return;
    writeQueue(m_vacOut, m_vacPlay, packed);
}

QVector<float> AudioEngine::pullMic(int count)
{
    QIODevice* src = (m_model->vac.vac1 && m_vacIn) ? m_vacIn : m_in;
    QVector<float> out;
    if (!src)
        return out;
    const int ch = std::max(1, m_inChannels);
    const QByteArray raw = src->read(count * ch * 2);
    const auto* s = reinterpret_cast<const qint16*>(raw.constData());
    const int frames = raw.size() / (2 * ch);
    out.resize(frames);
    float g = 1.f;
    if (m_model->vac.vac1)
        g = std::pow(10.f, m_model->vac.vac1GainTx / 20.f);
    for (int i = 0; i < frames; ++i) {
        float x = float(s[i * ch]) / 32768.f;
        if (ch >= 2)
            x = 0.5f * (x + float(s[i * ch + 1]) / 32768.f);
        out[i] = std::clamp(x * g, -1.f, 1.f);
    }
    return out;
}

} // namespace brick2
