#pragma once

#include "core/documenttypes.h"
#include "core/framerenderer.h"

namespace VolumeUtils
{
[[nodiscard]] int findZLoopIndex(const DocumentInfo &info);
[[nodiscard]] QVector3D sanitizedVoxelSpacing(const QVector3D &spacing);
[[nodiscard]] QVector<ChannelRenderSettings> defaultVolumeChannelSettings(const DocumentInfo &info,
                                                                          const QVector<ChannelRenderSettings> &seedSettings,
                                                                          int channelCount);
[[nodiscard]] ChannelAutoContrastAnalysis analyzeVolumeChannel(const RawVolume &volume, int channelIndex, int histogramBinCount = 256);
[[nodiscard]] bool applyAutoContrast(const RawVolume &volume,
                                     QVector<ChannelRenderSettings> &settings,
                                     QVector<ChannelAutoContrastAnalysis> *analyses = nullptr);
}
