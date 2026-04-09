#pragma once

#include "core/movieexporter.h"
#include "core/documentcontroller.h"

#include <QMainWindow>
#include <QRect>

class QAction;
class ChannelControlsWidget;
class QEvent;
class ImageViewport;
class QLabel;
class QMediaCaptureSession;
class QMediaRecorder;
class QPlainTextEdit;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTimer;
class QToolButton;
class QTreeWidget;
class QVideoFrameInput;
class QVBoxLayout;
class QCloseEvent;
class VolumeViewerWindow;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void openFile();
    void saveCurrentFrameAs();
    void saveCurrentRoiAs();
    void exportMovieAs();
    void exportRoiMovieAs();
    void open3DView();
    void updateDocumentUi();
    void updateCoordinateUi();
    void updateChannelUi();
    void updateFrameUi();
    void updateMetadataUi();
    void showErrorMessage(const QString &message);
    void updateBusyState(bool busy);
    void updateStatusMessage(const QString &message);
    void updateHoveredPixel(const QPoint &pixelPosition, bool insideImage);
    void updateZoomLabel(double zoomFactor, bool fitToWindow);

private:
    enum class ExportScope
    {
        Frame,
        Roi
    };

    enum class ExportMode
    {
        Cancelled,
        PreviewPng,
        AnalysisTiffs,
        Bundle
    };

    struct ExportBundleResult
    {
        bool previewRequested = false;
        bool previewSaved = false;
        QString previewPath;
        QStringList channelPaths;
        QStringList failures;
    };

    struct LoopWidgets
    {
        QWidget *row = nullptr;
        QLabel *label = nullptr;
        QSlider *slider = nullptr;
        QSpinBox *spinBox = nullptr;
        QLabel *details = nullptr;
    };

    struct MetadataWidgets
    {
        QTreeWidget *tree = nullptr;
        QPlainTextEdit *raw = nullptr;
    };

    void buildMenus();
    void buildCentralUi();
    void exportCurrentSelection(ExportScope scope);
    void exportMovieSelection(ExportScope scope);
    void rebuildNavigatorControls();
    void commitLoopSliderValue(int loopIndex);
    void handleTimePlaybackButton();
    void startTimePlayback();
    void stopTimePlayback();
    void advanceTimePlayback();
    void rebuildMetadataTabs();
    MetadataWidgets addMetadataTab(const QString &title);
    void setMetadataContent(const MetadataWidgets &widgets, const QJsonValue &jsonValue, const QString &rawText);
    void updateStaticMetadataUi();
    void updateFrameMetadataUi();
    void setOverviewContent(const DocumentInfo &info);
    [[nodiscard]] ExportMode promptForExportMode(ExportScope scope) const;
    [[nodiscard]] ExportBundleResult exportCurrentFrame(const QString &selectedPath,
                                                       ExportMode mode,
                                                       ExportScope scope) const;
    [[nodiscard]] bool writeChannelTiff(const QString &path,
                                        const RawFrame &frame,
                                        int channelIndex,
                                        QString *errorMessage,
                                        const QRect &cropRect = QRect()) const;
    [[nodiscard]] QString buildDefaultFrameSavePath(ExportScope scope,
                                                    const QString &extension = QStringLiteral(".png")) const;
    [[nodiscard]] QString buildDefaultMovieSavePath(ExportScope scope, const MovieExportSettings &settings) const;
    [[nodiscard]] int findTimeLoopIndex() const;
    [[nodiscard]] bool hasUsableZStack() const;
    void setMovieExportUiState(bool active);
    void startMovieExportPlayback(const MovieExportSettings &settings);
    void requestNextMovieExportFrame();
    void prepareCurrentMovieExportFrame();
    void trySendMovieExportFrame();
    void finishMovieExportPlayback(const QString &errorMessage = QString());
    void cleanupMovieExportPlayback();
    void openAutoContrastTuningDialog(int channelIndex);
    [[nodiscard]] QString sanitizeToken(const QString &value) const;
    void updateWindowTitle();
    void updateInfoLabel();

    DocumentController controller_;
    ImageViewport *imageViewport_ = nullptr;
    QWidget *navigatorContainer_ = nullptr;
    QVBoxLayout *navigatorRowsLayout_ = nullptr;
    QLabel *navigatorEmptyLabel_ = nullptr;
    QVector<LoopWidgets> loopControls_;
    ChannelControlsWidget *channelControlsWidget_ = nullptr;
    QTreeWidget *metadataOverviewTree_ = nullptr;
    QTabWidget *metadataTabs_ = nullptr;
    QVector<MetadataWidgets> metadataSectionWidgets_;
    MetadataWidgets frameMetadataWidgets_;
    QLabel *infoStatusLabel_ = nullptr;
    QLabel *zoomStatusLabel_ = nullptr;
    QLabel *pixelStatusLabel_ = nullptr;
    QAction *openAction_ = nullptr;
    QAction *reloadAction_ = nullptr;
    QAction *quitAction_ = nullptr;
    QAction *threeDViewAction_ = nullptr;
    bool movieExportInProgress_ = false;
    bool timePlaybackActive_ = false;
    bool timePlaybackAwaitingFrame_ = false;
    int timePlaybackStep_ = 1;
    int timePlaybackLoopIndex_ = -1;
    int timePlaybackNextFrameIndex_ = 0;
    QVector<int> timePlaybackTimeValues_;
    QTimer *timePlaybackTimer_ = nullptr;
    QToolButton *timePlaybackButton_ = nullptr;
    MovieExportSettings movieExportSettings_;
    QVector<int> movieExportTimeValues_;
    int movieExportNextFrameIndex_ = 0;
    int movieExportEncodedFrameCount_ = 0;
    bool movieExportAwaitingFrame_ = false;
    bool movieExportPendingEndOfStream_ = false;
    bool movieExportEndOfStreamSent_ = false;
    bool movieVideoFrameInputReady_ = false;
    QMediaCaptureSession *movieCaptureSession_ = nullptr;
    QMediaRecorder *movieRecorder_ = nullptr;
    QVideoFrameInput *movieVideoFrameInput_ = nullptr;
    QVideoFrame moviePendingFrame_;
    VolumeViewerWindow *volumeViewerWindow_ = nullptr;
};
