#include "core/volumeutils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>

namespace
{
QColor defaultColorForIndex(int index)
{
    static const std::array<QColor, 6> colors = {
        QColor(255, 255, 255),
        QColor(0, 255, 0),
        QColor(255, 0, 255),
        QColor(255, 170, 0),
        QColor(0, 255, 255),
        QColor(255, 80, 80)
    };

    return colors.at(static_cast<size_t>(index) % colors.size());
}

double sampleVolumeValue(const RawVolume &volume, int channelIndex, qsizetype voxelIndex)
{
    if (!volume.isValid() || channelIndex < 0 || channelIndex >= volume.components || voxelIndex < 0 || voxelIndex >= volume.voxelCount()) {
        return 0.0;
    }

    const int bytesPerComponent = volume.bytesPerComponent();
    const char *voxel = volume.channelData.at(channelIndex).constData() + (voxelIndex * bytesPerComponent);
    switch (bytesPerComponent) {
    case 1:
        return static_cast<unsigned char>(voxel[0]);
    case 2: {
        quint16 value = 0;
        std::memcpy(&value, voxel, sizeof(value));
        return value;
    }
    case 4: {
        if (volume.pixelDataType.compare(QStringLiteral("float"), Qt::CaseInsensitive) == 0) {
            float value = 0.0f;
            std::memcpy(&value, voxel, sizeof(value));
            return value;
        }
        quint32 value = 0;
        std::memcpy(&value, voxel, sizeof(value));
        return value;
    }
    default:
        return 0.0;
    }
}

int volumeSamplingStep(const RawVolume &volume)
{
    const double totalVoxels = static_cast<double>(volume.voxelCount());
    return std::max(1, static_cast<int>(std::cbrt(totalVoxels / 250000.0)));
}
}

int VolumeUtils::findZLoopIndex(const DocumentInfo &info)
{
    for (int index = 0; index < info.loops.size(); ++index) {
        const LoopInfo &loop = info.loops.at(index);
        if ((loop.type == QStringLiteral("ZStackLoop") || loop.label.compare(QStringLiteral("Z"), Qt::CaseInsensitive) == 0) && loop.size > 1) {
            return index;
        }
    }

    return -1;
}

QVector3D VolumeUtils::sanitizedVoxelSpacing(const QVector3D &spacing)
{
    const auto isValidAxis = [](float value) {
        return std::isfinite(value) && value > 0.0f;
    };

    const bool hasX = isValidAxis(spacing.x());
    const bool hasY = isValidAxis(spacing.y());
    const bool hasZ = isValidAxis(spacing.z());

    if (!hasX && !hasY && !hasZ) {
        return {1.0f, 1.0f, 1.0f};
    }

    const float fallbackXY = hasX && hasY ? 0.5f * (spacing.x() + spacing.y())
                                          : (hasX ? spacing.x() : (hasY ? spacing.y() : spacing.z()));
    const float x = hasX ? spacing.x() : (hasY ? spacing.y() : (hasZ ? spacing.z() : 1.0f));
    const float y = hasY ? spacing.y() : (hasX ? spacing.x() : (hasZ ? spacing.z() : 1.0f));
    const float z = hasZ ? spacing.z() : fallbackXY;

    return {x, y, z};
}

QVector<ChannelRenderSettings> VolumeUtils::defaultVolumeChannelSettings(const DocumentInfo &info,
                                                                         const QVector<ChannelRenderSettings> &seedSettings,
                                                                         int channelCount)
{
    QVector<ChannelRenderSettings> settings = FrameRenderer::defaultChannelSettings(info);
    if (settings.size() < channelCount) {
        const double defaultHigh = info.pixelDataType.compare(QStringLiteral("float"), Qt::CaseInsensitive) == 0
                                       ? 1.0
                                       : std::pow(2.0, qMax(info.bitsPerComponentSignificant, info.bitsPerComponentInMemory)) - 1.0;
        while (settings.size() < channelCount) {
            ChannelRenderSettings channel;
            channel.enabled = true;
            channel.color = defaultColorForIndex(settings.size());
            channel.low = 0.0;
            channel.high = defaultHigh > 0.0 ? defaultHigh : 1.0;
            channel.autoContrast = true;
            settings.push_back(channel);
        }
    }

    if (!seedSettings.isEmpty()) {
        const int limit = std::min(static_cast<int>(seedSettings.size()), channelCount);
        for (int index = 0; index < limit; ++index) {
            settings[index].enabled = seedSettings.at(index).enabled;
            settings[index].color = seedSettings.at(index).color;
        }
    }

    settings.resize(channelCount);
    return settings;
}

ChannelAutoContrastAnalysis VolumeUtils::analyzeVolumeChannel(const RawVolume &volume, int channelIndex, int histogramBinCount)
{
    ChannelAutoContrastAnalysis analysis;
    if (!volume.isValid() || channelIndex < 0 || channelIndex >= volume.components || histogramBinCount <= 0) {
        return analysis;
    }

    const int step = volumeSamplingStep(volume);
    QVector<double> samples;
    samples.reserve(static_cast<int>(volume.voxelCount() / std::max(1, step * step * step)));

    double minimum = std::numeric_limits<double>::max();
    double maximum = std::numeric_limits<double>::lowest();
    for (int z = 0; z < volume.depth; z += step) {
        for (int y = 0; y < volume.height; y += step) {
            for (int x = 0; x < volume.width; x += step) {
                const qsizetype voxelIndex = (static_cast<qsizetype>(z) * volume.width * volume.height)
                                             + (static_cast<qsizetype>(y) * volume.width)
                                             + x;
                const double value = sampleVolumeValue(volume, channelIndex, voxelIndex);
                samples.push_back(value);
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
        }
    }

    if (samples.isEmpty()) {
        return analysis;
    }

    analysis.minimumValue = minimum;
    analysis.maximumValue = maximum;
    analysis.sortedSamples = samples;
    std::sort(analysis.sortedSamples.begin(), analysis.sortedSamples.end());

    analysis.histogramBins.fill(0, histogramBinCount);
    if (histogramBinCount == 1 || qFuzzyCompare(minimum + 1.0, maximum + 1.0)) {
        analysis.histogramBins[0] = static_cast<quint64>(samples.size());
        return analysis;
    }

    const double denominator = maximum - minimum;
    for (double value : samples) {
        const double normalized = std::clamp((value - minimum) / denominator, 0.0, 1.0);
        const int binIndex = std::clamp(static_cast<int>(normalized * (histogramBinCount - 1)), 0, histogramBinCount - 1);
        ++analysis.histogramBins[binIndex];
    }

    return analysis;
}

bool VolumeUtils::applyAutoContrast(const RawVolume &volume,
                                    QVector<ChannelRenderSettings> &settings,
                                    QVector<ChannelAutoContrastAnalysis> *analyses)
{
    if (!volume.isValid() || settings.isEmpty()) {
        return false;
    }

    QVector<ChannelAutoContrastAnalysis> workingAnalyses;
    workingAnalyses.reserve(settings.size());

    bool changed = false;
    std::optional<ChannelAutoContrastAnalysis> sharedSingleChannelAnalysis;
    for (int channelIndex = 0; channelIndex < settings.size(); ++channelIndex) {
        ChannelAutoContrastAnalysis analysis;
        if (volume.components == 1 && channelIndex > 0) {
            if (!sharedSingleChannelAnalysis.has_value()) {
                sharedSingleChannelAnalysis = analyzeVolumeChannel(volume, 0);
            }
            analysis = *sharedSingleChannelAnalysis;
        } else {
            const int sourceChannel = std::min(channelIndex, volume.components - 1);
            analysis = analyzeVolumeChannel(volume, sourceChannel);
            if (volume.components == 1 && !sharedSingleChannelAnalysis.has_value()) {
                sharedSingleChannelAnalysis = analysis;
            }
        }

        workingAnalyses.push_back(analysis);
        if (settings.at(channelIndex).autoContrast) {
            changed = FrameRenderer::applyAutoContrastToChannel(analysis, settings[channelIndex]) || changed;
        }
    }

    if (analyses) {
        *analyses = workingAnalyses;
    }
    return changed;
}
