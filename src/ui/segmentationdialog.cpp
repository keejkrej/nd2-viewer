#include "ui/segmentationdialog.h"

#include "ui/imageviewport.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <QtConcurrent>

#include <QVTKOpenGLNativeWidget.h>
#include <vtkCamera.h>
#include <vtkColorTransferFunction.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkNew.h>
#include <vtkPiecewiseFunction.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>
#include <vtkSmartVolumeMapper.h>
#include <vtkVolume.h>
#include <vtkVolumeProperty.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace
{
constexpr int kThresholdSliderSteps = 10000;

const std::array<QColor, 12> &labelPalette()
{
    static const std::array<QColor, 12> colors = {
        QColor(230, 75, 75),
        QColor(80, 180, 95),
        QColor(75, 135, 235),
        QColor(235, 170, 55),
        QColor(170, 90, 220),
        QColor(55, 190, 205),
        QColor(245, 95, 160),
        QColor(150, 190, 65),
        QColor(235, 115, 55),
        QColor(110, 110, 230),
        QColor(55, 170, 130),
        QColor(210, 210, 70)
    };
    return colors;
}

QString thresholdMethodName(SegmentationThresholdMethod method)
{
    return method == SegmentationThresholdMethod::Li ? QObject::tr("Li") : QObject::tr("Otsu");
}

QImage maskImage2D(const SegmentationMask2D &mask)
{
    if (!mask.isValid()) {
        return {};
    }

    QImage image(mask.width, mask.height, QImage::Format_ARGB32);
    const auto *source = reinterpret_cast<const unsigned char *>(mask.data.constData());
    for (int y = 0; y < mask.height; ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < mask.width; ++x) {
            const bool foreground = source[static_cast<qsizetype>(y) * mask.width + x] != 0;
            line[x] = foreground ? qRgba(255, 255, 255, 255) : qRgba(0, 0, 0, 255);
        }
    }
    return image;
}

QImage labelImage2D(const SegmentationLabels2D &labels)
{
    if (!labels.isValid()) {
        return {};
    }

    QImage image(labels.width, labels.height, QImage::Format_ARGB32);
    image.fill(Qt::black);
    const auto &colors = labelPalette();
    for (int y = 0; y < labels.height; ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < labels.width; ++x) {
            const quint32 label = labels.labels.at(y * labels.width + x);
            if (label == 0) {
                line[x] = qRgba(0, 0, 0, 255);
                continue;
            }
            const QColor color = colors.at(static_cast<size_t>((label - 1) % colors.size()));
            line[x] = qRgba(color.red(), color.green(), color.blue(), 255);
        }
    }
    return image;
}

class SegmentationImageWindow final : public QDialog
{
public:
    SegmentationImageWindow(const QImage &image, const QString &title, QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(title);
        setModal(false);
        resize(960, 720);

        auto *layout = new QVBoxLayout(this);
        auto *toolbar = new QWidget(this);
        auto *toolbarLayout = new QHBoxLayout(toolbar);
        toolbarLayout->setContentsMargins(0, 0, 0, 0);
        auto *fitButton = new QPushButton(tr("Fit"), toolbar);
        auto *actualSizeButton = new QPushButton(tr("Actual Size"), toolbar);
        toolbarLayout->addWidget(fitButton);
        toolbarLayout->addWidget(actualSizeButton);
        toolbarLayout->addStretch(1);
        layout->addWidget(toolbar);

        auto *viewport = new ImageViewport(this);
        viewport->setImage(image);
        layout->addWidget(viewport, 1);

        connect(fitButton, &QPushButton::clicked, viewport, &ImageViewport::zoomToFit);
        connect(actualSizeButton, &QPushButton::clicked, viewport, &ImageViewport::setActualSize);
        QTimer::singleShot(0, viewport, &ImageViewport::zoomToFit);
    }
};
} // namespace

class SegmentationVolumeWidget final : public QVTKOpenGLNativeWidget
{
public:
    explicit SegmentationVolumeWidget(QWidget *parent = nullptr)
        : QVTKOpenGLNativeWidget(parent)
    {
        renderWindow_ = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
        setRenderWindow(renderWindow_);
        renderer_ = vtkSmartPointer<vtkRenderer>::New();
        renderer_->SetBackground(0.02, 0.02, 0.025);
        renderWindow_->AddRenderer(renderer_);
    }

    void setMask(const SegmentationMask3D &mask)
    {
        if (!mask.isValid()) {
            clear();
            return;
        }

        vtkSmartPointer<vtkImageData> imageData = vtkSmartPointer<vtkImageData>::New();
        imageData->SetDimensions(mask.width, mask.height, mask.depth);
        imageData->SetSpacing(mask.voxelSpacing.x(), mask.voxelSpacing.y(), mask.voxelSpacing.z());
        imageData->AllocateScalars(VTK_UNSIGNED_CHAR, 1);
        std::memcpy(imageData->GetScalarPointer(), mask.data.constData(), static_cast<size_t>(mask.data.size()));
        imageData->Modified();

        buildPipeline(imageData, 1);
    }

    void setLabels(const SegmentationLabels3D &labels)
    {
        if (!labels.isValid()) {
            clear();
            return;
        }

        vtkSmartPointer<vtkImageData> imageData = vtkSmartPointer<vtkImageData>::New();
        imageData->SetDimensions(labels.width, labels.height, labels.depth);
        imageData->SetSpacing(labels.voxelSpacing.x(), labels.voxelSpacing.y(), labels.voxelSpacing.z());
        imageData->AllocateScalars(VTK_UNSIGNED_INT, 1);
        auto *target = reinterpret_cast<quint32 *>(imageData->GetScalarPointer());
        std::copy(labels.labels.cbegin(), labels.labels.cend(), target);
        imageData->Modified();

        buildPipeline(imageData, labels.componentCount);
    }

    void clear()
    {
        if (volume_) {
            renderer_->RemoveVolume(volume_);
            volume_ = nullptr;
        }
        renderWindow_->Render();
    }

private:
    void buildPipeline(vtkImageData *imageData, quint32 componentCount)
    {
        if (volume_) {
            renderer_->RemoveVolume(volume_);
        }

        auto mapper = vtkSmartPointer<vtkSmartVolumeMapper>::New();
        mapper->SetRequestedRenderModeToGPU();
        mapper->SetBlendModeToMaximumIntensity();
        mapper->SetInputData(imageData);

        auto colorTransfer = vtkSmartPointer<vtkColorTransferFunction>::New();
        auto opacityTransfer = vtkSmartPointer<vtkPiecewiseFunction>::New();
        colorTransfer->AddRGBPoint(0.0, 0.0, 0.0, 0.0);
        opacityTransfer->AddPoint(0.0, 0.0);

        const auto &colors = labelPalette();
        const quint32 maxLabel = std::max<quint32>(1, componentCount);
        for (quint32 label = 1; label <= maxLabel; ++label) {
            const QColor color = componentCount == 1
                                     ? QColor(255, 255, 255)
                                     : colors.at(static_cast<size_t>((label - 1) % colors.size()));
            colorTransfer->AddRGBPoint(label, color.redF(), color.greenF(), color.blueF());
            opacityTransfer->AddPoint(label, componentCount == 1 ? 0.45 : 0.75);
        }

        auto property = vtkSmartPointer<vtkVolumeProperty>::New();
        property->SetColor(colorTransfer);
        property->SetScalarOpacity(opacityTransfer);
        property->SetInterpolationTypeToNearest();
        property->ShadeOff();

        volume_ = vtkSmartPointer<vtkVolume>::New();
        volume_->SetMapper(mapper);
        volume_->SetProperty(property);
        renderer_->AddVolume(volume_);
        renderer_->ResetCamera();
        if (vtkCamera *camera = renderer_->GetActiveCamera()) {
            camera->Azimuth(35.0);
            camera->Elevation(20.0);
            renderer_->ResetCameraClippingRange();
        }
        renderWindow_->Render();
    }

    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow_;
    vtkSmartPointer<vtkRenderer> renderer_;
    vtkSmartPointer<vtkVolume> volume_;
};

class SegmentationVolumeWindow final : public QDialog
{
public:
    SegmentationVolumeWindow(const SegmentationLabels3D &labels, const QString &title, QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(title);
        setModal(false);
        resize(960, 720);

        auto *layout = new QVBoxLayout(this);
        auto *viewer = new SegmentationVolumeWidget(this);
        viewer->setLabels(labels);
        layout->addWidget(viewer, 1);
    }
};

SegmentationDialog::SegmentationDialog(const DocumentInfo &documentInfo,
                                       const QVector<ChannelRenderSettings> &channelSettings,
                                       const RawFrame &frame,
                                       const FrameCoordinateState &coordinates,
                                       QWidget *parent)
    : QDialog(parent),
      documentInfo_(documentInfo),
      channelSettings_(channelSettings),
      frame_(frame),
      coordinates_(coordinates),
      sourceKind_(SourceKind::Frame2D)
{
    buildUi();
}

SegmentationDialog::SegmentationDialog(const DocumentInfo &documentInfo,
                                       const QVector<ChannelRenderSettings> &channelSettings,
                                       const RawVolume &volume,
                                       QWidget *parent)
    : QDialog(parent),
      documentInfo_(documentInfo),
      channelSettings_(channelSettings),
      volume_(volume),
      coordinates_(volume.fixedCoordinates),
      sourceKind_(SourceKind::Volume3D)
{
    buildUi();
}

SegmentationDialog::~SegmentationDialog()
{
    thresholdWatcher_.waitForFinished();
    mask2DWatcher_.waitForFinished();
    mask3DWatcher_.waitForFinished();
    labels2DWatcher_.waitForFinished();
    labels3DWatcher_.waitForFinished();
}

void SegmentationDialog::reject()
{
    if (processing_) {
        statusLabel_->setText(tr("Wait for segmentation processing to finish before closing."));
        return;
    }
    QDialog::reject();
}

void SegmentationDialog::buildUi()
{
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false);
    setWindowTitle(sourceKind_ == SourceKind::Volume3D ? tr("3D Segmentation") : tr("Segmentation"));
    resize(1040, 760);

    auto *layout = new QVBoxLayout(this);

    auto *form = new QFormLayout();
    channelComboBox_ = new QComboBox(this);
    methodComboBox_ = new QComboBox(this);
    methodComboBox_->addItem(tr("Otsu"), QVariant::fromValue(static_cast<int>(SegmentationThresholdMethod::Otsu)));
    methodComboBox_->addItem(tr("Li"), QVariant::fromValue(static_cast<int>(SegmentationThresholdMethod::Li)));
    populateChannels();
    form->addRow(tr("Channel"), channelComboBox_);
    form->addRow(tr("Threshold method"), methodComboBox_);

    auto *thresholdRow = new QWidget(this);
    auto *thresholdLayout = new QHBoxLayout(thresholdRow);
    thresholdLayout->setContentsMargins(0, 0, 0, 0);
    thresholdLayout->setSpacing(8);
    thresholdSpinBox_ = new QDoubleSpinBox(thresholdRow);
    thresholdSpinBox_->setDecimals(4);
    thresholdSpinBox_->setRange(0.0, 1.0);
    thresholdSpinBox_->setValue(0.0);
    thresholdSlider_ = new QSlider(Qt::Horizontal, thresholdRow);
    thresholdSlider_->setRange(0, kThresholdSliderSteps);
    thresholdSlider_->setValue(0);
    thresholdSlider_->setEnabled(false);
    thresholdSpinBox_->setEnabled(false);
    thresholdLayout->addWidget(thresholdSpinBox_, 0);
    thresholdLayout->addWidget(thresholdSlider_, 1);
    form->addRow(tr("Threshold"), thresholdRow);
    layout->addLayout(form);

    auto *buttonRow = new QWidget(this);
    auto *buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    runButton_ = new QPushButton(tr("Run"), buttonRow);
    applyButton_ = new QPushButton(tr("Apply"), buttonRow);
    segmentButton_ = new QPushButton(tr("Segment"), buttonRow);
    buttonLayout->addWidget(runButton_);
    buttonLayout->addWidget(applyButton_);
    buttonLayout->addWidget(segmentButton_);
    buttonLayout->addStretch(1);
    layout->addWidget(buttonRow);

    previewStack_ = new QStackedWidget(this);
    preview2D_ = new ImageViewport(previewStack_);
    preview3D_ = new SegmentationVolumeWidget(previewStack_);
    previewStack_->addWidget(preview2D_);
    previewStack_->addWidget(preview3D_);
    previewStack_->setCurrentWidget(sourceKind_ == SourceKind::Frame2D ? static_cast<QWidget *>(preview2D_)
                                                                       : static_cast<QWidget *>(preview3D_));
    layout->addWidget(previewStack_, 1);

    statusLabel_ = new QLabel(sourceSummary(), this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    connect(runButton_, &QPushButton::clicked, this, &SegmentationDialog::runThreshold);
    connect(applyButton_, &QPushButton::clicked, this, &SegmentationDialog::applyThreshold);
    connect(segmentButton_, &QPushButton::clicked, this, &SegmentationDialog::segmentCurrentMask);
    connect(channelComboBox_, &QComboBox::currentIndexChanged, this, [this]() {
        hasThreshold_ = false;
        maskCurrent_ = false;
        thresholdSpinBox_->setEnabled(false);
        thresholdSlider_->setEnabled(false);
        updateButtonState();
    });
    connect(methodComboBox_, &QComboBox::currentIndexChanged, this, [this]() {
        hasThreshold_ = false;
        maskCurrent_ = false;
        thresholdSpinBox_->setEnabled(false);
        thresholdSlider_->setEnabled(false);
        updateButtonState();
    });
    connect(thresholdSpinBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this]() {
        if (hasThreshold_) {
            syncSliderFromThreshold();
            maskCurrent_ = false;
            updateButtonState();
        }
    });
    connect(thresholdSlider_, &QSlider::valueChanged, this, [this]() {
        if (hasThreshold_) {
            syncThresholdFromSlider();
            maskCurrent_ = false;
            updateButtonState();
        }
    });

    connect(&thresholdWatcher_, &QFutureWatcher<SegmentationThresholdResult>::finished,
            this, &SegmentationDialog::handleThresholdFinished);
    connect(&mask2DWatcher_, &QFutureWatcher<SegmentationMask2D>::finished,
            this, &SegmentationDialog::handleMask2DFinished);
    connect(&mask3DWatcher_, &QFutureWatcher<SegmentationMask3D>::finished,
            this, &SegmentationDialog::handleMask3DFinished);
    connect(&labels2DWatcher_, &QFutureWatcher<SegmentationLabels2D>::finished,
            this, &SegmentationDialog::handleLabels2DFinished);
    connect(&labels3DWatcher_, &QFutureWatcher<SegmentationLabels3D>::finished,
            this, &SegmentationDialog::handleLabels3DFinished);

    updateButtonState();
}

void SegmentationDialog::populateChannels()
{
    channelComboBox_->clear();
    const int rawComponentCount = sourceKind_ == SourceKind::Frame2D ? frame_.components : volume_.components;
    const int channelCount = rawComponentCount == 1 ? channelSettings_.size()
                                                    : std::min(rawComponentCount, static_cast<int>(channelSettings_.size()));
    for (int channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
        QString label;
        if (channelIndex < documentInfo_.channels.size() && !documentInfo_.channels.at(channelIndex).name.isEmpty()) {
            label = documentInfo_.channels.at(channelIndex).name;
        } else {
            label = tr("Channel %1").arg(channelIndex + 1);
        }
        channelComboBox_->addItem(label, channelIndex);
    }
}

void SegmentationDialog::runThreshold()
{
    const int channelIndex = selectedChannelIndex();
    if (channelIndex < 0) {
        QMessageBox::warning(this, tr("Segmentation"), tr("Select a valid channel before calculating a threshold."));
        return;
    }

    hasThreshold_ = false;
    maskCurrent_ = false;
    setProcessing(true, tr("Calculating %1 threshold...").arg(thresholdMethodName(selectedThresholdMethod())));
    if (sourceKind_ == SourceKind::Frame2D) {
        const RawFrame frame = frame_;
        const SegmentationThresholdMethod method = selectedThresholdMethod();
        thresholdWatcher_.setFuture(QtConcurrent::run([frame, channelIndex, method]() {
            return SegmentationProcessor::calculateThreshold2D(frame, channelIndex, method);
        }));
    } else {
        const RawVolume volume = volume_;
        const SegmentationThresholdMethod method = selectedThresholdMethod();
        thresholdWatcher_.setFuture(QtConcurrent::run([volume, channelIndex, method]() {
            return SegmentationProcessor::calculateThreshold3D(volume, channelIndex, method);
        }));
    }
}

void SegmentationDialog::applyThreshold()
{
    if (!hasThreshold_) {
        QMessageBox::information(this, tr("Segmentation"), tr("Run automatic thresholding before applying a mask."));
        return;
    }

    const int channelIndex = selectedChannelIndex();
    const double threshold = thresholdSpinBox_->value();
    setProcessing(true, tr("Applying binary threshold..."));
    if (sourceKind_ == SourceKind::Frame2D) {
        const RawFrame frame = frame_;
        mask2DWatcher_.setFuture(QtConcurrent::run([frame, channelIndex, threshold]() {
            return SegmentationProcessor::binarize2D(frame, channelIndex, threshold);
        }));
    } else {
        const RawVolume volume = volume_;
        mask3DWatcher_.setFuture(QtConcurrent::run([volume, channelIndex, threshold]() {
            return SegmentationProcessor::binarize3D(volume, channelIndex, threshold);
        }));
    }
}

void SegmentationDialog::segmentCurrentMask()
{
    if (!hasThreshold_) {
        QMessageBox::information(this, tr("Segmentation"), tr("Run automatic thresholding before segmentation."));
        return;
    }

    if (!maskCurrent_) {
        pendingAfterApply_ = PendingAfterApply::Segment;
        applyThreshold();
        return;
    }

    setProcessing(true, tr("Labeling connected components..."));
    if (sourceKind_ == SourceKind::Frame2D) {
        const SegmentationMask2D mask = mask2D_;
        labels2DWatcher_.setFuture(QtConcurrent::run([mask]() {
            return SegmentationProcessor::labelConnectedComponents(mask);
        }));
    } else {
        const SegmentationMask3D mask = mask3D_;
        labels3DWatcher_.setFuture(QtConcurrent::run([mask]() {
            return SegmentationProcessor::labelConnectedComponents(mask);
        }));
    }
}

void SegmentationDialog::handleThresholdFinished()
{
    const SegmentationThresholdResult result = thresholdWatcher_.result();
    setProcessing(false);
    if (!result.success) {
        QMessageBox::warning(this,
                             tr("Segmentation"),
                             result.errorMessage.isEmpty() ? tr("Thresholding failed.") : result.errorMessage);
        statusLabel_->setText(tr("Thresholding failed."));
        return;
    }

    setThresholdRange(result.minimumValue, result.maximumValue);
    setThresholdValue(result.threshold);
    hasThreshold_ = true;
    maskCurrent_ = false;
    thresholdSpinBox_->setEnabled(true);
    thresholdSlider_->setEnabled(true);
    statusLabel_->setText(tr("%1 threshold for %2: %3")
                              .arg(thresholdMethodName(selectedThresholdMethod()),
                                   selectedChannelLabel(),
                                   QString::number(result.threshold, 'g', 8)));
    updateButtonState();
}

void SegmentationDialog::handleMask2DFinished()
{
    mask2D_ = mask2DWatcher_.result();
    setProcessing(false);
    if (!mask2D_.isValid()) {
        QMessageBox::warning(this, tr("Segmentation"), tr("The binary 2D mask could not be prepared."));
        pendingAfterApply_ = PendingAfterApply::None;
        updateButtonState();
        return;
    }

    preview2D_->setImage(maskImage2D(mask2D_));
    preview2D_->zoomToFit();
    maskCurrent_ = true;
    statusLabel_->setText(tr("Binary mask applied at threshold %1.").arg(thresholdSpinBox_->value(), 0, 'g', 8));
    updateButtonState();

    if (pendingAfterApply_ == PendingAfterApply::Segment) {
        pendingAfterApply_ = PendingAfterApply::None;
        segmentCurrentMask();
    }
}

void SegmentationDialog::handleMask3DFinished()
{
    mask3D_ = mask3DWatcher_.result();
    setProcessing(false);
    if (!mask3D_.isValid()) {
        QMessageBox::warning(this, tr("Segmentation"), tr("The binary 3D mask could not be prepared."));
        pendingAfterApply_ = PendingAfterApply::None;
        updateButtonState();
        return;
    }

    preview3D_->setMask(mask3D_);
    maskCurrent_ = true;
    statusLabel_->setText(tr("3D binary mask applied at threshold %1.").arg(thresholdSpinBox_->value(), 0, 'g', 8));
    updateButtonState();

    if (pendingAfterApply_ == PendingAfterApply::Segment) {
        pendingAfterApply_ = PendingAfterApply::None;
        segmentCurrentMask();
    }
}

void SegmentationDialog::handleLabels2DFinished()
{
    const SegmentationLabels2D result = labels2DWatcher_.result();
    setProcessing(false);
    if (!result.isValid()) {
        QMessageBox::warning(this,
                             tr("Segmentation"),
                             result.errorMessage.isEmpty() ? tr("Connected-component labeling failed.") : result.errorMessage);
        updateButtonState();
        return;
    }

    const QImage image = labelImage2D(result);
    preview2D_->setImage(image);
    preview2D_->zoomToFit();
    auto *window = new SegmentationImageWindow(image,
                                               tr("Segmentation - %1 - %2 components")
                                                   .arg(selectedChannelLabel())
                                                   .arg(result.componentCount),
                                               this);
    window->show();
    statusLabel_->setText(tr("Segmented %1 2D components.").arg(result.componentCount));
    updateButtonState();
}

void SegmentationDialog::handleLabels3DFinished()
{
    const SegmentationLabels3D result = labels3DWatcher_.result();
    setProcessing(false);
    if (!result.isValid()) {
        QMessageBox::warning(this,
                             tr("Segmentation"),
                             result.errorMessage.isEmpty() ? tr("Connected-component labeling failed.") : result.errorMessage);
        updateButtonState();
        return;
    }

    preview3D_->setLabels(result);
    auto *window = new SegmentationVolumeWindow(result,
                                                tr("3D Segmentation - %1 - %2 components")
                                                    .arg(selectedChannelLabel())
                                                    .arg(result.componentCount),
                                                this);
    window->show();
    statusLabel_->setText(tr("Segmented %1 3D components.").arg(result.componentCount));
    updateButtonState();
}

void SegmentationDialog::setProcessing(bool processing, const QString &message)
{
    processing_ = processing;
    if (!message.isEmpty()) {
        statusLabel_->setText(message);
    }
    updateButtonState();
    emit processingStateChanged(processing_);
}

void SegmentationDialog::setThresholdRange(double minimum, double maximum)
{
    if (!std::isfinite(minimum)) {
        minimum = 0.0;
    }
    if (!std::isfinite(maximum) || maximum <= minimum) {
        maximum = minimum + 1.0;
    }

    thresholdMinimum_ = minimum;
    thresholdMaximum_ = maximum;
    thresholdSpinBox_->setRange(thresholdMinimum_, thresholdMaximum_);
    thresholdSpinBox_->setSingleStep((thresholdMaximum_ - thresholdMinimum_) / 100.0);
}

void SegmentationDialog::setThresholdValue(double value)
{
    const QSignalBlocker spinBlocker(thresholdSpinBox_);
    thresholdSpinBox_->setValue(std::clamp(value, thresholdMinimum_, thresholdMaximum_));
    syncSliderFromThreshold();
}

void SegmentationDialog::syncSliderFromThreshold()
{
    const double range = thresholdMaximum_ - thresholdMinimum_;
    const int sliderValue = range > 0.0
                                ? static_cast<int>(std::round(((thresholdSpinBox_->value() - thresholdMinimum_) / range)
                                                              * kThresholdSliderSteps))
                                : 0;
    const QSignalBlocker blocker(thresholdSlider_);
    thresholdSlider_->setValue(std::clamp(sliderValue, 0, kThresholdSliderSteps));
}

void SegmentationDialog::syncThresholdFromSlider()
{
    const double value = thresholdMinimum_
                         + (static_cast<double>(thresholdSlider_->value()) / kThresholdSliderSteps)
                               * (thresholdMaximum_ - thresholdMinimum_);
    const QSignalBlocker blocker(thresholdSpinBox_);
    thresholdSpinBox_->setValue(value);
}

void SegmentationDialog::updateButtonState()
{
    const bool hasChannel = selectedChannelIndex() >= 0;
    runButton_->setEnabled(!processing_ && hasChannel);
    applyButton_->setEnabled(!processing_ && hasChannel && hasThreshold_);
    segmentButton_->setEnabled(!processing_ && hasChannel && hasThreshold_);
    channelComboBox_->setEnabled(!processing_);
    methodComboBox_->setEnabled(!processing_);
    thresholdSpinBox_->setEnabled(!processing_ && hasThreshold_);
    thresholdSlider_->setEnabled(!processing_ && hasThreshold_);
}

int SegmentationDialog::selectedChannelIndex() const
{
    return channelComboBox_ && channelComboBox_->currentIndex() >= 0 ? channelComboBox_->currentData().toInt() : -1;
}

SegmentationThresholdMethod SegmentationDialog::selectedThresholdMethod() const
{
    const int value = methodComboBox_->currentData().toInt();
    return static_cast<SegmentationThresholdMethod>(value);
}

QString SegmentationDialog::selectedChannelLabel() const
{
    return channelComboBox_ ? channelComboBox_->currentText() : tr("Channel");
}

QString SegmentationDialog::sourceSummary() const
{
    if (sourceKind_ == SourceKind::Volume3D) {
        return tr("Source: %1 x %2 x %3 volume, %4")
            .arg(volume_.width)
            .arg(volume_.height)
            .arg(volume_.depth)
            .arg(coordinates_.values.isEmpty() ? tr("current coordinates") : tr("fixed non-Z coordinates"));
    }

    return tr("Source: %1 x %2 frame").arg(frame_.width).arg(frame_.height);
}
