#pragma once

#include "core/documentreader.h"

#include <QHash>
#include <QMutex>
#include <QString>
#include <QVector>

#include <memory>

#include <libCZI.h>

class CziReader : public DocumentReader
{
public:
    CziReader() = default;
    ~CziReader() override;

    bool open(const QString &path, QString *errorMessage = nullptr) override;
    void close() override;

    [[nodiscard]] bool isOpen() const override;
    [[nodiscard]] QString filePath() const override;
    [[nodiscard]] const DocumentInfo &documentInfo() const override;
    [[nodiscard]] int sequenceCount() const override;

    bool sequenceForCoords(const QVector<int> &coords, int *sequenceIndex, QString *errorMessage = nullptr) const override;

    RawFrame readFrameForCoords(const QVector<int> &coords, QString *errorMessage = nullptr) const override;
    MetadataSection frameMetadataForCoords(const QVector<int> &coords, QString *errorMessage = nullptr) const override;

    static QString dimensionName(libCZI::DimensionIndex dimension);
    static QString dimensionCoordinateSummary(const libCZI::IDimCoordinate &coordinate);

protected:
    RawFrame readFrame(int sequenceIndex, QString *errorMessage = nullptr) const override;
    MetadataSection frameMetadataSection(int sequenceIndex, QString *errorMessage = nullptr) const override;

private:
    struct LoopBinding
    {
        libCZI::DimensionIndex dimension = libCZI::DimensionIndex::invalid;
        int start = 0;
        int size = 0;
    };

    struct SequencePlane
    {
        QVector<int> coordinates;
        libCZI::IntRect frameRect = {0, 0, 0, 0};
        QVector<QVector<int>> channelSubblockIndices;
    };

    static DocumentInfo buildFallbackInfo(const QString &path);
    static QString coordinateKey(const QVector<int> &coords);
    static QString loopType(libCZI::DimensionIndex dimension);
    static QString loopLabel(libCZI::DimensionIndex dimension);
    static MetadataSection jsonMetadataSection(const QString &title, const QJsonValue &treeValue, const QString &rawText);
    static QColor defaultColorForIndex(int index);
    static bool isLayer0SubBlock(const libCZI::SubBlockInfo &subBlockInfo);
    static bool isSupportedPixelType(libCZI::PixelType pixelType);
    static int bitsPerComponentFor(libCZI::PixelType pixelType);
    static QString pixelDataTypeFor(libCZI::PixelType pixelType);
    QString formatCoordinateSelection(const QVector<int> &coords) const;

    bool loadDocumentInfo(QString *errorMessage);
    bool validateSubBlockInfo(const libCZI::SubBlockInfo &subBlockInfo, QString *errorMessage) const;
    bool buildSequenceMap(QString *errorMessage);
    QJsonObject buildSummaryMetadata(const QString &metadataXml) const;
    const SequencePlane *sequencePlaneForIndex(int sequenceIndex, QString *errorMessage = nullptr) const;
    bool planeCoordinateForChannel(const SequencePlane &sequencePlane,
                                   int channelSlot,
                                   libCZI::CDimCoordinate *coordinate,
                                   libCZI::PixelType *pixelType,
                                   QString *errorMessage = nullptr) const;

    mutable QMutex mutex_;
    std::shared_ptr<libCZI::IStream> stream_;
    std::shared_ptr<libCZI::ICZIReader> reader_;
    DocumentInfo info_;
    QVector<LoopBinding> loopBindings_;
    QVector<int> channelValues_;
    QVector<SequencePlane> sequencePlanes_;
    QHash<QString, int> coordsToSequence_;
    libCZI::IntRect documentFrameRect_ = {0, 0, 0, 0};
    libCZI::ISingleChannelPyramidLayerTileAccessor::PyramidLayerInfo selectedPyramidLayer_ = {0, 0};
    int selectedPyramidScale_ = 1;
};
