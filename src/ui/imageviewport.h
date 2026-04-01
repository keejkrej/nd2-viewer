#pragma once

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QWidget>

class ImageViewport : public QWidget
{
    Q_OBJECT

public:
    explicit ImageViewport(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    [[nodiscard]] const QImage &image() const;
    [[nodiscard]] bool hasImage() const;

    void zoomToFit();
    void setActualSize();
    void setZoomFactor(double zoomFactor);
    [[nodiscard]] double zoomFactor() const;
    [[nodiscard]] bool isFitToWindow() const;

signals:
    void hoveredPixelChanged(const QPoint &pixelPosition, bool insideImage);
    void zoomChanged(double zoomFactor, bool fitToWindow);

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
    [[nodiscard]] double effectiveScale() const;
    [[nodiscard]] QSizeF scaledImageSize() const;
    [[nodiscard]] QRectF imageRect() const;
    [[nodiscard]] QPointF imageToWidget(const QPointF &imagePoint) const;
    [[nodiscard]] QPointF widgetToImage(const QPointF &widgetPoint) const;
    void clampPanOffset();

    QImage image_;
    double zoomFactor_ = 1.0;
    bool fitToWindow_ = true;
    QPointF panOffset_;
    bool panning_ = false;
    QPoint lastMousePosition_;
};
