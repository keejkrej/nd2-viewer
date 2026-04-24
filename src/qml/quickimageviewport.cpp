#include "qml/quickimageviewport.h"

#include "qml/qmldocumentcontroller.h"

#include <QCursor>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QWheelEvent>

#include <algorithm>

QuickImageViewport::QuickImageViewport(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setAntialiasing(false);
}

QmlDocumentController *QuickImageViewport::controller() const { return controller_; }

void QuickImageViewport::setController(QmlDocumentController *controller)
{
    if (controller_ == controller) {
        return;
    }
    if (controller_) {
        disconnect(controller_, nullptr, this, nullptr);
    }
    controller_ = controller;
    if (controller_) {
        connect(controller_, &QmlDocumentController::frameRevisionChanged, this, [this]() { update(); });
        connect(controller_, &QmlDocumentController::deconvolutionStateChanged, this, [this]() { update(); });
        connect(controller_, &QmlDocumentController::fit2DRequested, this, &QuickImageViewport::zoomToFit);
        connect(controller_, &QmlDocumentController::actualSize2DRequested, this, &QuickImageViewport::setActualSize);
        connect(controller_, &QmlDocumentController::roiChanged, this, [this]() { update(); });
        connect(controller_, &QmlDocumentController::documentStateChanged, this, [this]() {
            zoomToFit();
            update();
        });
    }
    emit controllerChanged();
    update();
}

QString QuickImageViewport::imageRole() const { return imageRole_; }

void QuickImageViewport::setImageRole(const QString &role)
{
    if (imageRole_ == role) {
        return;
    }
    imageRole_ = role;
    emit imageRoleChanged();
    zoomToFit();
    update();
}

double QuickImageViewport::zoomFactor() const { return effectiveScale(); }
bool QuickImageViewport::fitToWindow() const { return fitToWindow_; }
bool QuickImageViewport::roiMode() const { return roiMode_; }

void QuickImageViewport::setRoiMode(bool enabled)
{
    if (roiMode_ == enabled) {
        return;
    }
    roiMode_ = enabled;
    emit roiModeChanged();
}

void QuickImageViewport::zoomToFit()
{
    fitToWindow_ = true;
    panOffset_ = {};
    emit zoomChanged();
    update();
}

void QuickImageViewport::setActualSize()
{
    fitToWindow_ = false;
    zoomFactor_ = 1.0;
    clampPan();
    emit zoomChanged();
    update();
}

void QuickImageViewport::clearRoi()
{
    if (controller_) {
        controller_->clearRoi();
    }
}

void QuickImageViewport::paint(QPainter *painter)
{
    painter->fillRect(boundingRect(), QColor(18, 18, 18));
    const QImage &image = activeImage();
    if (image.isNull()) {
        painter->setPen(QColor(163, 163, 163));
        painter->drawText(boundingRect(), Qt::AlignCenter, tr("Open an ND2 or CZI file"));
        return;
    }

    const QRectF target = imageRect();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, effectiveScale() < 1.0);
    painter->drawImage(target, image);

    if (controller_ && imageRole_ == QStringLiteral("frame") && controller_->hasCurrentRoi()) {
        const QRectF roiRect = QRectF(controller_->currentRoi());
        const QRectF itemRoi(imageToItem(roiRect.topLeft()), imageToItem(roiRect.bottomRight()));
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(QPen(QColor(250, 220, 80), 1.5, Qt::DashLine));
        painter->drawRect(itemRoi.normalized());
    }

    if (drawingRoi_) {
        const QRectF itemRoi(imageToItem(QPointF(currentDragRoi_.left(), currentDragRoi_.top())),
                             imageToItem(QPointF(currentDragRoi_.right(), currentDragRoi_.bottom())));
        painter->setPen(QPen(QColor(255, 255, 255), 1.2, Qt::DashLine));
        painter->drawRect(itemRoi.normalized());
    }
}

void QuickImageViewport::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        clampPan();
        update();
    }
}

void QuickImageViewport::wheelEvent(QWheelEvent *event)
{
    if (activeImage().isNull()) {
        return;
    }
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    setZoomFactor(effectiveScale() * factor, event->position());
    event->accept();
}

void QuickImageViewport::mousePressEvent(QMouseEvent *event)
{
    if (activeImage().isNull()) {
        return;
    }
    lastMousePos_ = event->position();
    if (roiMode_ && event->button() == Qt::LeftButton && imageRole_ == QStringLiteral("frame")) {
        drawingRoi_ = true;
        roiStart_ = itemToImage(event->position());
        currentDragRoi_ = QRect(roiStart_.toPoint(), QSize(1, 1));
    } else if (event->button() == Qt::LeftButton) {
        panning_ = true;
    }
    event->accept();
}

void QuickImageViewport::mouseMoveEvent(QMouseEvent *event)
{
    if (panning_) {
        panOffset_ += event->position() - lastMousePos_;
        fitToWindow_ = false;
        lastMousePos_ = event->position();
        clampPan();
        emit zoomChanged();
        update();
    } else if (drawingRoi_) {
        const QRect imageBounds(QPoint(0, 0), activeImage().size());
        currentDragRoi_ = QRect(roiStart_.toPoint(), itemToImage(event->position()).toPoint()).normalized().intersected(imageBounds);
        update();
    }
    updateHover(event->position());
    event->accept();
}

void QuickImageViewport::mouseReleaseEvent(QMouseEvent *event)
{
    if (drawingRoi_ && controller_) {
        drawingRoi_ = false;
        if (currentDragRoi_.width() > 1 && currentDragRoi_.height() > 1) {
            controller_->setRoi(currentDragRoi_.x(), currentDragRoi_.y(), currentDragRoi_.width(), currentDragRoi_.height());
        }
        update();
    }
    panning_ = false;
    event->accept();
}

void QuickImageViewport::hoverMoveEvent(QHoverEvent *event)
{
    updateHover(event->position());
}

void QuickImageViewport::hoverLeaveEvent(QHoverEvent *)
{
    if (controller_) {
        controller_->updateHoveredPixel(0, 0, false);
    }
}

void QuickImageViewport::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        zoomToFit();
        event->accept();
    }
}

const QImage &QuickImageViewport::activeImage() const
{
    if (!controller_) {
        return emptyImage_;
    }
    if (imageRole_ == QStringLiteral("deconvolution")) {
        return controller_->deconvolutionImage();
    }
    return controller_->currentImage();
}

double QuickImageViewport::fittedScale() const
{
    const QImage &image = activeImage();
    if (image.isNull() || width() <= 0.0 || height() <= 0.0) {
        return 1.0;
    }
    return std::min(width() / image.width(), height() / image.height());
}

double QuickImageViewport::effectiveScale() const
{
    return fitToWindow_ ? fittedScale() : zoomFactor_;
}

QRectF QuickImageViewport::imageRect() const
{
    const QImage &image = activeImage();
    if (image.isNull()) {
        return {};
    }
    const QSizeF scaled(image.width() * effectiveScale(), image.height() * effectiveScale());
    const QPointF centered((width() - scaled.width()) * 0.5, (height() - scaled.height()) * 0.5);
    return QRectF(centered + panOffset_, scaled);
}

QPointF QuickImageViewport::itemToImage(const QPointF &point) const
{
    const QRectF rect = imageRect();
    const double scale = effectiveScale();
    if (scale <= 0.0) {
        return {};
    }
    return QPointF((point.x() - rect.left()) / scale, (point.y() - rect.top()) / scale);
}

QPointF QuickImageViewport::imageToItem(const QPointF &point) const
{
    const QRectF rect = imageRect();
    const double scale = effectiveScale();
    return QPointF(rect.left() + point.x() * scale, rect.top() + point.y() * scale);
}

void QuickImageViewport::clampPan()
{
    if (fitToWindow_) {
        panOffset_ = {};
        return;
    }
    const QRectF rect = imageRect();
    if (rect.width() <= width()) {
        panOffset_.setX(0);
    }
    if (rect.height() <= height()) {
        panOffset_.setY(0);
    }
}

void QuickImageViewport::updateHover(const QPointF &point)
{
    if (!controller_ || imageRole_ != QStringLiteral("frame")) {
        return;
    }
    const QPoint pixel = itemToImage(point).toPoint();
    const bool inside = QRect(QPoint(0, 0), activeImage().size()).contains(pixel);
    controller_->updateHoveredPixel(pixel.x(), pixel.y(), inside);
}

void QuickImageViewport::setZoomFactor(double zoomFactor, const QPointF &anchor)
{
    const QPointF imageAnchor = itemToImage(anchor);
    fitToWindow_ = false;
    zoomFactor_ = std::clamp(zoomFactor, 0.02, 64.0);
    const QPointF newAnchor = imageToItem(imageAnchor);
    panOffset_ += anchor - newAnchor;
    clampPan();
    emit zoomChanged();
    update();
}
