#include "core/volumeloader.h"

#include "core/documentreader.h"
#include "core/documentreaderfactory.h"
#include "core/volumeutils.h"

#include <algorithm>
#include <cstring>
#include <memory>

namespace
{
bool copyPlaneToVolume(const RawFrame &frame, RawVolume &volume, int zIndex, QString *errorMessage)
{
    if (!frame.isValid() || !volume.isValid()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The volume assembly inputs are invalid.");
        }
        return false;
    }

    const qsizetype destinationPlaneOffset = static_cast<qsizetype>(zIndex) * volume.planeBytes();
    const int bytesPerComponent = frame.bytesPerComponent();
    const qsizetype destinationRowBytes = static_cast<qsizetype>(volume.width) * bytesPerComponent;

    if (frame.width != volume.width || frame.height != volume.height || frame.bitsPerComponent != volume.bitsPerComponent) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The z-stack planes do not share the same dimensions or pixel type.");
        }
        return false;
    }

    if (frame.components == 1) {
        for (int channelIndex = 0; channelIndex < volume.components; ++channelIndex) {
            char *destination = volume.channelData[channelIndex].data() + destinationPlaneOffset;
            for (int y = 0; y < volume.height; ++y) {
                const char *sourceRow = frame.data.constData() + (static_cast<qsizetype>(y) * frame.bytesPerLine);
                std::memcpy(destination + (static_cast<qsizetype>(y) * destinationRowBytes), sourceRow, static_cast<size_t>(destinationRowBytes));
            }
        }
        return true;
    }

    if (frame.components < volume.components) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The z-stack planes expose inconsistent channel counts.");
        }
        return false;
    }

    for (int channelIndex = 0; channelIndex < volume.components; ++channelIndex) {
        char *destination = volume.channelData[channelIndex].data() + destinationPlaneOffset;
        for (int y = 0; y < volume.height; ++y) {
            const char *sourceRow = frame.data.constData() + (static_cast<qsizetype>(y) * frame.bytesPerLine);
            char *destinationRow = destination + (static_cast<qsizetype>(y) * destinationRowBytes);
            for (int x = 0; x < volume.width; ++x) {
                const char *sourcePixel = sourceRow + (static_cast<qsizetype>((x * frame.components + channelIndex) * bytesPerComponent));
                std::memcpy(destinationRow + (static_cast<qsizetype>(x) * bytesPerComponent), sourcePixel, static_cast<size_t>(bytesPerComponent));
            }
        }
    }

    return true;
}
}

VolumeLoadResult VolumeLoader::load(const QString &path,
                                    const DocumentInfo &info,
                                    const FrameCoordinateState &coordinates,
                                    const QVector<ChannelRenderSettings> &seedChannelSettings,
                                    DocumentReaderOptions readerOptions)
{
    VolumeLoadResult result;
    qInfo("3D volume load requested: path=%s loops=%lld channels=%lld frame=%dx%d",
          qPrintable(path),
          static_cast<long long>(info.loops.size()),
          static_cast<long long>(info.channels.size()),
          info.frameSize.width(),
          info.frameSize.height());

    const int zLoopIndex = VolumeUtils::findZLoopIndex(info);
    if (zLoopIndex < 0) {
        result.error = QStringLiteral("This file does not expose a z-stack for 3D viewing.");
        return result;
    }

    QString readerError;
    std::unique_ptr<DocumentReader> reader = createDocumentReaderForPath(path, readerOptions, &readerError);
    if (!reader) {
        result.error = readerError;
        return result;
    }
    if (!reader->open(path, &readerError)) {
        result.error = readerError;
        return result;
    }

    QVector<int> baseCoordinates = coordinates.values;
    if (baseCoordinates.size() < info.loops.size()) {
        baseCoordinates.resize(info.loops.size());
    }
    baseCoordinates[zLoopIndex] = 0;

    const RawFrame firstFrame = reader->readFrameForCoords(baseCoordinates, &readerError);
    if (!firstFrame.isValid()) {
        result.error = readerError;
        return result;
    }
    qInfo("3D volume first frame: sequence=%d width=%d height=%d components=%d bits=%d type=%s",
          firstFrame.sequenceIndex,
          firstFrame.width,
          firstFrame.height,
          firstFrame.components,
          firstFrame.bitsPerComponent,
          qPrintable(firstFrame.pixelDataType));

    const int displayChannelCount = firstFrame.components == 1
                                        ? std::max({1,
                                                    static_cast<int>(seedChannelSettings.size()),
                                                    static_cast<int>(info.channels.size()),
                                                    info.componentCount})
                                        : firstFrame.components;

    result.volume.fixedCoordinates.values = baseCoordinates;
    result.volume.zLoopIndex = zLoopIndex;
    result.volume.width = firstFrame.width;
    result.volume.height = firstFrame.height;
    result.volume.depth = info.loops.at(zLoopIndex).size;
    result.volume.bitsPerComponent = firstFrame.bitsPerComponent;
    result.volume.components = displayChannelCount;
    result.volume.pixelDataType = firstFrame.pixelDataType;
    result.volume.voxelSpacing = VolumeUtils::sanitizedVoxelSpacing(info.axesCalibration);
    result.volume.channelData.resize(displayChannelCount);
    const qsizetype bytesPerChannel = result.volume.voxelCount() * result.volume.bytesPerComponent();
    for (int index = 0; index < displayChannelCount; ++index) {
        result.volume.channelData[index].resize(bytesPerChannel);
    }

    if (!result.volume.isValid()) {
        result.error = QStringLiteral("The selected z-stack could not allocate a 3D volume.");
        return result;
    }

    for (int z = 0; z < result.volume.depth; ++z) {
        result.volume.fixedCoordinates.values[zLoopIndex] = z;

        const RawFrame frame = reader->readFrameForCoords(result.volume.fixedCoordinates.values, &readerError);
        if (!frame.isValid()) {
            result.error = readerError;
            return result;
        }

        if (!copyPlaneToVolume(frame, result.volume, z, &readerError)) {
            result.error = readerError;
            return result;
        }
    }

    result.volume.fixedCoordinates.values = coordinates.values;
    if (result.volume.fixedCoordinates.values.size() < info.loops.size()) {
        result.volume.fixedCoordinates.values.resize(info.loops.size());
    }
    result.channelSettings = VolumeUtils::defaultVolumeChannelSettings(info, seedChannelSettings, displayChannelCount);
    for (int channelIndex = 0; channelIndex < result.channelSettings.size(); ++channelIndex) {
        const ChannelRenderSettings &settings = result.channelSettings.at(channelIndex);
        qInfo("3D inherited channel settings channel=%d enabled=%d low=%.6f high=%.6f",
              channelIndex,
              settings.enabled ? 1 : 0,
              settings.low,
              settings.high);
    }
    result.success = result.volume.isValid();
    if (!result.success && result.error.isEmpty()) {
        result.error = QStringLiteral("The 3D volume could not be assembled from the selected z-stack.");
    }
    return result;
}
