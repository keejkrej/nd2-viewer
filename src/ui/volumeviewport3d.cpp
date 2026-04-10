#include "ui/volumeviewport3d.h"

#include "ui/volumeviewport3d_gl.h"

#if defined(Q_OS_MACOS) && defined(ND2VIEWER_HAS_VTK_3D)
#include "ui/volumeviewport3d_vtk.h"
#endif

#include <QVBoxLayout>

VolumeViewport3D::VolumeViewport3D(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

#if defined(Q_OS_MACOS) && defined(ND2VIEWER_HAS_VTK_3D)
    backend_ = std::make_unique<VolumeViewport3DBackendVtk>(this);
#else
    backend_ = std::make_unique<VolumeViewport3DBackendGl>(this);
#endif

    layout->addWidget(backend_->widget());
}

VolumeViewport3D::~VolumeViewport3D() = default;

void VolumeViewport3D::setVolume(const RawVolume &volume, const QVector<ChannelRenderSettings> &channelSettings)
{
    backend_->setVolume(volume, channelSettings);
}

void VolumeViewport3D::setChannelSettings(const QVector<ChannelRenderSettings> &channelSettings)
{
    backend_->setChannelSettings(channelSettings);
}

void VolumeViewport3D::resetView()
{
    backend_->resetView();
}

void VolumeViewport3D::fitToVolume()
{
    backend_->fitToVolume();
}

VolumeViewport3DCameraState VolumeViewport3D::cameraState() const
{
    return backend_->cameraState();
}

void VolumeViewport3D::setCameraState(const VolumeViewport3DCameraState &state)
{
    backend_->setCameraState(state);
}

QImage VolumeViewport3D::captureImage() const
{
    return backend_->widget()->grab().toImage().convertToFormat(QImage::Format_ARGB32);
}

QString VolumeViewport3D::lastError() const
{
    return backend_->lastError();
}

QString VolumeViewport3D::renderSummary() const
{
    return backend_->renderSummary();
}
