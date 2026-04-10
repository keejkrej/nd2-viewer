#include "core/volumeutils.h"

#include "core/framerenderer.h"

#include <algorithm>
#include <array>
#include <cmath>

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
            settings.push_back(channel);
        }
    }

    if (!seedSettings.isEmpty()) {
        const int limit = std::min(static_cast<int>(seedSettings.size()), channelCount);
        for (int index = 0; index < limit; ++index) {
            settings[index] = seedSettings.at(index);
        }
    }

    settings.resize(channelCount);
    return settings;
}
