#pragma once

#include "core/documenttypes.h"

#include <QString>
#include <QVector>

struct VolumeLoadResult
{
    bool success = false;
    RawVolume volume;
    QVector<ChannelRenderSettings> channelSettings;
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
