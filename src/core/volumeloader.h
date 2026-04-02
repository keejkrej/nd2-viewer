#pragma once

#include "core/documenttypes.h"
#include "core/framerenderer.h"

#include <QString>
#include <QVector>

struct VolumeLoadResult
{
    bool success = false;
    RawVolume volume;
    QVector<ChannelRenderSettings> channelSettings;
    QVector<ChannelAutoContrastAnalysis> analyses;
    QString error;
};

class VolumeLoader
{
public:
    [[nodiscard]] static VolumeLoadResult load(const QString &path,
                                               const DocumentInfo &info,
                                               const FrameCoordinateState &coordinates,
                                               const QVector<ChannelRenderSettings> &seedChannelSettings);
};
