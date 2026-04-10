#pragma once

#include "core/documenttypes.h"

#include <QVector3D>
#include <QString>

class QWidget;

struct VolumeViewport3DCameraState
{
    bool valid = false;
    QVector3D position;
    QVector3D focalPoint;
    QVector3D viewUp = {0.0f, 1.0f, 0.0f};
};

class VolumeViewport3DBackend
{
public:
    virtual ~VolumeViewport3DBackend() = default;

    [[nodiscard]] virtual QWidget *widget() = 0;
    virtual void setVolume(const RawVolume &volume, const QVector<ChannelRenderSettings> &channelSettings) = 0;
    virtual void setChannelSettings(const QVector<ChannelRenderSettings> &channelSettings) = 0;
    virtual void resetView() = 0;
    virtual void fitToVolume() = 0;
    [[nodiscard]] virtual VolumeViewport3DCameraState cameraState() const = 0;
    virtual void setCameraState(const VolumeViewport3DCameraState &state) = 0;
    [[nodiscard]] virtual QString lastError() const = 0;
    [[nodiscard]] virtual QString renderSummary() const = 0;
};
