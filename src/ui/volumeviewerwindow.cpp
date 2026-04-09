#include "ui/volumeviewerwindow.h"

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
    auto *channelsTitle = new QLabel(tr("<b>3D Channels</b>"), sidebar);
    channelControlsWidget_ = new ChannelControlsWidget(sidebar);
    channelControlsWidget_->setAutoContrastControlsVisible(false);
    channelControlsWidget_->setEnabled(false);
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
    connect(&volumeWatcher_, &QFutureWatcher<VolumeLoadResult>::finished, this, &VolumeViewerWindow::handleVolumeLoadFinished);
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
