#include "ui/volumeviewport3d_vtk.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QtMath>

#include <vtkCamera.h>
#include <vtkColorTransferFunction.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkPiecewiseFunction.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkSmartVolumeMapper.h>
#include <vtkVolume.h>
#include <vtkVolumeProperty.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
constexpr int kFallbackMaxTextureSize3D = 2048;
constexpr double kDefaultYawDegrees = -35.0;
constexpr double kDefaultPitchDegrees = 25.0;

QVector3D cameraOffsetForYawPitch(double yawDegrees, double pitchDegrees)
{
    const double yawRadians = qDegreesToRadians(yawDegrees);
    const double pitchRadians = qDegreesToRadians(pitchDegrees);
    return QVector3D(static_cast<float>(std::cos(pitchRadians) * std::sin(yawRadians)),
                     static_cast<float>(std::sin(pitchRadians)),
                     static_cast<float>(std::cos(pitchRadians) * std::cos(yawRadians)));
}

QString formatSummary(const RawVolume &source, int renderWidth, int renderHeight, int renderDepth,
                      int factorX, int factorY, int factorZ)
{
    const QString original = QStringLiteral("%1 × %2 × %3").arg(source.width).arg(source.height).arg(source.depth);
    if (factorX == 1 && factorY == 1 && factorZ == 1) {
        return original;
    }

    return QStringLiteral("%1 (rendering %2 × %3 × %4)")
        .arg(original)
        .arg(renderWidth)
        .arg(renderHeight)
        .arg(renderDepth);
}
}

VolumeViewport3DBackendVtk::VolumeViewport3DBackendVtk(QWidget *parent)
    : QVTKOpenGLNativeWidget(parent)
{
    setMinimumSize(500, 400);
    setFormat(QVTKOpenGLNativeWidget::defaultFormat());

    renderWindow_ = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    renderWindow_->SetMultiSamples(0);
    setRenderWindow(renderWindow_);

    renderer_ = vtkSmartPointer<vtkRenderer>::New();
    renderer_->SetBackground(0.02, 0.02, 0.03);
    renderWindow_->AddRenderer(renderer_);

    interactorStyle_ = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
    if (renderWindow_->GetInteractor()) {
        renderWindow_->GetInteractor()->SetInteractorStyle(interactorStyle_);
        renderWindow_->GetInteractor()->SetDesiredUpdateRate(30.0);
        renderWindow_->GetInteractor()->SetStillUpdateRate(0.01);
    }
    renderWindow_->SetDesiredUpdateRate(30.0);
}

QWidget *VolumeViewport3DBackendVtk::widget()
{
    return this;
}

void VolumeViewport3DBackendVtk::setVolume(const RawVolume &volume,
                                           const QVector<ChannelRenderSettings> &channelSettings)
{
    sourceVolume_ = volume;
    channelSettings_ = channelSettings;
    lastError_.clear();
    renderSummary_.clear();

    for (ChannelPipeline &pipeline : channels_) {
        if (pipeline.addedToRenderer && pipeline.volume) {
            renderer_->RemoveVolume(pipeline.volume);
        }
    }
    channels_.clear();

    if (!sourceVolume_.isValid()) {
        renderNow();
        return;
    }

    const int channelCount = effectiveChannelCount();
    const PreparedVolumeData prepared = prepareRenderData(sourceVolume_, channelCount);
    if (!prepared.isValid()) {
        if (lastError_.isEmpty()) {
            lastError_ = tr("The VTK 3D volume could not be prepared.");
        }
        renderSummary_ = QStringLiteral("%1 × %2 × %3").arg(sourceVolume_.width).arg(sourceVolume_.height).arg(sourceVolume_.depth);
        renderNow();
        return;
    }

    rebuildPipelines(prepared);
    renderSummary_ = prepared.summary;
    syncChannelVisuals();
    resetView();
}

void VolumeViewport3DBackendVtk::setChannelSettings(const QVector<ChannelRenderSettings> &channelSettings)
{
    channelSettings_ = channelSettings;
    syncChannelVisuals();
    renderNow();
}

void VolumeViewport3DBackendVtk::resetView()
{
    double bounds[6];
    if (!computeVisibleBounds(bounds)) {
        return;
    }

    renderer_->ResetCamera(bounds);
    vtkCamera *camera = renderer_->GetActiveCamera();
    double fittedPosition[3];
    double fittedFocalPoint[3];
    camera->GetPosition(fittedPosition);
    camera->GetFocalPoint(fittedFocalPoint);
    const double fittedDistance = std::sqrt(vtkMath::Distance2BetweenPoints(fittedPosition, fittedFocalPoint));

    const double center[3] = {
        0.5 * (bounds[0] + bounds[1]),
        0.5 * (bounds[2] + bounds[3]),
        0.5 * (bounds[4] + bounds[5])
    };
    const QVector3D offset = cameraOffsetForYawPitch(kDefaultYawDegrees, kDefaultPitchDegrees);
    camera->SetFocalPoint(center);
    camera->SetPosition(center[0] + (offset.x() * fittedDistance),
                        center[1] + (offset.y() * fittedDistance),
                        center[2] + (offset.z() * fittedDistance));
    camera->SetViewUp(0.0, 1.0, 0.0);
    camera->OrthogonalizeViewUp();
    renderer_->ResetCameraClippingRange(bounds);
    renderNow();
}

void VolumeViewport3DBackendVtk::fitToVolume()
{
    double bounds[6];
    if (!computeVisibleBounds(bounds)) {
        return;
    }

    vtkCamera *camera = renderer_->GetActiveCamera();
    double position[3];
    double focalPoint[3];
    double viewUp[3];
    camera->GetPosition(position);
    camera->GetFocalPoint(focalPoint);
    camera->GetViewUp(viewUp);

    double viewDirection[3] = {
        focalPoint[0] - position[0],
        focalPoint[1] - position[1],
        focalPoint[2] - position[2]
    };
    if (vtkMath::Normalize(viewDirection) == 0.0) {
        viewDirection[0] = 0.0;
        viewDirection[1] = 0.0;
        viewDirection[2] = -1.0;
    }

    renderer_->ResetCamera(bounds);

    double fittedPosition[3];
    double fittedFocalPoint[3];
    camera->GetPosition(fittedPosition);
    camera->GetFocalPoint(fittedFocalPoint);
    const double fittedDistance = std::sqrt(vtkMath::Distance2BetweenPoints(fittedPosition, fittedFocalPoint));

    const double center[3] = {
        0.5 * (bounds[0] + bounds[1]),
        0.5 * (bounds[2] + bounds[3]),
        0.5 * (bounds[4] + bounds[5])
    };
    const double newPosition[3] = {
        center[0] - viewDirection[0] * fittedDistance,
        center[1] - viewDirection[1] * fittedDistance,
        center[2] - viewDirection[2] * fittedDistance
    };

    camera->SetFocalPoint(center);
    camera->SetPosition(newPosition);
    camera->SetViewUp(viewUp);
    camera->OrthogonalizeViewUp();
    renderer_->ResetCameraClippingRange(bounds);
    renderNow();
}

VolumeViewport3DCameraState VolumeViewport3DBackendVtk::cameraState() const
{
    VolumeViewport3DCameraState state;
    if (!renderer_ || !renderer_->GetActiveCamera()) {
        return state;
    }

    vtkCamera *camera = renderer_->GetActiveCamera();
    double position[3];
    double focalPoint[3];
    double viewUp[3];
    camera->GetPosition(position);
    camera->GetFocalPoint(focalPoint);
    camera->GetViewUp(viewUp);

    state.valid = true;
    state.position = QVector3D(position[0], position[1], position[2]);
    state.focalPoint = QVector3D(focalPoint[0], focalPoint[1], focalPoint[2]);
    state.viewUp = QVector3D(viewUp[0], viewUp[1], viewUp[2]);
    return state;
}

void VolumeViewport3DBackendVtk::setCameraState(const VolumeViewport3DCameraState &state)
{
    if (!state.valid || !renderer_ || !renderer_->GetActiveCamera()) {
        return;
    }

    vtkCamera *camera = renderer_->GetActiveCamera();
    camera->SetPosition(state.position.x(), state.position.y(), state.position.z());
    camera->SetFocalPoint(state.focalPoint.x(), state.focalPoint.y(), state.focalPoint.z());
    camera->SetViewUp(state.viewUp.x(), state.viewUp.y(), state.viewUp.z());
    camera->OrthogonalizeViewUp();

    double bounds[6];
    if (computeVisibleBounds(bounds)) {
        renderer_->ResetCameraClippingRange(bounds);
    }
    renderNow();
}

QString VolumeViewport3DBackendVtk::lastError() const
{
    return lastError_;
}

QString VolumeViewport3DBackendVtk::renderSummary() const
{
    return renderSummary_;
}

int VolumeViewport3DBackendVtk::effectiveChannelCount() const
{
    return std::min(sourceVolume_.components, static_cast<int>(channelSettings_.size()));
}

int VolumeViewport3DBackendVtk::queryMaxTextureSize3D()
{
    if (maxTextureSize3D_ > 0) {
        return maxTextureSize3D_;
    }

    int queried = 0;
    if (context()) {
        makeCurrent();
        if (QOpenGLFunctions *functions = context()->functions()) {
            const char *renderer = reinterpret_cast<const char *>(functions->glGetString(GL_RENDERER));
            const char *vendor = reinterpret_cast<const char *>(functions->glGetString(GL_VENDOR));
            const char *version = reinterpret_cast<const char *>(functions->glGetString(GL_VERSION));
            qInfo("VTK 3D OpenGL context: renderer=%s vendor=%s version=%s",
                  renderer ? renderer : "unknown",
                  vendor ? vendor : "unknown",
                  version ? version : "unknown");
            functions->glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &queried);
        }
        doneCurrent();
    }

    maxTextureSize3D_ = queried > 0 ? queried : kFallbackMaxTextureSize3D;
    qInfo("VTK 3D texture limit=%d", maxTextureSize3D_);
    return maxTextureSize3D_;
}

VolumeViewport3DBackendVtk::PreparedVolumeData VolumeViewport3DBackendVtk::prepareRenderData(const RawVolume &volume,
                                                                                              int channelCount)
{
    PreparedVolumeData prepared;
    if (!volume.isValid() || channelCount <= 0) {
        return prepared;
    }

    const int limit = std::max(1, queryMaxTextureSize3D());
    prepared.factorX = std::max(1, (volume.width + limit - 1) / limit);
    prepared.factorY = std::max(1, (volume.height + limit - 1) / limit);
    prepared.factorZ = std::max(1, (volume.depth + limit - 1) / limit);
    prepared.width = (volume.width + prepared.factorX - 1) / prepared.factorX;
    prepared.height = (volume.height + prepared.factorY - 1) / prepared.factorY;
    prepared.depth = (volume.depth + prepared.factorZ - 1) / prepared.factorZ;
    prepared.spacing = {
        volume.voxelSpacing.x() * prepared.factorX,
        volume.voxelSpacing.y() * prepared.factorY,
        volume.voxelSpacing.z() * prepared.factorZ
    };

    const bool uint8 = volume.bytesPerComponent() == 1 && volume.pixelDataType.compare(QStringLiteral("unsigned"), Qt::CaseInsensitive) == 0;
    const bool uint16 = volume.bytesPerComponent() == 2 && volume.pixelDataType.compare(QStringLiteral("unsigned"), Qt::CaseInsensitive) == 0;
    if (uint8) {
        prepared.storage = ScalarStorage::UInt8;
    } else if (uint16) {
        prepared.storage = ScalarStorage::UInt16;
    } else {
        prepared.storage = ScalarStorage::Float32;
    }

    const int dstBytesPerComponent = storageBytesPerComponent(prepared.storage);
    const qsizetype dstVoxelCount = static_cast<qsizetype>(prepared.width) * prepared.height * prepared.depth;
    prepared.channelData.resize(channelCount);
    for (int channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
        QByteArray &dst = prepared.channelData[channelIndex];
        dst.resize(dstVoxelCount * dstBytesPerComponent);

        for (int z = 0; z < prepared.depth; ++z) {
            const int srcZ = std::min(z * prepared.factorZ, volume.depth - 1);
            for (int y = 0; y < prepared.height; ++y) {
                // Qt image rows are top-to-bottom, while VTK's world Y axis points up.
                // Mirror the source rows here so the 3D volume aligns with the 2D view.
                const int srcY = volume.height - 1 - std::min(y * prepared.factorY, volume.height - 1);
                for (int x = 0; x < prepared.width; ++x) {
                    const int srcX = std::min(x * prepared.factorX, volume.width - 1);
                    const qsizetype srcVoxelIndex = (static_cast<qsizetype>(srcZ) * volume.width * volume.height)
                                                    + (static_cast<qsizetype>(srcY) * volume.width)
                                                    + srcX;
                    const qsizetype dstVoxelIndex = (static_cast<qsizetype>(z) * prepared.width * prepared.height)
                                                    + (static_cast<qsizetype>(y) * prepared.width)
                                                    + x;

                    char *dstPtr = dst.data() + dstVoxelIndex * dstBytesPerComponent;
                    if (prepared.storage == ScalarStorage::UInt8) {
                        dstPtr[0] = volume.channelData.at(channelIndex).constData()[srcVoxelIndex];
                    } else if (prepared.storage == ScalarStorage::UInt16) {
                        std::memcpy(dstPtr,
                                    volume.channelData.at(channelIndex).constData() + srcVoxelIndex * 2,
                                    sizeof(quint16));
                    } else {
                        const float value = static_cast<float>(sourceVoxelAsDouble(volume, channelIndex, srcVoxelIndex));
                        std::memcpy(dstPtr, &value, sizeof(float));
                    }
                }
            }
        }
    }

    prepared.summary = formatSummary(volume,
                                     prepared.width,
                                     prepared.height,
                                     prepared.depth,
                                     prepared.factorX,
                                     prepared.factorY,
                                     prepared.factorZ);
    return prepared;
}

double VolumeViewport3DBackendVtk::sourceVoxelAsDouble(const RawVolume &volume, int channelIndex, qsizetype voxelIndex) const
{
    const char *base = volume.channelData.at(channelIndex).constData() + (voxelIndex * volume.bytesPerComponent());
    switch (volume.bytesPerComponent()) {
    case 1:
        return static_cast<unsigned char>(base[0]);
    case 2: {
        quint16 value = 0;
        std::memcpy(&value, base, sizeof(value));
        return value;
    }
    case 4:
        if (volume.pixelDataType.compare(QStringLiteral("float"), Qt::CaseInsensitive) == 0) {
            float value = 0.0f;
            std::memcpy(&value, base, sizeof(value));
            return value;
        } else {
            quint32 value = 0;
            std::memcpy(&value, base, sizeof(value));
            return static_cast<double>(value);
        }
    default:
        return 0.0;
    }
}

int VolumeViewport3DBackendVtk::storageBytesPerComponent(ScalarStorage storage) const
{
    switch (storage) {
    case ScalarStorage::UInt8:
        return 1;
    case ScalarStorage::UInt16:
        return 2;
    case ScalarStorage::Float32:
        return 4;
    }
    return 4;
}

int VolumeViewport3DBackendVtk::vtkScalarType(ScalarStorage storage) const
{
    switch (storage) {
    case ScalarStorage::UInt8:
        return VTK_UNSIGNED_CHAR;
    case ScalarStorage::UInt16:
        return VTK_UNSIGNED_SHORT;
    case ScalarStorage::Float32:
        return VTK_FLOAT;
    }
    return VTK_FLOAT;
}

vtkSmartPointer<vtkImageData> VolumeViewport3DBackendVtk::buildImageData(const PreparedVolumeData &prepared, int channelIndex)
{
    vtkSmartPointer<vtkImageData> imageData = vtkSmartPointer<vtkImageData>::New();
    imageData->SetDimensions(prepared.width, prepared.height, prepared.depth);
    imageData->SetSpacing(prepared.spacing.x(), prepared.spacing.y(), prepared.spacing.z());
    imageData->AllocateScalars(vtkScalarType(prepared.storage), 1);
    std::memcpy(imageData->GetScalarPointer(),
                prepared.channelData.at(channelIndex).constData(),
                static_cast<size_t>(prepared.channelData.at(channelIndex).size()));
    imageData->Modified();
    return imageData;
}

void VolumeViewport3DBackendVtk::rebuildPipelines(const PreparedVolumeData &prepared)
{
    channels_.clear();
    channels_.resize(prepared.channelData.size());

    for (int channelIndex = 0; channelIndex < prepared.channelData.size(); ++channelIndex) {
        ChannelPipeline &pipeline = channels_[channelIndex];
        pipeline.imageData = buildImageData(prepared, channelIndex);
        pipeline.mapper = vtkSmartPointer<vtkSmartVolumeMapper>::New();
        pipeline.mapper->SetRequestedRenderModeToGPU();
        pipeline.mapper->SetInteractiveUpdateRate(30.0);
        pipeline.mapper->InteractiveAdjustSampleDistancesOn();
        // Napari defaults 3D image layers to MIP; use the same baseline here so
        // low=0 auto-contrast settings do not turn the full slab into an opaque block.
        pipeline.mapper->SetBlendModeToMaximumIntensity();
        pipeline.mapper->SetInputData(pipeline.imageData);

        pipeline.colorTransfer = vtkSmartPointer<vtkColorTransferFunction>::New();
        pipeline.opacityTransfer = vtkSmartPointer<vtkPiecewiseFunction>::New();

        pipeline.property = vtkSmartPointer<vtkVolumeProperty>::New();
        pipeline.property->SetColor(pipeline.colorTransfer);
        pipeline.property->SetScalarOpacity(pipeline.opacityTransfer);
        pipeline.property->SetInterpolationTypeToLinear();
        pipeline.property->ShadeOff();
        pipeline.property->SetIndependentComponents(true);

        pipeline.volume = vtkSmartPointer<vtkVolume>::New();
        pipeline.volume->SetMapper(pipeline.mapper);
        pipeline.volume->SetProperty(pipeline.property);

        updateTransferFunctions(channelIndex);
    }
}

void VolumeViewport3DBackendVtk::syncChannelVisuals()
{
    for (int channelIndex = 0; channelIndex < channels_.size(); ++channelIndex) {
        updateTransferFunctions(channelIndex);
        ChannelPipeline &pipeline = channels_[channelIndex];
        const bool shouldShow = channelIndex < channelSettings_.size() && channelSettings_.at(channelIndex).enabled;
        if (shouldShow && !pipeline.addedToRenderer) {
            renderer_->AddVolume(pipeline.volume);
            pipeline.addedToRenderer = true;
        } else if (!shouldShow && pipeline.addedToRenderer) {
            renderer_->RemoveVolume(pipeline.volume);
            pipeline.addedToRenderer = false;
        }
    }
}

void VolumeViewport3DBackendVtk::updateTransferFunctions(int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= channels_.size() || channelIndex >= channelSettings_.size()) {
        return;
    }

    const ChannelRenderSettings &settings = channelSettings_.at(channelIndex);
    ChannelPipeline &pipeline = channels_[channelIndex];
    const double epsilon = 1.0e-6;
    const double low = settings.low;
    const double high = std::max(settings.high, low + epsilon);
    const QColor color = settings.color;

    pipeline.colorTransfer->RemoveAllPoints();
    pipeline.colorTransfer->AddRGBPoint(low, 0.0, 0.0, 0.0);
    pipeline.colorTransfer->AddRGBPoint(high, color.redF(), color.greenF(), color.blueF());

    pipeline.opacityTransfer->RemoveAllPoints();
    pipeline.opacityTransfer->AddPoint(low, 0.0);
    pipeline.opacityTransfer->AddPoint(high, 1.0);
}

bool VolumeViewport3DBackendVtk::computeVisibleBounds(double bounds[6]) const
{
    renderer_->ComputeVisiblePropBounds(bounds);
    return std::isfinite(bounds[0]) && std::isfinite(bounds[1]) && bounds[0] <= bounds[1]
        && std::isfinite(bounds[2]) && std::isfinite(bounds[3]) && bounds[2] <= bounds[3]
        && std::isfinite(bounds[4]) && std::isfinite(bounds[5]) && bounds[4] <= bounds[5];
}

void VolumeViewport3DBackendVtk::renderNow()
{
    if (renderWindow_) {
        renderWindow_->Render();
    }
}
