#pragma once

#include "core/nd2types.h"

#include <QPoint>
#include <QString>

class FrameRenderer
{
public:
    static QVector<ChannelRenderSettings> defaultChannelSettings(const Nd2DocumentInfo &info);
    static bool applyAutoContrast(const RawFrame &frame, QVector<ChannelRenderSettings> &settings);
    static RenderedFrame render(const RawFrame &frame,
                                const FrameCoordinateState &coordinates,
                                const QVector<ChannelRenderSettings> &settings);
    static QString pixelDescription(const RawFrame &frame, const QPoint &pixelPosition);
};
