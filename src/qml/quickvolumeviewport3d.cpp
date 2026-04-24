#include "qml/quickvolumeviewport3d.h"

#include "qml/qmldocumentcontroller.h"

#include <QColor>
#include <QtMath>

#include <vtkCamera.h>
#include <vtkColorTransferFunction.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkMath.h>
#include <vtkObjectFactory.h>
#include <vtkPiecewiseFunction.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkSmartPointer.h>
#include <vtkSmartVolumeMapper.h>
#include <vtkVolume.h>
#include <vtkVolumeProperty.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr double kDefaultYawDegrees = -35.0;
constexpr double kDefaultPitchDegrees = 25.0;
constexpr int kFallbackMaxTextureSize3D = 2048;

class QuickVolumeVtkData final : public vtkObject
{
public:
    static QuickVolumeVtkData *New();
    vtkTypeMacro(QuickVolumeVtkData, vtkObject);

    enum class ScalarStorage
    {
        UInt8,
        UInt16,
        Float32
    };

    struct PreparedVolumeData
    {
        int width = 0;
        int height = 0;
        int depth = 0;
        int factorX = 1;
        int factorY = 1;
        int factorZ = 1;
        QVector3D spacing = {1.0f, 1.0f, 1.0f};
        ScalarStorage storage = ScalarStorage::UInt16;
        QVector<QByteArray> channelData;

        [[nodiscard]] bool isValid() const
        {
            return width > 0 && height > 0 && depth > 0 && !channelData.isEmpty();
        }
    };

    struct ChannelPipeline
    {
        vtkSmartPointer<vtkImageData> imageData;
        vtkSmartPointer<vtkSmartVolumeMapper> mapper;
        vtkSmartPointer<vtkColorTransferFunction> colorTransfer;
        vtkSmartPointer<vtkPiecewiseFunction> opacityTransfer;
        vtkSmartPointer<vtkVolumeProperty> property;
        vtkSmartPointer<vtkVolume> volume;
        bool addedToRenderer = false;
    };

    vtkSmartPointer<vtkRenderer> renderer;
    vtkSmartPointer<vtkInteractorStyleTrackballCamera> interactorStyle;
    QVector<ChannelPipeline> channels;
    RawVolume sourceVolume;
    QVector<ChannelRenderSettings> channelSettings;
    QString summary;
    QString error;

    [[nodiscard]] int effectiveChannelCount() const
    {
        return std::min(sourceVolume.components, static_cast<int>(channelSettings.size()));
    }

    [[nodiscard]] static QVector3D cameraOffsetForYawPitch(double yawDegrees, double pitchDegrees)
    {
        const double yawRadians = qDegreesToRadians(yawDegrees);
        const double pitchRadians = qDegreesToRadians(pitchDegrees);
        return QVector3D(static_cast<float>(std::cos(pitchRadians) * std::sin(yawRadians)),
                         static_cast<float>(std::sin(pitchRadians)),
                         static_cast<float>(std::cos(pitchRadians) * std::cos(yawRadians)));
    }

    [[nodiscard]] static QString formatSummary(const RawVolume &source,
                                               int width,
                                               int height,
                                               int depth,
                                               int factorX,
                                               int factorY,
                                               int factorZ)
    {
        const QString original = QStringLiteral("%1 x %2 x %3").arg(source.width).arg(source.height).arg(source.depth);
        if (factorX == 1 && factorY == 1 && factorZ == 1) {
            return original;
        }
        return QStringLiteral("%1 (rendering %2 x %3 x %4)").arg(original).arg(width).arg(height).arg(depth);
    }

    [[nodiscard]] static double sourceVoxelAsDouble(const RawVolume &volume, int channelIndex, qsizetype voxelIndex)
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
            }
            {
            quint32 value = 0;
            std::memcpy(&value, base, sizeof(value));
            return static_cast<double>(value);
            }
        default:
            return 0.0;
        }
    }

    [[nodiscard]] static int storageBytesPerComponent(ScalarStorage storage)
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

    [[nodiscard]] static int vtkScalarType(ScalarStorage storage)
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

    [[nodiscard]] PreparedVolumeData prepareRenderData(const RawVolume &volume, int channelCount)
    {
        PreparedVolumeData prepared;
        if (!volume.isValid() || channelCount <= 0) {
            return prepared;
        }

        const int limit = kFallbackMaxTextureSize3D;
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
        prepared.storage = uint8 ? ScalarStorage::UInt8 : (uint16 ? ScalarStorage::UInt16 : ScalarStorage::Float32);

        const int dstBytesPerComponent = storageBytesPerComponent(prepared.storage);
        const qsizetype dstVoxelCount = static_cast<qsizetype>(prepared.width) * prepared.height * prepared.depth;
        prepared.channelData.resize(channelCount);
        for (int channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
            QByteArray &dst = prepared.channelData[channelIndex];
            dst.resize(dstVoxelCount * dstBytesPerComponent);
            for (int z = 0; z < prepared.depth; ++z) {
                const int srcZ = std::min(z * prepared.factorZ, volume.depth - 1);
                for (int y = 0; y < prepared.height; ++y) {
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
                            std::memcpy(dstPtr, volume.channelData.at(channelIndex).constData() + srcVoxelIndex * 2, sizeof(quint16));
                        } else {
                            const float value = static_cast<float>(sourceVoxelAsDouble(volume, channelIndex, srcVoxelIndex));
                            std::memcpy(dstPtr, &value, sizeof(float));
                        }
                    }
                }
            }
        }
        summary = formatSummary(volume, prepared.width, prepared.height, prepared.depth,
                                prepared.factorX, prepared.factorY, prepared.factorZ);
        return prepared;
    }

    [[nodiscard]] vtkSmartPointer<vtkImageData> buildImageData(const PreparedVolumeData &prepared, int channelIndex)
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

    void updateTransferFunctions(int channelIndex)
    {
        if (channelIndex < 0 || channelIndex >= channels.size() || channelIndex >= channelSettings.size()) {
            return;
        }
        const ChannelRenderSettings &settings = channelSettings.at(channelIndex);
        ChannelPipeline &pipeline = channels[channelIndex];
        const double low = settings.low;
        const double high = std::max(settings.high, low + 1.0e-6);
        const QColor color = settings.color;
        pipeline.colorTransfer->RemoveAllPoints();
        pipeline.colorTransfer->AddRGBPoint(low, 0.0, 0.0, 0.0);
        pipeline.colorTransfer->AddRGBPoint(high, color.redF(), color.greenF(), color.blueF());
        pipeline.opacityTransfer->RemoveAllPoints();
        pipeline.opacityTransfer->AddPoint(low, 0.0);
        pipeline.opacityTransfer->AddPoint(high, 1.0);
    }

    void rebuildPipelines(const PreparedVolumeData &prepared)
    {
        channels.clear();
        channels.resize(prepared.channelData.size());
        for (int channelIndex = 0; channelIndex < prepared.channelData.size(); ++channelIndex) {
            ChannelPipeline &pipeline = channels[channelIndex];
            pipeline.imageData = buildImageData(prepared, channelIndex);
            pipeline.mapper = vtkSmartPointer<vtkSmartVolumeMapper>::New();
            pipeline.mapper->SetRequestedRenderModeToGPU();
            pipeline.mapper->SetBlendModeToMaximumIntensity();
            pipeline.mapper->SetInputData(pipeline.imageData);
            pipeline.colorTransfer = vtkSmartPointer<vtkColorTransferFunction>::New();
            pipeline.opacityTransfer = vtkSmartPointer<vtkPiecewiseFunction>::New();
            pipeline.property = vtkSmartPointer<vtkVolumeProperty>::New();
            pipeline.property->SetColor(pipeline.colorTransfer);
            pipeline.property->SetScalarOpacity(pipeline.opacityTransfer);
            pipeline.property->SetInterpolationTypeToLinear();
            pipeline.property->ShadeOff();
            pipeline.volume = vtkSmartPointer<vtkVolume>::New();
            pipeline.volume->SetMapper(pipeline.mapper);
            pipeline.volume->SetProperty(pipeline.property);
            updateTransferFunctions(channelIndex);
        }
    }

    void syncChannelVisuals()
    {
        for (int channelIndex = 0; channelIndex < channels.size(); ++channelIndex) {
            updateTransferFunctions(channelIndex);
            ChannelPipeline &pipeline = channels[channelIndex];
            const bool shouldShow = channelIndex < channelSettings.size() && channelSettings.at(channelIndex).enabled;
            if (shouldShow && !pipeline.addedToRenderer) {
                renderer->AddVolume(pipeline.volume);
                pipeline.addedToRenderer = true;
            } else if (!shouldShow && pipeline.addedToRenderer) {
                renderer->RemoveVolume(pipeline.volume);
                pipeline.addedToRenderer = false;
            }
        }
    }

    [[nodiscard]] bool computeVisibleBounds(double bounds[6]) const
    {
        renderer->ComputeVisiblePropBounds(bounds);
        return std::isfinite(bounds[0]) && std::isfinite(bounds[1]) && bounds[0] <= bounds[1]
            && std::isfinite(bounds[2]) && std::isfinite(bounds[3]) && bounds[2] <= bounds[3]
            && std::isfinite(bounds[4]) && std::isfinite(bounds[5]) && bounds[4] <= bounds[5];
    }

    void resetView()
    {
        double bounds[6];
        if (!computeVisibleBounds(bounds)) {
            return;
        }
        renderer->ResetCamera(bounds);
        vtkCamera *camera = renderer->GetActiveCamera();
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
        camera->SetPosition(center[0] + offset.x() * fittedDistance,
                            center[1] + offset.y() * fittedDistance,
                            center[2] + offset.z() * fittedDistance);
        camera->SetViewUp(0.0, 1.0, 0.0);
        camera->OrthogonalizeViewUp();
        renderer->ResetCameraClippingRange(bounds);
    }

    void setVolume(const RawVolume &volume, const QVector<ChannelRenderSettings> &settings)
    {
        sourceVolume = volume;
        channelSettings = settings;
        error.clear();
        summary.clear();
        if (renderer) {
            renderer->RemoveAllViewProps();
        }
        channels.clear();
        if (!sourceVolume.isValid()) {
            return;
        }
        const PreparedVolumeData prepared = prepareRenderData(sourceVolume, effectiveChannelCount());
        if (!prepared.isValid()) {
            error = QStringLiteral("The VTK 3D volume could not be prepared.");
            return;
        }
        rebuildPipelines(prepared);
        syncChannelVisuals();
        resetView();
    }
};

vtkStandardNewMacro(QuickVolumeVtkData);

QuickVolumeVtkData *asData(QQuickVTKItem::vtkUserData userData)
{
    return QuickVolumeVtkData::SafeDownCast(userData);
}
}

QuickVolumeViewport3D::QuickVolumeViewport3D(QQuickItem *parent)
    : QQuickVTKItem(parent)
{
}

QmlDocumentController *QuickVolumeViewport3D::controller() const { return controller_; }

void QuickVolumeViewport3D::setController(QmlDocumentController *controller)
{
    if (controller_ == controller) {
        return;
    }
    if (controller_) {
        disconnect(controller_, nullptr, this, nullptr);
    }
    controller_ = controller;
    if (controller_) {
        connect(controller_, &QmlDocumentController::volumeChanged, this, &QuickVolumeViewport3D::syncVolumeFromController);
        connect(controller_, &QmlDocumentController::volumeChannelSettingsChanged, this, &QuickVolumeViewport3D::syncChannelsFromController);
        syncVolumeFromController();
    }
    emit controllerChanged();
}

QString QuickVolumeViewport3D::summary() const { return summary_; }
QString QuickVolumeViewport3D::errorText() const { return errorText_; }

void QuickVolumeViewport3D::resetView()
{
    dispatch_async([](vtkRenderWindow *renderWindow, vtkUserData userData) {
        if (QuickVolumeVtkData *data = asData(userData)) {
            data->resetView();
            renderWindow->Render();
        }
    });
    scheduleRender();
}

void QuickVolumeViewport3D::fitToVolume()
{
    resetView();
}

QQuickVTKItem::vtkUserData QuickVolumeViewport3D::initializeVTK(vtkRenderWindow *renderWindow)
{
    vtkSmartPointer<QuickVolumeVtkData> data = vtkSmartPointer<QuickVolumeVtkData>::New();
    data->renderer = vtkSmartPointer<vtkRenderer>::New();
    data->renderer->SetBackground(0.015, 0.018, 0.025);
    data->interactorStyle = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
    renderWindow->AddRenderer(data->renderer);
    renderWindow->SetDesiredUpdateRate(30.0);
    if (renderWindow->GetInteractor()) {
        renderWindow->GetInteractor()->SetInteractorStyle(data->interactorStyle);
        renderWindow->GetInteractor()->SetDesiredUpdateRate(30.0);
        renderWindow->GetInteractor()->SetStillUpdateRate(0.01);
    }
    data->setVolume(volume_, channelSettings_);
    setSummary(data->summary);
    setErrorText(data->error);
    return data;
}

void QuickVolumeViewport3D::destroyingVTK(vtkRenderWindow *renderWindow, vtkUserData)
{
    renderWindow->RemoveAllObservers();
}

void QuickVolumeViewport3D::syncVolumeFromController()
{
    if (!controller_) {
        volume_ = {};
        channelSettings_.clear();
    } else {
        volume_ = controller_->currentVolume();
        channelSettings_ = controller_->channelSettings();
    }
    const RawVolume volume = volume_;
    const QVector<ChannelRenderSettings> settings = channelSettings_;
    dispatch_async([this, volume, settings](vtkRenderWindow *renderWindow, vtkUserData userData) {
        if (QuickVolumeVtkData *data = asData(userData)) {
            data->setVolume(volume, settings);
            setSummary(data->summary);
            setErrorText(data->error);
            renderWindow->Render();
        }
    });
    scheduleRender();
}

void QuickVolumeViewport3D::syncChannelsFromController()
{
    if (!controller_) {
        return;
    }
    channelSettings_ = controller_->channelSettings();
    const QVector<ChannelRenderSettings> settings = channelSettings_;
    dispatch_async([this, settings](vtkRenderWindow *renderWindow, vtkUserData userData) {
        if (QuickVolumeVtkData *data = asData(userData)) {
            data->channelSettings = settings;
            data->syncChannelVisuals();
            renderWindow->Render();
        }
    });
    scheduleRender();
}

void QuickVolumeViewport3D::setSummary(const QString &summary)
{
    if (summary_ == summary) {
        return;
    }
    summary_ = summary;
    emit summaryChanged();
}

void QuickVolumeViewport3D::setErrorText(const QString &text)
{
    if (errorText_ == text) {
        return;
    }
    errorText_ = text;
    emit errorTextChanged();
}
