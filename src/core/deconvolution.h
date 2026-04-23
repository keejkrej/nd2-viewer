#pragma once

#include "core/documenttypes.h"

#include <QImage>
#include <QString>
#include <QVector>

struct DeconvolutionSettings
{
    int iterations = 20;
    double gaussianSigmaPixels = 1.2;
    int kernelRadiusPixels = 5;
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
    [[nodiscard]] static DeconvolutionResult run2D(const RawFrame &frame,
                                                   const FrameCoordinateState &coordinates,
                                                   const QVector<ChannelRenderSettings> &channelSettings,
                                                   const DeconvolutionSettings &settings);
};
