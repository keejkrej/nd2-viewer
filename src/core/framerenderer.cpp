#include "core/framerenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>

namespace
{
constexpr double kMinimumPercentileGap = 0.001;

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

int effectiveComponentIndex(const RawFrame &frame, int channelIndex)
{
    if (channelIndex < 0) {
        return -1;
    }

    if (frame.components == 1) {
        return 0;
    }

    if (channelIndex >= frame.components) {
        return -1;
    }

    return channelIndex;
}

int samplingStep(const RawFrame &frame)
{
    return qMax(1, static_cast<int>(std::sqrt((frame.width * frame.height) / 200000.0)));
}

void sanitizePercentileRange(ChannelRenderSettings &settings)
{
    settings.lowPercentile = std::clamp(settings.lowPercentile, 0.0, 100.0);
    settings.highPercentile = std::clamp(settings.highPercentile, 0.0, 100.0);

    if (settings.highPercentile - settings.lowPercentile >= kMinimumPercentileGap) {
        return;
    }

    if (settings.lowPercentile >= 100.0) {
        settings.lowPercentile = 100.0 - kMinimumPercentileGap;
        settings.highPercentile = 100.0;
        return;
    }

    settings.highPercentile = std::min(100.0, settings.lowPercentile + kMinimumPercentileGap);
    if (settings.highPercentile - settings.lowPercentile < kMinimumPercentileGap) {
        settings.lowPercentile = std::max(0.0, settings.highPercentile - kMinimumPercentileGap);
    }
}

double adjustedHighValue(double lowValue, double highValue)
{
    return highValue > lowValue ? highValue : lowValue + 1.0e-9;
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

QVector<ChannelRenderSettings> FrameRenderer::defaultChannelSettings(const DocumentInfo &info)
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
        channel.lowPercentile = 0.1;
        channel.highPercentile = 99.9;
        if (i < info.channels.size() && info.channels.at(i).color.isValid()) {
            channel.color = info.channels.at(i).color;
        } else {
            channel.color = defaultColorForIndex(i);
        }
        settings.push_back(channel);
    }

    return settings;
}

ChannelAutoContrastAnalysis FrameRenderer::analyzeChannel(const RawFrame &frame, int channelIndex, int histogramBinCount)
{
    ChannelAutoContrastAnalysis analysis;

    if (!frame.isValid() || histogramBinCount <= 0) {
        return analysis;
    }

    const int component = effectiveComponentIndex(frame, channelIndex);
    if (component < 0) {
        return analysis;
    }

    const int step = samplingStep(frame);
    const int sampleColumns = (frame.width + step - 1) / step;
    const int sampleRows = (frame.height + step - 1) / step;
    QVector<double> samples;
    samples.reserve(sampleColumns * sampleRows);

    double minimum = std::numeric_limits<double>::max();
    double maximum = std::numeric_limits<double>::lowest();
    for (int y = 0; y < frame.height; y += step) {
        for (int x = 0; x < frame.width; x += step) {
            const double value = sampleValue(frame, x, y, component);
            samples.push_back(value);
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
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

bool FrameRenderer::applyAutoContrastToChannel(const ChannelAutoContrastAnalysis &analysis, ChannelRenderSettings &settings)
{
    if (!analysis.isValid()) {
        return false;
    }

    sanitizePercentileRange(settings);

    const double minimum = percentileToValue(analysis, settings.lowPercentile);
    const double maximum = percentileToValue(analysis, settings.highPercentile);
    const double adjustedHigh = adjustedHighValue(minimum, maximum);
    if (!qFuzzyCompare(settings.low + 1.0, minimum + 1.0) || !qFuzzyCompare(settings.high + 1.0, adjustedHigh + 1.0)) {
        settings.low = minimum;
        settings.high = adjustedHigh;
        return true;
    }

    return false;
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

    bool changed = false;
    std::optional<ChannelAutoContrastAnalysis> sharedSingleChannelAnalysis;
    for (int channelIndex = 0; channelIndex < effectiveChannels; ++channelIndex) {
        auto &setting = settings[channelIndex];
        if (!setting.autoContrast) {
            continue;
        }

        if (frame.components == 1) {
            if (!sharedSingleChannelAnalysis.has_value()) {
                sharedSingleChannelAnalysis = analyzeChannel(frame, channelIndex);
            }
            changed = applyAutoContrastToChannel(*sharedSingleChannelAnalysis, setting) || changed;
            continue;
        }

        changed = applyAutoContrastToChannel(analyzeChannel(frame, channelIndex), setting) || changed;
    }

    return changed;
}

double FrameRenderer::percentileToValue(const ChannelAutoContrastAnalysis &analysis, double percentile)
{
    if (!analysis.isValid()) {
        return 0.0;
    }

    const double clampedPercentile = std::clamp(percentile, 0.0, 100.0);
    if (analysis.sortedSamples.size() == 1) {
        return analysis.sortedSamples.front();
    }

    const double position = (clampedPercentile / 100.0) * (analysis.sortedSamples.size() - 1);
    const int maxIndex = static_cast<int>(analysis.sortedSamples.size()) - 1;
    const int lowerIndex = std::clamp(static_cast<int>(std::floor(position)), 0, maxIndex);
    const int upperIndex = std::clamp(static_cast<int>(std::ceil(position)), 0, maxIndex);
    if (lowerIndex == upperIndex) {
        return analysis.sortedSamples.at(lowerIndex);
    }

    const double fraction = position - lowerIndex;
    const double lowerValue = analysis.sortedSamples.at(lowerIndex);
    const double upperValue = analysis.sortedSamples.at(upperIndex);
    return lowerValue + ((upperValue - lowerValue) * fraction);
}

double FrameRenderer::valueToPercentile(const ChannelAutoContrastAnalysis &analysis, double value)
{
    if (!analysis.isValid()) {
        return 0.0;
    }

    if (analysis.sortedSamples.size() == 1) {
        return 0.0;
    }

    const auto begin = analysis.sortedSamples.cbegin();
    const auto end = analysis.sortedSamples.cend();
    const auto lowerIt = std::lower_bound(begin, end, value);
    const auto upperIt = std::upper_bound(begin, end, value);

    if (lowerIt != upperIt) {
        const double lowerIndex = std::distance(begin, lowerIt);
        const double upperIndex = std::distance(begin, upperIt) - 1.0;
        const double rank = (lowerIndex + upperIndex) / 2.0;
        return std::clamp((rank / (analysis.sortedSamples.size() - 1)) * 100.0, 0.0, 100.0);
    }

    if (lowerIt == begin) {
        return 0.0;
    }
    if (lowerIt == end) {
        return 100.0;
    }

    const int upperIndex = static_cast<int>(std::distance(begin, lowerIt));
    const int lowerIndex = upperIndex - 1;
    const double lowerValue = analysis.sortedSamples.at(lowerIndex);
    const double upperValue = analysis.sortedSamples.at(upperIndex);
    if (qFuzzyCompare(lowerValue + 1.0, upperValue + 1.0)) {
        return std::clamp((static_cast<double>(upperIndex) / (analysis.sortedSamples.size() - 1)) * 100.0, 0.0, 100.0);
    }

    const double fraction = std::clamp((value - lowerValue) / (upperValue - lowerValue), 0.0, 1.0);
    const double rank = lowerIndex + fraction;
    return std::clamp((rank / (analysis.sortedSamples.size() - 1)) * 100.0, 0.0, 100.0);
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
