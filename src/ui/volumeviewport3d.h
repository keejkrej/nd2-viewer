#pragma once

#include "core/documenttypes.h"

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector>

class QOpenGLBuffer;
class QOpenGLShaderProgram;
class QOpenGLTexture;
class QOpenGLVertexArrayObject;

class VolumeViewport3D : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    enum class RenderMode
    {
        Hybrid = 0,
        Smooth = 1,
        Points = 2
    };

    explicit VolumeViewport3D(QWidget *parent = nullptr);
    ~VolumeViewport3D() override;

    void setVolume(const RawVolume &volume, const QVector<ChannelRenderSettings> &channelSettings);
    void setChannelSettings(const QVector<ChannelRenderSettings> &channelSettings);
    void resetView();
    void fitToVolume();
    void setRenderMode(RenderMode mode);
    [[nodiscard]] RenderMode renderMode() const;
    [[nodiscard]] QString lastError() const;

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void ensurePointCloud();
    void ensureTextures();
    void clearTextures();
    void clearPointCloud();
    [[nodiscard]] float fittedDistanceForCurrentBounds() const;
    [[nodiscard]] QVector3D normalizedScale() const;
    [[nodiscard]] QVector3D cameraPositionObject() const;
    [[nodiscard]] int effectiveChannelCount() const;

    RawVolume volume_;
    QVector<ChannelRenderSettings> channelSettings_;
    QVector<QOpenGLTexture *> textures_;
    QOpenGLShaderProgram *program_ = nullptr;
    QOpenGLShaderProgram *pointProgram_ = nullptr;
    QOpenGLBuffer *vertexBuffer_ = nullptr;
    QOpenGLBuffer *pointBuffer_ = nullptr;
    QOpenGLVertexArrayObject *vertexArray_ = nullptr;
    QOpenGLVertexArrayObject *pointArray_ = nullptr;
    QPoint lastMousePosition_;
    float yawDegrees_ = -35.0f;
    float pitchDegrees_ = 25.0f;
    float distance_ = 2.6f;
    bool texturesDirty_ = false;
    bool pointCloudDirty_ = false;
    bool shaderReady_ = false;
    bool pointShaderReady_ = false;
    bool pendingAutoFit_ = false;
    QString lastError_;
    int pointCount_ = 0;
    RenderMode renderMode_ = RenderMode::Hybrid;
    QVector3D contentBoundsMin_ = {-0.6f, -0.6f, -0.2f};
    QVector3D contentBoundsMax_ = {0.6f, 0.6f, 0.2f};
    QVector3D contentTexMin_ = {0.0f, 0.0f, 0.0f};
    QVector3D contentTexMax_ = {1.0f, 1.0f, 1.0f};
};
