#pragma once

#include "ui/volumeviewport3d_backend.h"

#include <array>

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector>

class QOpenGLBuffer;
class QOpenGLShaderProgram;
class QOpenGLTexture;
class QOpenGLVertexArrayObject;

class VolumeViewport3DBackendGl final : public QOpenGLWidget,
                                        protected QOpenGLFunctions_3_3_Core,
                                        public VolumeViewport3DBackend
{
public:
    explicit VolumeViewport3DBackendGl(QWidget *parent = nullptr);
    ~VolumeViewport3DBackendGl() override;

    [[nodiscard]] QWidget *widget() override;
    void setVolume(const RawVolume &volume, const QVector<ChannelRenderSettings> &channelSettings) override;
    void setChannelSettings(const QVector<ChannelRenderSettings> &channelSettings) override;
    void resetView() override;
    void fitToVolume() override;
    [[nodiscard]] VolumeViewport3DCameraState cameraState() const override;
    void setCameraState(const VolumeViewport3DCameraState &state) override;
    [[nodiscard]] QString lastError() const override;
    [[nodiscard]] QString renderSummary() const override;

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void ensureTextures();
    void updateContentBoundsFromVolume();
    void clearTextures();
    [[nodiscard]] float fittedDistanceForCurrentBounds() const;
    [[nodiscard]] QVector3D normalizedScale() const;
    [[nodiscard]] QVector3D cameraPositionObject() const;
    [[nodiscard]] int effectiveChannelCount() const;

    static constexpr int kVolumeChannelUniforms = 8;

    RawVolume volume_;
    QVector<ChannelRenderSettings> channelSettings_;
    QVector<QOpenGLTexture *> textures_;
    QOpenGLShaderProgram *program_ = nullptr;
    QOpenGLBuffer *vertexBuffer_ = nullptr;
    QOpenGLVertexArrayObject *vertexArray_ = nullptr;
    QPoint lastMousePosition_;
    float yawDegrees_ = -35.0f;
    float pitchDegrees_ = 25.0f;
    float distance_ = 2.6f;
    bool texturesDirty_ = false;
    bool shaderReady_ = false;
    bool pendingAutoFit_ = false;
    QString lastError_;
    QString renderSummary_;
    QVector3D contentBoundsMin_ = {-0.6f, -0.6f, -0.2f};
    QVector3D contentBoundsMax_ = {0.6f, 0.6f, 0.2f};
    QVector3D contentTexMin_ = {0.0f, 0.0f, 0.0f};
    QVector3D contentTexMax_ = {1.0f, 1.0f, 1.0f};
    std::array<float, kVolumeChannelUniforms> channelSampleScale_{};
};
