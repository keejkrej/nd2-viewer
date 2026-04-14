#include "core/nd2reader.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMutexLocker>

#include <algorithm>
#include <array>
#include <vector>

namespace
{
QString takeSdkString(LIMSTR value)
{
    if (!value) {
        return {};
    }

    const QString result = QString::fromUtf8(value);
    Lim_FileFreeString(value);
    return result;
}

QJsonObject firstChannelVolume(const QJsonDocument &metadataDoc)
{
    if (!metadataDoc.isObject()) {
        return {};
    }

    const auto channels = metadataDoc.object().value(QStringLiteral("channels")).toArray();
    if (channels.isEmpty()) {
        return {};
    }

    return channels.first().toObject().value(QStringLiteral("volume")).toObject();
}

QString readJsonString(LIMFILEHANDLE handle, LIMSTR (*getter)(LIMFILEHANDLE))
{
    return takeSdkString(getter(handle));
}

QColor parsePackedColorRef(quint32 packed)
{
    return QColor::fromRgb(packed & 0xFFu, (packed >> 8) & 0xFFu, (packed >> 16) & 0xFFu);
}
} // namespace

Nd2Reader::~Nd2Reader()
{
    close();
}

bool Nd2Reader::open(const QString &path, QString *errorMessage)
{
    QMutexLocker locker(&mutex_);

    if (handle_) {
        Lim_FileClose(handle_);
        handle_ = nullptr;
    }

    info_ = buildFallbackInfo(path);
#ifdef Q_OS_WIN
    handle_ = Lim_FileOpenForRead(reinterpret_cast<LIMCWSTR>(path.utf16()));
#else
    const QByteArray pathUtf8 = path.toUtf8();
    handle_ = Lim_FileOpenForReadUtf8(pathUtf8.constData());
#endif
    if (!handle_) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open file with the Nikon ND2 SDK.");
        }
        return false;
    }

    if (!loadDocumentInfo(errorMessage)) {
        Lim_FileClose(handle_);
        handle_ = nullptr;
        info_ = buildFallbackInfo(path);
        return false;
    }

    return true;
}

void Nd2Reader::close()
{
    QMutexLocker locker(&mutex_);
    if (handle_) {
        Lim_FileClose(handle_);
        handle_ = nullptr;
    }
    info_ = {};
}

bool Nd2Reader::isOpen() const
{
    QMutexLocker locker(&mutex_);
    return handle_ != nullptr;
}

QString Nd2Reader::filePath() const
{
    QMutexLocker locker(&mutex_);
    return info_.filePath;
}

const DocumentInfo &Nd2Reader::documentInfo() const
{
    return info_;
}

int Nd2Reader::sequenceCount() const
{
    QMutexLocker locker(&mutex_);
    return info_.sequenceCount;
}

QString Nd2Reader::formatCoordinateSelection(const QVector<int> &coords) const
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
        if (type == QStringLiteral("XYPosLoop")) {
            return 3;
        }
        if (type == QStringLiteral("MultipointLoop")) {
            return 4;
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

bool Nd2Reader::sequenceForCoords(const QVector<int> &coords, int *sequenceIndex, QString *errorMessage) const
{
    QMutexLocker locker(&mutex_);

    if (!handle_) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No ND2 document is open.");
        }
        return false;
    }

    std::vector<LIMUINT> nativeCoords(static_cast<size_t>(coords.size()), 0);
    for (int i = 0; i < coords.size(); ++i) {
        nativeCoords[static_cast<size_t>(i)] = static_cast<LIMUINT>(coords.at(i));
    }

    LIMUINT sdkSequenceIndex = 0;
    if (!Lim_FileGetSeqIndexFromCoords(handle_, nativeCoords.data(), nativeCoords.size(), &sdkSequenceIndex)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The selected loop coordinate combination does not exist in this file.");
        }
        return false;
    }

    if (sequenceIndex) {
        *sequenceIndex = static_cast<int>(sdkSequenceIndex);
    }
    return true;
}

RawFrame Nd2Reader::readFrameForCoords(const QVector<int> &coords, QString *errorMessage) const
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
        *errorMessage = selection.isEmpty()
                            ? readError
                            : QStringLiteral("Failed to read ND2 plane at %1 (sequence %2): %3")
                                  .arg(selection, QString::number(sequenceIndex + 1), readError);
    }

    return {};
}

RawFrame Nd2Reader::readFrame(int sequenceIndex, QString *errorMessage) const
{
    QMutexLocker locker(&mutex_);
    RawFrame frame;

    if (!handle_) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No ND2 document is open.");
        }
        return frame;
    }

    LIMPICTURE picture = {};
    const LIMRESULT result = Lim_FileGetImageData(handle_, static_cast<LIMUINT>(sequenceIndex), &picture);
    if (result != LIM_OK) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The Nikon ND2 SDK failed to read image data (error %1).").arg(result);
        }
        return frame;
    }

    frame.sequenceIndex = sequenceIndex;
    frame.width = static_cast<int>(picture.uiWidth);
    frame.height = static_cast<int>(picture.uiHeight);
    frame.bitsPerComponent = static_cast<int>(picture.uiBitsPerComp);
    frame.components = static_cast<int>(picture.uiComponents);
    frame.pixelDataType = info_.pixelDataType;
    frame.bytesPerLine = static_cast<qsizetype>(picture.uiWidthBytes);
    frame.data = QByteArray(static_cast<const char *>(picture.pImageData), static_cast<qsizetype>(picture.uiSize));

    Lim_DestroyPicture(&picture);
    return frame;
}

QString Nd2Reader::frameMetadataText(int sequenceIndex, QString *errorMessage) const
{
    QMutexLocker locker(&mutex_);
    if (!handle_) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No ND2 document is open.");
        }
        return {};
    }

    return takeSdkString(Lim_FileGetFrameMetadata(handle_, static_cast<LIMUINT>(sequenceIndex)));
}

MetadataSection Nd2Reader::frameMetadataSection(int sequenceIndex, QString *errorMessage) const
{
    const QString rawText = frameMetadataText(sequenceIndex, errorMessage);
    return metadataSection(QStringLiteral("Frame Metadata"), parseJsonText(rawText), rawText);
}

MetadataSection Nd2Reader::frameMetadataForCoords(const QVector<int> &coords, QString *errorMessage) const
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
        *errorMessage = selection.isEmpty()
                            ? metadataError
                            : QStringLiteral("Failed to read ND2 metadata at %1 (sequence %2): %3")
                                  .arg(selection, QString::number(sequenceIndex + 1), metadataError);
    }

    return section;
}

QJsonDocument Nd2Reader::parseJsonText(const QString &jsonText)
{
    if (jsonText.trimmed().isEmpty()) {
        return {};
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        return {};
    }
    return doc;
}

bool Nd2Reader::loadDocumentInfo(QString *errorMessage)
{
    info_.sequenceCount = static_cast<int>(Lim_FileGetSeqCount(handle_));
    info_.metadataSections.clear();

    const QString attributesText = readJsonString(handle_, Lim_FileGetAttributes);
    const QString experimentText = readJsonString(handle_, Lim_FileGetExperiment);
    const QString metadataText = readJsonString(handle_, Lim_FileGetMetadata);
    const QString textInfoText = readJsonString(handle_, Lim_FileGetTextinfo);

    const QJsonDocument attributesJson = parseJsonText(attributesText);
    const QJsonDocument experimentJson = parseJsonText(experimentText);
    const QJsonDocument metadataJson = parseJsonText(metadataText);
    const QJsonDocument textInfoJson = parseJsonText(textInfoText);

    const QJsonObject attributesObject = attributesJson.object();
    info_.bitsPerComponentInMemory = firstIntValue(attributesObject, {"bitsPerComponentInMemory", "bitsPerComponent", "bits"});
    info_.bitsPerComponentSignificant = firstIntValue(attributesObject, {"bitsPerComponentSignificant", "significantBits"}, info_.bitsPerComponentInMemory);
    info_.componentCount = firstIntValue(attributesObject, {"componentCount", "components"}, 1);
    info_.pixelDataType = firstStringValue(attributesObject, {"pixelDataType", "componentDataType"}, QStringLiteral("unsigned"));
    info_.frameSize = QSize(
        firstIntValue(attributesObject, {"widthPx", "width"}, 0),
        firstIntValue(attributesObject, {"heightPx", "height"}, 0)
    );

    const QJsonObject metadataObject = metadataJson.object();
    const QJsonObject contentsObject = metadataObject.value(QStringLiteral("contents")).toObject();
    const QJsonObject firstVolume = firstChannelVolume(metadataJson);
    if (info_.sequenceCount <= 0) {
        info_.sequenceCount = firstIntValue(attributesObject, {"sequenceCount", "frameCount"},
                                            firstIntValue(contentsObject, {"frameCount", "sequenceCount"}, 0));
    }
    if (info_.componentCount <= 0) {
        info_.componentCount = firstIntValue(contentsObject, {"channelCount", "componentCount"}, 1);
    }
    if (info_.pixelDataType.isEmpty()) {
        info_.pixelDataType = firstStringValue(firstVolume, {"componentDataType", "pixelDataType"}, QStringLiteral("unsigned"));
    }
    info_.axesCalibration = parseVector3(firstVolume.value(QStringLiteral("axesCalibration")));
    info_.voxelCount = parseVector3(firstVolume.value(QStringLiteral("voxelCount")));

    const LIMSIZE coordSize = Lim_FileGetCoordSize(handle_);
    const QJsonArray experimentArray = experimentJson.array();
    info_.loops.clear();
    info_.loops.reserve(static_cast<qsizetype>(coordSize));
    std::array<LIMCHAR, 256> buffer{};
    for (LIMSIZE i = 0; i < coordSize; ++i) {
        const LIMUINT loopSize = Lim_FileGetCoordInfo(handle_, static_cast<LIMUINT>(i), buffer.data(), buffer.size());
        LoopInfo loop;
        loop.type = QString::fromUtf8(buffer.data());
        loop.label = loopLabel(loop.type, static_cast<int>(i));
        loop.size = static_cast<int>(loopSize);
        if (static_cast<qsizetype>(i) < experimentArray.size() && experimentArray.at(static_cast<qsizetype>(i)).isObject()) {
            loop.details = experimentArray.at(static_cast<qsizetype>(i)).toObject();
        }
        info_.loops.push_back(loop);
        buffer.fill('\0');
    }

    const QJsonArray channelsArray = metadataObject.value(QStringLiteral("channels")).toArray();
    info_.channels.clear();
    info_.channels.reserve(channelsArray.size());
    for (const QJsonValue &channelValue : channelsArray) {
        const QJsonObject channelObject = channelValue.toObject();
        ChannelInfo channel;
        const QJsonObject innerChannel = channelObject.value(QStringLiteral("channel")).toObject();
        channel.index = firstIntValue(innerChannel, {"index", "id"}, info_.channels.size());
        channel.name = firstStringValue(innerChannel, {"name", "label"}, QStringLiteral("Channel %1").arg(channel.index + 1));
        channel.color = parseColorValue(innerChannel.value(QStringLiteral("colorRGB")));
        channel.details = innerChannel;
        channel.loopMap = channelObject.value(QStringLiteral("loops")).toObject();
        channel.microscope = channelObject.value(QStringLiteral("microscope")).toObject();
        channel.volume = channelObject.value(QStringLiteral("volume")).toObject();
        info_.channels.push_back(channel);
    }

    if (info_.channels.isEmpty()) {
        const QVector<QColor> fallbackColors = {
            QColor(255, 255, 255),
            QColor(0, 255, 0),
            QColor(255, 0, 255),
            QColor(255, 170, 0)
        };
        const int channelCount = qMax(info_.componentCount, 1);
        info_.channels.reserve(channelCount);
        for (int i = 0; i < channelCount; ++i) {
            ChannelInfo channel;
            channel.index = i;
            channel.name = QStringLiteral("Channel %1").arg(i + 1);
            channel.color = fallbackColors.at(i % fallbackColors.size());
            info_.channels.push_back(channel);
        }
    }

    if (!info_.frameSize.isValid() && info_.sequenceCount > 0) {
        QString frameError;
        RawFrame frame = readFrame(0, &frameError);
        if (frame.isValid()) {
            info_.frameSize = QSize(frame.width, frame.height);
            if (info_.bitsPerComponentInMemory <= 0) {
                info_.bitsPerComponentInMemory = frame.bitsPerComponent;
            }
            if (info_.bitsPerComponentSignificant <= 0) {
                info_.bitsPerComponentSignificant = frame.bitsPerComponent;
            }
            if (info_.componentCount <= 0) {
                info_.componentCount = frame.components;
            }
        }
    }

    if (info_.sequenceCount <= 0 || !info_.frameSize.isValid()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The file opened, but the SDK did not expose valid frame dimensions or sequence count.");
        }
        return false;
    }

    info_.metadataSections = {
        metadataSection(QStringLiteral("Attributes"), attributesJson, attributesText),
        metadataSection(QStringLiteral("Experiment"), experimentJson, experimentText),
        metadataSection(QStringLiteral("Text Info"), textInfoJson, textInfoText),
        metadataSection(QStringLiteral("Document Metadata"), metadataJson, metadataText),
    };

    return true;
}

QColor Nd2Reader::parseColorValue(const QJsonValue &value)
{
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        if (array.size() >= 3) {
            return QColor(array.at(0).toInt(), array.at(1).toInt(), array.at(2).toInt());
        }
    }

    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        return QColor(object.value(QStringLiteral("r")).toInt(),
                      object.value(QStringLiteral("g")).toInt(),
                      object.value(QStringLiteral("b")).toInt());
    }

    if (value.isString()) {
        const QString stringValue = value.toString();
        const QColor asNamedColor(stringValue);
        if (asNamedColor.isValid()) {
            return asNamedColor;
        }
        bool ok = false;
        const quint32 packed = stringValue.toUInt(&ok, 0);
        if (ok) {
            return parsePackedColorRef(packed);
        }
    }

    if (value.isDouble()) {
        const quint32 packed = static_cast<quint32>(value.toInt());
        return parsePackedColorRef(packed);
    }

    return Qt::white;
}

QVector3D Nd2Reader::parseVector3(const QJsonValue &value)
{
    if (!value.isArray()) {
        return {};
    }

    const QJsonArray array = value.toArray();
    const float x = array.size() > 0 ? static_cast<float>(array.at(0).toDouble()) : 0.0f;
    const float y = array.size() > 1 ? static_cast<float>(array.at(1).toDouble()) : 0.0f;
    const float z = array.size() > 2 ? static_cast<float>(array.at(2).toDouble()) : 0.0f;
    return {x, y, z};
}

int Nd2Reader::firstIntValue(const QJsonObject &object, std::initializer_list<const char *> keys, int fallback)
{
    for (const char *key : keys) {
        const auto value = object.value(QLatin1StringView(key));
        if (value.isDouble()) {
            return value.toInt();
        }
    }
    return fallback;
}

QString Nd2Reader::firstStringValue(const QJsonObject &object, std::initializer_list<const char *> keys, const QString &fallback)
{
    for (const char *key : keys) {
        const auto value = object.value(QLatin1StringView(key));
        if (value.isString()) {
            return value.toString();
        }
    }
    return fallback;
}

QString Nd2Reader::loopLabel(const QString &type, int index)
{
    if (type == QStringLiteral("TimeLoop")) {
        return QStringLiteral("Time");
    }
    if (type == QStringLiteral("ZStackLoop")) {
        return QStringLiteral("Z");
    }
    if (type == QStringLiteral("XYPosLoop")) {
        return QStringLiteral("XY");
    }
    if (type == QStringLiteral("NETimeLoop")) {
        return QStringLiteral("NE Time");
    }
    if (!type.isEmpty() && type != QStringLiteral("Unknown")) {
        return type;
    }
    return QStringLiteral("Loop %1").arg(index + 1);
}

DocumentInfo Nd2Reader::buildFallbackInfo(const QString &path)
{
    DocumentInfo info;
    info.format = DocumentFormat::Nd2;
    info.filePath = path;
    return info;
}

MetadataSection Nd2Reader::metadataSection(const QString &title, const QJsonDocument &document, const QString &rawText)
{
    MetadataSection section;
    section.title = title;
    section.treeValue = document.isArray() ? QJsonValue(document.array())
                                           : (document.isObject() ? QJsonValue(document.object()) : QJsonValue(QJsonValue::Null));
    section.rawText = rawText.isEmpty() ? QString::fromUtf8(document.toJson(QJsonDocument::Indented)) : rawText;
    return section;
}
