#pragma once

#include "ui/volumeviewport3d_backend.h"

#include <QByteArray>
#include <QVector3D>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkSmartPointer.h>

class vtkColorTransferFunction;
class vtkGenericOpenGLRenderWindow;
class vtkImageData;
class vtkInteractorStyleTrackballCamera;
class vtkPiecewiseFunction;
class vtkRenderer;
class vtkSmartVolumeMapper;
class vtkVolume;
class vtkVolumeProperty;

class VolumeViewport3DBackendVtk final : public QVTKOpenGLNativeWidget, public VolumeViewport3DBackend
{
public:
    explicit VolumeViewport3DBackendVtk(QWidget *parent = nullptr);
    ~VolumeViewport3DBackendVtk() override = default;

    [[nodiscard]] QWidget *widget() override;
    void setVolume(const RawVolume &volume, const QVector<ChannelRenderSettings> &channelSettings) override;
    void setChannelSettings(const QVector<ChannelRenderSettings> &channelSettings) override;
    void resetView() override;
    void fitToVolume() override;
    [[nodiscard]] VolumeViewport3DCameraState cameraState() const override;
    void setCameraState(const VolumeViewport3DCameraState &state) override;
    [[nodiscard]] QString lastError() const override;
    [[nodiscard]] QString renderSummary() const override;

private:
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
        QString summary;

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

    [[nodiscard]] int effectiveChannelCount() const;
    [[nodiscard]] int queryMaxTextureSize3D();
    [[nodiscard]] PreparedVolumeData prepareRenderData(const RawVolume &volume, int channelCount);
    [[nodiscard]] double sourceVoxelAsDouble(const RawVolume &volume, int channelIndex, qsizetype voxelIndex) const;
    [[nodiscard]] int storageBytesPerComponent(ScalarStorage storage) const;
    [[nodiscard]] int vtkScalarType(ScalarStorage storage) const;
    [[nodiscard]] vtkSmartPointer<vtkImageData> buildImageData(const PreparedVolumeData &prepared, int channelIndex);
    void rebuildPipelines(const PreparedVolumeData &prepared);
    void syncChannelVisuals();
    void updateTransferFunctions(int channelIndex);
    [[nodiscard]] bool computeVisibleBounds(double bounds[6]) const;
    void renderNow();

    RawVolume sourceVolume_;
    QVector<ChannelRenderSettings> channelSettings_;
    QVector<ChannelPipeline> channels_;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow_;
    vtkSmartPointer<vtkRenderer> renderer_;
    vtkSmartPointer<vtkInteractorStyleTrackballCamera> interactorStyle_;
    QString lastError_;
    QString renderSummary_;
    int maxTextureSize3D_ = 0;
};
