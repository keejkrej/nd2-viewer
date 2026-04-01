#include "ui/imageviewport.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>

#include <algorithm>

ImageViewport::ImageViewport(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);
}

void ImageViewport::setImage(const QImage &image)
{
    image_ = image;
    panOffset_ = {};
    if (fitToWindow_) {
        zoomToFit();
    } else {
        clampPanOffset();
        update();
        emit zoomChanged(effectiveScale(), fitToWindow_);
    }
}

const QImage &ImageViewport::image() const
{
    return image_;
}

bool ImageViewport::hasImage() const
{
    return !image_.isNull();
}

void ImageViewport::zoomToFit()
{
    fitToWindow_ = true;
    panOffset_ = {};
    update();
    emit zoomChanged(effectiveScale(), fitToWindow_);
}

void ImageViewport::setActualSize()
{
    fitToWindow_ = false;
    zoomFactor_ = 1.0;
    panOffset_ = {};
    clampPanOffset();
    update();
    emit zoomChanged(effectiveScale(), fitToWindow_);
}

void ImageViewport::setZoomFactor(double zoomFactor)
{
    fitToWindow_ = false;
    zoomFactor_ = std::clamp(zoomFactor, 0.05, 64.0);
    clampPanOffset();
    update();
    emit zoomChanged(effectiveScale(), fitToWindow_);
}

double ImageViewport::zoomFactor() const
{
    return effectiveScale();
}

bool ImageViewport::isFitToWindow() const
{
    return fitToWindow_;
}

void ImageViewport::contextMenuEvent(QContextMenuEvent *event)
{
    if (!hasImage() || !isPointInsideImage(event->pos())) {
        event->ignore();
        return;
    }

    QMenu menu(this);
    QAction *saveAction = menu.addAction(tr("Export Current Frame..."));
    QAction *chosenAction = menu.exec(event->globalPos());
    if (chosenAction == saveAction) {
        emit saveImageRequested();
    }
    event->accept();
}

void ImageViewport::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), QColor(14, 14, 18));

    if (image_.isNull()) {
        painter.setPen(QColor(175, 175, 185));
        painter.drawText(rect(), Qt::AlignCenter, tr("Open an ND2 file to start viewing."));
        return;
    }

    const QRectF targetRect = imageRect();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, targetRect.width() < image_.width() || targetRect.height() < image_.height());
    painter.drawImage(targetRect, image_);

    painter.setPen(QPen(QColor(255, 255, 255, 30), 1.0));
    painter.drawRect(targetRect);
}

void ImageViewport::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (fitToWindow_) {
        emit zoomChanged(effectiveScale(), fitToWindow_);
    } else {
        clampPanOffset();
    }
}

void ImageViewport::wheelEvent(QWheelEvent *event)
{
    if (image_.isNull()) {
        event->ignore();
        return;
    }

    const QPointF anchorImagePoint = widgetToImage(event->position());
    const double direction = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;

    fitToWindow_ = false;
    zoomFactor_ = std::clamp(zoomFactor_ * direction, 0.05, 64.0);

    const QPointF newWidgetPoint = imageToWidget(anchorImagePoint);
    panOffset_ += event->position() - newWidgetPoint;
    clampPanOffset();
    update();
    emit zoomChanged(effectiveScale(), fitToWindow_);
    event->accept();
}

void ImageViewport::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && hasImage()) {
        panning_ = true;
        lastMousePosition_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void ImageViewport::mouseMoveEvent(QMouseEvent *event)
{
    if (panning_ && !fitToWindow_) {
        panOffset_ += event->pos() - lastMousePosition_;
        lastMousePosition_ = event->pos();
        clampPanOffset();
        update();
    }

    const QPointF imagePosition = widgetToImage(event->position());
    const QPoint pixel(static_cast<int>(std::floor(imagePosition.x())), static_cast<int>(std::floor(imagePosition.y())));
    const bool inside = pixel.x() >= 0 && pixel.y() >= 0 && pixel.x() < image_.width() && pixel.y() < image_.height();
    emit hoveredPixelChanged(pixel, inside);

    QWidget::mouseMoveEvent(event);
}

void ImageViewport::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && panning_) {
        panning_ = false;
        unsetCursor();
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void ImageViewport::leaveEvent(QEvent *event)
{
    emit hoveredPixelChanged({}, false);
    QWidget::leaveEvent(event);
}

void ImageViewport::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event)

    if (fitToWindow_) {
        setActualSize();
    } else {
        zoomToFit();
    }
}

double ImageViewport::effectiveScale() const
{
    if (image_.isNull()) {
        return 1.0;
    }

    if (!fitToWindow_) {
        return zoomFactor_;
    }

    const double widthScale = static_cast<double>(width()) / static_cast<double>(image_.width());
    const double heightScale = static_cast<double>(height()) / static_cast<double>(image_.height());
    return std::min(widthScale, heightScale);
}

QSizeF ImageViewport::scaledImageSize() const
{
    if (image_.isNull()) {
        return {};
    }

    const double scale = effectiveScale();
    return QSizeF(image_.width() * scale, image_.height() * scale);
}

QRectF ImageViewport::imageRect() const
{
    const QSizeF scaledSize = scaledImageSize();
    const QPointF topLeft((width() - scaledSize.width()) / 2.0, (height() - scaledSize.height()) / 2.0);
    return QRectF(topLeft + panOffset_, scaledSize);
}

bool ImageViewport::isPointInsideImage(const QPointF &widgetPoint) const
{
    if (image_.isNull()) {
        return false;
    }

    const QPointF imagePoint = widgetToImage(widgetPoint);
    return imagePoint.x() >= 0.0 && imagePoint.y() >= 0.0
           && imagePoint.x() < image_.width() && imagePoint.y() < image_.height();
}

QPointF ImageViewport::imageToWidget(const QPointF &imagePoint) const
{
    const QRectF rect = imageRect();
    const double scale = effectiveScale();
    return rect.topLeft() + QPointF(imagePoint.x() * scale, imagePoint.y() * scale);
}

QPointF ImageViewport::widgetToImage(const QPointF &widgetPoint) const
{
    const QRectF rect = imageRect();
    const double scale = effectiveScale();
    if (qFuzzyIsNull(scale)) {
        return {};
    }

    return QPointF((widgetPoint.x() - rect.left()) / scale, (widgetPoint.y() - rect.top()) / scale);
}

void ImageViewport::clampPanOffset()
{
    if (fitToWindow_ || image_.isNull()) {
        panOffset_ = {};
        return;
    }

    const QSizeF scaledSize = scaledImageSize();
    const double horizontalSlack = std::max(0.0, (scaledSize.width() - width()) / 2.0);
    const double verticalSlack = std::max(0.0, (scaledSize.height() - height()) / 2.0);
    panOffset_.setX(std::clamp(panOffset_.x(), -horizontalSlack, horizontalSlack));
    panOffset_.setY(std::clamp(panOffset_.y(), -verticalSlack, verticalSlack));
}
