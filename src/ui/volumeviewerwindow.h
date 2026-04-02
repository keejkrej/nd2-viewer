#pragma once

#include "core/documenttypes.h"
#include "core/volumeloader.h"

#include <QFutureWatcher>
#include <QMainWindow>

class ChannelControlsWidget;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class VolumeViewport3D;

class VolumeViewerWindow : public QMainWindow
{
    Q_OBJECT

public:
    VolumeViewerWindow(const QString &path,
                       const DocumentInfo &info,
                       const FrameCoordinateState &coordinates,
                       const QVector<ChannelRenderSettings> &seedChannelSettings,
                       QWidget *parent = nullptr);
    ~VolumeViewerWindow() override;

private:
    void buildUi();
    void startLoad();
    void handleVolumeLoadFinished();
    void setLoadedChannelSettings(const QVector<ChannelRenderSettings> &settings);
    void updateSelectionUi();
    void updateSelectionOpacityLabel();
    void autoContrastChannel(int channelIndex);
    void autoContrastAllChannels();
    void openAutoContrastTuningDialog(int channelIndex);
    [[nodiscard]] QString coordinateSummary() const;

    QString path_;
    DocumentInfo info_;
    FrameCoordinateState coordinates_;
    QVector<ChannelRenderSettings> seedChannelSettings_;
    RawVolume volume_;
    QVector<ChannelRenderSettings> channelSettings_;
    QVector<ChannelAutoContrastAnalysis> analyses_;
    VolumeViewport3D *viewport_ = nullptr;
    ChannelControlsWidget *channelControlsWidget_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *coordinatesLabel_ = nullptr;
    QLabel *selectionStatusLabel_ = nullptr;
    QLabel *selectionOpacityValueLabel_ = nullptr;
    QComboBox *renderModeComboBox_ = nullptr;
    QPushButton *fitToVolumeButton_ = nullptr;
    QPushButton *resetViewButton_ = nullptr;
    QPushButton *addViewButton_ = nullptr;
    QPushButton *acceptContourButton_ = nullptr;
    QPushButton *undoViewButton_ = nullptr;
    QPushButton *clearSelectionButton_ = nullptr;
    QSlider *selectionOpacitySlider_ = nullptr;
    QFutureWatcher<VolumeLoadResult> volumeWatcher_;
};
