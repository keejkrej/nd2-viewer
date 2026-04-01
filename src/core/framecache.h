#pragma once

#include "core/nd2types.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>

#include <list>

class FrameCache
{
public:
    explicit FrameCache(int capacity = 12)
        : capacity_(capacity)
    {
    }

    void clear()
    {
        QMutexLocker locker(&mutex_);
        entries_.clear();
        index_.clear();
    }

    void setCapacity(int capacity)
    {
        QMutexLocker locker(&mutex_);
        capacity_ = qMax(1, capacity);
        trimLocked();
    }

    [[nodiscard]] int capacity() const
    {
        QMutexLocker locker(&mutex_);
        return capacity_;
    }

    bool tryGet(int sequenceIndex, RawFrame *frame) const
    {
        QMutexLocker locker(&mutex_);
        const auto it = index_.find(sequenceIndex);
        if (it == index_.end()) {
            return false;
        }

        entries_.splice(entries_.begin(), entries_, it.value());
        if (frame) {
            *frame = it.value()->frame;
        }
        return true;
    }

    void insert(const RawFrame &frame)
    {
        if (!frame.isValid()) {
            return;
        }

        QMutexLocker locker(&mutex_);
        const auto existing = index_.find(frame.sequenceIndex);
        if (existing != index_.end()) {
            existing.value()->frame = frame;
            entries_.splice(entries_.begin(), entries_, existing.value());
            return;
        }

        entries_.push_front({frame.sequenceIndex, frame});
        index_.insert(frame.sequenceIndex, entries_.begin());
        trimLocked();
    }

private:
    struct Entry
    {
        int sequenceIndex = -1;
        RawFrame frame;
    };

    void trimLocked()
    {
        while (static_cast<int>(entries_.size()) > capacity_) {
            auto last = std::prev(entries_.end());
            index_.remove(last->sequenceIndex);
            entries_.erase(last);
        }
    }

    mutable QMutex mutex_;
    int capacity_ = 12;
    mutable std::list<Entry> entries_;
    mutable QHash<int, std::list<Entry>::iterator> index_;
};
