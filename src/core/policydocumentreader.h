#pragma once

#include "core/documentreader.h"
#include "core/readfailurepolicy.h"

#include <QMutex>

#include <memory>

class PolicyDocumentReader : public DocumentReader
{
public:
    PolicyDocumentReader(std::unique_ptr<DocumentReader> inner, DocumentReaderOptions options);
    ~PolicyDocumentReader() override = default;

    bool open(const QString &path, QString *errorMessage = nullptr) override;
    void close() override;

    [[nodiscard]] bool isOpen() const override;
    [[nodiscard]] QString filePath() const override;
    [[nodiscard]] const DocumentInfo &documentInfo() const override;
    [[nodiscard]] int sequenceCount() const override;
    void setOptions(const DocumentReaderOptions &options);
    [[nodiscard]] DocumentReaderOptions options() const;

    bool sequenceForCoords(const QVector<int> &coords, int *sequenceIndex, QString *errorMessage = nullptr) const override;
    [[nodiscard]] RawFrame readFrameForCoords(const QVector<int> &coords, QString *errorMessage = nullptr) const override;
    [[nodiscard]] MetadataSection frameMetadataForCoords(const QVector<int> &coords, QString *errorMessage = nullptr) const override;

protected:
    RawFrame readFrame(int sequenceIndex, QString *errorMessage = nullptr) const override;
    MetadataSection frameMetadataSection(int sequenceIndex, QString *errorMessage = nullptr) const override;

private:
    [[nodiscard]] QString coordinateSummary(const QVector<int> &coords) const;
    [[nodiscard]] RawFrame makeBlackFrame(const QVector<int> &coords, int sequenceIndex) const;
    void recordIssue(ReadIssueKind kind, const QVector<int> &coords, int sequenceIndex, const QString &message) const;

    std::unique_ptr<DocumentReader> inner_;
    mutable QMutex optionsMutex_;
    DocumentReaderOptions options_;
};
