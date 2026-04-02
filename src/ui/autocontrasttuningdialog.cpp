#include "ui/autocontrasttuningdialog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

class HistogramWidget : public QWidget
{
public:
    explicit HistogramWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(220);
        setMouseTracking(true);
    }

    void setHistogram(const QVector<quint64> &bins, double minimumValue, double maximumValue)
    {
        bins_ = bins;
        minimumValue_ = minimumValue;
        maximumValue_ = maximumValue;
        update();
    }

    void setLevels(double lowValue, double highValue)
    {
        lowValue_ = lowValue;
        highValue_ = highValue;
        update();
    }

    void setLevelsChangedCallback(std::function<void(double, double, bool)> callback)
    {
        levelsChangedCallback_ = std::move(callback);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), palette().window());

        const QRectF plot = plotRect();
        painter.fillRect(plot, palette().base());
        painter.setPen(palette().mid().color());
        painter.drawRect(plot);

        if (bins_.isEmpty()) {
            painter.setPen(palette().text().color());
            painter.drawText(plot, Qt::AlignCenter, tr("Histogram unavailable"));
            return;
        }

        const quint64 maxCount = qMax<quint64>(1, *std::max_element(bins_.cbegin(), bins_.cend()));
        const double barWidth = plot.width() / bins_.size();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(90, 150, 210));
        for (int index = 0; index < bins_.size(); ++index) {
            const double barHeight = plot.height() * (static_cast<double>(bins_.at(index)) / maxCount);
            const QRectF bar(plot.left() + (index * barWidth),
                             plot.bottom() - barHeight,
                             qMax(1.0, barWidth - 1.0),
                             barHeight);
            painter.drawRect(bar);
        }

        painter.setPen(QPen(QColor(230, 140, 40), 2.0));
        painter.drawLine(QPointF(valueToX(lowValue_), plot.top()), QPointF(valueToX(lowValue_), plot.bottom()));
        painter.setPen(QPen(QColor(40, 180, 180), 2.0));
        painter.drawLine(QPointF(valueToX(highValue_), plot.top()), QPointF(valueToX(highValue_), plot.bottom()));

        painter.setPen(palette().text().color());
        painter.drawText(QRectF(plot.left(), plot.bottom() + 6.0, plot.width() / 2.0, 18.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QString::number(minimumValue_, 'g', 6));
        painter.drawText(QRectF(plot.center().x(), plot.bottom() + 6.0, plot.width() / 2.0, 18.0),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(maximumValue_, 'g', 6));
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || bins_.isEmpty()) {
            QWidget::mousePressEvent(event);
            return;
        }

        dragHandle_ = pickHandle(event->position().x());
        updateDraggedLevel(event->position().x(), false);
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (dragHandle_ == DragHandle::None) {
            QWidget::mouseMoveEvent(event);
            return;
        }

        updateDraggedLevel(event->position().x(), false);
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && dragHandle_ != DragHandle::None) {
            updateDraggedLevel(event->position().x(), true);
            dragHandle_ = DragHandle::None;
            event->accept();
            return;
        }

        QWidget::mouseReleaseEvent(event);
    }

private:
    enum class DragHandle
    {
        None,
        Low,
        High
    };

    [[nodiscard]] QRectF plotRect() const
    {
        const QRect area = contentsRect();
        return QRectF(area.adjusted(12, 12, -12, -28));
    }

    [[nodiscard]] double valueToX(double value) const
    {
        const QRectF plot = plotRect();
        if (plot.width() <= 0.0) {
            return plot.left();
        }

        if (qFuzzyCompare(minimumValue_ + 1.0, maximumValue_ + 1.0)) {
            return plot.center().x();
        }

        const double normalized = std::clamp((value - minimumValue_) / (maximumValue_ - minimumValue_), 0.0, 1.0);
        return plot.left() + (normalized * plot.width());
    }

    [[nodiscard]] double xToValue(double x) const
    {
        const QRectF plot = plotRect();
        if (plot.width() <= 0.0 || qFuzzyCompare(minimumValue_ + 1.0, maximumValue_ + 1.0)) {
            return minimumValue_;
        }

        const double normalized = std::clamp((x - plot.left()) / plot.width(), 0.0, 1.0);
        return minimumValue_ + (normalized * (maximumValue_ - minimumValue_));
    }

    [[nodiscard]] DragHandle pickHandle(double x) const
    {
        const double lowDistance = std::abs(x - valueToX(lowValue_));
        const double highDistance = std::abs(x - valueToX(highValue_));
        return lowDistance <= highDistance ? DragHandle::Low : DragHandle::High;
    }

    void updateDraggedLevel(double x, bool commitPreview)
    {
        const double value = xToValue(x);
        if (dragHandle_ == DragHandle::Low) {
            lowValue_ = std::min(value, highValue_);
        } else if (dragHandle_ == DragHandle::High) {
            highValue_ = std::max(value, lowValue_);
        }

        update();
        if (levelsChangedCallback_) {
            levelsChangedCallback_(lowValue_, highValue_, commitPreview);
        }
    }

    QVector<quint64> bins_;
    double minimumValue_ = 0.0;
    double maximumValue_ = 1.0;
    double lowValue_ = 0.0;
    double highValue_ = 1.0;
    DragHandle dragHandle_ = DragHandle::None;
    std::function<void(double, double, bool)> levelsChangedCallback_;
};

AutoContrastTuningDialog::AutoContrastTuningDialog(const QString &channelName,
                                                   const QString &description,
                                                   const ChannelAutoContrastAnalysis &analysis,
                                                   const ChannelRenderSettings &initialSettings,
                                                   QWidget *parent)
    : QDialog(parent)
    , analysis_(analysis)
    , settings_(initialSettings)
{
    setWindowTitle(tr("Tune Live Auto - %1").arg(channelName));
    setModal(true);
    resize(620, 0);

    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(description, this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    histogramWidget_ = new HistogramWidget(this);
    histogramWidget_->setHistogram(analysis_.histogramBins, analysis_.minimumValue, analysis_.maximumValue);
    layout->addWidget(histogramWidget_);

    auto *inputsRow = new QWidget(this);
    auto *inputsLayout = new QHBoxLayout(inputsRow);
    inputsLayout->setContentsMargins(0, 0, 0, 0);
    inputsLayout->setSpacing(8);

    auto *minLabel = new QLabel(tr("Min percentile"), inputsRow);
    minPercentileSpinBox_ = new QDoubleSpinBox(inputsRow);
    minPercentileSpinBox_->setDecimals(3);
    minPercentileSpinBox_->setRange(0.0, 100.0);
    minPercentileSpinBox_->setSingleStep(0.1);

    auto *maxLabel = new QLabel(tr("Max percentile"), inputsRow);
    maxPercentileSpinBox_ = new QDoubleSpinBox(inputsRow);
    maxPercentileSpinBox_->setDecimals(3);
    maxPercentileSpinBox_->setRange(0.0, 100.0);
    maxPercentileSpinBox_->setSingleStep(0.1);

    thresholdLabel_ = new QLabel(this);

    inputsLayout->addWidget(minLabel);
    inputsLayout->addWidget(minPercentileSpinBox_, 1);
    inputsLayout->addWidget(maxLabel);
    inputsLayout->addWidget(maxPercentileSpinBox_, 1);
    layout->addWidget(inputsRow);
    layout->addWidget(thresholdLabel_);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    connect(minPercentileSpinBox_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        setPercentiles(value, settings_.highPercentile, true);
    });
    connect(maxPercentileSpinBox_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        setPercentiles(settings_.lowPercentile, value, true);
    });

    histogramWidget_->setLevelsChangedCallback([this](double lowValue, double highValue, bool commitPreview) {
        const double lowPercentile = FrameRenderer::valueToPercentile(analysis_, lowValue);
        const double highPercentile = FrameRenderer::valueToPercentile(analysis_, highValue);
        setPercentiles(lowPercentile, highPercentile, commitPreview);
    });

    setPercentiles(settings_.lowPercentile, settings_.highPercentile, false);
}

void AutoContrastTuningDialog::setPreviewCallback(std::function<void(const ChannelRenderSettings &)> callback)
{
    previewCallback_ = std::move(callback);
}

ChannelRenderSettings AutoContrastTuningDialog::currentSettings() const
{
    return settings_;
}

void AutoContrastTuningDialog::setPercentiles(double lowPercentile, double highPercentile, bool emitPreview)
{
    sanitizePercentiles(lowPercentile, highPercentile);
    settings_.lowPercentile = lowPercentile;
    settings_.highPercentile = highPercentile;

    const QSignalBlocker minBlocker(minPercentileSpinBox_);
    const QSignalBlocker maxBlocker(maxPercentileSpinBox_);
    minPercentileSpinBox_->setValue(settings_.lowPercentile);
    maxPercentileSpinBox_->setValue(settings_.highPercentile);

    FrameRenderer::applyAutoContrastToChannel(analysis_, settings_);
    histogramWidget_->setLevels(settings_.low, settings_.high);
    thresholdLabel_->setText(tr("Current thresholds: low=%1   high=%2")
                                 .arg(QString::number(settings_.low, 'g', 6),
                                      QString::number(settings_.high, 'g', 6)));

    if (emitPreview && previewCallback_) {
        previewCallback_(settings_);
    }
}

void AutoContrastTuningDialog::sanitizePercentiles(double &lowPercentile, double &highPercentile) const
{
    constexpr double minimumGap = 0.001;

    lowPercentile = std::clamp(lowPercentile, 0.0, 100.0);
    highPercentile = std::clamp(highPercentile, 0.0, 100.0);

    if (highPercentile - lowPercentile >= minimumGap) {
        return;
    }

    if (lowPercentile >= 100.0) {
        lowPercentile = 100.0 - minimumGap;
        highPercentile = 100.0;
        return;
    }

    highPercentile = std::min(100.0, lowPercentile + minimumGap);
    if (highPercentile - lowPercentile < minimumGap) {
        lowPercentile = std::max(0.0, highPercentile - minimumGap);
    }
}
