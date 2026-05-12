#pragma once

#include "core/documenttypes.h"
#include "core/segmentation.h"

#include <QDialog>
#include <QFutureWatcher>

class ImageViewport;
class QLabel;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QPushButton;
class QSlider;
class QStackedWidget;
class SegmentationVolumeWidget;

class SegmentationDialog final : public QDialog
{
    Q_OBJECT

public:
    SegmentationDialog(const DocumentInfo &documentInfo,
                       const QVector<ChannelRenderSettings> &channelSettings,
                       const RawFrame &frame,
                       const FrameCoordinateState &coordinates,
                       QWidget *parent = nullptr);
    SegmentationDialog(const DocumentInfo &documentInfo,
                       const QVector<ChannelRenderSettings> &channelSettings,
                       const RawVolume &volume,
                       QWidget *parent = nullptr);
    ~SegmentationDialog() override;

signals:
    void processingStateChanged(bool processing);

protected:
    void reject() override;

private:
    enum class SourceKind
    {
        Frame2D,
        Volume3D
    };

    enum class PendingAfterApply
    {
        None,
        Segment
    };

    void buildUi();
    void populateChannels();
    void runThreshold();
    void applyThreshold();
    void segmentCurrentMask();
    void handleThresholdFinished();
    void handleMask2DFinished();
    void handleMask3DFinished();
    void handleLabels2DFinished();
    void handleLabels3DFinished();
    void setProcessing(bool processing, const QString &message = QString());
    void setThresholdRange(double minimum, double maximum);
    void setThresholdValue(double value);
    void syncSliderFromThreshold();
    void syncThresholdFromSlider();
    void updateButtonState();
    [[nodiscard]] int selectedChannelIndex() const;
    [[nodiscard]] SegmentationThresholdMethod selectedThresholdMethod() const;
    [[nodiscard]] QString selectedChannelLabel() const;
    [[nodiscard]] QString sourceSummary() const;

    DocumentInfo documentInfo_;
    QVector<ChannelRenderSettings> channelSettings_;
    RawFrame frame_;
    RawVolume volume_;
    FrameCoordinateState coordinates_;
    SourceKind sourceKind_ = SourceKind::Frame2D;
    bool hasThreshold_ = false;
    bool maskCurrent_ = false;
    bool processing_ = false;
    double thresholdMinimum_ = 0.0;
    double thresholdMaximum_ = 1.0;
    PendingAfterApply pendingAfterApply_ = PendingAfterApply::None;

    QComboBox *channelComboBox_ = nullptr;
    QComboBox *methodComboBox_ = nullptr;
    QDoubleSpinBox *thresholdSpinBox_ = nullptr;
    QSlider *thresholdSlider_ = nullptr;
    QPushButton *runButton_ = nullptr;
    QPushButton *applyButton_ = nullptr;
    QPushButton *segmentButton_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QStackedWidget *previewStack_ = nullptr;
    ImageViewport *preview2D_ = nullptr;
    SegmentationVolumeWidget *preview3D_ = nullptr;

    SegmentationMask2D mask2D_;
    SegmentationMask3D mask3D_;
    QFutureWatcher<SegmentationThresholdResult> thresholdWatcher_;
    QFutureWatcher<SegmentationMask2D> mask2DWatcher_;
    QFutureWatcher<SegmentationMask3D> mask3DWatcher_;
    QFutureWatcher<SegmentationLabels2D> labels2DWatcher_;
    QFutureWatcher<SegmentationLabels3D> labels3DWatcher_;
};
