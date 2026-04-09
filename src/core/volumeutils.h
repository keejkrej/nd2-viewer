#pragma once

#include "core/documenttypes.h"

namespace VolumeUtils
{
[[nodiscard]] int findZLoopIndex(const DocumentInfo &info);
[[nodiscard]] QVector3D sanitizedVoxelSpacing(const QVector3D &spacing);
[[nodiscard]] QVector<ChannelRenderSettings> defaultVolumeChannelSettings(const DocumentInfo &info,
                                                                           const QVector<ChannelRenderSettings> &seedSettings,
                                                                           int channelCount);
}
