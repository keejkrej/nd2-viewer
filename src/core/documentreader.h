#pragma once

#include "core/documenttypes.h"

class PolicyDocumentReader;

class DocumentReader
{
public:
    friend class PolicyDocumentReader;

    virtual ~DocumentReader() = default;

    virtual bool open(const QString &path, QString *errorMessage = nullptr) = 0;
    virtual void close() = 0;

    [[nodiscard]] virtual bool isOpen() const = 0;
    [[nodiscard]] virtual QString filePath() const = 0;
    [[nodiscard]] virtual const DocumentInfo &documentInfo() const = 0;
    [[nodiscard]] virtual int sequenceCount() const = 0;

    virtual bool sequenceForCoords(const QVector<int> &coords, int *sequenceIndex, QString *errorMessage = nullptr) const = 0;

    [[nodiscard]] virtual RawFrame readFrameForCoords(const QVector<int> &coords, QString *errorMessage = nullptr) const
    {
        int sequenceIndex = -1;
        if (!sequenceForCoords(coords, &sequenceIndex, errorMessage)) {
            return {};
        }

        return readFrame(sequenceIndex, errorMessage);
    }

    [[nodiscard]] virtual MetadataSection frameMetadataForCoords(const QVector<int> &coords, QString *errorMessage = nullptr) const
    {
        int sequenceIndex = -1;
        if (!sequenceForCoords(coords, &sequenceIndex, errorMessage)) {
            return {};
        }

        return frameMetadataSection(sequenceIndex, errorMessage);
    }

protected:
    virtual RawFrame readFrame(int sequenceIndex, QString *errorMessage = nullptr) const = 0;
    virtual MetadataSection frameMetadataSection(int sequenceIndex, QString *errorMessage = nullptr) const = 0;
};
