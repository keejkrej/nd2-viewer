#include "core/policydocumentreader.h"

#include <QMutexLocker>

#include <algorithm>
#include <QStringList>

namespace
{
QString loopLabelFor(const LoopInfo &loop, int index)
{
    if (!loop.label.isEmpty()) {
        return loop.label;
    }
    if (!loop.type.isEmpty()) {
        return loop.type;
    }
    return QStringLiteral("Loop %1").arg(index + 1);
}
}

PolicyDocumentReader::PolicyDocumentReader(std::unique_ptr<DocumentReader> inner, DocumentReaderOptions options)
    : inner_(std::move(inner))
    , options_(std::move(options))
{
}

void PolicyDocumentReader::setOptions(const DocumentReaderOptions &options)
{
    QMutexLocker locker(&optionsMutex_);
    options_ = options;
}

DocumentReaderOptions PolicyDocumentReader::options() const
{
    QMutexLocker locker(&optionsMutex_);
    return options_;
}

bool PolicyDocumentReader::open(const QString &path, QString *errorMessage)
{
    return inner_ ? inner_->open(path, errorMessage) : false;
}

void PolicyDocumentReader::close()
{
    if (inner_) {
        inner_->close();
    }
}

bool PolicyDocumentReader::isOpen() const
{
    return inner_ && inner_->isOpen();
}

QString PolicyDocumentReader::filePath() const
{
    return inner_ ? inner_->filePath() : QString();
}

const DocumentInfo &PolicyDocumentReader::documentInfo() const
{
    return inner_->documentInfo();
}

int PolicyDocumentReader::sequenceCount() const
{
    return inner_ ? inner_->sequenceCount() : 0;
}

bool PolicyDocumentReader::sequenceForCoords(const QVector<int> &coords, int *sequenceIndex, QString *errorMessage) const
{
    return inner_ && inner_->sequenceForCoords(coords, sequenceIndex, errorMessage);
}

RawFrame PolicyDocumentReader::readFrameForCoords(const QVector<int> &coords, QString *errorMessage) const
{
    const DocumentReaderOptions options = this->options();
    QString innerError;
    int sequenceIndex = -1;
    if (inner_) {
        inner_->sequenceForCoords(coords, &sequenceIndex, nullptr);
    }

    const RawFrame frame = inner_ ? inner_->readFrameForCoords(coords, &innerError) : RawFrame{};
    if (frame.isValid() || options.failurePolicy == ReadFailurePolicy::Strict) {
        if (errorMessage) {
            *errorMessage = innerError;
        }
        return frame;
    }

    recordIssue(ReadIssueKind::FrameSubstituted, coords, sequenceIndex, innerError);
    if (errorMessage) {
        *errorMessage = innerError;
    }
    return makeBlackFrame(coords, sequenceIndex);
}

MetadataSection PolicyDocumentReader::frameMetadataForCoords(const QVector<int> &coords, QString *errorMessage) const
{
    const DocumentReaderOptions options = this->options();
    QString innerError;
    int sequenceIndex = -1;
    if (inner_) {
        inner_->sequenceForCoords(coords, &sequenceIndex, nullptr);
    }

    const MetadataSection section = inner_ ? inner_->frameMetadataForCoords(coords, &innerError) : MetadataSection{};
    if (innerError.isEmpty() || options.failurePolicy == ReadFailurePolicy::Strict) {
        if (errorMessage) {
            *errorMessage = innerError;
        }
        return section;
    }

    recordIssue(ReadIssueKind::MetadataUnavailable, coords, sequenceIndex, innerError);
    if (errorMessage) {
        *errorMessage = innerError;
    }

    MetadataSection placeholder;
    placeholder.title = QStringLiteral("Frame Metadata");
    placeholder.rawText = QStringLiteral("Metadata was unavailable because the source reader reported an error.");
    return placeholder;
}

RawFrame PolicyDocumentReader::readFrame(int sequenceIndex, QString *errorMessage) const
{
    return inner_ ? inner_->readFrame(sequenceIndex, errorMessage) : RawFrame{};
}

MetadataSection PolicyDocumentReader::frameMetadataSection(int sequenceIndex, QString *errorMessage) const
{
    return inner_ ? inner_->frameMetadataSection(sequenceIndex, errorMessage) : MetadataSection{};
}

QString PolicyDocumentReader::coordinateSummary(const QVector<int> &coords) const
{
    if (!inner_) {
        return {};
    }

    const DocumentInfo &info = inner_->documentInfo();
    QStringList parts;
    parts.reserve(coords.size());
    for (int index = 0; index < coords.size(); ++index) {
        const QString label = index < info.loops.size() ? loopLabelFor(info.loops.at(index), index)
                                                        : QStringLiteral("Loop %1").arg(index + 1);
        parts.push_back(QStringLiteral("%1=%2").arg(label, QString::number(coords.at(index))));
    }
    return parts.join(QStringLiteral(", "));
}

RawFrame PolicyDocumentReader::makeBlackFrame(const QVector<int> &coords, int sequenceIndex) const
{
    RawFrame frame;
    if (!inner_) {
        return frame;
    }

    const DocumentInfo &info = inner_->documentInfo();
    const int width = qMax(info.frameSize.width(), 1);
    const int height = qMax(info.frameSize.height(), 1);
    const int bitsPerComponent =
        qMax(info.bitsPerComponentSignificant > 0 ? info.bitsPerComponentSignificant : info.bitsPerComponentInMemory, 8);
    const int components = qMax(std::max(info.componentCount, static_cast<int>(info.channels.size())), 1);
    const QString pixelDataType = info.pixelDataType.isEmpty() ? QStringLiteral("unsigned") : info.pixelDataType;
    const int bytesPerComponent = (bitsPerComponent + 7) / 8;

    frame.sequenceIndex = sequenceIndex;
    frame.width = width;
    frame.height = height;
    frame.bitsPerComponent = bitsPerComponent;
    frame.components = components;
    frame.pixelDataType = pixelDataType;
    frame.bytesPerLine = static_cast<qsizetype>(width) * components * bytesPerComponent;
    frame.data.fill('\0', frame.bytesPerLine * height);

    Q_UNUSED(coords);
    return frame;
}

void PolicyDocumentReader::recordIssue(ReadIssueKind kind, const QVector<int> &coords, int sequenceIndex, const QString &message) const
{
    const DocumentReaderOptions options = this->options();
    if (!options.issueLog) {
        return;
    }

    ReadIssue issue;
    issue.kind = kind;
    issue.sourcePath = filePath();
    issue.coordinateSummary = coordinateSummary(coords);
    issue.coordinates = coords;
    issue.sequenceIndex = sequenceIndex;
    issue.message = message;
    options.issueLog->record(issue);
}
