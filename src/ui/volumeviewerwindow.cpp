#include "ui/volumeviewerwindow.h"

#include "ui/autocontrasttuningdialog.h"
#include "ui/channelcontrolswidget.h"
#include "ui/volumeviewport3d.h"
#include "core/volumeutils.h"

#include <QtConcurrent>

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QComboBox>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

VolumeViewerWindow::VolumeViewerWindow(const QString &path,
                                       const DocumentInfo &info,
                                       const FrameCoordinateState &coordinates,
                                       const QVector<ChannelRenderSettings> &seedChannelSettings,
                                       QWidget *parent)
    : QMainWindow(parent)
    , path_(path)
    , info_(info)
    , coordinates_(coordinates)
    , seedChannelSettings_(seedChannelSettings)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    setWindowTitle(tr("%1 - 3D View").arg(QFileInfo(path_).fileName()));
    resize(1440, 920);

    buildUi();
    startLoad();
}

VolumeViewerWindow::~VolumeViewerWindow()
{
    if (volumeWatcher_.isRunning()) {
        volumeWatcher_.waitForFinished();
    }
}

void VolumeViewerWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto *header = new QWidget(central);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    statusLabel_ = new QLabel(tr("Loading 3D volume…"), header);
    coordinatesLabel_ = new QLabel(coordinateSummary(), header);
    coordinatesLabel_->setWordWrap(true);
    renderModeComboBox_ = new QComboBox(header);
    renderModeComboBox_->addItem(tr("Balanced"), static_cast<int>(VolumeViewport3D::RenderMode::Hybrid));
    renderModeComboBox_->addItem(tr("Volume"), static_cast<int>(VolumeViewport3D::RenderMode::Smooth));
    renderModeComboBox_->addItem(tr("Detail"), static_cast<int>(VolumeViewport3D::RenderMode::Points));
    renderModeComboBox_->setCurrentIndex(0);
    renderModeComboBox_->setEnabled(false);
    renderModeComboBox_->setToolTip(tr("Switch between the blended volume look, a smoother volume-only render, or the point-detail view."));
    fitToVolumeButton_ = new QPushButton(tr("Fit To Volume"), header);
    fitToVolumeButton_->setEnabled(false);
    fitToVolumeButton_->setToolTip(tr("Keep the current angle and reframe the visible volume to fill the viewport."));
    resetViewButton_ = new QPushButton(tr("Reset View"), header);
    resetViewButton_->setEnabled(false);
    resetViewButton_->setToolTip(tr("Restore the default camera angle and refit the volume."));

    headerLayout->addWidget(statusLabel_, 1);
    headerLayout->addWidget(coordinatesLabel_, 2);
    headerLayout->addWidget(renderModeComboBox_);
    headerLayout->addWidget(fitToVolumeButton_);
    headerLayout->addWidget(resetViewButton_);

    auto *splitter = new QSplitter(Qt::Horizontal, central);
    viewport_ = new VolumeViewport3D(splitter);

    auto *sidebar = new QWidget(splitter);
    sidebar->setMinimumWidth(320);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(8);
    auto *selectionTitle = new QLabel(tr("<b>3D Select</b>"), sidebar);
    selectionStatusLabel_ = new QLabel(tr("Rotate to a view, click Add View, then draw a contour around the nucleus."), sidebar);
    selectionStatusLabel_->setWordWrap(true);
    auto *selectionOpacityRow = new QHBoxLayout();
    selectionOpacityRow->setContentsMargins(0, 0, 0, 0);
    selectionOpacityRow->setSpacing(6);
    auto *selectionOpacityLabel = new QLabel(tr("Overlay Opacity"), sidebar);
    selectionOpacitySlider_ = new QSlider(Qt::Horizontal, sidebar);
    selectionOpacitySlider_->setRange(5, 100);
    selectionOpacitySlider_->setValue(static_cast<int>(std::lround(viewport_->selectionOverlayOpacity() * 100.0)));
    selectionOpacitySlider_->setEnabled(false);
    selectionOpacitySlider_->setToolTip(tr("Adjust how strongly the 3D selection overlay is drawn."));
    selectionOpacityValueLabel_ = new QLabel(sidebar);
    selectionOpacityValueLabel_->setMinimumWidth(44);
    selectionOpacityValueLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    selectionOpacityRow->addWidget(selectionOpacityLabel);
    selectionOpacityRow->addWidget(selectionOpacitySlider_, 1);
    selectionOpacityRow->addWidget(selectionOpacityValueLabel_);
    addViewButton_ = new QPushButton(tr("Add View"), sidebar);
    addViewButton_->setEnabled(false);
    acceptContourButton_ = new QPushButton(tr("Accept Contour"), sidebar);
    acceptContourButton_->setEnabled(false);
    undoViewButton_ = new QPushButton(tr("Undo Last View"), sidebar);
    undoViewButton_->setEnabled(false);
    clearSelectionButton_ = new QPushButton(tr("Clear"), sidebar);
    clearSelectionButton_->setEnabled(false);
    auto *selectionButtonsRow = new QHBoxLayout();
    selectionButtonsRow->setContentsMargins(0, 0, 0, 0);
    selectionButtonsRow->setSpacing(6);
    selectionButtonsRow->addWidget(addViewButton_);
    selectionButtonsRow->addWidget(acceptContourButton_);
    auto *selectionButtonsRow2 = new QHBoxLayout();
    selectionButtonsRow2->setContentsMargins(0, 0, 0, 0);
    selectionButtonsRow2->setSpacing(6);
    selectionButtonsRow2->addWidget(undoViewButton_);
    selectionButtonsRow2->addWidget(clearSelectionButton_);
    auto *channelsTitle = new QLabel(tr("<b>3D Channels</b>"), sidebar);
    channelControlsWidget_ = new ChannelControlsWidget(sidebar);
    channelControlsWidget_->setEnabled(false);
    sidebarLayout->addWidget(selectionTitle);
    sidebarLayout->addWidget(selectionStatusLabel_);
    sidebarLayout->addLayout(selectionOpacityRow);
    sidebarLayout->addLayout(selectionButtonsRow);
    sidebarLayout->addLayout(selectionButtonsRow2);
    sidebarLayout->addWidget(channelsTitle);
    sidebarLayout->addWidget(channelControlsWidget_, 1);

    splitter->addWidget(viewport_);
    splitter->addWidget(sidebar);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({1050, 360});

    layout->addWidget(header);
    layout->addWidget(splitter, 1);
    setCentralWidget(central);

    connect(fitToVolumeButton_, &QPushButton::clicked, viewport_, &VolumeViewport3D::fitToVolume);
    connect(resetViewButton_, &QPushButton::clicked, viewport_, &VolumeViewport3D::resetView);
    connect(addViewButton_, &QPushButton::clicked, viewport_, &VolumeViewport3D::beginProjectionConstraint);
    connect(acceptContourButton_, &QPushButton::clicked, viewport_, &VolumeViewport3D::acceptPendingConstraint);
    connect(undoViewButton_, &QPushButton::clicked, viewport_, &VolumeViewport3D::undoLastConstraint);
    connect(clearSelectionButton_, &QPushButton::clicked, viewport_, &VolumeViewport3D::clearSelection);
    connect(selectionOpacitySlider_, &QSlider::valueChanged, this, [this](int value) {
        viewport_->setSelectionOverlayOpacity(static_cast<qreal>(value) / 100.0);
        updateSelectionOpacityLabel();
    });
    connect(renderModeComboBox_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) {
            return;
        }

        const QVariant modeValue = renderModeComboBox_->itemData(index);
        viewport_->setRenderMode(static_cast<VolumeViewport3D::RenderMode>(modeValue.toInt()));
    });
    connect(channelControlsWidget_, &ChannelControlsWidget::channelSettingsChanged, this,
            [this](int channelIndex, const ChannelRenderSettings &settings) {
                if (channelIndex < 0 || channelIndex >= channelSettings_.size()) {
                    return;
                }

                channelSettings_[channelIndex] = settings;
                viewport_->setChannelSettings(channelSettings_);
            });
    connect(channelControlsWidget_, &ChannelControlsWidget::autoContrastRequested, this, [this](int channelIndex) {
        autoContrastChannel(channelIndex);
    });
    connect(channelControlsWidget_, &ChannelControlsWidget::autoContrastAllRequested, this, [this]() {
        autoContrastAllChannels();
    });
    connect(channelControlsWidget_, &ChannelControlsWidget::autoContrastTuningRequested, this, [this](int channelIndex) {
        openAutoContrastTuningDialog(channelIndex);
    });
    connect(viewport_, &VolumeViewport3D::selectionStateChanged, this, &VolumeViewerWindow::updateSelectionUi);
    connect(&volumeWatcher_, &QFutureWatcher<VolumeLoadResult>::finished, this, &VolumeViewerWindow::handleVolumeLoadFinished);
    updateSelectionOpacityLabel();
    updateSelectionUi();
}

void VolumeViewerWindow::startLoad()
{
    volumeWatcher_.setFuture(QtConcurrent::run([path = path_, info = info_, coordinates = coordinates_, seed = seedChannelSettings_]() {
        return VolumeLoader::load(path, info, coordinates, seed);
    }));
}

void VolumeViewerWindow::handleVolumeLoadFinished()
{
    const VolumeLoadResult result = volumeWatcher_.result();
    if (!result.success || !result.volume.isValid()) {
        qWarning("3D volume load failed for %s: %s", qPrintable(path_), qPrintable(result.error));
        QMessageBox::warning(this,
                             tr("3D View"),
                             result.error.isEmpty() ? tr("The 3D volume could not be prepared.") : result.error);
        close();
        return;
    }

    volume_ = result.volume;
    analyses_ = result.analyses;
    statusLabel_->setText(tr("Loaded 3D volume %1 × %2 × %3")
                              .arg(volume_.width)
                              .arg(volume_.height)
                              .arg(volume_.depth));
    qInfo("3D volume loaded: path=%s size=%dx%dx%d channels=%d spacing=(%.9g, %.9g, %.9g)",
          qPrintable(path_),
          volume_.width,
          volume_.height,
          volume_.depth,
          volume_.components,
          volume_.voxelSpacing.x(),
          volume_.voxelSpacing.y(),
          volume_.voxelSpacing.z());
    renderModeComboBox_->setEnabled(true);
    fitToVolumeButton_->setEnabled(true);
    resetViewButton_->setEnabled(true);
    channelControlsWidget_->setEnabled(true);
    setLoadedChannelSettings(result.channelSettings);
    viewport_->setVolume(volume_, channelSettings_);
    updateSelectionUi();
    if (!viewport_->lastError().isEmpty()) {
        statusLabel_->setText(tr("3D shader error: %1").arg(viewport_->lastError()));
        QMessageBox::warning(this, tr("3D View"), viewport_->lastError());
    }
}

void VolumeViewerWindow::setLoadedChannelSettings(const QVector<ChannelRenderSettings> &settings)
{
    channelSettings_ = settings;
    channelControlsWidget_->setChannels(info_.channels, channelSettings_);
    viewport_->setChannelSettings(channelSettings_);
}

void VolumeViewerWindow::updateSelectionUi()
{
    if (!selectionStatusLabel_ || !viewport_) {
        return;
    }

    selectionStatusLabel_->setText(viewport_->selectionStatusText());
    const QString colorName = viewport_->selectionStatusWarning() ? QStringLiteral("#ffb085") : QStringLiteral("#d8d8d8");
    selectionStatusLabel_->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(colorName));

    int enabledChannelCount = 0;
    for (const ChannelRenderSettings &settings : channelSettings_) {
        enabledChannelCount += settings.enabled ? 1 : 0;
    }

    const bool volumeReady = volume_.isValid();
    const bool busy = viewport_->isSelectionBusy();
    const bool singleChannelReady = enabledChannelCount == 1;
    addViewButton_->setEnabled(volumeReady && singleChannelReady && !busy && !viewport_->hasPendingConstraint());
    acceptContourButton_->setEnabled(volumeReady && singleChannelReady && !busy && viewport_->hasPendingConstraint());
    undoViewButton_->setEnabled(volumeReady && !busy && viewport_->acceptedConstraintCount() > 0);
    clearSelectionButton_->setEnabled(volumeReady && !busy
                                      && (viewport_->acceptedConstraintCount() > 0 || viewport_->hasPendingConstraint() || viewport_->hasSelection()));
    if (selectionOpacitySlider_) {
        selectionOpacitySlider_->setEnabled(volumeReady && singleChannelReady);
    }
}

void VolumeViewerWindow::updateSelectionOpacityLabel()
{
    if (!selectionOpacityValueLabel_ || !viewport_) {
        return;
    }

    selectionOpacityValueLabel_->setText(tr("%1%").arg(static_cast<int>(std::lround(viewport_->selectionOverlayOpacity() * 100.0))));
}

void VolumeViewerWindow::autoContrastChannel(int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= channelSettings_.size() || channelIndex >= analyses_.size()) {
        return;
    }

    if (!FrameRenderer::applyAutoContrastToChannel(analyses_.at(channelIndex), channelSettings_[channelIndex])) {
        return;
    }

    channelControlsWidget_->updateSettings(channelSettings_);
    viewport_->setChannelSettings(channelSettings_);
}

void VolumeViewerWindow::autoContrastAllChannels()
{
    bool changed = false;
    for (int index = 0; index < channelSettings_.size() && index < analyses_.size(); ++index) {
        changed = FrameRenderer::applyAutoContrastToChannel(analyses_.at(index), channelSettings_[index]) || changed;
    }

    if (!changed) {
        return;
    }

    channelControlsWidget_->updateSettings(channelSettings_);
    viewport_->setChannelSettings(channelSettings_);
}

void VolumeViewerWindow::openAutoContrastTuningDialog(int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= channelSettings_.size() || channelIndex >= analyses_.size()) {
        return;
    }

    const QString channelName = (channelIndex < info_.channels.size() && !info_.channels.at(channelIndex).name.isEmpty())
                                    ? info_.channels.at(channelIndex).name
                                    : tr("Channel %1").arg(channelIndex + 1);
    const ChannelRenderSettings originalSettings = channelSettings_.at(channelIndex);

    AutoContrastTuningDialog dialog(channelName,
                                    tr("Adjust the min and max percentiles for this channel. The histogram uses a sampled snapshot of the current 3D volume."),
                                    analyses_.at(channelIndex),
                                    originalSettings,
                                    this);
    dialog.setPreviewCallback([this, channelIndex](const ChannelRenderSettings &previewSettings) {
        channelSettings_[channelIndex] = previewSettings;
        channelControlsWidget_->updateSettings(channelSettings_);
        viewport_->setChannelSettings(channelSettings_);
    });

    if (dialog.exec() == QDialog::Accepted) {
        channelSettings_[channelIndex] = dialog.currentSettings();
        channelControlsWidget_->updateSettings(channelSettings_);
        viewport_->setChannelSettings(channelSettings_);
        return;
    }

    channelSettings_[channelIndex] = originalSettings;
    channelControlsWidget_->updateSettings(channelSettings_);
    viewport_->setChannelSettings(channelSettings_);
}

QString VolumeViewerWindow::coordinateSummary() const
{
    QStringList parts;
    const int zLoopIndex = VolumeUtils::findZLoopIndex(info_);
    for (int index = 0; index < info_.loops.size() && index < coordinates_.values.size(); ++index) {
        if (index == zLoopIndex) {
            continue;
        }

        parts << QStringLiteral("%1=%2").arg(info_.loops.at(index).label).arg(coordinates_.values.at(index));
    }

    return parts.isEmpty() ? tr("3D view uses the full z-stack for the current file.") : tr("Fixed coordinates: %1").arg(parts.join(QStringLiteral(", ")));
}
