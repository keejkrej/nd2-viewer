#pragma once

#include "core/documenttypes.h"

#include <memory>

#include <QWidget>

class VolumeViewport3DBackend;

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
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] QString renderSummary() const;

private:
    std::unique_ptr<VolumeViewport3DBackend> backend_;
};
