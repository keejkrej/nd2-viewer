#pragma once

#include "core/documenttypes.h"
#include "ui/volumeviewport3d_backend.h"

#include <QQuickVTKItem.h>

class QmlDocumentController;

class QuickVolumeViewport3D : public QQuickVTKItem
{
    Q_OBJECT
    Q_PROPERTY(QmlDocumentController *controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY summaryChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

public:
    explicit QuickVolumeViewport3D(QQuickItem *parent = nullptr);

    [[nodiscard]] QmlDocumentController *controller() const;
    void setController(QmlDocumentController *controller);
    [[nodiscard]] QString summary() const;
    [[nodiscard]] QString errorText() const;

    Q_INVOKABLE void resetView();
    Q_INVOKABLE void fitToVolume();

    vtkUserData initializeVTK(vtkRenderWindow *renderWindow) override;
    void destroyingVTK(vtkRenderWindow *renderWindow, vtkUserData userData) override;

signals:
    void controllerChanged();
    void summaryChanged();
    void errorTextChanged();

private:
    void syncVolumeFromController();
    void syncChannelsFromController();
    void setSummary(const QString &summary);
    void setErrorText(const QString &text);

    QmlDocumentController *controller_ = nullptr;
    RawVolume volume_;
    QVector<ChannelRenderSettings> channelSettings_;
    QString summary_;
    QString errorText_;
};
