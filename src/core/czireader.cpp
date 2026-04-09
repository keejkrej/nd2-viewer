#include "core/czireader.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <map>
#include <set>
#include <string>

namespace
{
QJsonObject rectToJson(const libCZI::IntRect &rect)
{
    return {
        {QStringLiteral("x"), rect.x},
        {QStringLiteral("y"), rect.y},
        {QStringLiteral("width"), rect.w},
        {QStringLiteral("height"), rect.h},
    };
}

QJsonObject sizeToJson(const libCZI::IntSize &size)
{
    return {
        {QStringLiteral("width"), static_cast<int>(size.w)},
        {QStringLiteral("height"), static_cast<int>(size.h)},
    };
}

QJsonObject dimensionIntervalObject(libCZI::DimensionIndex dimension, int start, int size)
{
    return {
        {QStringLiteral("dimension"), CziReader::dimensionName(dimension)},
        {QStringLiteral("start"), start},
        {QStringLiteral("size"), size},
    };
}

QJsonObject subBlockInfoToJson(const libCZI::SubBlockInfo &subBlockInfo)
{
    return {
        {QStringLiteral("pixelType"), static_cast<int>(subBlockInfo.pixelType)},
        {QStringLiteral("compressionModeRaw"), subBlockInfo.compressionModeRaw},
        {QStringLiteral("coordinate"), CziReader::dimensionCoordinateSummary(subBlockInfo.coordinate)},
        {QStringLiteral("logicalRect"), rectToJson(subBlockInfo.logicalRect)},
        {QStringLiteral("physicalSize"), sizeToJson(subBlockInfo.physicalSize)},
        {QStringLiteral("mIndex"), subBlockInfo.IsMindexValid() ? QJsonValue(subBlockInfo.mIndex) : QJsonValue(QStringLiteral("invalid"))},
        {QStringLiteral("pyramidType"), static_cast<int>(subBlockInfo.pyramidType)},
    };
}

QString unsupportedMessage(const QString &reason)
{
    return QStringLiteral("This CZI file is not supported by the first-pass CZI reader: %1").arg(reason);
}
} // namespace

CziReader::~CziReader()
{
    close();
}

bool CziReader::open(const QString &path, QString *errorMessage)
{
    close();

    QMutexLocker locker(&mutex_);
    info_ = buildFallbackInfo(path);

    try {
        // libCZI converts wchar_t → UTF-8 for fopen. On Windows wchar_t is UTF-16; on macOS/Linux it is
        // UTF-32. Do not pass QString::utf16() as wchar_t* on POSIX — UTF-16 surrogate pairs are invalid
        // UTF-32 and trigger std::wstring_convert errors inside libCZI.
        const std::wstring wpath = path.toStdWString();
        stream_ = libCZI::CreateStreamFromFile(wpath.c_str());
        if (!stream_) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Failed to create a libCZI file stream.");
            }
            return false;
        }

        reader_ = libCZI::CreateCZIReader();
        if (!reader_) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Failed to create a libCZI reader.");
            }
            return false;
        }

        reader_->Open(stream_);
        if (!loadDocumentInfo(errorMessage)) {
            reader_.reset();
            stream_.reset();
            info_ = buildFallbackInfo(path);
            return false;
        }
    } catch (const std::exception &exception) {
        reader_.reset();
        stream_.reset();
        info_ = buildFallbackInfo(path);
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open the CZI file with libCZI: %1").arg(QString::fromUtf8(exception.what()));
        }
        return false;
    }

    return true;
}

void CziReader::close()
{
    QMutexLocker locker(&mutex_);
    if (reader_) {
        reader_->Close();
    }

    reader_.reset();
    stream_.reset();
    info_ = buildFallbackInfo(QString());
    loopBindings_.clear();
    channelValues_.clear();
    sequenceCoordinates_.clear();
    sequenceSubblockIndices_.clear();
    coordsToSequence_.clear();
}

bool CziReader::isOpen() const
{
    QMutexLocker locker(&mutex_);
    return static_cast<bool>(reader_);
}

QString CziReader::filePath() const
{
    QMutexLocker locker(&mutex_);
    return info_.filePath;
}

const DocumentInfo &CziReader::documentInfo() const
{
    return info_;
}

int CziReader::sequenceCount() const
{
    QMutexLocker locker(&mutex_);
    return info_.sequenceCount;
}

DocumentInfo CziReader::buildFallbackInfo(const QString &path)
{
    DocumentInfo info;
    info.format = DocumentFormat::Czi;
    info.filePath = path;
    return info;
}

QString CziReader::coordinateKey(const QVector<int> &coords)
{
    QStringList parts;
    parts.reserve(coords.size());
    for (int value : coords) {
        parts.push_back(QString::number(value));
    }
    return parts.join(QLatin1Char(','));
}

QString CziReader::loopType(libCZI::DimensionIndex dimension)
{
    switch (dimension) {
    case libCZI::DimensionIndex::T:
        return QStringLiteral("TimeLoop");
    case libCZI::DimensionIndex::Z:
        return QStringLiteral("ZStackLoop");
    case libCZI::DimensionIndex::S:
        return QStringLiteral("SceneLoop");
    case libCZI::DimensionIndex::R:
        return QStringLiteral("RotationLoop");
    case libCZI::DimensionIndex::I:
        return QStringLiteral("IlluminationLoop");
    case libCZI::DimensionIndex::H:
        return QStringLiteral("PhaseLoop");
    case libCZI::DimensionIndex::V:
        return QStringLiteral("ViewLoop");
    case libCZI::DimensionIndex::B:
        return QStringLiteral("BlockLoop");
    default:
        return QStringLiteral("Loop");
    }
}

QString CziReader::loopLabel(libCZI::DimensionIndex dimension)
{
    switch (dimension) {
    case libCZI::DimensionIndex::T:
        return QStringLiteral("Time");
    case libCZI::DimensionIndex::Z:
        return QStringLiteral("Z");
    case libCZI::DimensionIndex::S:
        return QStringLiteral("Scene");
    case libCZI::DimensionIndex::R:
        return QStringLiteral("Rotation");
    case libCZI::DimensionIndex::I:
        return QStringLiteral("Illumination");
    case libCZI::DimensionIndex::H:
        return QStringLiteral("Phase");
    case libCZI::DimensionIndex::V:
        return QStringLiteral("View");
    case libCZI::DimensionIndex::B:
        return QStringLiteral("Block");
    default:
        return dimensionName(dimension);
    }
}

QString CziReader::dimensionName(libCZI::DimensionIndex dimension)
{
    switch (dimension) {
    case libCZI::DimensionIndex::Z:
        return QStringLiteral("Z");
    case libCZI::DimensionIndex::C:
        return QStringLiteral("C");
    case libCZI::DimensionIndex::T:
        return QStringLiteral("T");
    case libCZI::DimensionIndex::R:
        return QStringLiteral("R");
    case libCZI::DimensionIndex::S:
        return QStringLiteral("S");
    case libCZI::DimensionIndex::I:
        return QStringLiteral("I");
    case libCZI::DimensionIndex::H:
        return QStringLiteral("H");
    case libCZI::DimensionIndex::V:
        return QStringLiteral("V");
    case libCZI::DimensionIndex::B:
        return QStringLiteral("B");
    default:
        return QStringLiteral("Invalid");
    }
}

QString CziReader::dimensionCoordinateSummary(const libCZI::IDimCoordinate &coordinate)
{
    QStringList parts;
    for (int rawDim = static_cast<int>(libCZI::DimensionIndex::MinDim); rawDim <= static_cast<int>(libCZI::DimensionIndex::MaxDim); ++rawDim) {
        const auto dimension = static_cast<libCZI::DimensionIndex>(rawDim);
        int value = 0;
        if (coordinate.TryGetPosition(dimension, &value)) {
            parts.push_back(QStringLiteral("%1%2").arg(dimensionName(dimension), QString::number(value)));
        }
    }

    return parts.join(QStringLiteral(", "));
}

MetadataSection CziReader::jsonMetadataSection(const QString &title, const QJsonValue &treeValue, const QString &rawText)
{
    MetadataSection section;
    section.title = title;
    section.treeValue = treeValue;
    section.rawText = rawText;
    return section;
}

QColor CziReader::defaultColorForIndex(int index)
{
    static const std::array<QColor, 6> colors = {
        QColor(255, 255, 255),
        QColor(0, 255, 0),
        QColor(255, 0, 255),
        QColor(255, 170, 0),
        QColor(0, 255, 255),
        QColor(255, 80, 80),
    };

    return colors.at(static_cast<size_t>(index) % colors.size());
}

bool CziReader::isSupportedPixelType(libCZI::PixelType pixelType)
{
    return pixelType == libCZI::PixelType::Gray8
        || pixelType == libCZI::PixelType::Gray16
        || pixelType == libCZI::PixelType::Gray32Float;
}

int CziReader::bitsPerComponentFor(libCZI::PixelType pixelType)
{
    switch (pixelType) {
    case libCZI::PixelType::Gray8:
        return 8;
    case libCZI::PixelType::Gray16:
        return 16;
    case libCZI::PixelType::Gray32Float:
        return 32;
    default:
        return 0;
    }
}

QString CziReader::pixelDataTypeFor(libCZI::PixelType pixelType)
{
    return pixelType == libCZI::PixelType::Gray32Float ? QStringLiteral("float") : QStringLiteral("unsigned");
}

bool CziReader::sequenceForCoords(const QVector<int> &coords, int *sequenceIndex, QString *errorMessage) const
{
    QMutexLocker locker(&mutex_);
    if (!reader_) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No CZI document is open.");
        }
        return false;
    }

    const int index = coordsToSequence_.value(coordinateKey(coords), -1);
    if (index < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The selected loop coordinate combination does not exist in this file.");
        }
        return false;
    }

    if (sequenceIndex) {
        *sequenceIndex = index;
    }
    return true;
}

QVector<int> CziReader::subblockIndicesForSequence(int sequenceIndex, QString *errorMessage) const
{
    if (!reader_) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No CZI document is open.");
        }
        return {};
    }

    if (sequenceIndex < 0 || sequenceIndex >= sequenceSubblockIndices_.size()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The requested CZI frame index is out of range.");
        }
        return {};
    }

    return sequenceSubblockIndices_.at(sequenceIndex);
}

RawFrame CziReader::readFrame(int sequenceIndex, QString *errorMessage) const
{
    QMutexLocker locker(&mutex_);
    RawFrame frame;

    const QVector<int> subblockIndices = subblockIndicesForSequence(sequenceIndex, errorMessage);
    if (subblockIndices.isEmpty()) {
        return frame;
    }

    frame.sequenceIndex = sequenceIndex;
    frame.width = info_.frameSize.width();
    frame.height = info_.frameSize.height();
    frame.bitsPerComponent = info_.bitsPerComponentSignificant > 0 ? info_.bitsPerComponentSignificant : info_.bitsPerComponentInMemory;
    frame.components = subblockIndices.size();
    frame.pixelDataType = info_.pixelDataType;
    frame.bytesPerLine = static_cast<qsizetype>(frame.width) * frame.components * frame.bytesPerComponent();
    frame.data.resize(frame.bytesPerLine * frame.height);

    for (int channelSlot = 0; channelSlot < subblockIndices.size(); ++channelSlot) {
        std::shared_ptr<libCZI::IBitmapData> bitmap;
        try {
            const std::shared_ptr<libCZI::ISubBlock> subBlock = reader_->ReadSubBlock(subblockIndices.at(channelSlot));
            if (!subBlock) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("libCZI could not read CZI sub-block %1.").arg(subblockIndices.at(channelSlot));
                }
                return {};
            }

            bitmap = libCZI::CreateBitmapFromSubBlock(subBlock.get());
            if (!bitmap) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("libCZI could not decode CZI sub-block %1.").arg(subblockIndices.at(channelSlot));
                }
                return {};
            }

            const libCZI::IntSize bitmapSize = bitmap->GetSize();
            if (static_cast<int>(bitmapSize.w) != frame.width || static_cast<int>(bitmapSize.h) != frame.height) {
                if (errorMessage) {
                    *errorMessage = unsupportedMessage(QStringLiteral("decoded channel planes do not share the same size"));
                }
                return {};
            }

            const libCZI::BitmapLockInfo lockInfo = bitmap->Lock();
            const int bytesPerComponent = frame.bytesPerComponent();
            for (int y = 0; y < frame.height; ++y) {
                const char *sourceRow = static_cast<const char *>(lockInfo.ptrDataRoi) + static_cast<qsizetype>(y) * lockInfo.stride;
                char *destinationRow = frame.data.data() + static_cast<qsizetype>(y) * frame.bytesPerLine;
                for (int x = 0; x < frame.width; ++x) {
                    const char *sourcePixel = sourceRow + static_cast<qsizetype>(x) * bytesPerComponent;
                    char *destinationPixel = destinationRow + static_cast<qsizetype>((x * frame.components + channelSlot) * bytesPerComponent);
                    std::memcpy(destinationPixel, sourcePixel, static_cast<size_t>(bytesPerComponent));
                }
            }
            bitmap->Unlock();
        } catch (const std::exception &exception) {
            if (bitmap && bitmap->GetLockCount() > 0) {
                bitmap->Unlock();
            }

            if (errorMessage) {
                *errorMessage = QStringLiteral("libCZI failed to decode frame %1: %2")
                                    .arg(sequenceIndex + 1)
                                    .arg(QString::fromUtf8(exception.what()));
            }
            return {};
        }
    }

    return frame;
}

MetadataSection CziReader::frameMetadataSection(int sequenceIndex, QString *errorMessage) const
{
    QMutexLocker locker(&mutex_);
    const QVector<int> subblockIndices = subblockIndicesForSequence(sequenceIndex, errorMessage);
    if (subblockIndices.isEmpty()) {
        return {};
    }

    QJsonArray channelArray;
    QStringList rawSections;
    for (int channelSlot = 0; channelSlot < subblockIndices.size(); ++channelSlot) {
        try {
            const std::shared_ptr<libCZI::ISubBlock> subBlock = reader_->ReadSubBlock(subblockIndices.at(channelSlot));
            if (!subBlock) {
                continue;
            }

            const libCZI::SubBlockInfo &subBlockInfo = subBlock->GetSubBlockInfo();
            const std::shared_ptr<libCZI::ISubBlockMetadata> metadata = libCZI::CreateSubBlockMetadataFromSubBlock(subBlock.get());
            const QString xml = metadata ? QString::fromUtf8(metadata->GetXml().c_str()) : QString();

            QJsonObject item = subBlockInfoToJson(subBlockInfo);
            item.insert(QStringLiteral("channelIndex"), channelSlot);
            item.insert(QStringLiteral("channelName"),
                        channelSlot < info_.channels.size() ? info_.channels.at(channelSlot).name : QStringLiteral("Channel %1").arg(channelSlot + 1));
            item.insert(QStringLiteral("subBlockIndex"), subblockIndices.at(channelSlot));
            item.insert(QStringLiteral("hasMetadataXml"), !xml.trimmed().isEmpty());
            channelArray.push_back(item);

            if (!xml.trimmed().isEmpty()) {
                rawSections << QStringLiteral("<!-- Channel %1: %2 -->\n%3")
                                   .arg(channelSlot + 1)
                                   .arg(channelSlot < info_.channels.size() ? info_.channels.at(channelSlot).name : QStringLiteral("Channel %1").arg(channelSlot + 1))
                                   .arg(xml);
            }
        } catch (const std::exception &exception) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("libCZI failed to read frame metadata: %1").arg(QString::fromUtf8(exception.what()));
            }
            return {};
        }
    }

    return jsonMetadataSection(QStringLiteral("Frame Metadata"),
                               channelArray,
                               rawSections.isEmpty() ? QStringLiteral("No frame-level XML metadata was found for the current CZI plane.")
                                                     : rawSections.join(QStringLiteral("\n\n")));
}

bool CziReader::loadDocumentInfo(QString *errorMessage)
{
    info_.metadataSections.clear();
    loopBindings_.clear();
    channelValues_.clear();
    sequenceCoordinates_.clear();
    sequenceSubblockIndices_.clear();
    coordsToSequence_.clear();

    if (hasUnsupportedPyramidData(errorMessage)) {
        return false;
    }

    if (!buildSequenceMap(errorMessage)) {
        return false;
    }

    if (info_.sequenceCount <= 0 || !info_.frameSize.isValid()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The CZI file opened, but libCZI did not expose any supported image planes.");
        }
        return false;
    }

    QString metadataXml;
    try {
        if (const std::shared_ptr<libCZI::IMetadataSegment> metadataSegment = reader_->ReadMetadataSegment()) {
            if (const std::shared_ptr<libCZI::ICziMetadata> metadata = metadataSegment->CreateMetaFromMetadataSegment()) {
                metadataXml = QString::fromUtf8(metadata->GetXml().c_str());
            }
        }
    } catch (const std::exception &) {
        metadataXml.clear();
    }

    const QJsonObject summaryObject = buildSummaryMetadata(metadataXml);
    const QString summaryText = QString::fromUtf8(QJsonDocument(summaryObject).toJson(QJsonDocument::Indented));
    info_.metadataSections = {
        jsonMetadataSection(QStringLiteral("Summary"), summaryObject, summaryText),
        jsonMetadataSection(QStringLiteral("Metadata XML"),
                            summaryObject,
                            metadataXml.isEmpty() ? QStringLiteral("No document-level CZI XML metadata was found.") : metadataXml),
    };

    return true;
}

bool CziReader::hasUnsupportedPyramidData(QString *errorMessage) const
{
    try {
        const libCZI::PyramidStatistics pyramidStatistics = reader_->GetPyramidStatistics();
        for (const auto &[sceneIndex, layers] : pyramidStatistics.scenePyramidStatistics) {
            Q_UNUSED(sceneIndex);
            for (const auto &layer : layers) {
                if (!layer.layerInfo.IsLayer0()) {
                    if (errorMessage) {
                        *errorMessage = unsupportedMessage(QStringLiteral("pyramid layers are present"));
                    }
                    return true;
                }
            }
        }
    } catch (const std::exception &) {
    }

    return false;
}

bool CziReader::validateSubBlockInfo(const libCZI::SubBlockInfo &subBlockInfo, QString *errorMessage) const
{
    if (!isSupportedPixelType(subBlockInfo.pixelType)) {
        if (errorMessage) {
            *errorMessage = unsupportedMessage(QStringLiteral("only Gray8, Gray16, and Gray32Float channel planes are supported"));
        }
        return false;
    }

    if (subBlockInfo.logicalRect.w <= 0 || subBlockInfo.logicalRect.h <= 0) {
        if (errorMessage) {
            *errorMessage = unsupportedMessage(QStringLiteral("a plane has invalid bounds"));
        }
        return false;
    }

    if (subBlockInfo.logicalRect.w != static_cast<int>(subBlockInfo.physicalSize.w)
        || subBlockInfo.logicalRect.h != static_cast<int>(subBlockInfo.physicalSize.h)) {
        if (errorMessage) {
            *errorMessage = unsupportedMessage(QStringLiteral("tiled, scaled, or non-layer0 planes are present"));
        }
        return false;
    }

    return true;
}

bool CziReader::buildSequenceMap(QString *errorMessage)
{
    static const std::array<libCZI::DimensionIndex, 8> supportedLoopDimensions = {
        libCZI::DimensionIndex::Z,
        libCZI::DimensionIndex::T,
        libCZI::DimensionIndex::S,
        libCZI::DimensionIndex::R,
        libCZI::DimensionIndex::I,
        libCZI::DimensionIndex::H,
        libCZI::DimensionIndex::V,
        libCZI::DimensionIndex::B,
    };

    const libCZI::SubBlockStatistics statistics = reader_->GetStatistics();
    std::set<int> actualChannels;
    info_.loops.clear();
    for (const auto dimension : supportedLoopDimensions) {
        int start = 0;
        int size = 0;
        if (!statistics.dimBounds.TryGetInterval(dimension, &start, &size) || size <= 1) {
            continue;
        }

        loopBindings_.push_back({dimension, start, size});

        LoopInfo loop;
        loop.type = loopType(dimension);
        loop.label = loopLabel(dimension);
        loop.size = size;
        loop.details = dimensionIntervalObject(dimension, start, size);
        info_.loops.push_back(loop);
    }

    struct PlaneRecord
    {
        bool initialized = false;
        QSize frameSize;
        libCZI::PixelType pixelType = libCZI::PixelType::Invalid;
        std::map<int, int> subblockByChannel;
    };

    std::map<QString, PlaneRecord> planeRecords;
    QString failureMessage;
    reader_->EnumerateSubBlocks([&](int index, const libCZI::SubBlockInfo &subBlockInfo) {
        if (!validateSubBlockInfo(subBlockInfo, &failureMessage)) {
            return false;
        }

        QVector<int> viewerCoords;
        viewerCoords.reserve(loopBindings_.size());
        for (const LoopBinding &binding : loopBindings_) {
            int actualValue = binding.start;
            if (subBlockInfo.coordinate.TryGetPosition(binding.dimension, &actualValue)) {
                if (actualValue < binding.start || actualValue >= binding.start + binding.size) {
                    failureMessage = unsupportedMessage(QStringLiteral("a plane uses loop coordinates outside the declared dimension bounds"));
                    return false;
                }
            }
            viewerCoords.push_back(actualValue - binding.start);
        }

        int actualChannel = 0;
        if (!subBlockInfo.coordinate.TryGetPosition(libCZI::DimensionIndex::C, &actualChannel)) {
            actualChannel = 0;
        }
        actualChannels.insert(actualChannel);

        PlaneRecord &record = planeRecords[coordinateKey(viewerCoords)];
        const QSize currentSize(subBlockInfo.logicalRect.w, subBlockInfo.logicalRect.h);
        if (!record.initialized) {
            record.initialized = true;
            record.frameSize = currentSize;
            record.pixelType = subBlockInfo.pixelType;
        } else if (record.frameSize != currentSize || record.pixelType != subBlockInfo.pixelType) {
            failureMessage = unsupportedMessage(QStringLiteral("a plane mixes channel sizes or pixel types"));
            return false;
        }

        if (record.subblockByChannel.contains(actualChannel)) {
            failureMessage = unsupportedMessage(QStringLiteral("multiple sub-blocks are required for a single plane/channel combination"));
            return false;
        }

        record.subblockByChannel.emplace(actualChannel, index);
        return true;
    });

    if (!failureMessage.isEmpty()) {
        if (errorMessage) {
            *errorMessage = failureMessage;
        }
        return false;
    }

    if (planeRecords.empty() || actualChannels.empty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No supported CZI image planes were found.");
        }
        return false;
    }

    channelValues_ = QVector<int>(actualChannels.begin(), actualChannels.end());
    info_.channels.clear();
    info_.channels.reserve(channelValues_.size());

    std::shared_ptr<libCZI::ICziMultiDimensionDocumentInfo> metadataDocInfo;
    std::shared_ptr<libCZI::IDisplaySettings> displaySettings;
    try {
        if (const std::shared_ptr<libCZI::IMetadataSegment> metadataSegment = reader_->ReadMetadataSegment()) {
            if (const std::shared_ptr<libCZI::ICziMetadata> metadata = metadataSegment->CreateMetaFromMetadataSegment()) {
                metadataDocInfo = metadata->GetDocumentInfo();
                if (metadataDocInfo) {
                    displaySettings = metadataDocInfo->GetDisplaySettings();
                }
            }
        }
    } catch (const std::exception &) {
    }

    const std::shared_ptr<libCZI::IDimensionsChannelsInfo> channelsInfo = metadataDocInfo ? metadataDocInfo->GetDimensionChannelsInfo() : nullptr;
    for (int slot = 0; slot < channelValues_.size(); ++slot) {
        const int actualChannel = channelValues_.at(slot);
        ChannelInfo channel;
        channel.index = slot;
        channel.name = QStringLiteral("Channel %1").arg(slot + 1);
        channel.color = defaultColorForIndex(slot);
        channel.details.insert(QStringLiteral("cziChannelIndex"), actualChannel);

        if (channelsInfo && actualChannel >= 0 && actualChannel < channelsInfo->GetChannelCount()) {
            if (const std::shared_ptr<libCZI::IDimensionChannelInfo> channelInfo = channelsInfo->GetChannel(actualChannel)) {
                std::wstring name;
                if (channelInfo->TryGetAttributeName(&name) && !name.empty()) {
                    channel.name = QString::fromStdWString(name);
                }

                std::wstring fluor;
                if (channelInfo->TryGetFluor(&fluor) && !fluor.empty()) {
                    channel.details.insert(QStringLiteral("fluor"), QString::fromStdWString(fluor));
                }

                double excitation = 0.0;
                if (channelInfo->TryGetExcitationWavelength(&excitation)) {
                    channel.details.insert(QStringLiteral("excitationWavelength"), excitation);
                }

                double emission = 0.0;
                if (channelInfo->TryGetEmissionWavelength(&emission)) {
                    channel.details.insert(QStringLiteral("emissionWavelength"), emission);
                }
            }
        }

        if (displaySettings) {
            if (const std::shared_ptr<libCZI::IChannelDisplaySetting> displaySetting = displaySettings->GetChannelDisplaySettings(actualChannel)) {
                libCZI::Rgb8Color tintColor = {};
                if (displaySetting->TryGetTintingColorRgb8(&tintColor)) {
                    channel.color = QColor(tintColor.r, tintColor.g, tintColor.b);
                }
            }
        }

        info_.channels.push_back(channel);
    }

    std::vector<QVector<int>> sortedCoordinates;
    sortedCoordinates.reserve(planeRecords.size());
    for (const auto &[key, record] : planeRecords) {
        Q_UNUSED(record);
        QVector<int> coords;
        if (!key.isEmpty()) {
            const QStringList parts = key.split(QLatin1Char(','), Qt::KeepEmptyParts);
            coords.reserve(parts.size());
            for (const QString &part : parts) {
                coords.push_back(part.toInt());
            }
        }
        sortedCoordinates.push_back(coords);
    }

    std::sort(sortedCoordinates.begin(), sortedCoordinates.end(), [](const QVector<int> &lhs, const QVector<int> &rhs) {
        return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
    });

    for (const QVector<int> &coords : sortedCoordinates) {
        const PlaneRecord &record = planeRecords.at(coordinateKey(coords));
        QVector<int> subblocks;
        subblocks.reserve(channelValues_.size());
        for (int actualChannel : std::as_const(channelValues_)) {
            const auto it = record.subblockByChannel.find(actualChannel);
            if (it == record.subblockByChannel.end()) {
                if (errorMessage) {
                    *errorMessage = unsupportedMessage(QStringLiteral("channel coverage is irregular across planes"));
                }
                return false;
            }
            subblocks.push_back(it->second);
        }

        sequenceCoordinates_.push_back(coords);
        sequenceSubblockIndices_.push_back(subblocks);
        coordsToSequence_.insert(coordinateKey(coords), sequenceCoordinates_.size() - 1);
    }

    const PlaneRecord &firstPlane = planeRecords.begin()->second;
    info_.frameSize = firstPlane.frameSize;
    info_.sequenceCount = sequenceSubblockIndices_.size();
    info_.componentCount = channelValues_.size();
    info_.bitsPerComponentInMemory = bitsPerComponentFor(firstPlane.pixelType);
    info_.bitsPerComponentSignificant = info_.bitsPerComponentInMemory;
    info_.pixelDataType = pixelDataTypeFor(firstPlane.pixelType);

    if (metadataDocInfo) {
        const libCZI::ScalingInfo scalingInfo = metadataDocInfo->GetScalingInfo();
        info_.axesCalibration = QVector3D(static_cast<float>(scalingInfo.scaleX),
                                          static_cast<float>(scalingInfo.scaleY),
                                          static_cast<float>(scalingInfo.scaleZ));
    }

    return true;
}

QJsonObject CziReader::buildSummaryMetadata(const QString &metadataXml) const
{
    QJsonObject summary;
    summary.insert(QStringLiteral("format"), QStringLiteral("CZI"));
    summary.insert(QStringLiteral("filePath"), info_.filePath);
    summary.insert(QStringLiteral("sequenceCount"), info_.sequenceCount);
    summary.insert(QStringLiteral("frameSize"),
                   QJsonObject{{QStringLiteral("width"), info_.frameSize.width()},
                               {QStringLiteral("height"), info_.frameSize.height()}});
    summary.insert(QStringLiteral("componentCount"), info_.componentCount);
    summary.insert(QStringLiteral("pixelDataType"), info_.pixelDataType);
    summary.insert(QStringLiteral("bitsPerComponentInMemory"), info_.bitsPerComponentInMemory);
    summary.insert(QStringLiteral("bitsPerComponentSignificant"), info_.bitsPerComponentSignificant);

    QJsonArray loopsArray;
    for (const LoopInfo &loop : info_.loops) {
        loopsArray.push_back(QJsonObject{
            {QStringLiteral("type"), loop.type},
            {QStringLiteral("label"), loop.label},
            {QStringLiteral("size"), loop.size},
            {QStringLiteral("details"), loop.details},
        });
    }
    summary.insert(QStringLiteral("loops"), loopsArray);

    QJsonArray channelsArray;
    for (const ChannelInfo &channel : info_.channels) {
        channelsArray.push_back(QJsonObject{
            {QStringLiteral("index"), channel.index},
            {QStringLiteral("name"), channel.name},
            {QStringLiteral("color"), channel.color.name()},
            {QStringLiteral("details"), channel.details},
        });
    }
    summary.insert(QStringLiteral("channels"), channelsArray);

    try {
        const libCZI::SubBlockStatistics statistics = reader_->GetStatistics();
        summary.insert(QStringLiteral("subBlockCount"), statistics.subBlockCount);
        summary.insert(QStringLiteral("boundingBox"), rectToJson(statistics.boundingBox));
        summary.insert(QStringLiteral("boundingBoxLayer0Only"), rectToJson(statistics.boundingBoxLayer0Only));

        QJsonArray dimensionBounds;
        for (int rawDim = static_cast<int>(libCZI::DimensionIndex::MinDim); rawDim <= static_cast<int>(libCZI::DimensionIndex::MaxDim); ++rawDim) {
            const auto dimension = static_cast<libCZI::DimensionIndex>(rawDim);
            int start = 0;
            int size = 0;
            if (statistics.dimBounds.TryGetInterval(dimension, &start, &size)) {
                dimensionBounds.push_back(dimensionIntervalObject(dimension, start, size));
            }
        }
        summary.insert(QStringLiteral("dimensionBounds"), dimensionBounds);
    } catch (const std::exception &) {
    }

    summary.insert(QStringLiteral("metadataXmlAvailable"), !metadataXml.trimmed().isEmpty());
    return summary;
}
