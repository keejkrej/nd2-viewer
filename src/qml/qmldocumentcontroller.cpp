#include "qml/qmldocumentcontroller.h"

#include "core/framerenderer.h"
#include "core/volumeutils.h"
#include "qml/channellistmodel.h"
#include "qml/looplistmodel.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
#include <QStandardPaths>

#include <QtConcurrent>

#include <algorithm>

QmlDocumentController::QmlDocumentController(QObject *parent)
    : QObject(parent)
    , loopModel_(new LoopListModel(this))
    , channelModel_(new ChannelListModel(this))
{
    connect(&controller_, &DocumentController::documentChanged, this, &QmlDocumentController::handleDocumentChanged);
    connect(&controller_, &DocumentController::coordinateStateChanged, this, &QmlDocumentController::handleCoordinateChanged);
    connect(&controller_, &DocumentController::channelSettingsChanged, this, &QmlDocumentController::handleChannelChanged);
    connect(&controller_, &DocumentController::frameReady, this, &QmlDocumentController::handleFrameReady);
    connect(&controller_, &DocumentController::metadataChanged, this, &QmlDocumentController::refreshDocumentText);
    connect(&controller_, &DocumentController::busyChanged, this, &QmlDocumentController::handleBusyChanged);
    connect(&controller_, &DocumentController::statusTextChanged, this, &QmlDocumentController::handleStatusTextChanged);
    connect(&controller_, &DocumentController::errorOccurred, this, &QmlDocumentController::handleError);
    connect(&volumeWatcher_, &QFutureWatcher<VolumeLoadResult>::finished, this, &QmlDocumentController::handleVolumeLoadFinished);
    connect(&deconvolutionWatcher_, &QFutureWatcher<DeconvolutionResult>::finished,
            this, &QmlDocumentController::handleDeconvolutionFinished);
    connect(&playbackTimer_, &QTimer::timeout, this, &QmlDocumentController::advancePlayback);
    playbackTimer_.setInterval(100);

    refreshModels();
    refreshDocumentText();
}

QAbstractItemModel *QmlDocumentController::loopModel() const { return loopModel_; }
QAbstractItemModel *QmlDocumentController::channelModel() const { return channelModel_; }
bool QmlDocumentController::hasDocument() const { return controller_.hasDocument(); }
bool QmlDocumentController::busy() const { return busy_; }
QString QmlDocumentController::windowTitle() const
{
    return controller_.hasDocument()
               ? QStringLiteral("%1 - nd2-viewer").arg(QFileInfo(controller_.currentPath()).fileName())
               : QStringLiteral("nd2-viewer");
}
QString QmlDocumentController::statusText() const { return statusText_; }
QString QmlDocumentController::errorText() const { return errorText_; }
QString QmlDocumentController::infoText() const { return infoText_; }
QString QmlDocumentController::pixelText() const { return pixelText_; }
QString QmlDocumentController::fileInfoText() const { return fileInfoText_; }
int QmlDocumentController::frameRevision() const { return frameRevision_; }
bool QmlDocumentController::liveAutoEnabled() const { return controller_.liveAutoEnabled(); }
bool QmlDocumentController::volumeAvailable() const { return controller_.hasDocument() && zLoopIndex() >= 0; }
bool QmlDocumentController::volumeViewActive() const { return volumeViewActive_; }
QString QmlDocumentController::viewActionText() const { return volumeViewActive_ ? tr("Reset") : tr("Fit"); }
bool QmlDocumentController::deconvolutionBusy() const { return deconvolutionBusy_; }
bool QmlDocumentController::hasDeconvolutionResult() const { return !deconvolutionImage_.isNull(); }
int QmlDocumentController::deconvolutionRevision() const { return deconvolutionRevision_; }
QString QmlDocumentController::deconvolutionTitle() const { return deconvolutionTitle_; }
const QImage &QmlDocumentController::currentImage() const { return controller_.renderedFrame().image; }
const QImage &QmlDocumentController::deconvolutionImage() const { return deconvolutionImage_; }
QString QmlDocumentController::pixelInfoAt(const QPoint &pixelPosition) const { return controller_.pixelInfoAt(pixelPosition); }
bool QmlDocumentController::hasCurrentRoi() const { return !roi_.isNull() && roi_.isValid(); }
QRect QmlDocumentController::currentRoi() const { return roi_; }
const RawVolume &QmlDocumentController::currentVolume() const { return cachedVolume_; }
const QVector<ChannelRenderSettings> &QmlDocumentController::channelSettings() const { return controller_.channelSettings(); }

void QmlDocumentController::openUrl(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty()) {
        return;
    }
    controller_.openFile(path);
}

void QmlDocumentController::reload()
{
    if (controller_.hasDocument()) {
        controller_.reloadCurrentFrame();
    }
}

void QmlDocumentController::clearError()
{
    setErrorText({});
}

void QmlDocumentController::setLoopPreviewValue(int loopIndex, int value)
{
    loopModel_->setPreviewValue(loopIndex, value);
}

void QmlDocumentController::commitLoopValue(int loopIndex, int value)
{
    if (volumeViewActive_ && loopIndex == zLoopIndex()) {
        return;
    }
    loopModel_->setCommittedValue(loopIndex, value);
    controller_.setCoordinateValue(loopIndex, value);
}

void QmlDocumentController::setChannelEnabled(int channelIndex, bool enabled)
{
    updateChannelSetting(channelIndex, [enabled](ChannelRenderSettings &settings) {
        settings.enabled = enabled;
    });
}

void QmlDocumentController::setChannelColor(int channelIndex, const QColor &color)
{
    updateChannelSetting(channelIndex, [color](ChannelRenderSettings &settings) {
        settings.color = color;
    });
}

void QmlDocumentController::setChannelLow(int channelIndex, double value)
{
    updateChannelSetting(channelIndex, [value](ChannelRenderSettings &settings) {
        settings.low = value;
    });
}

void QmlDocumentController::setChannelHigh(int channelIndex, double value)
{
    updateChannelSetting(channelIndex, [value](ChannelRenderSettings &settings) {
        settings.high = value;
    });
}

void QmlDocumentController::setChannelPercentiles(int channelIndex, double lowPercentile, double highPercentile)
{
    updateChannelSetting(channelIndex, [lowPercentile, highPercentile](ChannelRenderSettings &settings) {
        settings.lowPercentile = std::clamp(lowPercentile, 0.0, 100.0);
        settings.highPercentile = std::clamp(highPercentile, 0.0, 100.0);
    });
    if (controller_.liveAutoEnabled()) {
        autoContrastChannel(channelIndex);
    }
}

void QmlDocumentController::setLiveAutoEnabled(bool enabled)
{
    controller_.setLiveAutoEnabled(enabled);
    if (enabled) {
        autoContrastAll();
    }
}

void QmlDocumentController::autoContrastChannel(int channelIndex)
{
    if (volumeViewActive_ && cachedVolume_.isValid()) {
        QVector<ChannelRenderSettings> settings = controller_.channelSettings();
        if (channelIndex < 0 || channelIndex >= settings.size()) {
            return;
        }
        ChannelRenderSettings channelSettings = settings.at(channelIndex);
        if (FrameRenderer::applyAutoContrastToChannelFromZSlices(cachedVolume_, channelIndex, channelSettings)) {
            controller_.setChannelSettings(channelIndex, channelSettings);
        }
        return;
    }
    controller_.autoContrastChannel(channelIndex);
}

void QmlDocumentController::autoContrastAll()
{
    if (volumeViewActive_ && cachedVolume_.isValid()) {
        QVector<ChannelRenderSettings> settings = controller_.channelSettings();
        for (int channelIndex = 0; channelIndex < settings.size(); ++channelIndex) {
            ChannelRenderSettings channelSettings = settings.at(channelIndex);
            if (FrameRenderer::applyAutoContrastToChannelFromZSlices(cachedVolume_, channelIndex, channelSettings)) {
                settings[channelIndex] = channelSettings;
            }
        }
        controller_.setChannelSettings(settings);
        return;
    }
    controller_.autoContrastAllChannels();
}

void QmlDocumentController::fit2D()
{
    emit fit2DRequested();
}

void QmlDocumentController::actualSize2D()
{
    emit actualSize2DRequested();
}

void QmlDocumentController::reset3D()
{
    emit viewStateChanged();
}

void QmlDocumentController::setRoi(int x, int y, int width, int height)
{
    roi_ = QRect(x, y, width, height).normalized();
    emit roiChanged();
}

void QmlDocumentController::clearRoi()
{
    roi_ = {};
    emit roiChanged();
}

void QmlDocumentController::updateHoveredPixel(int x, int y, bool inside)
{
    const QString text = inside ? controller_.pixelInfoAt(QPoint(x, y)) : QString();
    if (pixelText_ == text) {
        return;
    }
    pixelText_ = text;
    emit pixelTextChanged();
}

bool QmlDocumentController::exportRenderedFrame(const QUrl &url, bool roiOnly)
{
    QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty() || controller_.renderedFrame().image.isNull()) {
        return false;
    }
    if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".png");
    }

    QImage image = controller_.renderedFrame().image;
    if (roiOnly && hasCurrentRoi()) {
        image = image.copy(roi_.intersected(image.rect()));
    }
    const bool saved = !image.isNull() && image.save(path, "PNG");
    setStatusText(saved ? tr("Saved %1").arg(path) : tr("Could not save %1").arg(path));
    return saved;
}

void QmlDocumentController::runDeconvolution(int iterations, double sigmaPixels, int kernelRadiusPixels, bool useRoi)
{
    if (deconvolutionBusy_ || volumeViewActive_ || !controller_.currentRawFrame().isValid()) {
        return;
    }

    DeconvolutionSettings settings;
    settings.iterations = std::max(1, iterations);
    settings.gaussianSigmaPixels = std::max(0.1, sigmaPixels);
    settings.kernelRadiusPixels = std::max(1, kernelRadiusPixels);
    settings.useRoi = useRoi && hasCurrentRoi();
    settings.roiRect = roi_;

    const RawFrame frame = controller_.currentRawFrame();
    const FrameCoordinateState coordinates = controller_.coordinateState();
    const QVector<ChannelRenderSettings> channelSettings = controller_.channelSettings();
    deconvolutionBusy_ = true;
    deconvolutionTitle_ = tr("Deconvolution - iter %1, sigma %2 px, radius %3 px")
                              .arg(settings.iterations)
                              .arg(settings.gaussianSigmaPixels)
                              .arg(settings.kernelRadiusPixels);
    emit deconvolutionStateChanged();
    deconvolutionWatcher_.setFuture(QtConcurrent::run([frame, coordinates, channelSettings, settings]() {
        return DeconvolutionProcessor::run2D(frame, coordinates, channelSettings, settings);
    }));
}

void QmlDocumentController::startPlayback(int step)
{
    if (!controller_.hasDocument()) {
        return;
    }
    playbackLoopIndex_ = findTimeLoopIndex();
    if (playbackLoopIndex_ < 0) {
        return;
    }
    playbackStep_ = std::max(1, step);
    setPlaybackActive(true);
}

void QmlDocumentController::stopPlayback()
{
    setPlaybackActive(false);
}

void QmlDocumentController::setVolumeViewActive(bool active)
{
    if (active && !volumeAvailable()) {
        active = false;
    }
    if (volumeViewActive_ == active) {
        return;
    }
    volumeViewActive_ = active;
    refreshModels();
    emit viewStateChanged();
    if (volumeViewActive_) {
        requestVolumeLoad();
    }
}

void QmlDocumentController::handleDocumentChanged()
{
    cachedVolume_ = {};
    volumeLoadGeneration_ = 0;
    volumeViewActive_ = false;
    roi_ = {};
    deconvolutionImage_ = {};
    ++frameRevision_;
    refreshModels();
    refreshDocumentText();
    emit documentStateChanged();
    emit viewStateChanged();
    emit roiChanged();
    emit frameRevisionChanged();
    emit deconvolutionStateChanged();
}

void QmlDocumentController::handleCoordinateChanged()
{
    refreshModels();
    refreshDocumentText();
    maybeReloadVolumeForCoordinateChange();
}

void QmlDocumentController::handleChannelChanged()
{
    channelModel_->setSettings(controller_.channelSettings());
    emit channelStateChanged();
    emit volumeChannelSettingsChanged();
}

void QmlDocumentController::handleFrameReady()
{
    ++frameRevision_;
    refreshDocumentText();
    emit frameRevisionChanged();
}

void QmlDocumentController::handleBusyChanged(bool busy)
{
    if (busy_ == busy) {
        return;
    }
    busy_ = busy;
    emit busyChanged();
}

void QmlDocumentController::handleStatusTextChanged(const QString &text)
{
    setStatusText(text);
}

void QmlDocumentController::handleError(const QString &message)
{
    setErrorText(message);
}

void QmlDocumentController::handleVolumeLoadFinished()
{
    const VolumeLoadResult result = volumeWatcher_.result();
    if (result.loadRequestId != volumeLoadGeneration_) {
        return;
    }
    if (!result.success || !result.volume.isValid()) {
        cachedVolume_ = {};
        setVolumeViewActive(false);
        setErrorText(result.error.isEmpty() ? tr("The 3D volume could not be prepared.") : result.error);
        return;
    }
    cachedVolume_ = result.volume;
    setStatusText(tr("Loaded 3D volume %1 × %2 × %3")
                      .arg(cachedVolume_.width)
                      .arg(cachedVolume_.height)
                      .arg(cachedVolume_.depth));
    emit volumeChanged();
}

void QmlDocumentController::handleDeconvolutionFinished()
{
    const DeconvolutionResult result = deconvolutionWatcher_.result();
    deconvolutionBusy_ = false;
    if (!result.success || result.image.isNull()) {
        setErrorText(result.errorMessage.isEmpty() ? tr("Deconvolution failed.") : result.errorMessage);
    } else {
        deconvolutionImage_ = result.image;
        ++deconvolutionRevision_;
        setStatusText(tr("Deconvolution complete."));
    }
    emit deconvolutionStateChanged();
}

void QmlDocumentController::advancePlayback()
{
    if (!controller_.hasDocument() || playbackLoopIndex_ < 0 || playbackLoopIndex_ >= controller_.documentInfo().loops.size()) {
        stopPlayback();
        return;
    }
    const int maximum = qMax(controller_.documentInfo().loops.at(playbackLoopIndex_).size - 1, 0);
    const int current = playbackLoopIndex_ < controller_.coordinateState().values.size()
                            ? controller_.coordinateState().values.at(playbackLoopIndex_)
                            : 0;
    const int next = current + playbackStep_ > maximum ? 0 : current + playbackStep_;
    commitLoopValue(playbackLoopIndex_, next);
}

int QmlDocumentController::findTimeLoopIndex() const
{
    const DocumentInfo &info = controller_.documentInfo();
    for (int index = 0; index < info.loops.size(); ++index) {
        const QString label = info.loops.at(index).label;
        if (label.compare(QStringLiteral("Time"), Qt::CaseInsensitive) == 0
            || label.compare(QStringLiteral("T"), Qt::CaseInsensitive) == 0) {
            return index;
        }
    }
    for (int index = 0; index < info.loops.size(); ++index) {
        if (info.loops.at(index).type == QStringLiteral("NETimeLoop")) {
            return index;
        }
    }
    return -1;
}

int QmlDocumentController::zLoopIndex() const
{
    return VolumeUtils::findZLoopIndex(controller_.documentInfo());
}

QString QmlDocumentController::buildInfoText() const
{
    if (!controller_.hasDocument()) {
        return tr("No file loaded");
    }
    const DocumentInfo &info = controller_.documentInfo();
    QStringList coords;
    for (int index = 0; index < info.loops.size() && index < controller_.coordinateState().values.size(); ++index) {
        coords << QStringLiteral("%1=%2").arg(info.loops.at(index).label).arg(controller_.coordinateState().values.at(index));
    }
    return QStringLiteral("%1  |  %2 x %3  |  %4")
        .arg(QFileInfo(controller_.currentPath()).fileName())
        .arg(info.frameSize.width())
        .arg(info.frameSize.height())
        .arg(coords.join(QStringLiteral(", ")));
}

QString QmlDocumentController::buildFileInfoText() const
{
    if (!controller_.hasDocument()) {
        return tr("No file loaded");
    }
    const DocumentInfo &info = controller_.documentInfo();
    QStringList lines;
    lines << QStringLiteral("Path: %1").arg(info.filePath);
    lines << QStringLiteral("Frames: %1").arg(info.sequenceCount);
    lines << QStringLiteral("Size: %1 x %2").arg(info.frameSize.width()).arg(info.frameSize.height());
    lines << QStringLiteral("Components: %1").arg(info.componentCount);
    lines << QStringLiteral("Pixel type: %1, %2-bit significant")
                 .arg(info.pixelDataType)
                 .arg(info.bitsPerComponentSignificant);
    lines << QStringLiteral("");
    lines << QStringLiteral("Loops");
    for (const LoopInfo &loop : info.loops) {
        lines << QStringLiteral("  %1: %2 steps (%3)").arg(loop.label).arg(loop.size).arg(loop.type);
    }
    lines << QStringLiteral("");
    lines << QStringLiteral("Channels");
    for (const ChannelInfo &channel : info.channels) {
        lines << QStringLiteral("  %1: %2").arg(channel.index).arg(channel.name);
    }
    if (!controller_.currentFrameMetadataSection().rawText.isEmpty()) {
        lines << QStringLiteral("");
        lines << QStringLiteral("Current Frame Metadata");
        lines << controller_.currentFrameMetadataSection().rawText;
    }
    return lines.join(QStringLiteral("\n"));
}

void QmlDocumentController::refreshModels()
{
    const int lockedLoop = volumeViewActive_ ? zLoopIndex() : -1;
    loopModel_->setState(controller_.documentInfo(), controller_.coordinateState(), findTimeLoopIndex(), lockedLoop);
    channelModel_->setChannels(controller_.documentInfo().channels, controller_.channelSettings());
}

void QmlDocumentController::refreshDocumentText()
{
    infoText_ = buildInfoText();
    fileInfoText_ = buildFileInfoText();
    emit infoTextChanged();
    emit fileInfoTextChanged();
}

void QmlDocumentController::requestVolumeLoad()
{
    if (!volumeViewActive_ || !volumeAvailable()) {
        return;
    }
    ++volumeLoadGeneration_;
    const int generation = volumeLoadGeneration_;
    const QString path = controller_.currentPath();
    const DocumentInfo info = controller_.documentInfo();
    const FrameCoordinateState coordinates = controller_.coordinateState();
    const QVector<ChannelRenderSettings> settings = controller_.channelSettings();
    setStatusText(tr("Loading 3D volume..."));
    volumeWatcher_.setFuture(QtConcurrent::run([path, info, coordinates, settings, generation]() {
        VolumeLoadResult result = VolumeLoader::load(path, info, coordinates, settings);
        result.loadRequestId = generation;
        return result;
    }));
}

void QmlDocumentController::maybeReloadVolumeForCoordinateChange()
{
    if (!volumeViewActive_ || !cachedVolume_.isValid()) {
        return;
    }
    const int z = zLoopIndex();
    const QVector<int> &current = controller_.coordinateState().values;
    const QVector<int> &fixed = cachedVolume_.fixedCoordinates.values;
    for (int index = 0; index < controller_.documentInfo().loops.size(); ++index) {
        if (index == z) {
            continue;
        }
        const int currentValue = index < current.size() ? current.at(index) : 0;
        const int fixedValue = index < fixed.size() ? fixed.at(index) : 0;
        if (currentValue != fixedValue) {
            requestVolumeLoad();
            return;
        }
    }
}

void QmlDocumentController::setStatusText(const QString &text)
{
    if (statusText_ == text) {
        return;
    }
    statusText_ = text;
    emit statusTextChanged();
}

void QmlDocumentController::setErrorText(const QString &text)
{
    if (errorText_ == text) {
        return;
    }
    errorText_ = text;
    emit errorTextChanged();
}

void QmlDocumentController::updateChannelSetting(int channelIndex,
                                                 const std::function<void(ChannelRenderSettings &)> &mutator)
{
    QVector<ChannelRenderSettings> settings = controller_.channelSettings();
    if (channelIndex < 0 || channelIndex >= settings.size()) {
        return;
    }
    mutator(settings[channelIndex]);
    controller_.setChannelSettings(channelIndex, settings.at(channelIndex));
}

void QmlDocumentController::setPlaybackActive(bool active)
{
    if (active) {
        playbackTimer_.start();
    } else {
        playbackTimer_.stop();
    }
}
