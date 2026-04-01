#include "ui/imageviewport.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kMinRoiDimension = 1.0;
}

ImageViewport::ImageViewport(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);
    updateCursor();
}

void ImageViewport::setImage(const QImage &image)
{
    const bool preserveView = !fitToWindow_ && !image_.isNull() && !image.isNull();
    const QPointF anchorImagePoint = preserveView ? widgetToImage(viewportCenter()) : QPointF();

    image_ = image;
    if (fitToWindow_) {
        zoomToFit();
    } else {
        if (preserveView) {
            const QSizeF scaledSize = scaledImageSize();
            const QPointF centeredTopLeft((width() - scaledSize.width()) / 2.0,
                                          (height() - scaledSize.height()) / 2.0);
            const double scale = effectiveScale();
            panOffset_ = viewportCenter() - centeredTopLeft
                         - QPointF(anchorImagePoint.x() * scale, anchorImagePoint.y() * scale);
        } else {
            panOffset_ = {};
        }
        clampPanOffset();
        update();
        emit zoomChanged(effectiveScale(), fitToWindow_);
    }

    if (image_.isNull()) {
        setRoiRectInternal({});
    } else if (roiRect_.isValid() && !roiRect_.isEmpty()) {
        setRoiRectInternal(roiRect_.intersected(QRect(0, 0, image_.width(), image_.height())));
    }

    if (drawingRoi_ && !hasImage()) {
        drawingRoi_ = false;
    }
    updateCursor();
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

void ImageViewport::setInteractionMode(InteractionMode mode)
{
    if (interactionMode_ == mode) {
        return;
    }

    interactionMode_ = mode;
    if (interactionMode_ != InteractionMode::DrawRoi) {
        drawingRoi_ = false;
    }
    updateCursor();
    update();
}

ImageViewport::InteractionMode ImageViewport::interactionMode() const
{
    return interactionMode_;
}

bool ImageViewport::hasRoi() const
{
    return roiRect_.isValid() && !roiRect_.isEmpty();
}

QRect ImageViewport::roiRect() const
{
    return roiRect_;
}

void ImageViewport::clearRoi()
{
    setRoiRectInternal({});
}

void ImageViewport::contextMenuEvent(QContextMenuEvent *event)
{
    if (!hasImage() || !isPointInsideImage(event->pos())) {
        event->ignore();
        return;
    }

    QMenu menu(this);
    QAction *saveAction = menu.addAction(tr("Export Current Frame..."));
    QAction *exportRoiAction = menu.addAction(tr("Export Current ROI..."));
    exportRoiAction->setEnabled(hasRoi());

    QAction *chosenAction = menu.exec(event->globalPos());
    if (chosenAction == saveAction) {
        emit saveImageRequested();
    } else if (chosenAction == exportRoiAction) {
        emit exportRoiRequested();
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

    QRectF roiImageRect;
    if (drawingRoi_) {
        roiImageRect = currentDragImageRect();
    } else if (hasRoi()) {
        roiImageRect = QRectF(roiRect_.x(), roiRect_.y(), roiRect_.width(), roiRect_.height());
    }

    if (roiImageRect.width() >= kMinRoiDimension && roiImageRect.height() >= kMinRoiDimension) {
        const QRectF roiWidgetRect = widgetRectForImageRect(roiImageRect);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setBrush(QColor(84, 181, 255, 48));
        painter.setPen(QPen(QColor(84, 181, 255), 1.5));
        painter.drawRect(roiWidgetRect);
    }
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
        if (interactionMode_ == InteractionMode::DrawRoi) {
            if (!isPointInsideImage(event->position())) {
                event->ignore();
                return;
            }
            drawingRoi_ = true;
            roiDragStartImage_ = clampedImagePoint(event->position());
            roiDragCurrentImage_ = roiDragStartImage_;
            updateCursor();
            update();
            event->accept();
            return;
        }

        panning_ = true;
        lastMousePosition_ = event->pos();
        updateCursor();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void ImageViewport::mouseMoveEvent(QMouseEvent *event)
{
    if (drawingRoi_) {
        roiDragCurrentImage_ = clampedImagePoint(event->position());
        update();
    } else if (panning_ && !fitToWindow_) {
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
    if (event->button() == Qt::LeftButton && drawingRoi_) {
        drawingRoi_ = false;
        roiDragCurrentImage_ = clampedImagePoint(event->position());
        const QRect newRoi = normalizedRoiRect(currentDragImageRect());
        if (newRoi.isValid() && !newRoi.isEmpty()) {
            setRoiRectInternal(newRoi);
        } else {
            setRoiRectInternal({});
        }
        updateCursor();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && panning_) {
        panning_ = false;
        updateCursor();
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

QPointF ImageViewport::viewportCenter() const
{
    return QPointF(width() / 2.0, height() / 2.0);
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

QPointF ImageViewport::clampedImagePoint(const QPointF &widgetPoint) const
{
    if (image_.isNull()) {
        return {};
    }

    const QPointF imagePoint = widgetToImage(widgetPoint);
    return QPointF(std::clamp(imagePoint.x(), 0.0, static_cast<double>(image_.width())),
                   std::clamp(imagePoint.y(), 0.0, static_cast<double>(image_.height())));
}

QRectF ImageViewport::currentDragImageRect() const
{
    return QRectF(roiDragStartImage_, roiDragCurrentImage_).normalized();
}

QRectF ImageViewport::widgetRectForImageRect(const QRectF &imageRect) const
{
    const QPointF topLeft = imageToWidget(imageRect.topLeft());
    const QPointF bottomRight = imageToWidget(QPointF(imageRect.x() + imageRect.width(),
                                                      imageRect.y() + imageRect.height()));
    return QRectF(topLeft, bottomRight).normalized();
}

QRect ImageViewport::normalizedRoiRect(const QRectF &imageRect) const
{
    if (image_.isNull()) {
        return {};
    }

    const QRectF normalized = imageRect.normalized();
    const int left = std::clamp(static_cast<int>(std::floor(normalized.left())), 0, image_.width());
    const int top = std::clamp(static_cast<int>(std::floor(normalized.top())), 0, image_.height());
    const int right = std::clamp(static_cast<int>(std::ceil(normalized.right())), 0, image_.width());
    const int bottom = std::clamp(static_cast<int>(std::ceil(normalized.bottom())), 0, image_.height());
    const QRect normalizedRect(left, top, std::max(0, right - left), std::max(0, bottom - top));
    if (normalizedRect.width() < kMinRoiDimension || normalizedRect.height() < kMinRoiDimension) {
        return {};
    }
    return normalizedRect;
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

void ImageViewport::setRoiRectInternal(const QRect &roiRect)
{
    const QRect normalized = roiRect.normalized();
    const QRect bounded = image_.isNull()
                              ? QRect()
                              : normalized.intersected(QRect(0, 0, image_.width(), image_.height()));
    const QRect nextRect = (bounded.isValid() && !bounded.isEmpty()) ? bounded : QRect();
    const bool hadRoi = hasRoi();
    const bool nextHasRoi = nextRect.isValid() && !nextRect.isEmpty();
    if (roiRect_ == nextRect && hadRoi == nextHasRoi) {
        return;
    }

    roiRect_ = nextRect;
    emit roiChanged(roiRect_);
    if (hadRoi != nextHasRoi) {
        emit roiPresenceChanged(nextHasRoi);
    }
    update();
}

void ImageViewport::updateCursor()
{
    if (panning_) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (interactionMode_ == InteractionMode::DrawRoi && hasImage()) {
        setCursor(Qt::CrossCursor);
        return;
    }

    unsetCursor();
}
