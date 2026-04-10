#include "core/documentcontroller.h"

#include <QtConcurrent>

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
    reader_ = createDocumentReaderForPath(path, &errorMessage);
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
    queuedSequenceIndex_ = -1;
    renderedFrame_ = {};
    currentRawFrame_ = {};
    currentFrameMetadataSection_ = {};
    channelSettingsRevision_ = 0;

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
    queuedSequenceIndex_ = -1;
    channelSettingsRevision_ = 0;
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
    QString errorMessage;
    const int sequenceIndex = resolveSequenceIndexForCurrentState(&errorMessage);
    if (sequenceIndex < 0) {
        emit errorOccurred(errorMessage);
        return;
    }

    beginFrameLoad(sequenceIndex);
}

void DocumentController::beginFrameLoad(int sequenceIndex)
{
    if (frameWatcher_.isRunning()) {
        queuedSequenceIndex_ = sequenceIndex;
        return;
    }

    setBusy(true);
    const int requestId = ++requestCounter_;
    const int settingsRevision = channelSettingsRevision_;
    const FrameCoordinateState coordinates = coordinateState_;
    const QVector<ChannelRenderSettings> channelSettings = channelSettings_;
    frameWatcher_.setFuture(QtConcurrent::run([this, requestId, settingsRevision, sequenceIndex, coordinates, channelSettings]() {
        FrameLoadResult result;
        result.requestId = requestId;
        result.settingsRevision = settingsRevision;
        result.sequenceIndex = sequenceIndex;

        QString frameError;
        RawFrame frame = reader_->readFrame(sequenceIndex, &frameError);
        if (!frame.isValid()) {
            result.error = frameError;
            return result;
        }

        QString metadataError;
        result.metadataSection = reader_->frameMetadataSection(sequenceIndex, &metadataError);
        result.frame = frame;
        result.channelSettings = channelSettings;
        result.channelSettingsChanged = FrameRenderer::applyAutoContrast(result.frame, result.channelSettings);
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

    currentSequenceIndex_ = result.sequenceIndex;
    currentRawFrame_ = result.frame;
    currentFrameMetadataSection_ = result.metadataSection;

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
    if (queuedSequenceIndex_ >= 0 && queuedSequenceIndex_ != currentSequenceIndex_) {
        const int nextSequence = queuedSequenceIndex_;
        queuedSequenceIndex_ = -1;
        beginFrameLoad(nextSequence);
        return;
    }

    queuedSequenceIndex_ = -1;
    setBusy(false);
}

int DocumentController::resolveSequenceIndexForCurrentState(QString *errorMessage) const
{
    if (!reader_ || !reader_->isOpen()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Open an ND2 or CZI file first.");
        }
        return -1;
    }

    if (coordinateState_.values.isEmpty()) {
        return 0;
    }

    int sequenceIndex = -1;
    if (!reader_->sequenceForCoords(coordinateState_.values, &sequenceIndex, errorMessage)) {
        return -1;
    }

    return sequenceIndex;
}

void DocumentController::rerenderCurrentFrame(bool updateAutoContrast)
{
    if (!currentRawFrame_.isValid()) {
        return;
    }

    if (updateAutoContrast && FrameRenderer::applyAutoContrast(currentRawFrame_, channelSettings_)) {
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
