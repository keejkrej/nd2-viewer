#include "core/czireader.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSize>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <libCZI_Utilities.h>

namespace
{
constexpr int kPreferredPyramidEdge = 2000;

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
    return QStringLiteral("This CZI file is not supported by the CZI reader: %1").arg(reason);
}

bool rectsEqual(const libCZI::IntRect &lhs, const libCZI::IntRect &rhs)
{
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.w == rhs.w && lhs.h == rhs.h;
}

libCZI::IntRect unitedRect(const libCZI::IntRect &lhs, const libCZI::IntRect &rhs)
{
    if (!lhs.IsNonEmpty()) {
        return rhs;
    }

    if (!rhs.IsNonEmpty()) {
        return lhs;
    }

    const int left = std::min(lhs.x, rhs.x);
    const int top = std::min(lhs.y, rhs.y);
    const int right = std::max(lhs.x + lhs.w, rhs.x + rhs.w);
    const int bottom = std::max(lhs.y + lhs.h, rhs.y + rhs.h);
    return {left, top, right - left, bottom - top};
}

bool isLayer0PyramidLayer(const libCZI::ISingleChannelPyramidLayerTileAccessor::PyramidLayerInfo &layerInfo)
{
    return layerInfo.minificationFactor == 0 && layerInfo.pyramidLayerNo == 0;
}

int effectivePyramidScale(const libCZI::ISingleChannelPyramidLayerTileAccessor::PyramidLayerInfo &layerInfo)
{
    if (isLayer0PyramidLayer(layerInfo)) {
        return 1;
    }

    int scale = 1;
    for (int i = 0; i < layerInfo.pyramidLayerNo; ++i) {
        scale *= static_cast<int>(layerInfo.minificationFactor);
    }

    return qMax(scale, 1);
}

QSize scaledFrameSize(const libCZI::IntRect &rect, int scale)
{
    return QSize(qMax(1, rect.w / qMax(scale, 1)),
                 qMax(1, rect.h / qMax(scale, 1)));
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
    documentDimensionInfo_.reset();
    info_ = buildFallbackInfo(QString());
    loopBindings_.clear();
    channelValues_.clear();
    sequencePlanes_.clear();
    coordsToSequence_.clear();
    documentFrameRect_ = {0, 0, 0, 0};
    selectedPyramidLayer_ = {0, 0};
    selectedPyramidScale_ = 1;
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

bool CziReader::isLayer0SubBlock(const libCZI::SubBlockInfo &subBlockInfo)
{
    return subBlockInfo.logicalRect.w == static_cast<int>(subBlockInfo.physicalSize.w)
        && subBlockInfo.logicalRect.h == static_cast<int>(subBlockInfo.physicalSize.h);
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

QString CziReader::formatCoordinateSelection(const QVector<int> &coords) const
{
    struct CoordinateField
    {
        int priority = 0;
        QString text;
    };

    auto loopPriority = [](const QString &type) {
        if (type == QStringLiteral("TimeLoop")) {
            return 0;
        }
        if (type == QStringLiteral("ZStackLoop")) {
            return 1;
        }
        if (type == QStringLiteral("PhaseLoop")) {
            return 2;
        }
        if (type == QStringLiteral("SceneLoop")) {
            return 3;
        }
        if (type == QStringLiteral("RotationLoop")) {
            return 4;
        }
        if (type == QStringLiteral("IlluminationLoop")) {
            return 5;
        }
        if (type == QStringLiteral("ViewLoop")) {
            return 6;
        }
        if (type == QStringLiteral("BlockLoop")) {
            return 7;
        }
        return 100;
    };

    QVector<CoordinateField> fields;
    fields.reserve(std::min(coords.size(), info_.loops.size()));
    for (int index = 0; index < coords.size() && index < info_.loops.size(); ++index) {
        const LoopInfo &loop = info_.loops.at(index);
        const QString label = loop.label.isEmpty() ? loop.type : loop.label;
        fields.push_back({loopPriority(loop.type), QStringLiteral("%1=%2").arg(label, QString::number(coords.at(index)))});
    }

    std::sort(fields.begin(), fields.end(), [](const CoordinateField &lhs, const CoordinateField &rhs) {
        if (lhs.priority != rhs.priority) {
            return lhs.priority < rhs.priority;
        }
        return lhs.text < rhs.text;
    });

    QStringList parts;
    parts.reserve(fields.size());
    for (const CoordinateField &field : std::as_const(fields)) {
        parts.push_back(field.text);
    }
    return parts.join(QStringLiteral(", "));
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

RawFrame CziReader::readFrameForCoords(const QVector<int> &coords, QString *errorMessage) const
{
    int sequenceIndex = -1;
    QString sequenceError;
    if (!sequenceForCoords(coords, &sequenceIndex, &sequenceError)) {
        if (errorMessage) {
            const QString selection = formatCoordinateSelection(coords);
            *errorMessage = selection.isEmpty()
                                ? sequenceError
                                : QStringLiteral("%1: %2").arg(selection,
                                                               sequenceError.isEmpty()
                                                                   ? QStringLiteral("The selected loop coordinate combination does not exist in this file.")
                                                                   : sequenceError);
        }
        return {};
    }

    QString readError;
    const RawFrame frame = readFrame(sequenceIndex, &readError);
    if (frame.isValid()) {
        return frame;
    }

    if (errorMessage) {
        const QString selection = formatCoordinateSelection(coords);
        QString detail = readError;

        if (selection.isEmpty()) {
            *errorMessage = detail;
        } else if (detail.isEmpty()) {
            *errorMessage = QStringLiteral("Failed to read CZI plane at %1 (sequence %2).")
                                .arg(selection, QString::number(sequenceIndex + 1));
        } else {
            *errorMessage = QStringLiteral("Failed to read CZI plane at %1 (sequence %2): %3")
                                .arg(selection, QString::number(sequenceIndex + 1), detail);
        }
    }

    return {};
}

MetadataSection CziReader::frameMetadataForCoords(const QVector<int> &coords, QString *errorMessage) const
{
    int sequenceIndex = -1;
    QString sequenceError;
    if (!sequenceForCoords(coords, &sequenceIndex, &sequenceError)) {
        if (errorMessage) {
            const QString selection = formatCoordinateSelection(coords);
            *errorMessage = selection.isEmpty()
                                ? sequenceError
                                : QStringLiteral("%1: %2").arg(selection,
                                                               sequenceError.isEmpty()
                                                                   ? QStringLiteral("The selected loop coordinate combination does not exist in this file.")
                                                                   : sequenceError);
        }
        return {};
    }

    QString metadataError;
    const MetadataSection section = frameMetadataSection(sequenceIndex, &metadataError);
    if (!metadataError.isEmpty() && errorMessage) {
        const QString selection = formatCoordinateSelection(coords);
        QString detail = metadataError;

        *errorMessage = selection.isEmpty()
                            ? detail
                            : QStringLiteral("Failed to read CZI metadata at %1 (sequence %2): %3")
                                  .arg(selection, QString::number(sequenceIndex + 1), detail);
    }

    return section;
}

const CziReader::SequencePlane *CziReader::sequencePlaneForIndex(int sequenceIndex, QString *errorMessage) const
{
    if (!reader_) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No CZI document is open.");
        }
        return nullptr;
    }

    if (sequenceIndex < 0 || sequenceIndex >= sequencePlanes_.size()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The requested CZI frame index is out of range.");
        }
        return nullptr;
    }

    return &sequencePlanes_.at(sequenceIndex);
}

std::optional<double> CziReader::relativeTimeMsForSequencePlane(const SequencePlane &plane) const
{
    if (!documentDimensionInfo_) {
        return std::nullopt;
    }

    int tBindingIndex = -1;
    for (int i = 0; i < loopBindings_.size(); ++i) {
        if (loopBindings_.at(i).dimension == libCZI::DimensionIndex::T) {
            tBindingIndex = i;
            break;
        }
    }
    if (tBindingIndex < 0 || tBindingIndex >= plane.coordinates.size()) {
        return std::nullopt;
    }

    const int tIndex = plane.coordinates.at(tBindingIndex);
    if (tIndex < 0) {
        return std::nullopt;
    }

    try {
        const std::shared_ptr<libCZI::IDimensionTInfo> tInfo = documentDimensionInfo_->GetDimensionTInfo();
        if (!tInfo) {
            return std::nullopt;
        }

        std::vector<double> offsets;
        if (tInfo->TryGetOffsetsList(&offsets) && !offsets.empty()) {
            if (tIndex < static_cast<int>(offsets.size())) {
                return offsets.at(static_cast<size_t>(tIndex)) * 1000.0;
            }
        }

        double offsetSeconds = 0.0;
        double incrementSeconds = 0.0;
        if (tInfo->TryGetIntervalDefinition(&offsetSeconds, &incrementSeconds)) {
            const double seconds = offsetSeconds + incrementSeconds * static_cast<double>(tIndex);
            if (std::isfinite(seconds)) {
                return seconds * 1000.0;
            }
        }
    } catch (const std::exception &) {
        return std::nullopt;
    }

    return std::nullopt;
}

bool CziReader::planeCoordinateForChannel(const SequencePlane &sequencePlane,
                                          int channelSlot,
                                          libCZI::CDimCoordinate *coordinate,
                                          libCZI::PixelType *pixelType,
                                          QString *errorMessage) const
{
    if (channelSlot < 0 || channelSlot >= sequencePlane.channelSubblockIndices.size()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The requested CZI channel index is out of range.");
        }
        return false;
    }

    const QVector<int> &subblockIndices = sequencePlane.channelSubblockIndices.at(channelSlot);
    if (subblockIndices.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The requested CZI plane does not contain any layer-0 image data.");
        }
        return false;
    }

    libCZI::SubBlockInfo subBlockInfo = {};
    if (!reader_->TryGetSubBlockInfo(subblockIndices.constFirst(), &subBlockInfo)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("libCZI could not query CZI sub-block %1.").arg(subblockIndices.constFirst());
        }
        return false;
    }

    if (coordinate) {
        *coordinate = subBlockInfo.coordinate;
        coordinate->Clear(libCZI::DimensionIndex::S);
    }

    if (pixelType) {
        *pixelType = subBlockInfo.pixelType;
    }

    return true;
}

RawFrame CziReader::readFrame(int sequenceIndex, QString *errorMessage) const
{
    QMutexLocker locker(&mutex_);
    RawFrame frame;

    const SequencePlane *sequencePlane = sequencePlaneForIndex(sequenceIndex, errorMessage);
    if (!sequencePlane) {
        return frame;
    }

    const libCZI::IntRect targetFrameRect =
        (documentFrameRect_.w > 0 && documentFrameRect_.h > 0) ? documentFrameRect_ : sequencePlane->frameRect;
    const bool usePyramidLayer = !isLayer0PyramidLayer(selectedPyramidLayer_);
    const bool requiresTileComposition =
        usePyramidLayer
        || std::any_of(sequencePlane->channelSubblockIndices.cbegin(),
                       sequencePlane->channelSubblockIndices.cend(),
                       [](const QVector<int> &indices) { return indices.size() > 1; });

    int actualScene = -1;
    for (int bindingIndex = 0; bindingIndex < loopBindings_.size() && bindingIndex < sequencePlane->coordinates.size(); ++bindingIndex) {
        const LoopBinding &binding = loopBindings_.at(bindingIndex);
        if (binding.dimension == libCZI::DimensionIndex::S) {
            actualScene = binding.start + sequencePlane->coordinates.at(bindingIndex);
            break;
        }
    }

    std::shared_ptr<libCZI::IIndexSet> sceneFilter;
    if (actualScene >= 0) {
        sceneFilter = libCZI::Utils::IndexSetFromString(std::to_wstring(actualScene));
    }

    const QSize targetFrameSize = usePyramidLayer ? scaledFrameSize(targetFrameRect, selectedPyramidScale_)
                                                  : QSize(targetFrameRect.w, targetFrameRect.h);

    frame.sequenceIndex = sequenceIndex;
    frame.width = targetFrameSize.width();
    frame.height = targetFrameSize.height();
    frame.bitsPerComponent = info_.bitsPerComponentSignificant > 0 ? info_.bitsPerComponentSignificant : info_.bitsPerComponentInMemory;
    frame.components = static_cast<int>(sequencePlane->channelSubblockIndices.size());
    frame.pixelDataType = info_.pixelDataType;
    frame.bytesPerLine = static_cast<qsizetype>(frame.width) * frame.components * frame.bytesPerComponent();
    frame.data.resize(frame.bytesPerLine * frame.height);
    std::memset(frame.data.data(), 0, static_cast<size_t>(frame.data.size()));

    std::shared_ptr<libCZI::ISingleChannelTileAccessor> tileAccessor;
    std::shared_ptr<libCZI::ISingleChannelPyramidLayerTileAccessor> pyramidAccessor;
    if (usePyramidLayer) {
        pyramidAccessor = reader_->CreateSingleChannelPyramidLayerTileAccessor();
        if (!pyramidAccessor) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("libCZI could not create a pyramid accessor for this CZI plane.");
            }
            return {};
        }
    } else if (requiresTileComposition) {
        tileAccessor = reader_->CreateSingleChannelTileAccessor();
        if (!tileAccessor) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("libCZI could not create a tile accessor for this CZI plane.");
            }
            return {};
        }
    }

    for (int channelSlot = 0; channelSlot < sequencePlane->channelSubblockIndices.size(); ++channelSlot) {
        const QVector<int> &subblockIndices = sequencePlane->channelSubblockIndices.at(channelSlot);
        if (subblockIndices.isEmpty()) {
            continue;
        }

        std::shared_ptr<libCZI::IBitmapData> bitmap;
        try {
            if (usePyramidLayer) {
                libCZI::CDimCoordinate planeCoordinate;
                libCZI::PixelType pixelType = libCZI::PixelType::Invalid;
                if (!planeCoordinateForChannel(*sequencePlane, channelSlot, &planeCoordinate, &pixelType, errorMessage)) {
                    return {};
                }

                libCZI::ISingleChannelPyramidLayerTileAccessor::Options options;
                options.Clear();
                options.backGroundColor = {0.0f, 0.0f, 0.0f};
                options.sortByM = true;
                options.sceneFilter = sceneFilter;
                bitmap = pyramidAccessor->Get(pixelType, targetFrameRect, &planeCoordinate, selectedPyramidLayer_, &options);
            } else if (requiresTileComposition) {
                libCZI::CDimCoordinate planeCoordinate;
                libCZI::PixelType pixelType = libCZI::PixelType::Invalid;
                if (!planeCoordinateForChannel(*sequencePlane, channelSlot, &planeCoordinate, &pixelType, errorMessage)) {
                    return {};
                }

                libCZI::ISingleChannelTileAccessor::Options options;
                options.Clear();
                options.backGroundColor = {0.0f, 0.0f, 0.0f};
                options.sortByM = true;
                options.useVisibilityCheckOptimization = true;
                options.sceneFilter = sceneFilter;
                bitmap = tileAccessor->Get(pixelType, targetFrameRect, &planeCoordinate, &options);
            } else {
                const std::shared_ptr<libCZI::ISubBlock> subBlock = reader_->ReadSubBlock(subblockIndices.constFirst());
                if (!subBlock) {
                    if (errorMessage) {
                        *errorMessage = QStringLiteral("libCZI could not read CZI sub-block %1.").arg(subblockIndices.constFirst());
                    }
                    return {};
                }

                bitmap = libCZI::CreateBitmapFromSubBlock(subBlock.get());
            }

            if (!bitmap) {
                if (errorMessage) {
                    *errorMessage = usePyramidLayer ? QStringLiteral("libCZI could not compose the requested CZI pyramid plane.")
                                                    : requiresTileComposition
                                                          ? QStringLiteral("libCZI could not compose the requested CZI plane.")
                                                          : QStringLiteral("libCZI could not decode CZI sub-block %1.")
                                                                .arg(subblockIndices.constFirst());
                }
                return {};
            }

            const libCZI::IntSize bitmapSize = bitmap->GetSize();
            const int bitmapWidth = static_cast<int>(bitmapSize.w);
            const int bitmapHeight = static_cast<int>(bitmapSize.h);
            const int expectedWidth = usePyramidLayer ? frame.width : requiresTileComposition ? targetFrameRect.w : sequencePlane->frameRect.w;
            const int expectedHeight = usePyramidLayer ? frame.height : requiresTileComposition ? targetFrameRect.h : sequencePlane->frameRect.h;
            if (bitmapWidth != expectedWidth || bitmapHeight != expectedHeight) {
                if (errorMessage) {
                    *errorMessage = unsupportedMessage(QStringLiteral("decoded layer-0 channel planes do not share the same size"));
                }
                return {};
            }

            if (!isSupportedPixelType(bitmap->GetPixelType())
                || bitsPerComponentFor(bitmap->GetPixelType()) != frame.bitsPerComponent
                || pixelDataTypeFor(bitmap->GetPixelType()) != frame.pixelDataType) {
                if (errorMessage) {
                    *errorMessage = unsupportedMessage(QStringLiteral("decoded CZI planes changed pixel type during composition"));
                }
                return {};
            }

            const int bytesPerComponent = frame.bytesPerComponent();
            const int xOffset = (usePyramidLayer || requiresTileComposition) ? 0 : (sequencePlane->frameRect.x - targetFrameRect.x);
            const int yOffset = (usePyramidLayer || requiresTileComposition) ? 0 : (sequencePlane->frameRect.y - targetFrameRect.y);
            if (xOffset < 0 || yOffset < 0 || xOffset + bitmapWidth > frame.width || yOffset + bitmapHeight > frame.height) {
                if (errorMessage) {
                    *errorMessage = unsupportedMessage(QStringLiteral("decoded CZI plane lies outside the composed document bounds"));
                }
                return {};
            }

            libCZI::ScopedBitmapLockerSP lockInfo{bitmap};
            for (int y = 0; y < bitmapHeight; ++y) {
                const char *sourceRow = static_cast<const char *>(lockInfo.ptrDataRoi) + static_cast<qsizetype>(y) * lockInfo.stride;
                char *destinationRow = frame.data.data() + static_cast<qsizetype>(y + yOffset) * frame.bytesPerLine;
                for (int x = 0; x < bitmapWidth; ++x) {
                    const char *sourcePixel = sourceRow + static_cast<qsizetype>(x) * bytesPerComponent;
                    char *destinationPixel =
                        destinationRow + static_cast<qsizetype>(((x + xOffset) * frame.components + channelSlot) * bytesPerComponent);
                    std::memcpy(destinationPixel, sourcePixel, static_cast<size_t>(bytesPerComponent));
                }
            }
        } catch (const std::exception &exception) {
            if (errorMessage) {
                *errorMessage = QString::fromUtf8(exception.what());
            }
            return {};
        }
    }

    return frame;
}

MetadataSection CziReader::frameMetadataSection(int sequenceIndex, QString *errorMessage) const
{
    QMutexLocker locker(&mutex_);
    const SequencePlane *sequencePlane = sequencePlaneForIndex(sequenceIndex, errorMessage);
    if (!sequencePlane) {
        return {};
    }

    QJsonArray channelArray;
    QStringList rawSections;
    constexpr int kMetadataSampleLimit = 8;
    for (int channelSlot = 0; channelSlot < sequencePlane->channelSubblockIndices.size(); ++channelSlot) {
        try {
            const QVector<int> &subblockIndices = sequencePlane->channelSubblockIndices.at(channelSlot);
            if (subblockIndices.isEmpty()) {
                continue;
            }

            QJsonObject item;
            item.insert(QStringLiteral("channelIndex"), channelSlot);
            item.insert(QStringLiteral("channelName"),
                        channelSlot < info_.channels.size() ? info_.channels.at(channelSlot).name : QStringLiteral("Channel %1").arg(channelSlot + 1));
            item.insert(QStringLiteral("tileCount"), subblockIndices.size());
            item.insert(QStringLiteral("frameRect"), rectToJson(sequencePlane->frameRect));

            QJsonArray sampledSubblocks;
            QStringList sampledXml;
            const int sampleCount = std::min(static_cast<int>(subblockIndices.size()), kMetadataSampleLimit);
            for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
                const int subblockIndex = subblockIndices.at(sampleIndex);
                const std::shared_ptr<libCZI::ISubBlock> subBlock = reader_->ReadSubBlock(subblockIndex);
                if (!subBlock) {
                    continue;
                }

                const libCZI::SubBlockInfo &subBlockInfo = subBlock->GetSubBlockInfo();
                QJsonObject subblockObject = subBlockInfoToJson(subBlockInfo);
                subblockObject.insert(QStringLiteral("subBlockIndex"), subblockIndex);
                sampledSubblocks.push_back(subblockObject);

                const std::shared_ptr<libCZI::ISubBlockMetadata> metadata = libCZI::CreateSubBlockMetadataFromSubBlock(subBlock.get());
                const QString xml = metadata ? QString::fromUtf8(metadata->GetXml().c_str()) : QString();
                if (!xml.trimmed().isEmpty()) {
                    sampledXml << QStringLiteral("<!-- Channel %1 sample tile %2 (sub-block %3) -->\n%4")
                                      .arg(channelSlot + 1)
                                      .arg(sampleIndex + 1)
                                      .arg(subblockIndex)
                                      .arg(xml);
                }
            }

            item.insert(QStringLiteral("sampledSubBlocks"), sampledSubblocks);
            item.insert(QStringLiteral("sampledTileCount"), sampleCount);
            item.insert(QStringLiteral("omittedTileCount"), std::max(0, static_cast<int>(subblockIndices.size()) - sampleCount));
            item.insert(QStringLiteral("hasMetadataXml"), !sampledXml.isEmpty());
            channelArray.push_back(item);

            if (!sampledXml.isEmpty()) {
                rawSections << sampledXml.join(QStringLiteral("\n\n"));
            }
        } catch (const std::exception &exception) {
            if (errorMessage) {
                *errorMessage = QString::fromUtf8(exception.what());
            }
            return {};
        }
    }

    QJsonObject treeRoot;
    treeRoot.insert(QStringLiteral("channels"), channelArray);
    if (const std::optional<double> relMs = relativeTimeMsForSequencePlane(*sequencePlane)) {
        treeRoot.insert(QStringLiteral("time"), QJsonObject{{QStringLiteral("relativeTimeMs"), *relMs}});
    }

    return jsonMetadataSection(QStringLiteral("Frame Metadata"),
                               treeRoot,
                               rawSections.isEmpty() ? QStringLiteral("No sampled frame-level XML metadata was found for the current CZI plane.")
                                                     : rawSections.join(QStringLiteral("\n\n")));
}

bool CziReader::loadDocumentInfo(QString *errorMessage)
{
    info_.metadataSections.clear();
    loopBindings_.clear();
    channelValues_.clear();
    sequencePlanes_.clear();
    coordsToSequence_.clear();
    documentDimensionInfo_.reset();
    documentFrameRect_ = {0, 0, 0, 0};
    selectedPyramidLayer_ = {0, 0};
    selectedPyramidScale_ = 1;

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

    struct ChannelPlaneRecord
    {
        bool initialized = false;
        libCZI::PixelType pixelType = libCZI::PixelType::Invalid;
        libCZI::IntRect frameRect = {0, 0, 0, 0};
        QVector<int> subblockIndices;
    };

    struct PlaneRecord
    {
        bool initialized = false;
        libCZI::PixelType pixelType = libCZI::PixelType::Invalid;
        std::map<int, ChannelPlaneRecord> channels;
    };

    std::map<QString, PlaneRecord> planeRecords;
    QString failureMessage;
    reader_->EnumerateSubBlocks([&](int index, const libCZI::SubBlockInfo &subBlockInfo) {
        if (!isLayer0SubBlock(subBlockInfo)) {
            return true;
        }

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
        if (!record.initialized) {
            record.initialized = true;
            record.pixelType = subBlockInfo.pixelType;
        } else if (record.pixelType != subBlockInfo.pixelType) {
            failureMessage = unsupportedMessage(QStringLiteral("a plane mixes channel pixel types"));
            return false;
        }

        ChannelPlaneRecord &channelRecord = record.channels[actualChannel];
        if (!channelRecord.initialized) {
            channelRecord.initialized = true;
            channelRecord.pixelType = subBlockInfo.pixelType;
            channelRecord.frameRect = subBlockInfo.logicalRect;
        } else if (channelRecord.pixelType != subBlockInfo.pixelType) {
            failureMessage = unsupportedMessage(QStringLiteral("a plane mixes channel pixel types"));
            return false;
        }

        channelRecord.frameRect = unitedRect(channelRecord.frameRect, subBlockInfo.logicalRect);
        channelRecord.subblockIndices.push_back(index);
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

    libCZI::IntRect resolvedFrameRect = {0, 0, 0, 0};
    bool haveResolvedFrameRect = false;
    libCZI::PixelType resolvedPixelType = libCZI::PixelType::Invalid;
    for (const QVector<int> &coords : sortedCoordinates) {
        const PlaneRecord &record = planeRecords.at(coordinateKey(coords));
        SequencePlane sequencePlane;
        sequencePlane.coordinates = coords;
        sequencePlane.channelSubblockIndices.reserve(channelValues_.size());

        libCZI::IntRect planeRect = {0, 0, 0, 0};
        bool havePlaneRect = false;
        for (int actualChannel : std::as_const(channelValues_)) {
            const auto it = record.channels.find(actualChannel);
            if (it == record.channels.end() || it->second.subblockIndices.isEmpty()) {
                sequencePlane.channelSubblockIndices.push_back({});
                continue;
            }

            const ChannelPlaneRecord &channelRecord = it->second;
            if (!havePlaneRect) {
                planeRect = channelRecord.frameRect;
                havePlaneRect = true;
            } else if (!rectsEqual(planeRect, channelRecord.frameRect)) {
                if (errorMessage) {
                    *errorMessage = unsupportedMessage(QStringLiteral("channel bounds differ within a layer-0 plane"));
                }
                return false;
            }

            sequencePlane.channelSubblockIndices.push_back(channelRecord.subblockIndices);
        }

        if (!havePlaneRect || planeRect.w <= 0 || planeRect.h <= 0) {
            if (errorMessage) {
                *errorMessage = unsupportedMessage(QStringLiteral("a plane has invalid layer-0 bounds"));
            }
            return false;
        }

        if (!haveResolvedFrameRect) {
            resolvedFrameRect = planeRect;
            haveResolvedFrameRect = true;
            resolvedPixelType = record.pixelType;
        } else if (resolvedPixelType != record.pixelType) {
            if (errorMessage) {
                *errorMessage = unsupportedMessage(QStringLiteral("pixel types vary across loop coordinates"));
            }
            return false;
        }

        resolvedFrameRect = unitedRect(resolvedFrameRect, planeRect);

        sequencePlane.frameRect = planeRect;
        sequencePlanes_.push_back(sequencePlane);
        coordsToSequence_.insert(coordinateKey(coords), sequencePlanes_.size() - 1);
    }

    documentFrameRect_ = resolvedFrameRect;
    info_.frameSize = QSize(resolvedFrameRect.w, resolvedFrameRect.h);
    selectedPyramidLayer_ = {0, 0};
    selectedPyramidScale_ = 1;

    try {
        const libCZI::PyramidStatistics pyramidStatistics = reader_->GetPyramidStatistics();
        std::map<int, libCZI::ISingleChannelPyramidLayerTileAccessor::PyramidLayerInfo> commonLayers;
        bool initializedCommonLayers = false;
        for (const auto &[sceneIndex, layers] : pyramidStatistics.scenePyramidStatistics) {
            Q_UNUSED(sceneIndex);

            std::map<int, libCZI::ISingleChannelPyramidLayerTileAccessor::PyramidLayerInfo> sceneLayers;
            for (const auto &layer : layers) {
                if (layer.layerInfo.IsLayer0() || layer.layerInfo.IsNotIdentifiedAsPyramidLayer()) {
                    continue;
                }

                const libCZI::ISingleChannelPyramidLayerTileAccessor::PyramidLayerInfo layerInfo = {
                    layer.layerInfo.minificationFactor,
                    layer.layerInfo.pyramidLayerNo,
                };
                const int scale = effectivePyramidScale(layerInfo);
                if (scale <= 1) {
                    continue;
                }

                sceneLayers.emplace(scale, layerInfo);
            }

            if (!initializedCommonLayers) {
                commonLayers = sceneLayers;
                initializedCommonLayers = true;
                continue;
            }

            for (auto it = commonLayers.begin(); it != commonLayers.end();) {
                if (!sceneLayers.contains(it->first)) {
                    it = commonLayers.erase(it);
                } else {
                    ++it;
                }
            }
        }

        if (!commonLayers.empty()) {
            const auto preferredLayer = std::find_if(commonLayers.begin(), commonLayers.end(), [&](const auto &entry) {
                const QSize size = scaledFrameSize(resolvedFrameRect, entry.first);
                return size.width() <= kPreferredPyramidEdge && size.height() <= kPreferredPyramidEdge;
            });

            const auto chosenLayer = preferredLayer != commonLayers.end() ? preferredLayer : std::prev(commonLayers.end());
            selectedPyramidLayer_ = chosenLayer->second;
            selectedPyramidScale_ = chosenLayer->first;
            info_.frameSize = scaledFrameSize(resolvedFrameRect, selectedPyramidScale_);
        }
    } catch (const std::exception &) {
    }

    info_.sequenceCount = static_cast<int>(sequencePlanes_.size());
    info_.componentCount = static_cast<int>(channelValues_.size());
    info_.bitsPerComponentInMemory = bitsPerComponentFor(resolvedPixelType);
    info_.bitsPerComponentSignificant = info_.bitsPerComponentInMemory;
    info_.pixelDataType = pixelDataTypeFor(resolvedPixelType);

    if (metadataDocInfo) {
        const libCZI::ScalingInfo scalingInfo = metadataDocInfo->GetScalingInfo();
        // libCZI reports pixel/voxel lengths in meters; DocumentInfo::axesCalibration matches ND2 (µm).
        constexpr double kMetersToMicrons = 1e6;
        info_.axesCalibration = QVector3D(static_cast<float>(scalingInfo.scaleX * kMetersToMicrons),
                                          static_cast<float>(scalingInfo.scaleY * kMetersToMicrons),
                                          static_cast<float>(scalingInfo.scaleZ * kMetersToMicrons));
        if (selectedPyramidScale_ > 1) {
            info_.axesCalibration.setX(info_.axesCalibration.x() * selectedPyramidScale_);
            info_.axesCalibration.setY(info_.axesCalibration.y() * selectedPyramidScale_);
        }
    }

    documentDimensionInfo_ = std::move(metadataDocInfo);

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

    try {
        const libCZI::PyramidStatistics pyramidStatistics = reader_->GetPyramidStatistics();
        QJsonObject pyramidObject;
        bool hasIgnoredHigherLayers = false;
        for (const auto &[sceneIndex, layers] : pyramidStatistics.scenePyramidStatistics) {
            QJsonArray sceneLayers;
            for (const auto &layer : layers) {
                QJsonObject layerObject{
                    {QStringLiteral("minificationFactor"), static_cast<int>(layer.layerInfo.minificationFactor)},
                    {QStringLiteral("pyramidLayerNo"), static_cast<int>(layer.layerInfo.pyramidLayerNo)},
                    {QStringLiteral("count"), layer.count},
                    {QStringLiteral("isLayer0"), layer.layerInfo.IsLayer0()},
                };
                sceneLayers.push_back(layerObject);
                hasIgnoredHigherLayers = hasIgnoredHigherLayers || !layer.layerInfo.IsLayer0();
            }

            const QString sceneKey = sceneIndex == (std::numeric_limits<int>::max)()
                                         ? QStringLiteral("invalidSceneIndex")
                                         : QString::number(sceneIndex);
            pyramidObject.insert(sceneKey, sceneLayers);
        }

        summary.insert(QStringLiteral("pyramidStatistics"), pyramidObject);
        summary.insert(QStringLiteral("ignoresHigherPyramidLevels"), hasIgnoredHigherLayers && isLayer0PyramidLayer(selectedPyramidLayer_));
        summary.insert(QStringLiteral("layer0ReadoutMode"), isLayer0PyramidLayer(selectedPyramidLayer_) ? QStringLiteral("fullPlane")
                                                                                                          : QStringLiteral("autoPyramid"));
        summary.insert(QStringLiteral("selectedPyramidScale"), selectedPyramidScale_);
        summary.insert(QStringLiteral("selectedPyramidLayer"),
                       QJsonObject{{QStringLiteral("minificationFactor"), static_cast<int>(selectedPyramidLayer_.minificationFactor)},
                                   {QStringLiteral("pyramidLayerNo"), static_cast<int>(selectedPyramidLayer_.pyramidLayerNo)},
                                   {QStringLiteral("isLayer0"), isLayer0PyramidLayer(selectedPyramidLayer_)}});
    } catch (const std::exception &) {
    }

    summary.insert(QStringLiteral("metadataXmlAvailable"), !metadataXml.trimmed().isEmpty());
    return summary;
}
