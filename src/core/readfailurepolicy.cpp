#include "core/readfailurepolicy.h"

#include <QMutexLocker>

void ReadIssueLog::record(const ReadIssue &issue)
{
    QMutexLocker locker(&mutex_);
    issues_.push_back(issue);
}

QVector<ReadIssue> ReadIssueLog::snapshot() const
{
    QMutexLocker locker(&mutex_);
    return issues_;
}

bool ReadIssueLog::isEmpty() const
{
    QMutexLocker locker(&mutex_);
    return issues_.isEmpty();
}

void ReadIssueLog::clear()
{
    QMutexLocker locker(&mutex_);
    issues_.clear();
}
