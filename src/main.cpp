#include "ui/mainwindow.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QMessageLogContext>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QSurfaceFormat>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
QFile *gLogFile = nullptr;
QMutex gLogMutex;

void appMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const char *level = "INFO";
    switch (type) {
    case QtDebugMsg:
        level = "DEBUG";
        break;
    case QtInfoMsg:
        level = "INFO";
        break;
    case QtWarningMsg:
        level = "WARN";
        break;
    case QtCriticalMsg:
        level = "ERROR";
        break;
    case QtFatalMsg:
        level = "FATAL";
        break;
    }

    const QString line = QStringLiteral("%1 [%2] %3 (%4:%5, %6)\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  QString::fromUtf8(level),
                                  message,
                                  QString::fromUtf8(context.file ? context.file : ""),
                                  QString::number(context.line),
                                  QString::fromUtf8(context.function ? context.function : ""));

#ifdef Q_OS_WIN
    OutputDebugStringW(reinterpret_cast<LPCWSTR>(line.utf16()));
#endif

    {
        QMutexLocker locker(&gLogMutex);
        if (gLogFile && gLogFile->isOpen()) {
            gLogFile->write(line.toUtf8());
            gLogFile->flush();
        }
    }

    if (type == QtFatalMsg) {
        std::abort();
    }
}

void setupLogging()
{
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(baseDir + QStringLiteral("/logs"));
    const QString logPath = baseDir + QStringLiteral("/logs/nd2-viewer.log");
    gLogFile = new QFile(logPath);
    gLogFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    qInstallMessageHandler(appMessageHandler);
    qInfo("Logging initialized: %s", qPrintable(logPath));
}
}

int main(int argc, char *argv[])
{
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("nd2-viewer"));
    QApplication::setOrganizationName(QStringLiteral("nd2-viewer"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app-icon.svg")));
    setupLogging();

    qRegisterMetaType<ChannelRenderSettings>();
    qRegisterMetaType<FrameCoordinateState>();
    qRegisterMetaType<MovieExportEstimate>();
    qRegisterMetaType<MovieExportResult>();
    qRegisterMetaType<MovieExportSettings>();
    qRegisterMetaType<DocumentInfo>();
    qRegisterMetaType<RawFrame>();
    qRegisterMetaType<RawVolume>();
    qRegisterMetaType<RenderedFrame>();
    qRegisterMetaType<MetadataSection>();

    MainWindow window;
    window.show();

    return app.exec();
}
