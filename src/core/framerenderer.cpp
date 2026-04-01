#include "core/framerenderer.h"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
double sampleValue(const RawFrame &frame, int x, int y, int component)
{
    const int bytesPerComponent = frame.bytesPerComponent();
    const qsizetype pixelOffset = static_cast<qsizetype>((x * frame.components + component) * bytesPerComponent);
    const qsizetype rowOffset = static_cast<qsizetype>(y) * frame.bytesPerLine;
    const char *pixel = frame.data.constData() + rowOffset + pixelOffset;

    switch (bytesPerComponent) {
    case 1:
        return static_cast<unsigned char>(pixel[0]);
    case 2: {
        quint16 value = 0;
        std::memcpy(&value, pixel, sizeof(value));
        return value;
    }
    case 4: {
        if (frame.pixelDataType.compare(QStringLiteral("float"), Qt::CaseInsensitive) == 0) {
            float value = 0.0f;
            std::memcpy(&value, pixel, sizeof(value));
            return value;
        }
        quint32 value = 0;
        std::memcpy(&value, pixel, sizeof(value));
        return value;
    }
    default:
        return 0.0;
    }
}

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

double normalize(double value, const ChannelRenderSettings &settings)
{
    const double denominator = settings.high - settings.low;
    if (qFuzzyIsNull(denominator)) {
        return 0.0;
    }
    return std::clamp((value - settings.low) / denominator, 0.0, 1.0);
}
} // namespace

QVector<ChannelRenderSettings> FrameRenderer::defaultChannelSettings(const Nd2DocumentInfo &info)
{
    const int count = qMax(info.channels.size(), qMax(info.componentCount, 1));
    QVector<ChannelRenderSettings> settings;
    settings.reserve(count);

    const double defaultHigh = info.pixelDataType.compare(QStringLiteral("float"), Qt::CaseInsensitive) == 0
                                   ? 1.0
                                   : std::pow(2.0, qMax(info.bitsPerComponentSignificant, info.bitsPerComponentInMemory)) - 1.0;

    for (int i = 0; i < count; ++i) {
        ChannelRenderSettings channel;
        channel.enabled = true;
        channel.low = 0.0;
        channel.high = defaultHigh > 0.0 ? defaultHigh : 1.0;
        channel.autoContrast = true;
        if (i < info.channels.size() && info.channels.at(i).color.isValid()) {
            channel.color = info.channels.at(i).color;
        } else {
            channel.color = defaultColorForIndex(i);
        }
        settings.push_back(channel);
    }

    return settings;
}

bool FrameRenderer::applyAutoContrast(const RawFrame &frame, QVector<ChannelRenderSettings> &settings)
{
    if (!frame.isValid() || settings.isEmpty()) {
        return false;
    }

    const int effectiveChannels = frame.components == 1 ? static_cast<int>(settings.size())
                                                        : std::min(frame.components, static_cast<int>(settings.size()));
    if (effectiveChannels <= 0) {
        return false;
    }

    std::vector<double> minimums(static_cast<size_t>(effectiveChannels), std::numeric_limits<double>::max());
    std::vector<double> maximums(static_cast<size_t>(effectiveChannels), std::numeric_limits<double>::lowest());

    const int step = qMax(1, static_cast<int>(std::sqrt((frame.width * frame.height) / 200000.0)));
    for (int y = 0; y < frame.height; y += step) {
        for (int x = 0; x < frame.width; x += step) {
            if (frame.components == 1) {
                const double value = sampleValue(frame, x, y, 0);
                for (int channelIndex = 0; channelIndex < effectiveChannels; ++channelIndex) {
                    minimums[static_cast<size_t>(channelIndex)] = std::min(minimums[static_cast<size_t>(channelIndex)], value);
                    maximums[static_cast<size_t>(channelIndex)] = std::max(maximums[static_cast<size_t>(channelIndex)], value);
                }
            } else {
                for (int channelIndex = 0; channelIndex < effectiveChannels; ++channelIndex) {
                    const double value = sampleValue(frame, x, y, channelIndex);
                    minimums[static_cast<size_t>(channelIndex)] = std::min(minimums[static_cast<size_t>(channelIndex)], value);
                    maximums[static_cast<size_t>(channelIndex)] = std::max(maximums[static_cast<size_t>(channelIndex)], value);
                }
            }
        }
    }

    bool changed = false;
    for (int channelIndex = 0; channelIndex < effectiveChannels; ++channelIndex) {
        auto &setting = settings[channelIndex];
        if (!setting.autoContrast) {
            continue;
        }

        const double minimum = minimums[static_cast<size_t>(channelIndex)];
        const double maximum = maximums[static_cast<size_t>(channelIndex)];
        const double adjustedHigh = qFuzzyCompare(minimum + 1.0, maximum + 1.0) ? minimum + 1.0 : maximum;
        if (!qFuzzyCompare(setting.low + 1.0, minimum + 1.0) || !qFuzzyCompare(setting.high + 1.0, adjustedHigh + 1.0)) {
            setting.low = minimum;
            setting.high = adjustedHigh;
            changed = true;
        }
    }

    return changed;
}

RenderedFrame FrameRenderer::render(const RawFrame &frame,
                                    const FrameCoordinateState &coordinates,
                                    const QVector<ChannelRenderSettings> &settings)
{
    RenderedFrame rendered;
    rendered.sequenceIndex = frame.sequenceIndex;
    rendered.coordinates = coordinates;

    if (!frame.isValid()) {
        return rendered;
    }

    QImage image(frame.width, frame.height, QImage::Format_ARGB32);
    image.fill(Qt::black);

    const int componentChannelCount = frame.components == 1 ? static_cast<int>(settings.size())
                                                            : std::min(frame.components, static_cast<int>(settings.size()));
    for (int y = 0; y < frame.height; ++y) {
        auto *scanLine = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < frame.width; ++x) {
            double red = 0.0;
            double green = 0.0;
            double blue = 0.0;

            if (frame.components == 1) {
                const double value = sampleValue(frame, x, y, 0);
                for (int channelIndex = 0; channelIndex < componentChannelCount; ++channelIndex) {
                    const auto &setting = settings.at(channelIndex);
                    if (!setting.enabled) {
                        continue;
                    }

                    const double scaled = normalize(value, setting);
                    red += scaled * setting.color.redF();
                    green += scaled * setting.color.greenF();
                    blue += scaled * setting.color.blueF();
                }
            } else {
                for (int channelIndex = 0; channelIndex < componentChannelCount; ++channelIndex) {
                    const auto &setting = settings.at(channelIndex);
                    if (!setting.enabled) {
                        continue;
                    }

                    const double value = sampleValue(frame, x, y, channelIndex);
                    const double scaled = normalize(value, setting);
                    red += scaled * setting.color.redF();
                    green += scaled * setting.color.greenF();
                    blue += scaled * setting.color.blueF();
                }
            }

            scanLine[x] = qRgba(static_cast<int>(std::clamp(red, 0.0, 1.0) * 255.0),
                                static_cast<int>(std::clamp(green, 0.0, 1.0) * 255.0),
                                static_cast<int>(std::clamp(blue, 0.0, 1.0) * 255.0),
                                255);
        }
    }

    rendered.image = image;
    return rendered;
}

QString FrameRenderer::pixelDescription(const RawFrame &frame, const QPoint &pixelPosition)
{
    if (!frame.isValid() || pixelPosition.x() < 0 || pixelPosition.y() < 0 || pixelPosition.x() >= frame.width
        || pixelPosition.y() >= frame.height) {
        return {};
    }

    QStringList components;
    for (int component = 0; component < frame.components; ++component) {
        components << QStringLiteral("c%1=%2").arg(component + 1).arg(sampleValue(frame, pixelPosition.x(), pixelPosition.y(), component), 0, 'f', 3);
    }

    return QStringLiteral("x=%1 y=%2  %3").arg(pixelPosition.x()).arg(pixelPosition.y()).arg(components.join(QStringLiteral("  ")));
}
