#pragma once

#include "core/documenttypes.h"

#include <QByteArray>
#include <QString>
#include <QVector>

enum class SegmentationThresholdMethod
{
    Otsu,
    Li
};

struct SegmentationThresholdResult
{
    bool success = false;
    double threshold = 0.0;
    double minimumValue = 0.0;
    double maximumValue = 1.0;
    QString errorMessage;
};

struct SegmentationMask2D
{
    int width = 0;
    int height = 0;
    QByteArray data;

    [[nodiscard]] bool isValid() const
    {
        return width > 0 && height > 0 && data.size() == static_cast<qsizetype>(width) * height;
    }
};

struct SegmentationMask3D
{
    int width = 0;
    int height = 0;
    int depth = 0;
    QVector3D voxelSpacing = {1.0f, 1.0f, 1.0f};
    QByteArray data;

    [[nodiscard]] bool isValid() const
    {
        return width > 0 && height > 0 && depth > 0
            && data.size() == static_cast<qsizetype>(width) * height * depth;
    }

    [[nodiscard]] qsizetype voxelCount() const
    {
        return static_cast<qsizetype>(width) * height * depth;
    }
};

struct SegmentationLabels2D
{
    bool success = false;
    int width = 0;
    int height = 0;
    quint32 componentCount = 0;
    QVector<quint32> labels;
    QString errorMessage;

    [[nodiscard]] bool isValid() const
    {
        return success && width > 0 && height > 0 && labels.size() == width * height;
    }
};

struct SegmentationLabels3D
{
    bool success = false;
    int width = 0;
    int height = 0;
    int depth = 0;
    QVector3D voxelSpacing = {1.0f, 1.0f, 1.0f};
    quint32 componentCount = 0;
    QVector<quint32> labels;
    QString errorMessage;

    [[nodiscard]] bool isValid() const
    {
        return success && width > 0 && height > 0 && depth > 0
            && labels.size() == static_cast<qsizetype>(width) * height * depth;
    }
};

class SegmentationProcessor
{
public:
    [[nodiscard]] static SegmentationThresholdResult calculateThreshold2D(const RawFrame &frame,
                                                                          int channelIndex,
                                                                          SegmentationThresholdMethod method);
    [[nodiscard]] static SegmentationThresholdResult calculateThreshold3D(const RawVolume &volume,
                                                                          int channelIndex,
                                                                          SegmentationThresholdMethod method);
    [[nodiscard]] static SegmentationMask2D binarize2D(const RawFrame &frame, int channelIndex, double threshold);
    [[nodiscard]] static SegmentationMask3D binarize3D(const RawVolume &volume, int channelIndex, double threshold);
    [[nodiscard]] static SegmentationLabels2D labelConnectedComponents(const SegmentationMask2D &mask);
    [[nodiscard]] static SegmentationLabels3D labelConnectedComponents(const SegmentationMask3D &mask);
};

Q_DECLARE_METATYPE(SegmentationThresholdResult)
Q_DECLARE_METATYPE(SegmentationMask2D)
Q_DECLARE_METATYPE(SegmentationMask3D)
Q_DECLARE_METATYPE(SegmentationLabels2D)
Q_DECLARE_METATYPE(SegmentationLabels3D)
