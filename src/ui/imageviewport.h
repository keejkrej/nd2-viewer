#pragma once

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QWidget>

class ImageViewport : public QWidget
{
    Q_OBJECT

public:
    enum class InteractionMode
    {
        Pan,
        DrawRoi
    };

    explicit ImageViewport(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    [[nodiscard]] const QImage &image() const;
    [[nodiscard]] bool hasImage() const;

    void zoomToFit();
    void setActualSize();
    void setZoomFactor(double zoomFactor);
    [[nodiscard]] double zoomFactor() const;
    [[nodiscard]] bool isFitToWindow() const;
    void setInteractionMode(InteractionMode mode);
    [[nodiscard]] InteractionMode interactionMode() const;
    [[nodiscard]] bool hasRoi() const;
    [[nodiscard]] QRect roiRect() const;
    void clearRoi();
    void setOverlayTimestamp(const QString &timestampText);
    void setScaleBarCalibrationMicronsPerPixel(double micronsPerPixel);

signals:
    void hoveredPixelChanged(const QPoint &pixelPosition, bool insideImage);
    void zoomChanged(double zoomFactor, bool fitToWindow);
    void saveImageRequested();
    void exportMovieRequested();
    void roiChanged(const QRect &roiRect);
    void roiPresenceChanged(bool hasRoi);
    void exportRoiRequested();
    void exportRoiMovieRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    [[nodiscard]] double fittedScaleForCurrentSize() const;
    [[nodiscard]] bool isAtFittedView() const;
    [[nodiscard]] double effectiveScale() const;
    [[nodiscard]] QSizeF scaledImageSize() const;
    [[nodiscard]] QRectF imageRect() const;
    [[nodiscard]] QPointF viewportCenter() const;
    [[nodiscard]] bool isPointInsideImage(const QPointF &widgetPoint) const;
    [[nodiscard]] QPointF imageToWidget(const QPointF &imagePoint) const;
    [[nodiscard]] QPointF widgetToImage(const QPointF &widgetPoint) const;
    [[nodiscard]] QPointF clampedImagePoint(const QPointF &widgetPoint) const;
    [[nodiscard]] QRectF currentDragImageRect() const;
    [[nodiscard]] QRectF widgetRectForImageRect(const QRectF &imageRect) const;
    [[nodiscard]] QRect normalizedRoiRect(const QRectF &imageRect) const;
    void clampPanOffset();
    void setRoiRectInternal(const QRect &roiRect);
    void updateCursor();
    [[nodiscard]] QString scaleBarLabelForMicrons(double microns) const;

    QImage image_;
    double zoomFactor_ = 1.0;
    QPointF panOffset_;
    InteractionMode interactionMode_ = InteractionMode::Pan;
    bool panning_ = false;
    bool drawingRoi_ = false;
    QPoint lastMousePosition_;
    QRect roiRect_;
    QPointF roiDragStartImage_;
    QPointF roiDragCurrentImage_;
    QString overlayTimestamp_;
    double scaleBarMicronsPerPixel_ = 0.0;
};
