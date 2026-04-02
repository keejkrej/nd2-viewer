#include "ui/mainwindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("nd2-viewer"));
    QApplication::setOrganizationName(QStringLiteral("nd2-viewer"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app-icon.svg")));

    qRegisterMetaType<ChannelRenderSettings>();
    qRegisterMetaType<FrameCoordinateState>();
    qRegisterMetaType<MovieExportEstimate>();
    qRegisterMetaType<MovieExportResult>();
    qRegisterMetaType<MovieExportSettings>();
    qRegisterMetaType<DocumentInfo>();
    qRegisterMetaType<RawFrame>();
    qRegisterMetaType<RenderedFrame>();
    qRegisterMetaType<MetadataSection>();

    MainWindow window;
    window.show();

    return app.exec();
}
