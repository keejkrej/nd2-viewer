#include "core/documentcontroller.h"

#include "core/policydocumentreader.h"

#include <QtConcurrent>

namespace
{
bool applyAutoContrastAllChannels(const RawFrame &frame, QVector<ChannelRenderSettings> &settings)
{
    if (!frame.isValid() || settings.isEmpty()) {
        return false;
    }

    bool changed = false;
    ChannelAutoContrastAnalysis sharedSingleChannelAnalysis;
    bool sharedSingleChannelAnalysisReady = false;
    for (int channelIndex = 0; channelIndex < settings.size(); ++channelIndex) {
        if (frame.components == 1) {
            if (!sharedSingleChannelAnalysisReady) {
                sharedSingleChannelAnalysis = FrameRenderer::analyzeChannel(frame, channelIndex);
                sharedSingleChannelAnalysisReady = true;
            }
            changed = FrameRenderer::applyAutoContrastToChannel(sharedSingleChannelAnalysis, settings[channelIndex]) || changed;
            continue;
        }

        changed = FrameRenderer::applyAutoContrastToChannel(FrameRenderer::analyzeChannel(frame, channelIndex), settings[channelIndex])
                  || changed;
    }

    return changed;
}
}

DocumentController::DocumentController(QObject *parent)
    : QObject(parent)
{
    connect(&frameWatcher_, &QFutureWatcher<FrameLoadResult>::finished, this, &DocumentController::handleFrameLoadFinished);
}

DocumentController::~DocumentController()
{
    closeFile();
}

bool DocumentController::openFile(const QString &path)
{
    closeFile();

    QString errorMessage;
    DocumentReaderOptions options = readOptions_;
    options.forcePolicyWrapper = true;
    reader_ = createDocumentReaderForPath(path, options, &errorMessage);
    if (!reader_) {
        emit errorOccurred(errorMessage);
        return false;
    }

    if (!reader_->open(path, &errorMessage)) {
        emit errorOccurred(errorMessage);
        reader_.reset();
        return false;
    }

    coordinateState_.values.fill(0, reader_->documentInfo().loops.size());
    channelSettings_ = FrameRenderer::defaultChannelSettings(reader_->documentInfo());
    currentSequenceIndex_ = -1;
    queuedCoordinateState_ = {};
    hasQueuedFrameLoad_ = false;
    renderedFrame_ = {};
    currentRawFrame_ = {};
    currentFrameMetadataSection_ = {};
    channelSettingsRevision_ = 0;
    liveAutoEnabled_ = false;
    pendingInitialAutoContrast_ = true;

    emit documentChanged();
    emit coordinateStateChanged();
    emit channelSettingsChanged();
    emit metadataChanged();
    emit statusTextChanged(QStringLiteral("Opened %1").arg(path));

    queueFrameLoadForCurrentCoords();
    return true;
}

void DocumentController::closeFile()
{
    if (frameWatcher_.isRunning()) {
        frameWatcher_.waitForFinished();
    }

    if (reader_) {
        reader_->close();
        reader_.reset();
    }
    coordinateState_ = {};
    channelSettings_.clear();
    currentRawFrame_ = {};
    renderedFrame_ = {};
    currentFrameMetadataSection_ = {};
    currentSequenceIndex_ = -1;
    queuedCoordinateState_ = {};
    hasQueuedFrameLoad_ = false;
    channelSettingsRevision_ = 0;
    liveAutoEnabled_ = false;
    pendingInitialAutoContrast_ = false;
    readOptions_ = {};
    setBusy(false);
}

bool DocumentController::hasDocument() const
{
    return reader_ && reader_->isOpen();
}

QString DocumentController::currentPath() const
{
    return reader_ ? reader_->filePath() : QString();
}

const DocumentInfo &DocumentController::documentInfo() const
{
    return reader_ ? reader_->documentInfo() : emptyDocumentInfo();
}

const FrameCoordinateState &DocumentController::coordinateState() const
{
    return coordinateState_;
}

const QVector<ChannelRenderSettings> &DocumentController::channelSettings() const
{
    return channelSettings_;
}

bool DocumentController::liveAutoEnabled() const
{
    return liveAutoEnabled_;
}

const RenderedFrame &DocumentController::renderedFrame() const
{
    return renderedFrame_;
}

const RawFrame &DocumentController::currentRawFrame() const
{
    return currentRawFrame_;
}

const MetadataSection &DocumentController::currentFrameMetadataSection() const
{
    return currentFrameMetadataSection_;
}

QString DocumentController::pixelInfoAt(const QPoint &pixelPosition) const
{
    return FrameRenderer::pixelDescription(currentRawFrame_, pixelPosition);
}

void DocumentController::setReadOptions(const DocumentReaderOptions &options)
{
    readOptions_ = options;
    readOptions_.forcePolicyWrapper = true;
    if (auto *policyReader = dynamic_cast<PolicyDocumentReader *>(reader_.get())) {
        policyReader->setOptions(readOptions_);
    }
}

DocumentReaderOptions DocumentController::readOptions() const
{
    return readOptions_;
}

void DocumentController::setCoordinateValue(int loopIndex, int value)
{
    if (loopIndex < 0 || loopIndex >= coordinateState_.values.size()) {
        return;
    }

    const DocumentInfo &info = reader_->documentInfo();
    if (loopIndex >= info.loops.size()) {
        return;
    }

    const int boundedValue = std::clamp(value, 0, qMax(info.loops.at(loopIndex).size - 1, 0));
    if (coordinateState_.values.at(loopIndex) == boundedValue) {
        return;
    }

    coordinateState_.values[loopIndex] = boundedValue;
    emit coordinateStateChanged();
    queueFrameLoadForCurrentCoords();
}

void DocumentController::setChannelSettings(int channelIndex, const ChannelRenderSettings &settings)
{
    if (channelIndex < 0 || channelIndex >= channelSettings_.size()) {
        return;
    }

    channelSettings_[channelIndex] = settings;
    ++channelSettingsRevision_;
    emit channelSettingsChanged();
    rerenderCurrentFrame(false);
}

void DocumentController::setChannelSettings(const QVector<ChannelRenderSettings> &settings)
{
    if (settings.size() != channelSettings_.size()) {
        return;
    }

    channelSettings_ = settings;
    ++channelSettingsRevision_;
    emit channelSettingsChanged();
    rerenderCurrentFrame(false);
}

void DocumentController::setLiveAutoEnabled(bool enabled)
{
    if (liveAutoEnabled_ == enabled) {
        return;
    }

    liveAutoEnabled_ = enabled;
    emit channelSettingsChanged();
}

void DocumentController::autoContrastChannel(int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= channelSettings_.size()) {
        return;
    }

    if (!currentRawFrame_.isValid()) {
        return;
    }

    const ChannelAutoContrastAnalysis analysis = FrameRenderer::analyzeChannel(currentRawFrame_, channelIndex);
    if (!FrameRenderer::applyAutoContrastToChannel(analysis, channelSettings_[channelIndex])) {
        return;
    }

    ++channelSettingsRevision_;
    emit channelSettingsChanged();
    rerenderCurrentFrame(false);
}

void DocumentController::autoContrastAllChannels()
{
    if (!currentRawFrame_.isValid()) {
        return;
    }

    bool changed = false;
    ChannelAutoContrastAnalysis sharedSingleChannelAnalysis;
    bool sharedSingleChannelAnalysisReady = false;
    for (int channelIndex = 0; channelIndex < channelSettings_.size(); ++channelIndex) {
        if (currentRawFrame_.components == 1) {
            if (!sharedSingleChannelAnalysisReady) {
                sharedSingleChannelAnalysis = FrameRenderer::analyzeChannel(currentRawFrame_, channelIndex);
                sharedSingleChannelAnalysisReady = true;
            }
            changed = FrameRenderer::applyAutoContrastToChannel(sharedSingleChannelAnalysis, channelSettings_[channelIndex]) || changed;
            continue;
        }

        changed = FrameRenderer::applyAutoContrastToChannel(FrameRenderer::analyzeChannel(currentRawFrame_, channelIndex),
                                                            channelSettings_[channelIndex])
                  || changed;
    }

    if (!changed) {
        return;
    }

    ++channelSettingsRevision_;
    emit channelSettingsChanged();
    rerenderCurrentFrame(false);
}

void DocumentController::reloadCurrentFrame()
{
    queueFrameLoadForCurrentCoords();
}

void DocumentController::setBusy(bool busy)
{
    if (busy_ == busy) {
        return;
    }

    busy_ = busy;
    emit busyChanged(busy_);
}

void DocumentController::queueFrameLoadForCurrentCoords()
{
    if (!reader_ || !reader_->isOpen()) {
        emit errorOccurred(QStringLiteral("Open an ND2 or CZI file first."));
        return;
    }

    beginFrameLoad(coordinateState_);
}

void DocumentController::beginFrameLoad(const FrameCoordinateState &coordinates)
{
    if (frameWatcher_.isRunning()) {
        queuedCoordinateState_ = coordinates;
        hasQueuedFrameLoad_ = true;
        return;
    }

    setBusy(true);
    const int requestId = ++requestCounter_;
    const int settingsRevision = channelSettingsRevision_;
    const bool applyInitialAutoContrast = pendingInitialAutoContrast_;
    const bool liveAutoEnabled = liveAutoEnabled_;
    const QVector<ChannelRenderSettings> channelSettings = channelSettings_;
    frameWatcher_.setFuture(QtConcurrent::run([this, requestId, settingsRevision, coordinates, channelSettings,
                                               applyInitialAutoContrast, liveAutoEnabled]() {
        FrameLoadResult result;
        result.requestId = requestId;
        result.settingsRevision = settingsRevision;

        QString frameError;
        RawFrame frame = reader_->readFrameForCoords(coordinates.values, &frameError);
        if (!frame.isValid()) {
            result.error = frameError;
            return result;
        }

        QString metadataError;
        result.metadataSection = reader_->frameMetadataForCoords(coordinates.values, &metadataError);
        result.frame = frame;
        result.sequenceIndex = frame.sequenceIndex;
        result.channelSettings = channelSettings;
        result.channelSettingsChanged =
            (applyInitialAutoContrast || liveAutoEnabled) ? applyAutoContrastAllChannels(result.frame, result.channelSettings) : false;
        result.renderedFrame = FrameRenderer::render(result.frame, coordinates, result.channelSettings);
        result.success = true;
        if (!metadataError.isEmpty()) {
            result.error = metadataError;
        }
        return result;
    }));
}

void DocumentController::handleFrameLoadFinished()
{
    const FrameLoadResult result = frameWatcher_.result();
    if (result.requestId != requestCounter_) {
        finishQueuedFrameIfNeeded();
        return;
    }

    if (!result.success || !result.frame.isValid()) {
        setBusy(false);
        if (!result.error.isEmpty()) {
            emit errorOccurred(result.error);
        }
        finishQueuedFrameIfNeeded();
        return;
    }

    currentSequenceIndex_ = result.frame.sequenceIndex;
    currentRawFrame_ = result.frame;
    currentFrameMetadataSection_ = result.metadataSection;
    pendingInitialAutoContrast_ = false;

    if (result.settingsRevision == channelSettingsRevision_) {
        channelSettings_ = result.channelSettings;
        renderedFrame_ = result.renderedFrame;
        if (result.channelSettingsChanged) {
            emit channelSettingsChanged();
        }
        emit frameReady();
    } else {
        rerenderCurrentFrame(true);
    }
    emit metadataChanged();
    emit statusTextChanged(QStringLiteral("Frame %1 / %2").arg(currentSequenceIndex_ + 1).arg(reader_->sequenceCount()));

    finishQueuedFrameIfNeeded();
}

void DocumentController::finishQueuedFrameIfNeeded()
{
    if (hasQueuedFrameLoad_ && queuedCoordinateState_.values != renderedFrame_.coordinates.values) {
        const FrameCoordinateState nextCoordinates = queuedCoordinateState_;
        queuedCoordinateState_ = {};
        hasQueuedFrameLoad_ = false;
        beginFrameLoad(nextCoordinates);
        return;
    }

    queuedCoordinateState_ = {};
    hasQueuedFrameLoad_ = false;
    setBusy(false);
}

void DocumentController::rerenderCurrentFrame(bool updateAutoContrast)
{
    if (!currentRawFrame_.isValid()) {
        return;
    }

    if (updateAutoContrast && liveAutoEnabled_ && applyAutoContrastAllChannels(currentRawFrame_, channelSettings_)) {
        emit channelSettingsChanged();
    }

    renderedFrame_ = FrameRenderer::render(currentRawFrame_, coordinateState_, channelSettings_);
    emit frameReady();
}

const DocumentInfo &DocumentController::emptyDocumentInfo()
{
    static const DocumentInfo info;
    return info;
}
