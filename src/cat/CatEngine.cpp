#include "cat/CatEngine.h"

#include "core/RadioModel.h"

#include <QHostAddress>

namespace brick2 {

CatEngine::CatEngine(RadioModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
{
    connect(&m_server, &QTcpServer::newConnection, this, &CatEngine::onNewConnection);
}

void CatEngine::start()
{
    stop();
    if (m_model->cat.enabled)
        m_server.listen(QHostAddress::Any, m_model->cat.port);
}

void CatEngine::stop()
{
    for (auto* c : m_clients)
        c->deleteLater();
    m_clients.clear();
    m_server.close();
}

void CatEngine::onNewConnection()
{
    while (auto* c = m_server.nextPendingConnection()) {
        m_clients.push_back(c);
        connect(c, &QTcpSocket::readyRead, this, &CatEngine::onReadyRead);
        connect(c, &QTcpSocket::disconnected, this, [this, c] {
            m_clients.removeAll(c);
            c->deleteLater();
        });
    }
}

void CatEngine::onReadyRead()
{
    auto* s = qobject_cast<QTcpSocket*>(sender());
    if (!s)
        return;
    m_acc += s->readAll();
    while (true) {
        const int sc = m_acc.indexOf(';');
        if (sc < 0)
            break;
        const QByteArray cmd = m_acc.left(sc);
        m_acc.remove(0, sc + 1);
        s->write(handle(cmd));
    }
}

QByteArray CatEngine::handle(const QByteArray& cmd)
{
    if (cmd.startsWith("ZZ"))
        return zz(cmd);

    const auto setFreq = [&](VfoState& v, const QByteArray& body) {
        bool ok = false;
        const qint64 f = body.toLongLong(&ok);
        if (ok && f > 0) {
            v.frequency = f;
            emit m_model->frequencyChanged();
        }
        return QByteArray();
    };

    if (cmd.startsWith("FA") && cmd.size() > 2)
        return setFreq(m_model->vfoA, cmd.mid(2));
    if (cmd.startsWith("FB") && cmd.size() > 2)
        return setFreq(m_model->vfoB, cmd.mid(2));
    if (cmd == "FA")
        return QByteArray("FA") + QByteArray::number(m_model->vfoA.frequency).rightJustified(11, '0') + ";";
    if (cmd == "FB")
        return QByteArray("FB") + QByteArray::number(m_model->vfoB.frequency).rightJustified(11, '0') + ";";
    if (cmd.startsWith("MD") && cmd.size() > 2) {
        const int md = cmd.mid(2).toInt();
        static const Mode map[] = {Mode::LSB, Mode::USB, Mode::USB, Mode::CWL, Mode::FM, Mode::AM, Mode::DIGL, Mode::CWU};
        if (md >= 1 && md <= 8)
            m_model->vfoA.mode = map[md - 1];
        if (m_model->vfoA.filter == FilterPreset::Var1 || m_model->vfoA.filter == FilterPreset::Var2)
            m_model->vfoA.edges = remapFilter(m_model->vfoA.edges, m_model->vfoA.mode);
        else
            m_model->vfoA.edges = defaultFilter(m_model->vfoA.mode, m_model->vfoA.filter);
        if (m_model->filterSync)
            m_model->syncTxFromRx();
        emit m_model->modeChanged();
        return {};
    }
    if (cmd == "MD")
        return QByteArray("MD2;");
    if (cmd == "TX") {
        m_model->mox = true;
        emit m_model->transmitChanged();
        return {};
    }
    if (cmd == "RX") {
        m_model->mox = false;
        emit m_model->transmitChanged();
        return {};
    }
    if (cmd.startsWith("PC") && cmd.size() > 2) {
        m_model->tx.drive = cmd.mid(2).toInt();
        return {};
    }
    if (cmd == "IF") {
        return QByteArray("IF") + QByteArray::number(m_model->vfoA.frequency).rightJustified(11, '0')
            + "     +00000000002000000;";
    }
    return QByteArray("?;");
}

QByteArray CatEngine::zz(const QByteArray& cmd)
{
    const auto reply = [](const QByteArray& prefix, const QByteArray& v) {
        return prefix + v + ";";
    };
    if (cmd == "ZZFA")
        return reply("ZZFA", QByteArray::number(m_model->vfoA.frequency).rightJustified(11, '0'));
    if (cmd.startsWith("ZZFA") && cmd.size() > 4) {
        m_model->vfoA.frequency = cmd.mid(4).toLongLong();
        emit m_model->frequencyChanged();
        return {};
    }
    if (cmd == "ZZFB")
        return reply("ZZFB", QByteArray::number(m_model->vfoB.frequency).rightJustified(11, '0'));
    if (cmd == "ZZMD")
        return reply("ZZMD", QByteArray::number(int(m_model->vfoA.mode)));
    if (cmd.startsWith("ZZMD") && cmd.size() > 4) {
        m_model->vfoA.mode = static_cast<Mode>(cmd.mid(4).toInt());
        if (m_model->vfoA.filter == FilterPreset::Var1 || m_model->vfoA.filter == FilterPreset::Var2)
            m_model->vfoA.edges = remapFilter(m_model->vfoA.edges, m_model->vfoA.mode);
        else
            m_model->vfoA.edges = defaultFilter(m_model->vfoA.mode, m_model->vfoA.filter);
        if (m_model->filterSync)
            m_model->syncTxFromRx();
        emit m_model->modeChanged();
        return {};
    }
    if (cmd.startsWith("ZZDM")) {
        if (cmd.size() > 4)
            m_model->display.mode = static_cast<DisplayMode>(cmd.mid(4).toInt());
        return reply("ZZDM", QByteArray::number(int(m_model->display.mode)));
    }
    if (cmd == "ZZTU") {
        m_model->tune = !m_model->tune;
        emit m_model->transmitChanged();
        return {};
    }
    if (cmd == "ZZTX")
        return reply("ZZTX", m_model->mox ? "1" : "0");
    if (cmd.startsWith("ZZTX")) {
        m_model->mox = cmd.endsWith("1");
        emit m_model->transmitChanged();
        return {};
    }
    if (cmd == "ZZSP")
        return reply("ZZSP", m_model->split ? "1" : "0");
    if (cmd.startsWith("ZZSP")) {
        m_model->split = cmd.endsWith("1");
        emit m_model->stateChanged();
        return {};
    }
    return QByteArray("?;");
}

} // namespace brick2
