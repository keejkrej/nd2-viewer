#pragma once

#include "core/documenttypes.h"

class DocumentReader
{
public:
    virtual ~DocumentReader() = default;

    virtual bool open(const QString &path, QString *errorMessage = nullptr) = 0;
    virtual void close() = 0;

    [[nodiscard]] virtual bool isOpen() const = 0;
    [[nodiscard]] virtual QString filePath() const = 0;
    [[nodiscard]] virtual const DocumentInfo &documentInfo() const = 0;
    [[nodiscard]] virtual int sequenceCount() const = 0;

    virtual bool sequenceForCoords(const QVector<int> &coords, int *sequenceIndex, QString *errorMessage = nullptr) const = 0;

    virtual RawFrame readFrame(int sequenceIndex, QString *errorMessage = nullptr) const = 0;
    virtual MetadataSection frameMetadataSection(int sequenceIndex, QString *errorMessage = nullptr) const = 0;
};
