#pragma once

#include "core/nd2types.h"

#include <QPoint>
#include <QString>
#include <QVector>

struct ChannelAutoContrastAnalysis
{
    QVector<double> sortedSamples;
    QVector<quint64> histogramBins;
    double minimumValue = 0.0;
    double maximumValue = 1.0;

    [[nodiscard]] bool isValid() const
    {
        return !sortedSamples.isEmpty() && !histogramBins.isEmpty();
    }
};

class FrameRenderer
{
public:
    static QVector<ChannelRenderSettings> defaultChannelSettings(const Nd2DocumentInfo &info);
    static ChannelAutoContrastAnalysis analyzeChannel(const RawFrame &frame, int channelIndex, int histogramBinCount = 256);
    static bool applyAutoContrastToChannel(const ChannelAutoContrastAnalysis &analysis, ChannelRenderSettings &settings);
    static bool applyAutoContrast(const RawFrame &frame, QVector<ChannelRenderSettings> &settings);
    [[nodiscard]] static double percentileToValue(const ChannelAutoContrastAnalysis &analysis, double percentile);
    [[nodiscard]] static double valueToPercentile(const ChannelAutoContrastAnalysis &analysis, double value);
    static RenderedFrame render(const RawFrame &frame,
                                const FrameCoordinateState &coordinates,
                                const QVector<ChannelRenderSettings> &settings);
    static QString pixelDescription(const RawFrame &frame, const QPoint &pixelPosition);
};
