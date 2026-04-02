#pragma once

#include "core/documenttypes.h"

#include <QFutureWatcher>
#include <QImage>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>
#include <QPolygonF>
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
    struct ProjectionConstraint
    {
        QSize viewportSize;
        QPolygonF contour;
        QImage maskImage;
        QMatrix4x4 modelViewProjection;
        QVector3D boundsMin;
        QVector3D boundsMax;
        QVector3D texMin;
        QVector3D texMax;

        [[nodiscard]] bool isValid() const
        {
            return viewportSize.isValid() && contour.size() >= 3 && !maskImage.isNull();
        }
    };

    struct SelectionMask3D
    {
        int width = 0;
        int height = 0;
        int depth = 0;
        QByteArray voxels;
        qsizetype selectedVoxelCount = 0;

        [[nodiscard]] bool isValid() const
        {
            return width > 0
                && height > 0
                && depth > 0
                && voxels.size() == static_cast<qsizetype>(width) * height * depth;
        }

        [[nodiscard]] qsizetype voxelCount() const
        {
            return static_cast<qsizetype>(width) * height * depth;
        }
    };

    struct SelectionComputationResult
    {
        SelectionMask3D latestMask;
        SelectionMask3D fusedMask;
        QString warning;
        bool success = false;
    };

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
    void beginProjectionConstraint();
    void acceptPendingConstraint();
    void undoLastConstraint();
    void clearSelection();
    [[nodiscard]] bool isSelectionBusy() const;
    [[nodiscard]] bool hasPendingConstraint() const;
    [[nodiscard]] bool hasSelection() const;
    [[nodiscard]] int acceptedConstraintCount() const;
    [[nodiscard]] QString selectionStatusText() const;
    [[nodiscard]] bool selectionStatusWarning() const;
    void setSelectionOverlayOpacity(qreal opacity);
    [[nodiscard]] qreal selectionOverlayOpacity() const;

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

signals:
    void selectionStateChanged();

private:
    [[nodiscard]] QMatrix4x4 currentModelViewProjection() const;
    [[nodiscard]] ProjectionConstraint capturePendingConstraint() const;
    [[nodiscard]] QVector3D voxelObjectPosition(int x,
                                                int y,
                                                int z,
                                                const QVector3D &boundsMin,
                                                const QVector3D &boundsMax,
                                                const QVector3D &texMin,
                                                const QVector3D &texMax) const;
    void rebuildOverlaySamples();
    void rebuildOverlaySamplesForMask(const SelectionMask3D &mask, QVector<QVector3D> *samples, int targetSampleCount) const;
    void startSelectionComputation(const QVector<ProjectionConstraint> &constraints);
    void handleSelectionComputationFinished();
    void ensurePointCloud();
    void ensureTextures();
    void clearTextures();
    void clearPointCloud();
    [[nodiscard]] float fittedDistanceForCurrentBounds() const;
    [[nodiscard]] QVector3D normalizedScale() const;
    [[nodiscard]] QVector3D cameraPositionObject() const;
    [[nodiscard]] int effectiveChannelCount() const;
    [[nodiscard]] int activeSelectionChannelCount() const;
    [[nodiscard]] QColor selectionOverlayColor() const;
    void drawOverlay(QPainter &painter);
    void drawProjectedMask(QPainter &painter,
                           const QVector<QVector3D> &voxels,
                           const QColor &color,
                           int pixelRadius,
                           qreal opacity) const;

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
    bool contourCaptureActive_ = false;
    bool contourDrawing_ = false;
    QPolygonF pendingContour_;
    QVector<ProjectionConstraint> acceptedConstraints_;
    QVector<ProjectionConstraint> pendingSelectionConstraints_;
    SelectionMask3D latestMask_;
    SelectionMask3D fusedMask_;
    QVector<QVector3D> latestOverlaySamples_;
    QVector<QVector3D> fusedOverlaySamples_;
    QFutureWatcher<SelectionComputationResult> selectionWatcher_;
    QString selectionStatusText_;
    bool selectionStatusWarning_ = false;
    qreal selectionOverlayOpacity_ = 0.72;
};
