#include "ui/mainwindow.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLibraryInfo>
#include <QMessageLogContext>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QSurfaceFormat>

#include <algorithm>

#if defined(Q_OS_MACOS) && defined(ND2VIEWER_HAS_VTK_3D)
#include <QVTKOpenGLNativeWidget.h>
#endif

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
    QSurfaceFormat format;
#if defined(Q_OS_MACOS) && defined(ND2VIEWER_HAS_VTK_3D)
    format = QVTKOpenGLNativeWidget::defaultFormat();
    format.setDepthBufferSize(std::max(format.depthBufferSize(), 24));
#else
    format.setDepthBufferSize(24);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
#endif
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("nd2-viewer"));
    QApplication::setOrganizationName(QStringLiteral("nd2-viewer"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app-icon.svg")));
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

    MainWindow window;
    window.show();

    return app.exec();
}
