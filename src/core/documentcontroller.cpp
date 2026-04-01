#include "core/documentcontroller.h"

#include <QtConcurrent>

DocumentController::DocumentController(QObject *parent)
    : QObject(parent)
    , frameCache_(10)
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
    if (!reader_.open(path, &errorMessage)) {
        emit errorOccurred(errorMessage);
        return false;
    }

    coordinateState_.values.fill(0, reader_.documentInfo().loops.size());
    channelSettings_ = FrameRenderer::defaultChannelSettings(reader_.documentInfo());
    currentSequenceIndex_ = -1;
    queuedSequenceIndex_ = -1;
    renderedFrame_ = {};
    currentRawFrame_ = {};
    currentFrameMetadata_ = {};
    currentFrameMetadataText_.clear();
    frameCache_.clear();

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

    reader_.close();
    frameCache_.clear();
    coordinateState_ = {};
    channelSettings_.clear();
    currentRawFrame_ = {};
    renderedFrame_ = {};
    currentFrameMetadata_ = {};
    currentFrameMetadataText_.clear();
    currentSequenceIndex_ = -1;
    queuedSequenceIndex_ = -1;
    setBusy(false);
}

bool DocumentController::hasDocument() const
{
    return reader_.isOpen();
}

QString DocumentController::currentPath() const
{
    return reader_.filePath();
}

const Nd2DocumentInfo &DocumentController::documentInfo() const
{
    return reader_.documentInfo();
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

QString DocumentController::currentFrameMetadataText() const
{
    return currentFrameMetadataText_;
}

QJsonDocument DocumentController::currentFrameMetadata() const
{
    return currentFrameMetadata_;
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

    const Nd2DocumentInfo &info = reader_.documentInfo();
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
    emit channelSettingsChanged();
    rerenderCurrentFrame(false);
}

void DocumentController::autoContrastChannel(int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= channelSettings_.size()) {
        return;
    }

    channelSettings_[channelIndex].autoContrast = true;
    rerenderCurrentFrame(true);
}

void DocumentController::autoContrastAllChannels()
{
    for (ChannelRenderSettings &settings : channelSettings_) {
        settings.autoContrast = true;
    }
    rerenderCurrentFrame(true);
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
    frameWatcher_.setFuture(QtConcurrent::run([this, requestId, sequenceIndex]() {
        FrameLoadResult result;
        result.requestId = requestId;
        result.sequenceIndex = sequenceIndex;

        RawFrame frame;
        if (!frameCache_.tryGet(sequenceIndex, &frame)) {
            QString frameError;
            frame = reader_.readFrame(sequenceIndex, &frameError);
            if (!frame.isValid()) {
                result.error = frameError;
                return result;
            }
            frameCache_.insert(frame);
        }

        QString metadataError;
        result.metadataText = reader_.frameMetadataText(sequenceIndex, &metadataError);
        result.metadata = reader_.frameMetadata(sequenceIndex);
        result.frame = frame;
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
    currentFrameMetadata_ = result.metadata;
    currentFrameMetadataText_ = result.metadataText;

    rerenderCurrentFrame(true);
    emit metadataChanged();
    emit statusTextChanged(QStringLiteral("Frame %1 / %2").arg(currentSequenceIndex_ + 1).arg(reader_.sequenceCount()));

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
    if (!reader_.isOpen()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Open an ND2 file first.");
        }
        return -1;
    }

    if (coordinateState_.values.isEmpty()) {
        return 0;
    }

    int sequenceIndex = -1;
    if (!reader_.sequenceForCoords(coordinateState_.values, &sequenceIndex, errorMessage)) {
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
