#pragma once

#include "core/documenttypes.h"

#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>

enum class DeconvolutionMethod
{
    Classical,
    DeepLearning
};

struct DeconvolutionSettings
{
    DeconvolutionMethod method = DeconvolutionMethod::Classical;
    int channelIndex = 0;
    int iterations = 20;
    double gaussianSigmaPixels = 1.2;
    int kernelRadiusPixels = 5;
    bool useRoi = false;
    QRect roiRect;
    QString modelPath;
};

struct DeconvolutionResult
{
    bool success = false;
    QImage image;
    QString errorMessage;
};

class DeconvolutionProcessor
{
public:
    [[nodiscard]] static QString defaultDebcrModelPath();
    [[nodiscard]] static DeconvolutionResult run2D(const RawFrame &frame,
                                                   const FrameCoordinateState &coordinates,
                                                   const QVector<ChannelRenderSettings> &channelSettings,
                                                   const DeconvolutionSettings &settings);
};
