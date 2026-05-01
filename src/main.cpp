#include "ui/mainwindow.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QMessageLogContext>
#include <QMutex>
#include <QMutexLocker>
#include <QPalette>
#include <QProcess>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QSurfaceFormat>

#include <algorithm>
#include <optional>

#include <QVTKOpenGLNativeWidget.h>

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

#ifdef Q_OS_LINUX
void configureLinuxQtPlatform()
{
    // vcpkg Qt typically ships only Fusion; Kvantum is not available as a style plugin.
    const QByteArray styleOverride = qgetenv("QT_STYLE_OVERRIDE").trimmed().toLower();
    if (!styleOverride.isEmpty() && styleOverride.startsWith(QByteArrayLiteral("kvantum"))) {
        qunsetenv("QT_STYLE_OVERRIDE");
    }
}

void applyLinuxFusionDarkPalette()
{
    QPalette pal;
    const QColor darkGray(53, 53, 53);
    const QColor black(25, 25, 25);
    const QColor blue(42, 130, 218);
    const QColor gray(128, 128, 128);

    pal.setColor(QPalette::Window, darkGray);
    pal.setColor(QPalette::WindowText, Qt::white);
    pal.setColor(QPalette::Base, black);
    pal.setColor(QPalette::AlternateBase, darkGray);
    pal.setColor(QPalette::ToolTipBase, Qt::white);
    pal.setColor(QPalette::ToolTipText, Qt::white);
    pal.setColor(QPalette::Text, Qt::white);
    pal.setColor(QPalette::Button, darkGray);
    pal.setColor(QPalette::ButtonText, Qt::white);
    pal.setColor(QPalette::BrightText, Qt::red);
    pal.setColor(QPalette::Link, blue);
    pal.setColor(QPalette::Highlight, blue);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, gray);
    pal.setColor(QPalette::Disabled, QPalette::Text, gray);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, gray);
    pal.setColor(QPalette::Disabled, QPalette::Highlight, QColor(80, 80, 80));
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, gray);
    QApplication::setPalette(pal);
}

std::optional<Qt::ColorScheme> gnomeDesktopColorSchemeFromGsettings()
{
    QProcess proc;
    proc.start(QStringLiteral("gsettings"),
               {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"), QStringLiteral("color-scheme")});
    if (!proc.waitForFinished(500)) {
        return std::nullopt;
    }
    const QString out = QString::fromUtf8(proc.readAllStandardOutput()).simplified();
    if (out.contains(QLatin1String("prefer-dark"))) {
        return Qt::ColorScheme::Dark;
    }
    if (out.contains(QLatin1String("prefer-light"))) {
        return Qt::ColorScheme::Light;
    }
    return std::nullopt;
}

void configureLinuxWidgetApplication(QApplication &app)
{
    if (QStyle *fusionStyle = QStyleFactory::create(QStringLiteral("Fusion"))) {
        app.setStyle(fusionStyle);
    }

    QStyleHints *hints = app.styleHints();
    const auto syncAppearance = [&]() {
        Qt::ColorScheme scheme = hints->colorScheme();
        if (scheme == Qt::ColorScheme::Unknown) {
            if (const std::optional<Qt::ColorScheme> guess = gnomeDesktopColorSchemeFromGsettings()) {
                scheme = *guess;
            }
        }
        if (scheme == Qt::ColorScheme::Dark) {
            applyLinuxFusionDarkPalette();
        } else if (scheme == Qt::ColorScheme::Light) {
            if (QStyle *st = app.style()) {
                app.setPalette(st->standardPalette());
            }
        }
    };
    syncAppearance();
    QObject::connect(hints, &QStyleHints::colorSchemeChanged, &app, [&](Qt::ColorScheme) { syncAppearance(); });
}
#endif

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
    QSurfaceFormat format = QVTKOpenGLNativeWidget::defaultFormat();
    format.setDepthBufferSize(std::max(format.depthBufferSize(), 24));
    QSurfaceFormat::setDefaultFormat(format);

    forceQtPluginPathsToActiveQt();
#ifdef Q_OS_LINUX
    configureLinuxQtPlatform();
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif

    QApplication app(argc, argv);
#ifdef Q_OS_LINUX
    configureLinuxWidgetApplication(app);
#endif
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
