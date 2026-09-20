#include "ui/MainWindow.h"

#include <QApplication>

#include <QIcon>
#include <QPainter>
#include <QPixmap>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Brick2SDR"));
    app.setApplicationDisplayName(QStringLiteral("Brick2SDR"));
    app.setOrganizationName(QStringLiteral("Brick2SDR"));
    QPixmap pix(64, 64);
    pix.fill(Qt::transparent);
    {
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(18, 22, 28));
        p.setPen(QPen(QColor(62, 200, 200), 3));
        p.drawRoundedRect(4, 4, 56, 56, 12, 12);
        p.setPen(QPen(QColor(240, 180, 41), 3));
        p.drawLine(16, 40, 28, 24);
        p.drawLine(28, 24, 36, 36);
        p.drawLine(36, 36, 48, 16);
    }
    app.setWindowIcon(QIcon(pix));
    qRegisterMetaType<brick2::RadioInfo>("brick2::RadioInfo");
    qRegisterMetaType<brick2::SpectrumFrame>("brick2::SpectrumFrame");
    brick2::MainWindow w;
    w.show();
    return app.exec();
}
