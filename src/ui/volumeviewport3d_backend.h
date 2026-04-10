#pragma once

#include "core/documenttypes.h"

#include <QString>

class QWidget;

class VolumeViewport3DBackend
{
public:
    virtual ~VolumeViewport3DBackend() = default;

    [[nodiscard]] virtual QWidget *widget() = 0;
    virtual void setVolume(const RawVolume &volume, const QVector<ChannelRenderSettings> &channelSettings) = 0;
    virtual void setChannelSettings(const QVector<ChannelRenderSettings> &channelSettings) = 0;
    virtual void resetView() = 0;
    virtual void fitToVolume() = 0;
    [[nodiscard]] virtual QString lastError() const = 0;
    [[nodiscard]] virtual QString renderSummary() const = 0;
};
