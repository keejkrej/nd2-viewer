#pragma once

#include "core/documenttypes.h"
#include "core/framerenderer.h"

#include <QDialog>

#include <functional>

class QLabel;
class QDoubleSpinBox;

class HistogramWidget;

class AutoContrastTuningDialog : public QDialog
{
    Q_OBJECT

public:
    AutoContrastTuningDialog(const QString &channelName,
                             const QString &description,
                             const ChannelAutoContrastAnalysis &analysis,
                             const ChannelRenderSettings &initialSettings,
                             QWidget *parent = nullptr);

    void setPreviewCallback(std::function<void(const ChannelRenderSettings &)> callback);
    [[nodiscard]] ChannelRenderSettings currentSettings() const;

private:
    void setPercentiles(double lowPercentile, double highPercentile, bool emitPreview);
    void sanitizePercentiles(double &lowPercentile, double &highPercentile) const;

    ChannelAutoContrastAnalysis analysis_;
    ChannelRenderSettings settings_;
    HistogramWidget *histogramWidget_ = nullptr;
    QDoubleSpinBox *minPercentileSpinBox_ = nullptr;
    QDoubleSpinBox *maxPercentileSpinBox_ = nullptr;
    QLabel *thresholdLabel_ = nullptr;
    std::function<void(const ChannelRenderSettings &)> previewCallback_;
};
