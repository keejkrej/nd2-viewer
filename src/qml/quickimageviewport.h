#pragma once

#include <QImage>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QRect>

class QmlDocumentController;

class QuickImageViewport : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QmlDocumentController *controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString imageRole READ imageRole WRITE setImageRole NOTIFY imageRoleChanged)
    Q_PROPERTY(double zoomFactor READ zoomFactor NOTIFY zoomChanged)
    Q_PROPERTY(bool fitToWindow READ fitToWindow NOTIFY zoomChanged)
    Q_PROPERTY(bool roiMode READ roiMode WRITE setRoiMode NOTIFY roiModeChanged)

public:
    explicit QuickImageViewport(QQuickItem *parent = nullptr);

    [[nodiscard]] QmlDocumentController *controller() const;
    void setController(QmlDocumentController *controller);
    [[nodiscard]] QString imageRole() const;
    void setImageRole(const QString &role);
    [[nodiscard]] double zoomFactor() const;
    [[nodiscard]] bool fitToWindow() const;
    [[nodiscard]] bool roiMode() const;
    void setRoiMode(bool enabled);

    Q_INVOKABLE void zoomToFit();
    Q_INVOKABLE void setActualSize();
    Q_INVOKABLE void clearRoi();

    void paint(QPainter *painter) override;

signals:
    void controllerChanged();
    void imageRoleChanged();
    void zoomChanged();
    void roiModeChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    [[nodiscard]] const QImage &activeImage() const;
    [[nodiscard]] double fittedScale() const;
    [[nodiscard]] double effectiveScale() const;
    [[nodiscard]] QRectF imageRect() const;
    [[nodiscard]] QPointF itemToImage(const QPointF &point) const;
    [[nodiscard]] QPointF imageToItem(const QPointF &point) const;
    void clampPan();
    void updateHover(const QPointF &point);
    void setZoomFactor(double zoomFactor, const QPointF &anchor);

    QmlDocumentController *controller_ = nullptr;
    QString imageRole_ = QStringLiteral("frame");
    QImage emptyImage_;
    double zoomFactor_ = 1.0;
    bool fitToWindow_ = true;
    bool roiMode_ = false;
    QPointF panOffset_;
    bool panning_ = false;
    bool drawingRoi_ = false;
    QPointF lastMousePos_;
    QPointF roiStart_;
    QRect currentDragRoi_;
};
