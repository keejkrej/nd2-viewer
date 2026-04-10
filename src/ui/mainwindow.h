#pragma once

#include "core/movieexporter.h"
#include "core/documentcontroller.h"
#include "core/volumeloader.h"
#include "ui/volumeviewport3d.h"

#include <QFutureWatcher>
#include <QMainWindow>
#include <QRect>

class QAction;
class ChannelControlsWidget;
class QEvent;
class FileInfoDialog;
class ImageViewport;
class QLabel;
class QMediaCaptureSession;
class QMediaRecorder;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTimer;
class QToolButton;
class QVideoFrameInput;
class QVBoxLayout;
class QCloseEvent;
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
    void updateDocumentUi();
    void updateCoordinateUi();
    void updateChannelUi();
    void updateFrameUi();
    void updateMetadataUi();
    void showErrorMessage(const QString &message);
    void updateBusyState(bool busy);
    void updateStatusMessage(const QString &message);
    void updateHoveredPixel(const QPoint &pixelPosition, bool insideImage);
    void handleVolumeLoadFinished();
    void maybeReloadVolumeForNonZCoordinateChange();

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

    void buildMenus();
    void buildCentralUi();
    void exportCurrentSelection(ExportScope scope);
    void exportMovieSelection(ExportScope scope);
    void exportCurrentVolumeFrame();
    void exportVolumeMovie();
    void showFileInfoDialog();
    void rebuildNavigatorControls();
    void commitLoopSliderValue(int loopIndex);
    void handleTimePlaybackButton();
    void startTimePlayback();
    void stopTimePlayback();
    void advanceTimePlayback();
    void completeTimePlaybackStep();
    void updateStaticMetadataUi();
    void updateFrameMetadataUi();
    void updateFileInfoDialog();
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
    [[nodiscard]] bool isVolumeViewActive() const;
    [[nodiscard]] bool volumeMatchesCurrentFixedCoordinates() const;
    void setVolumeViewActive(bool active);
    void updateViewModeButtons();
    void setMovieExportUiState(bool active);
    void startMovieExportPlayback(const MovieExportSettings &settings);
    void requestNextMovieExportFrame();
    void prepareCurrentMovieExportFrame();
    void consumePreparedMovieExportImage(const QImage &image);
    void trySendMovieExportFrame();
    void finishMovieExportPlayback(const QString &errorMessage = QString());
    void cleanupMovieExportPlayback();
    void restoreVolumeViewportAfterMovieExport();
    [[nodiscard]] QImage renderMovieExportFrameImage(int timeValue, QString *errorMessage) const;
    [[nodiscard]] QString movieExportBackendUnsupportedReason() const;
    void openAutoContrastTuningDialog(int channelIndex);
    void applyVolumeViewMode(bool volumeViewActive);
    void applyZLoopNavigatorLock();
    void startVolumeLoad();
    void syncVolumeViewportChannelSettings();
    void setLiveAutoForAllChannels(bool enabled);
    void autoContrastChannelForActiveView(int channelIndex);
    void autoContrastAllForActiveView();
    [[nodiscard]] QImage captureCurrentVolumeImage() const;
    [[nodiscard]] QString sanitizeToken(const QString &value) const;
    void updateWindowTitle();
    void updateInfoLabel();

    DocumentController controller_;
    ImageViewport *imageViewport_ = nullptr;
    QStackedWidget *viewerStack_ = nullptr;
    QWidget *viewModeControl_ = nullptr;
    QPushButton *view2dButton_ = nullptr;
    QPushButton *view3dButton_ = nullptr;
    QWidget *volumePage_ = nullptr;
    VolumeViewport3D *volumeViewport_ = nullptr;
    QPushButton *volumeFitButton_ = nullptr;
    QPushButton *volumeResetButton_ = nullptr;
    QFutureWatcher<VolumeLoadResult> volumeWatcher_;
    int volumeLoadGeneration_ = 0;
    RawVolume cachedVolume_;
    bool volumeViewportHasVolume_ = false;
    int documentZLoopIndex_ = -1;
    VolumeViewport3DCameraState pendingVolumeCameraState_;

    QWidget *navigatorContainer_ = nullptr;
    QVBoxLayout *navigatorRowsLayout_ = nullptr;
    QLabel *navigatorEmptyLabel_ = nullptr;
    QVector<LoopWidgets> loopControls_;
    ChannelControlsWidget *channelControlsWidget_ = nullptr;
    FileInfoDialog *fileInfoDialog_ = nullptr;
    QLabel *infoStatusLabel_ = nullptr;
    QLabel *pixelStatusLabel_ = nullptr;
    QAction *openAction_ = nullptr;
    QAction *reloadAction_ = nullptr;
    QAction *fileInfoAction_ = nullptr;
    QAction *quitAction_ = nullptr;
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
    DocumentInfo movieExportDocumentInfo_;
    QVector<int> movieExportTimeValues_;
    int movieExportNextFrameIndex_ = 0;
    int movieExportEncodedFrameCount_ = 0;
    bool movieExportAwaitingFrame_ = false;
    bool movieExportPendingEndOfStream_ = false;
    bool movieExportEndOfStreamSent_ = false;
    bool movieVideoFrameInputReady_ = false;
    bool movieExportVolumeView_ = false;
    QVector<ChannelRenderSettings> movieExportFrozenChannelSettings_;
    QVector<ChannelRenderSettings> movieExportOriginalChannelSettings_;
    bool movieExportOriginalLiveAutoEnabled_ = false;
    VolumeViewport3DCameraState movieExportFrozenCameraState_;
    RawVolume movieExportOriginalVolume_;
    VolumeViewport3DCameraState movieExportOriginalCameraState_;
    QMediaCaptureSession *movieCaptureSession_ = nullptr;
    QMediaRecorder *movieRecorder_ = nullptr;
    QVideoFrameInput *movieVideoFrameInput_ = nullptr;
    QVideoFrame moviePendingFrame_;
};
