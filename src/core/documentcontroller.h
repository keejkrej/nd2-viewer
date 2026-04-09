#pragma once

#include "core/documentreader.h"
#include "core/documentreaderfactory.h"
#include "core/framerenderer.h"

#include <QFutureWatcher>
#include <QObject>

#include <memory>

class DocumentController : public QObject
{
    Q_OBJECT

public:
    explicit DocumentController(QObject *parent = nullptr);
    ~DocumentController() override;

    bool openFile(const QString &path);
    void closeFile();

    [[nodiscard]] bool hasDocument() const;
    [[nodiscard]] QString currentPath() const;
    [[nodiscard]] const DocumentInfo &documentInfo() const;
    [[nodiscard]] const FrameCoordinateState &coordinateState() const;
    [[nodiscard]] const QVector<ChannelRenderSettings> &channelSettings() const;
    [[nodiscard]] const RenderedFrame &renderedFrame() const;
    [[nodiscard]] const RawFrame &currentRawFrame() const;
    [[nodiscard]] const MetadataSection &currentFrameMetadataSection() const;
    [[nodiscard]] QString pixelInfoAt(const QPoint &pixelPosition) const;

public slots:
    void setCoordinateValue(int loopIndex, int value);
    void setChannelSettings(int channelIndex, const ChannelRenderSettings &settings);
    void autoContrastChannel(int channelIndex);
    void autoContrastAllChannels();
    void reloadCurrentFrame();

signals:
    void documentChanged();
    void coordinateStateChanged();
    void channelSettingsChanged();
    void frameReady();
    void metadataChanged();
    void busyChanged(bool busy);
    void statusTextChanged(const QString &text);
    void errorOccurred(const QString &message);

private:
    struct FrameLoadResult
    {
        int requestId = 0;
        int settingsRevision = 0;
        int sequenceIndex = -1;
        bool success = false;
        RawFrame frame;
        MetadataSection metadataSection;
        RenderedFrame renderedFrame;
        QVector<ChannelRenderSettings> channelSettings;
        bool channelSettingsChanged = false;
        QString error;
    };

    void setBusy(bool busy);
    void queueFrameLoadForCurrentCoords();
    void beginFrameLoad(int sequenceIndex);
    void handleFrameLoadFinished();
    void finishQueuedFrameIfNeeded();
    int resolveSequenceIndexForCurrentState(QString *errorMessage = nullptr) const;
    void rerenderCurrentFrame(bool updateAutoContrast);
    [[nodiscard]] static const DocumentInfo &emptyDocumentInfo();

    std::unique_ptr<DocumentReader> reader_;
    FrameCoordinateState coordinateState_;
    QVector<ChannelRenderSettings> channelSettings_;
    RawFrame currentRawFrame_;
    RenderedFrame renderedFrame_;
    MetadataSection currentFrameMetadataSection_;
    int currentSequenceIndex_ = -1;
    int queuedSequenceIndex_ = -1;
    int requestCounter_ = 0;
    int channelSettingsRevision_ = 0;
    bool busy_ = false;
    QFutureWatcher<FrameLoadResult> frameWatcher_;
};
