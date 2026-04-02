#pragma once

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaType>
#include <QSize>
#include <QString>
#include <QVector>
#include <QVector3D>

enum class DocumentFormat
{
    Nd2,
    Czi
};

struct MetadataSection
{
    QString title;
    QJsonValue treeValue;
    QString rawText;
};

struct LoopInfo
{
    QString type;
    QString label;
    int size = 0;
    QJsonObject details;
};

struct ChannelInfo
{
    int index = -1;
    QString name;
    QColor color = Qt::white;
    QJsonObject details;
    QJsonObject loopMap;
    QJsonObject microscope;
    QJsonObject volume;
};

struct DocumentInfo
{
    DocumentFormat format = DocumentFormat::Nd2;
    QString filePath;
    int sequenceCount = 0;
    QSize frameSize;
    int bitsPerComponentInMemory = 0;
    int bitsPerComponentSignificant = 0;
    int componentCount = 0;
    QString pixelDataType = QStringLiteral("unsigned");
    QVector3D axesCalibration;
    QVector3D voxelCount;
    QVector<LoopInfo> loops;
    QVector<ChannelInfo> channels;
    QVector<MetadataSection> metadataSections;

    [[nodiscard]] bool isValid() const
    {
        return sequenceCount > 0 && frameSize.isValid();
    }
};

struct FrameCoordinateState
{
    QVector<int> values;
};

struct ChannelRenderSettings
{
    bool enabled = true;
    QColor color = Qt::white;
    double low = 0.0;
    double high = 1.0;
    bool autoContrast = true;
    double lowPercentile = 0.1;
    double highPercentile = 99.9;
};

struct RawFrame
{
    int sequenceIndex = -1;
    int width = 0;
    int height = 0;
    int bitsPerComponent = 0;
    int components = 0;
    QString pixelDataType = QStringLiteral("unsigned");
    qsizetype bytesPerLine = 0;
    QByteArray data;

    [[nodiscard]] bool isValid() const
    {
        return width > 0 && height > 0 && components > 0 && bitsPerComponent > 0 && !data.isEmpty();
    }

    [[nodiscard]] int bytesPerComponent() const
    {
        return (bitsPerComponent + 7) / 8;
    }
};

struct RenderedFrame
{
    int sequenceIndex = -1;
    FrameCoordinateState coordinates;
    QImage image;

    [[nodiscard]] bool isValid() const
    {
        return !image.isNull();
    }
};

Q_DECLARE_METATYPE(ChannelRenderSettings)
Q_DECLARE_METATYPE(ChannelInfo)
Q_DECLARE_METATYPE(DocumentInfo)
Q_DECLARE_METATYPE(FrameCoordinateState)
Q_DECLARE_METATYPE(MetadataSection)
Q_DECLARE_METATYPE(RawFrame)
Q_DECLARE_METATYPE(RenderedFrame)
