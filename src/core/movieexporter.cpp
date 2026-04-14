#include "core/movieexporter.h"

#include "core/documentreaderfactory.h"
#include "core/framerenderer.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QSet>
#include <QTextStream>
#include <QThread>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoFrameInput>

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

MovieExportResult validateSettingsWithReader(const MovieExportSettings &settings, DocumentReader *reader)
{
    MovieExportResult result;
    result.outputPath = settings.outputPath;

    if (settings.sourcePath.isEmpty()) {
        result.errorMessage = QObject::tr("No ND2 or CZI file is available for movie export.");
        return result;
    }

    if (settings.outputPath.isEmpty()) {
        result.errorMessage = QObject::tr("Choose an output MP4 path before exporting.");
        return result;
    }

    if (settings.timeLoopIndex < 0) {
        result.errorMessage = QObject::tr("This file does not expose a time loop for movie export.");
        return result;
    }

    if (settings.fps <= 0.0) {
        result.errorMessage = QObject::tr("The movie FPS must be greater than zero.");
        return result;
    }

    if (!reader->open(settings.sourcePath, &result.errorMessage)) {
        if (result.errorMessage.isEmpty()) {
            result.errorMessage = QObject::tr("Failed to open the source file for export.");
        }
        return result;
    }

    const DocumentInfo &info = reader->documentInfo();
    if (settings.timeLoopIndex >= info.loops.size()) {
        result.errorMessage = QObject::tr("The selected time loop is no longer available.");
        reader->close();
        return result;
    }

    const LoopInfo &timeLoop = info.loops.at(settings.timeLoopIndex);
    if (timeLoop.size <= 0) {
        result.errorMessage = QObject::tr("The selected time loop is empty.");
        reader->close();
        return result;
    }

    if (settings.fixedCoordinates.size() != info.loops.size()) {
        result.errorMessage = QObject::tr("The current loop coordinates do not match this document.");
        reader->close();
        return result;
    }

    if (settings.startFrame < 0 || settings.endFrame < settings.startFrame || settings.endFrame >= timeLoop.size) {
        result.errorMessage = QObject::tr("The requested time range is outside the available frames.");
        reader->close();
        return result;
    }

    if (movieExportFrameCount(settings) <= 0) {
        result.errorMessage = QObject::tr("The requested time range does not contain any exportable frames.");
        reader->close();
        return result;
    }

    if (!settings.outputSize.isValid()) {
        result.errorMessage = QObject::tr("The rendered export size is invalid.");
        reader->close();
        return result;
    }

    QMediaFormat format(QMediaFormat::MPEG4);
    format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    format.setAudioCodec(QMediaFormat::AudioCodec::Unspecified);
    if (!format.isSupported(QMediaFormat::Encode)) {
        result.errorMessage = QObject::tr("MP4/H.264 export is not available in this Qt Multimedia backend.");
        reader->close();
        return result;
    }

    result.success = true;
    return result;
}
} // namespace

QString buildMovieExportWarningReportPath(const QString &outputPath)
{
    const QFileInfo info(outputPath);
    return info.dir().filePath(info.completeBaseName() + QStringLiteral(".warnings.txt"));
}

bool writeMovieExportWarningReport(const QString &reportPath,
                                   const MovieExportSettings &settings,
                                   bool volumeView,
                                   const QVector<ReadIssue> &issues,
                                   QString *errorMessage)
{
    QFile file(reportPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Could not write %1.").arg(QDir::toNativeSeparators(reportPath));
        }
        return false;
    }

    QSet<int> affectedTimeValues;
    for (const ReadIssue &issue : issues) {
        if (settings.timeLoopIndex >= 0 && settings.timeLoopIndex < issue.coordinates.size()) {
            affectedTimeValues.insert(issue.coordinates.at(settings.timeLoopIndex));
        }
    }

    QTextStream stream(&file);
    stream << "nd2-viewer movie export warning report\n";
    stream << "Mode: " << (volumeView ? "3D" : "2D") << "\n";
    stream << "Policy: SubstituteBlack\n";
    stream << "Source: " << QDir::toNativeSeparators(settings.sourcePath) << "\n";
    stream << "Output: " << QDir::toNativeSeparators(settings.outputPath) << "\n";
    stream << "Affected export timepoints: " << affectedTimeValues.size() << "\n";
    stream << "Underlying read substitutions: " << issues.size() << "\n\n";

    for (const ReadIssue &issue : issues) {
        stream << "- ";
        stream << (issue.kind == ReadIssueKind::FrameSubstituted ? "frame-substituted" : "metadata-unavailable");
        if (!issue.coordinateSummary.isEmpty()) {
            stream << " at " << issue.coordinateSummary;
        }
        if (issue.sequenceIndex >= 0) {
            stream << " (sequence " << issue.sequenceIndex + 1 << ")";
        }
        if (!issue.message.isEmpty()) {
            stream << ": " << issue.message;
        }
        stream << "\n";
    }

    return true;
}

QVector<int> buildTimeFrameValues(int startFrame, int endFrame, int step)
{
    QVector<int> values;
    if (endFrame < startFrame) {
        return values;
    }

    const int safeStep = qMax(step, 1);
    for (int timeValue = startFrame; timeValue <= endFrame; timeValue += safeStep) {
        values.push_back(timeValue);
    }
    return values;
}

QVector<int> buildTimeFrameValues(const MovieExportSettings &settings)
{
    if (settings.timeLoopIndex < 0) {
        return {};
    }

    return buildTimeFrameValues(settings.startFrame, settings.endFrame, settings.frameStep());
}

MovieExportEstimate estimateMovieExport(const MovieExportSettings &settings, const QImage &sampleImage)
{
    MovieExportEstimate estimate;

    if (settings.timeLoopIndex < 0) {
        estimate.errorMessage = QObject::tr("This file does not expose a time loop for movie export.");
        return estimate;
    }

    if (settings.fps <= 0.0) {
        estimate.errorMessage = QObject::tr("The movie FPS must be greater than zero.");
        return estimate;
    }

    if (!settings.outputSize.isValid()) {
        estimate.errorMessage = QObject::tr("The rendered export size is invalid.");
        return estimate;
    }

    estimate.frameCount = movieExportFrameCount(settings);
    if (estimate.frameCount <= 0) {
        estimate.errorMessage = QObject::tr("The requested time range does not contain any exportable frames.");
        return estimate;
    }

    estimate.outputSize = settings.outputSize;
    estimate.durationSeconds = estimate.frameCount / settings.fps;

    QImage image = sampleImage;
    if (image.size() != settings.outputSize) {
        image = image.scaled(settings.outputSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    image = image.convertToFormat(QImage::Format_ARGB32);
    if (image.isNull()) {
        estimate.errorMessage = QObject::tr("The current rendered frame is unavailable for file size estimation.");
        return estimate;
    }

    QByteArray compressedBytes;
    QBuffer buffer(&compressedBytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "JPEG", 90)) {
        estimate.errorMessage = QObject::tr("The current rendered frame could not be encoded for file size estimation.");
        return estimate;
    }

    estimate.estimatedBytes = qMax<qint64>(compressedBytes.size() * estimate.frameCount * 3 / 4, 64 * 1024);
    estimate.valid = true;
    return estimate;
}

int movieExportFrameCount(const MovieExportSettings &settings)
{
    return buildTimeFrameValues(settings).size();
}

MovieExportWorker::MovieExportWorker(const MovieExportSettings &settings, QObject *parent)
    : QObject(parent)
    , settings_(settings)
    , readIssueLog_(std::make_shared<ReadIssueLog>())
    , workingChannelSettings_(settings.channelSettings)
{
    result_.outputPath = settings_.outputPath;
}

void MovieExportWorker::start()
{
    MovieExportResult validation = validateSettings();
    if (!validation.success) {
        finishWithError(validation.errorMessage);
        return;
    }

    timeValues_ = buildTimeFrameValues(settings_);
    result_.outputPath = settings_.outputPath;

    initializeRecorder();
    if (!recorder_ || !videoFrameInput_) {
        finishWithError(tr("Movie export could not initialize the Qt Multimedia recorder."));
        return;
    }

    emit progressChanged(0, timeValues_.size(), tr("Preparing movie export…"));
    recorder_->record();
}

MovieExportResult MovieExportWorker::validateSettings() const
{
    QString errorMessage;
    DocumentReaderOptions options;
    options.failurePolicy = ReadFailurePolicy::SubstituteBlack;
    options.issueLog = readIssueLog_;
    reader_ = createDocumentReaderForPath(settings_.sourcePath, options, &errorMessage);
    if (!reader_) {
        MovieExportResult result;
        result.outputPath = settings_.outputPath;
        result.errorMessage = errorMessage;
        return result;
    }

    return validateSettingsWithReader(settings_, reader_.get());
}

QImage MovieExportWorker::renderFrameImage(int timeValue, QString *errorMessage)
{
    QVector<int> coordinates = settings_.fixedCoordinates;
    coordinates[settings_.timeLoopIndex] = timeValue;

    RawFrame rawFrame = reader_->readFrameForCoords(coordinates, errorMessage);
    if (!rawFrame.isValid()) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = tr("The file reader could not read the requested loop coordinates.");
        }
        return {};
    }

    FrameCoordinateState coordinateState;
    coordinateState.values = coordinates;
    if (settings_.liveAutoEnabled) {
        applyAutoContrastAllChannels(rawFrame, workingChannelSettings_);
    }
    QImage image = FrameRenderer::render(rawFrame, coordinateState, workingChannelSettings_).image;
    if (settings_.roiRect.isValid() && !settings_.roiRect.isEmpty()) {
        image = image.copy(settings_.roiRect);
    }
    return image.convertToFormat(QImage::Format_ARGB32);
}

bool MovieExportWorker::sendPendingFrame()
{
    if (!videoFrameInput_) {
        return false;
    }

    if (pendingFrame_.isValid()) {
        if (!videoFrameInput_->sendVideoFrame(pendingFrame_)) {
            return false;
        }
        pendingFrame_ = {};
        ++result_.encodedFrameCount;
        emit progressChanged(result_.encodedFrameCount,
                             timeValues_.size(),
                             tr("Encoding frame %1 of %2…").arg(result_.encodedFrameCount).arg(timeValues_.size()));
        return true;
    }

    if (pendingEndOfStream_) {
        if (!videoFrameInput_->sendVideoFrame(QVideoFrame())) {
            return false;
        }
        pendingEndOfStream_ = false;
        endOfStreamSent_ = true;
        emit progressChanged(timeValues_.size(), timeValues_.size(), tr("Finalizing movie…"));
        return true;
    }

    return true;
}

void MovieExportWorker::initializeRecorder()
{
    QMediaFormat format(QMediaFormat::MPEG4);
    format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    format.setAudioCodec(QMediaFormat::AudioCodec::Unspecified);

    QVideoFrameFormat videoFrameFormat(settings_.outputSize,
                                       QVideoFrameFormat::pixelFormatFromImageFormat(QImage::Format_ARGB32));
    videoFrameFormat.setStreamFrameRate(settings_.fps);

    captureSession_ = new QMediaCaptureSession(this);
    recorder_ = new QMediaRecorder(this);
    videoFrameInput_ = new QVideoFrameInput(videoFrameFormat, this);

    captureSession_->setRecorder(recorder_);
    captureSession_->setVideoFrameInput(videoFrameInput_);

    recorder_->setOutputLocation(QUrl::fromLocalFile(settings_.outputPath));
    recorder_->setMediaFormat(format);
    recorder_->setEncodingMode(QMediaRecorder::ConstantQualityEncoding);
    recorder_->setQuality(QMediaRecorder::HighQuality);
    recorder_->setVideoResolution(settings_.outputSize);
    recorder_->setVideoFrameRate(settings_.fps);
    recorder_->setAutoStop(true);

    connect(videoFrameInput_, &QVideoFrameInput::readyToSendVideoFrame, this, [this]() {
        handleReadyToSend();
    });
    connect(recorder_, &QMediaRecorder::errorOccurred, this,
            [this](QMediaRecorder::Error, const QString &errorString) {
                finishWithError(errorString.isEmpty() ? tr("Movie export failed in the media recorder.") : errorString);
            });
    connect(recorder_, &QMediaRecorder::recorderStateChanged, this,
            [this](QMediaRecorder::RecorderState state) {
                if (state == QMediaRecorder::RecordingState) {
                    handleReadyToSend();
                }
                if (state == QMediaRecorder::StoppedState && endOfStreamSent_ && !completionEmitted_) {
                    finishSuccessfully();
                }
            });
}

void MovieExportWorker::finishWithError(const QString &message)
{
    if (completionEmitted_) {
        return;
    }

    completionEmitted_ = true;
    result_.success = false;
    result_.errorMessage = message;
    if (reader_) {
        reader_->close();
        reader_.reset();
    }
    emit finished(result_);
}

void MovieExportWorker::finishSuccessfully()
{
    if (completionEmitted_) {
        return;
    }

    const QVector<ReadIssue> issues = readIssueLog_ ? readIssueLog_->snapshot() : QVector<ReadIssue>{};
    completionEmitted_ = true;
    result_.success = true;
    result_.substitutedReadCount = issues.size();
    if (!issues.isEmpty()) {
        QString reportError;
        const QString reportPath = buildMovieExportWarningReportPath(settings_.outputPath);
        if (writeMovieExportWarningReport(reportPath, settings_, false, issues, &reportError)) {
            result_.warningReportPath = reportPath;
        } else {
            result_.errorMessage = reportError;
        }
    } else {
        result_.errorMessage.clear();
    }
    result_.bytesWritten = QFileInfo(result_.outputPath).exists() ? QFileInfo(result_.outputPath).size() : 0;
    if (reader_) {
        reader_->close();
        reader_.reset();
    }
    emit finished(result_);
}

void MovieExportWorker::handleReadyToSend()
{
    if (completionEmitted_ || !recorder_ || recorder_->recorderState() != QMediaRecorder::RecordingState) {
        return;
    }

    while (true) {
        if (!sendPendingFrame()) {
            return;
        }

        if (nextFrameCursor_ >= timeValues_.size()) {
            if (endOfStreamSent_) {
                return;
            }

            pendingEndOfStream_ = true;
            continue;
        }

        QString errorMessage;
        const int timeValue = timeValues_.at(nextFrameCursor_);
        QImage image = renderFrameImage(timeValue, &errorMessage);
        if (image.isNull()) {
            finishWithError(errorMessage.isEmpty() ? tr("A rendered frame could not be prepared for movie export.") : errorMessage);
            return;
        }

        pendingFrame_ = QVideoFrame(image);
        pendingFrame_.setStreamFrameRate(settings_.fps);
        const qint64 startTimeUs = qRound64((result_.encodedFrameCount * 1000000.0) / settings_.fps);
        const qint64 endTimeUs = qRound64(((result_.encodedFrameCount + 1) * 1000000.0) / settings_.fps);
        pendingFrame_.setStartTime(startTimeUs);
        pendingFrame_.setEndTime(endTimeUs);
        ++nextFrameCursor_;
    }
}
