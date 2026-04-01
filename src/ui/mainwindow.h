#pragma once

#include "core/documentcontroller.h"

#include <QMainWindow>

class ChannelControlsWidget;
class ImageViewport;
class QLabel;
class QPlainTextEdit;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTextBrowser;
class QVBoxLayout;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void openFile();
    void saveCurrentFrameAs();
    void updateDocumentUi();
    void updateCoordinateUi();
    void updateChannelUi();
    void updateFrameUi();
    void updateMetadataUi();
    void showErrorMessage(const QString &message);
    void updateBusyState(bool busy);
    void updateStatusMessage(const QString &message);
    void updateHoveredPixel(const QPoint &pixelPosition, bool insideImage);
    void updateZoomLabel(double zoomFactor, bool fitToWindow);

private:
    enum class ExportMode
    {
        Cancelled,
        PreviewPng,
        AnalysisTiffs,
        Bundle
    };

    struct ExportBundleResult
    {
        bool previewRequested = false;
        bool previewSaved = false;
        QString previewPath;
        QStringList channelPaths;
        QStringList failures;
    };

    struct LoopWidgets
    {
        QWidget *row = nullptr;
        QLabel *label = nullptr;
        QSlider *slider = nullptr;
        QSpinBox *spinBox = nullptr;
        QLabel *details = nullptr;
        bool lazySliderCommit = false;
    };

    struct MetadataWidgets
    {
        QTextBrowser *summary = nullptr;
        QPlainTextEdit *raw = nullptr;
    };

    void buildMenus();
    void buildCentralUi();
    void buildDockUi();
    void rebuildNavigatorControls();
    MetadataWidgets addMetadataTab(const QString &title);
    void setMetadataContent(const MetadataWidgets &widgets, const QString &summaryHtml, const QString &rawText);
    [[nodiscard]] ExportMode promptForExportMode() const;
    [[nodiscard]] ExportBundleResult exportCurrentFrame(const QString &selectedPath, ExportMode mode) const;
    [[nodiscard]] bool writeChannelTiff(const QString &path,
                                        const RawFrame &frame,
                                        int channelIndex,
                                        QString *errorMessage) const;
    [[nodiscard]] QString buildDefaultFrameSavePath(const QString &extension = QStringLiteral(".png")) const;
    [[nodiscard]] QString sanitizeToken(const QString &value) const;
    void updateWindowTitle();
    void updateInfoLabel();

    DocumentController controller_;
    ImageViewport *imageViewport_ = nullptr;
    QWidget *navigatorContainer_ = nullptr;
    QVBoxLayout *navigatorRowsLayout_ = nullptr;
    QLabel *navigatorEmptyLabel_ = nullptr;
    QVector<LoopWidgets> loopControls_;
    ChannelControlsWidget *channelControlsWidget_ = nullptr;
    QTabWidget *metadataTabs_ = nullptr;
    MetadataWidgets attributesWidgets_;
    MetadataWidgets experimentWidgets_;
    MetadataWidgets metadataWidgets_;
    MetadataWidgets textInfoWidgets_;
    QLabel *infoStatusLabel_ = nullptr;
    QLabel *zoomStatusLabel_ = nullptr;
    QLabel *pixelStatusLabel_ = nullptr;
};
