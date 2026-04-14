#pragma once

#include <QMutex>
#include <QString>
#include <QVector>

#include <memory>

enum class ReadFailurePolicy
{
    Strict,
    SubstituteBlack
};

enum class ReadIssueKind
{
    FrameSubstituted,
    MetadataUnavailable
};

struct ReadIssue
{
    ReadIssueKind kind = ReadIssueKind::FrameSubstituted;
    QString sourcePath;
    QString coordinateSummary;
    QVector<int> coordinates;
    int sequenceIndex = -1;
    QString message;
};

class ReadIssueLog
{
public:
    void record(const ReadIssue &issue);
    [[nodiscard]] QVector<ReadIssue> snapshot() const;
    [[nodiscard]] bool isEmpty() const;
    void clear();

private:
    mutable QMutex mutex_;
    QVector<ReadIssue> issues_;
};

struct DocumentReaderOptions
{
    ReadFailurePolicy failurePolicy = ReadFailurePolicy::Strict;
    std::shared_ptr<ReadIssueLog> issueLog;
    bool forcePolicyWrapper = false;
};
