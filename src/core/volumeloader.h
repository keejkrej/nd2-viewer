#pragma once

#include "core/readfailurepolicy.h"
#include "core/documenttypes.h"

#include <QString>
#include <QVector>

struct VolumeLoadResult
{
    bool success = false;
    RawVolume volume;
    QVector<ChannelRenderSettings> channelSettings;
    QString error;
    int loadRequestId = 0;
};

class VolumeLoader
{
public:
    [[nodiscard]] static VolumeLoadResult load(const QString &path,
                                               const DocumentInfo &info,
                                               const FrameCoordinateState &coordinates,
                                               const QVector<ChannelRenderSettings> &seedChannelSettings,
                                               DocumentReaderOptions readerOptions = {});
};
