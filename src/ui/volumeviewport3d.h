#pragma once

#include "ui/volumeviewport3d_backend.h"

#include <QImage>
#include <QWidget>
#include <memory>

class VolumeViewport3D : public QWidget
{
    Q_OBJECT

public:
    explicit VolumeViewport3D(QWidget *parent = nullptr);
    ~VolumeViewport3D() override;

    void setVolume(const RawVolume &volume, const QVector<ChannelRenderSettings> &channelSettings);
    void setChannelSettings(const QVector<ChannelRenderSettings> &channelSettings);
    void resetView();
    void fitToVolume();
    [[nodiscard]] VolumeViewport3DCameraState cameraState() const;
    void setCameraState(const VolumeViewport3DCameraState &state);
    [[nodiscard]] QImage captureImage() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] QString renderSummary() const;

private:
    std::unique_ptr<VolumeViewport3DBackend> backend_;
};
