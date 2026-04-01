#include "ui/mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("nd2-viewer"));
    QApplication::setOrganizationName(QStringLiteral("nd2-viewer"));

    qRegisterMetaType<ChannelRenderSettings>();
    qRegisterMetaType<FrameCoordinateState>();
    qRegisterMetaType<Nd2DocumentInfo>();
    qRegisterMetaType<RawFrame>();
    qRegisterMetaType<RenderedFrame>();

    MainWindow window;
    window.show();

    return app.exec();
}
