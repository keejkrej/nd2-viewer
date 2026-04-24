#include "qml/qmldocumentcontroller.h"
#include "qml/quickimageviewport.h"
#include "qml/quickvolumeviewport3d.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLibraryInfo>
#include <QMessageLogContext>
#include <QMutex>
#include <QMutexLocker>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>

#include <algorithm>

#include <QQuickVTKItem.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
QFile *gLogFile = nullptr;
QMutex gLogMutex;

bool hasQtFfmpegMediaPlugin(const QStringList &libraryPaths)
{
    for (const QString &basePath : libraryPaths) {
        const QDir multimediaDir(basePath + QStringLiteral("/multimedia"));
        const QStringList entries = multimediaDir.entryList(QDir::Files);
        for (const QString &entry : entries) {
            if (entry.contains(QStringLiteral("ffmpeg"), Qt::CaseInsensitive)
                && entry.contains(QStringLiteral("mediaplugin"), Qt::CaseInsensitive)) {
                return true;
            }
        }
    }

    return false;
}

void preferQtFfmpegMediaBackendIfAvailable()
{
    if (qEnvironmentVariableIsSet("QT_MEDIA_BACKEND")) {
        qInfo("QT_MEDIA_BACKEND already set to %s", qPrintable(qEnvironmentVariable("QT_MEDIA_BACKEND")));
        return;
    }

    QStringList libraryPaths = QCoreApplication::libraryPaths();
    const QString qtPluginPath = QLibraryInfo::path(QLibraryInfo::PluginsPath);
    if (!qtPluginPath.isEmpty() && !libraryPaths.contains(qtPluginPath)) {
        libraryPaths.push_back(qtPluginPath);
    }

    if (hasQtFfmpegMediaPlugin(libraryPaths)) {
        qputenv("QT_MEDIA_BACKEND", QByteArrayLiteral("ffmpeg"));
        qInfo("QT_MEDIA_BACKEND set to ffmpeg");
    } else {
        qInfo("Qt FFmpeg multimedia plugin not found; leaving QT_MEDIA_BACKEND unset");
    }
}

void forceQtPluginPathsToActiveQt()
{
    const QString qtPluginPath = QLibraryInfo::path(QLibraryInfo::PluginsPath);
    if (qtPluginPath.isEmpty()) {
        return;
    }

    qputenv("QT_PLUGIN_PATH", qtPluginPath.toUtf8());
    qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", QDir(qtPluginPath).filePath(QStringLiteral("platforms")).toUtf8());
}

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
    [[maybe_unused]] const bool logDirReady = QDir().mkpath(baseDir + QStringLiteral("/logs"));
    const QString logPath = baseDir + QStringLiteral("/logs/nd2-viewer.log");
    gLogFile = new QFile(logPath);
    [[maybe_unused]] const bool logFileOpened = gLogFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    qInstallMessageHandler(appMessageHandler);
    qInfo("Logging initialized: %s", qPrintable(logPath));
}
}

int main(int argc, char *argv[])
{
    QQuickVTKItem::setGraphicsApi();

    forceQtPluginPathsToActiveQt();

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("nd2-viewer"));
    QApplication::setOrganizationName(QStringLiteral("nd2-viewer"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app-icon.svg")));
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    setupLogging();
    preferQtFfmpegMediaBackendIfAvailable();

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

    qmlRegisterType<QuickImageViewport>("Nd2Viewer", 1, 0, "QuickImageViewport");
    qmlRegisterType<QuickVolumeViewport3D>("Nd2Viewer", 1, 0, "QuickVolumeViewport3D");

    QmlDocumentController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appController"), &controller);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() {
        QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/Nd2Viewer/src/qml/Main.qml")));

    return app.exec();
}
