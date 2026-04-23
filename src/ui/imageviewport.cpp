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
constexpr int kOverlayMargin = 12;
constexpr double kScaleBarTargetFraction = 0.2;
constexpr int kScaleBarMinPixels = 48;
constexpr int kScaleBarMaxPixels = 220;
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
    const bool preserveView = !image_.isNull() && !image.isNull();
    const QPointF anchorImagePoint = preserveView ? widgetToImage(viewportCenter()) : QPointF();

    image_ = image;
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
    emit zoomChanged(effectiveScale(), isAtFittedView());

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
    if (image_.isNull()) {
        return;
    }

    zoomFactor_ = fittedScaleForCurrentSize();
    panOffset_ = {};
    update();
    emit zoomChanged(effectiveScale(), isAtFittedView());
}

void ImageViewport::setActualSize()
{
    zoomFactor_ = 1.0;
    panOffset_ = {};
    clampPanOffset();
    update();
    emit zoomChanged(effectiveScale(), isAtFittedView());
}

void ImageViewport::setZoomFactor(double zoomFactor)
{
    zoomFactor_ = std::clamp(zoomFactor, 0.05, 64.0);
    clampPanOffset();
    update();
    emit zoomChanged(effectiveScale(), isAtFittedView());
}

double ImageViewport::zoomFactor() const
{
    return effectiveScale();
}

bool ImageViewport::isFitToWindow() const
{
    return isAtFittedView();
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

void ImageViewport::setOverlayTimestamp(const QString &timestampText)
{
    if (overlayTimestamp_ == timestampText) {
        return;
    }

    overlayTimestamp_ = timestampText;
    update();
}

void ImageViewport::setScaleBarCalibrationMicronsPerPixel(double micronsPerPixel)
{
    const double clamped = std::isfinite(micronsPerPixel) && micronsPerPixel > 0.0 ? micronsPerPixel : 0.0;
    if (qFuzzyCompare(scaleBarMicronsPerPixel_ + 1.0, clamped + 1.0)) {
        return;
    }

    scaleBarMicronsPerPixel_ = clamped;
    update();
}

void ImageViewport::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), QColor(14, 14, 18));

    if (image_.isNull()) {
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

    const bool showScaleBar = scaleBarMicronsPerPixel_ > 0.0;
    const bool showTimestamp = !overlayTimestamp_.trimmed().isEmpty();
    if (!showScaleBar && !showTimestamp) {
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont overlayFont = painter.font();
    overlayFont.setPointSizeF(std::max(8.0, overlayFont.pointSizeF()));
    painter.setFont(overlayFont);
    const QFontMetrics fontMetrics(overlayFont);

    if (showTimestamp) {
        const QString text = overlayTimestamp_.trimmed();
        const QRect textRect = fontMetrics.boundingRect(text);
        const QRect bubble(kOverlayMargin - 6,
                           kOverlayMargin - 3,
                           textRect.width() + 12,
                           textRect.height() + 6);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 145));
        painter.drawRoundedRect(bubble, 4.0, 4.0);
        painter.setPen(QColor(255, 255, 255, 230));
        painter.drawText(QPointF(kOverlayMargin, kOverlayMargin + fontMetrics.ascent()), text);
    }

    if (showScaleBar) {
        const double pixelsPerMicron = effectiveScale() / scaleBarMicronsPerPixel_;
        if (pixelsPerMicron > 0.0) {
            const double targetPx = std::clamp(width() * kScaleBarTargetFraction,
                                               static_cast<double>(kScaleBarMinPixels),
                                               static_cast<double>(kScaleBarMaxPixels));
            const double targetMicrons = targetPx / pixelsPerMicron;
            const double exponent = std::floor(std::log10(std::max(targetMicrons, 1e-9)));
            const double base = std::pow(10.0, exponent);
            const double normalized = targetMicrons / base;
            const double snapped = normalized < 1.5 ? 1.0 : (normalized < 3.5 ? 2.0 : (normalized < 7.5 ? 5.0 : 10.0));
            const double scaleMicrons = snapped * base;
            const double scalePixels = scaleMicrons * pixelsPerMicron;

            if (scalePixels >= 1.0) {
                const int x0 = kOverlayMargin;
                const int y0 = height() - kOverlayMargin;
                const int x1 = x0 + static_cast<int>(std::round(scalePixels));

                const QString label = scaleBarLabelForMicrons(scaleMicrons);
                const QRect labelRect = fontMetrics.boundingRect(label);
                const int bubbleHeight = labelRect.height() + 28;
                const int bubbleWidth = std::max(labelRect.width() + 16, x1 - x0 + 16);
                const QRect bubble(x0 - 8, y0 - bubbleHeight - 4, bubbleWidth, bubbleHeight);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0, 0, 0, 145));
                painter.drawRoundedRect(bubble, 4.0, 4.0);

                painter.setPen(QPen(QColor(255, 255, 255, 235), 3.0, Qt::SolidLine, Qt::SquareCap));
                painter.drawLine(QPointF(x0, y0 - 12), QPointF(x1, y0 - 12));
                painter.setPen(QPen(QColor(255, 255, 255, 235), 2.0));
                painter.drawLine(QPointF(x0, y0 - 17), QPointF(x0, y0 - 7));
                painter.drawLine(QPointF(x1, y0 - 17), QPointF(x1, y0 - 7));
                painter.setPen(QColor(255, 255, 255, 230));
                painter.drawText(QPointF(x0, y0 - 20), label);
            }
        }
    }
}

void ImageViewport::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    clampPanOffset();
    emit zoomChanged(effectiveScale(), isAtFittedView());
}

void ImageViewport::wheelEvent(QWheelEvent *event)
{
    if (image_.isNull()) {
        event->ignore();
        return;
    }

    const QPointF anchorImagePoint = widgetToImage(event->position());
    const double direction = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;

    zoomFactor_ = std::clamp(zoomFactor_ * direction, 0.05, 64.0);

    const QPointF newWidgetPoint = imageToWidget(anchorImagePoint);
    panOffset_ += event->position() - newWidgetPoint;
    clampPanOffset();
    update();
    emit zoomChanged(effectiveScale(), isAtFittedView());
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
    } else if (panning_) {
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

    if (isAtFittedView()) {
        setActualSize();
    } else {
        zoomToFit();
    }
}

double ImageViewport::fittedScaleForCurrentSize() const
{
    if (image_.isNull()) {
        return 1.0;
    }

    const double widthScale = static_cast<double>(width()) / static_cast<double>(image_.width());
    const double heightScale = static_cast<double>(height()) / static_cast<double>(image_.height());
    return std::min(widthScale, heightScale);
}

bool ImageViewport::isAtFittedView() const
{
    if (image_.isNull()) {
        return false;
    }

    const double fittedScale = fittedScaleForCurrentSize();
    const double scaleTolerance = std::max(1.0e-6, fittedScale * 1.0e-6);
    const bool scaleMatches = std::abs(zoomFactor_ - fittedScale) <= scaleTolerance;
    const bool panMatches = std::abs(panOffset_.x()) <= 0.5 && std::abs(panOffset_.y()) <= 0.5;
    return scaleMatches && panMatches;
}

double ImageViewport::effectiveScale() const
{
    if (image_.isNull()) {
        return 1.0;
    }
    return zoomFactor_;
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
    if (image_.isNull()) {
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

QString ImageViewport::scaleBarLabelForMicrons(double microns) const
{
    if (microns >= 1000.0) {
        return QStringLiteral("%1 mm").arg(QString::number(microns / 1000.0, 'g', 3));
    }

    if (microns < 1.0) {
        return QStringLiteral("%1 nm").arg(QString::number(microns * 1000.0, 'g', 3));
    }

    return QStringLiteral("%1 µm").arg(QString::number(microns, 'g', 3));
}
