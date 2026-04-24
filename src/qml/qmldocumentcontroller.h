#pragma once

#include "core/deconvolution.h"
#include "core/documentcontroller.h"
#include "core/movieexporter.h"
#include "core/volumeloader.h"
#include "ui/volumeviewport3d_backend.h"

#include <QAbstractItemModel>
#include <QColor>
#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <functional>

class ChannelListModel;
class LoopListModel;
class QuickVolumeViewport3D;

class QmlDocumentController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *loopModel READ loopModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *channelModel READ channelModel CONSTANT)
    Q_PROPERTY(bool hasDocument READ hasDocument NOTIFY documentStateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY documentStateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(QString infoText READ infoText NOTIFY infoTextChanged)
    Q_PROPERTY(QString pixelText READ pixelText NOTIFY pixelTextChanged)
    Q_PROPERTY(QString fileInfoText READ fileInfoText NOTIFY fileInfoTextChanged)
    Q_PROPERTY(int frameRevision READ frameRevision NOTIFY frameRevisionChanged)
    Q_PROPERTY(bool liveAutoEnabled READ liveAutoEnabled NOTIFY channelStateChanged)
    Q_PROPERTY(bool volumeAvailable READ volumeAvailable NOTIFY viewStateChanged)
    Q_PROPERTY(bool volumeViewActive READ volumeViewActive WRITE setVolumeViewActive NOTIFY viewStateChanged)
    Q_PROPERTY(QString viewActionText READ viewActionText NOTIFY viewStateChanged)
    Q_PROPERTY(bool deconvolutionBusy READ deconvolutionBusy NOTIFY deconvolutionStateChanged)
    Q_PROPERTY(bool hasDeconvolutionResult READ hasDeconvolutionResult NOTIFY deconvolutionStateChanged)
    Q_PROPERTY(int deconvolutionRevision READ deconvolutionRevision NOTIFY deconvolutionStateChanged)
    Q_PROPERTY(QString deconvolutionTitle READ deconvolutionTitle NOTIFY deconvolutionStateChanged)

public:
    explicit QmlDocumentController(QObject *parent = nullptr);

    [[nodiscard]] QAbstractItemModel *loopModel() const;
    [[nodiscard]] QAbstractItemModel *channelModel() const;
    [[nodiscard]] bool hasDocument() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString windowTitle() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString errorText() const;
    [[nodiscard]] QString infoText() const;
    [[nodiscard]] QString pixelText() const;
    [[nodiscard]] QString fileInfoText() const;
    [[nodiscard]] int frameRevision() const;
    [[nodiscard]] bool liveAutoEnabled() const;
    [[nodiscard]] bool volumeAvailable() const;
    [[nodiscard]] bool volumeViewActive() const;
    void setVolumeViewActive(bool active);
    [[nodiscard]] QString viewActionText() const;
    [[nodiscard]] bool deconvolutionBusy() const;
    [[nodiscard]] bool hasDeconvolutionResult() const;
    [[nodiscard]] int deconvolutionRevision() const;
    [[nodiscard]] QString deconvolutionTitle() const;

    [[nodiscard]] const QImage &currentImage() const;
    [[nodiscard]] const QImage &deconvolutionImage() const;
    [[nodiscard]] QString pixelInfoAt(const QPoint &pixelPosition) const;
    [[nodiscard]] bool hasCurrentRoi() const;
    [[nodiscard]] QRect currentRoi() const;
    [[nodiscard]] const RawVolume &currentVolume() const;
    [[nodiscard]] const QVector<ChannelRenderSettings> &channelSettings() const;

    Q_INVOKABLE void openUrl(const QUrl &url);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void setLoopPreviewValue(int loopIndex, int value);
    Q_INVOKABLE void commitLoopValue(int loopIndex, int value);
    Q_INVOKABLE void setChannelEnabled(int channelIndex, bool enabled);
    Q_INVOKABLE void setChannelColor(int channelIndex, const QColor &color);
    Q_INVOKABLE void setChannelLow(int channelIndex, double value);
    Q_INVOKABLE void setChannelHigh(int channelIndex, double value);
    Q_INVOKABLE void setChannelPercentiles(int channelIndex, double lowPercentile, double highPercentile);
    Q_INVOKABLE void setLiveAutoEnabled(bool enabled);
    Q_INVOKABLE void autoContrastChannel(int channelIndex);
    Q_INVOKABLE void autoContrastAll();
    Q_INVOKABLE void fit2D();
    Q_INVOKABLE void actualSize2D();
    Q_INVOKABLE void reset3D();
    Q_INVOKABLE void setRoi(int x, int y, int width, int height);
    Q_INVOKABLE void clearRoi();
    Q_INVOKABLE void updateHoveredPixel(int x, int y, bool inside);
    Q_INVOKABLE bool exportRenderedFrame(const QUrl &url, bool roiOnly);
    Q_INVOKABLE void runDeconvolution(int iterations, double sigmaPixels, int kernelRadiusPixels, bool useRoi);
    Q_INVOKABLE void startPlayback(int step);
    Q_INVOKABLE void stopPlayback();

signals:
    void documentStateChanged();
    void busyChanged();
    void statusTextChanged();
    void errorTextChanged();
    void infoTextChanged();
    void pixelTextChanged();
    void fileInfoTextChanged();
    void frameRevisionChanged();
    void channelStateChanged();
    void viewStateChanged();
    void roiChanged();
    void deconvolutionStateChanged();
    void volumeChanged();
    void volumeChannelSettingsChanged();
    void fit2DRequested();
    void actualSize2DRequested();

private slots:
    void handleDocumentChanged();
    void handleCoordinateChanged();
    void handleChannelChanged();
    void handleFrameReady();
    void handleBusyChanged(bool busy);
    void handleStatusTextChanged(const QString &text);
    void handleError(const QString &message);
    void handleVolumeLoadFinished();
    void handleDeconvolutionFinished();
    void advancePlayback();

private:
    [[nodiscard]] int findTimeLoopIndex() const;
    [[nodiscard]] int zLoopIndex() const;
    [[nodiscard]] QString buildInfoText() const;
    [[nodiscard]] QString buildFileInfoText() const;
    void refreshModels();
    void refreshDocumentText();
    void requestVolumeLoad();
    void maybeReloadVolumeForCoordinateChange();
    void setStatusText(const QString &text);
    void setErrorText(const QString &text);
    void updateChannelSetting(int channelIndex, const std::function<void(ChannelRenderSettings &)> &mutator);
    void setPlaybackActive(bool active);

    DocumentController controller_;
    LoopListModel *loopModel_ = nullptr;
    ChannelListModel *channelModel_ = nullptr;
    bool busy_ = false;
    QString statusText_;
    QString errorText_;
    QString infoText_;
    QString fileInfoText_;
    QString pixelText_;
    int frameRevision_ = 0;
    bool volumeViewActive_ = false;
    int volumeLoadGeneration_ = 0;
    RawVolume cachedVolume_;
    QFutureWatcher<VolumeLoadResult> volumeWatcher_;
    QFutureWatcher<DeconvolutionResult> deconvolutionWatcher_;
    bool deconvolutionBusy_ = false;
    QImage deconvolutionImage_;
    int deconvolutionRevision_ = 0;
    QString deconvolutionTitle_;
    QRect roi_;
    QTimer playbackTimer_;
    int playbackLoopIndex_ = -1;
    int playbackStep_ = 1;
};
