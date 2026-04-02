#include "ui/mainwindow.h"

#include "ui/channelcontrolswidget.h"
#include "ui/imageviewport.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QLineEdit>
#include <QMenuBar>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoFrameFormat>
#include <QVideoFrameInput>

#include <tiffio.h>

#include <algorithm>
#include <cstring>

namespace
{
QString prettyJson(const QJsonDocument &document)
{
    if (document.isNull()) {
        return QStringLiteral("null");
    }
    return QString::fromUtf8(document.toJson(QJsonDocument::Indented));
}

QJsonValue documentValue(const QJsonDocument &document)
{
    if (document.isArray()) {
        return QJsonValue(document.array());
    }
    if (document.isObject()) {
        return QJsonValue(document.object());
    }
    return QJsonValue(QJsonValue::Null);
}

QString scalarToDisplayString(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 15);
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isNull()) {
        return QStringLiteral("null");
    }
    if (value.isUndefined()) {
        return QStringLiteral("undefined");
    }
    return QString();
}

QString containerSummary(const QJsonValue &value)
{
    if (value.isObject()) {
        return QStringLiteral("{%1 field%2}")
            .arg(value.toObject().size())
            .arg(value.toObject().size() == 1 ? QString() : QStringLiteral("s"));
    }
    if (value.isArray()) {
        return QStringLiteral("[%1 item%2]")
            .arg(value.toArray().size())
            .arg(value.toArray().size() == 1 ? QString() : QStringLiteral("s"));
    }
    return scalarToDisplayString(value);
}

void populateJsonTreeItem(QTreeWidgetItem *parent, const QString &key, const QJsonValue &value)
{
    auto *item = new QTreeWidgetItem(parent);
    item->setText(0, key);
    item->setText(1, containerSummary(value));

    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (object.isEmpty()) {
            auto *emptyItem = new QTreeWidgetItem(item);
            emptyItem->setText(0, QStringLiteral("(empty object)"));
            emptyItem->setText(1, QStringLiteral("{}"));
            return;
        }
        for (auto it = object.begin(); it != object.end(); ++it) {
            populateJsonTreeItem(item, it.key(), it.value());
        }
        return;
    }

    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        if (array.isEmpty()) {
            auto *emptyItem = new QTreeWidgetItem(item);
            emptyItem->setText(0, QStringLiteral("(empty array)"));
            emptyItem->setText(1, QStringLiteral("[]"));
            return;
        }
        for (int index = 0; index < array.size(); ++index) {
            populateJsonTreeItem(item, QStringLiteral("[%1]").arg(index), array.at(index));
        }
    }
}

void populateJsonTree(QTreeWidget *tree, const QJsonValue &value)
{
    tree->clear();

    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (object.isEmpty()) {
            auto *item = new QTreeWidgetItem(tree);
            item->setText(0, QStringLiteral("(empty object)"));
            item->setText(1, QStringLiteral("{}"));
        } else {
            for (auto it = object.begin(); it != object.end(); ++it) {
                populateJsonTreeItem(tree->invisibleRootItem(), it.key(), it.value());
            }
        }
    } else if (value.isArray()) {
        const QJsonArray array = value.toArray();
        if (array.isEmpty()) {
            auto *item = new QTreeWidgetItem(tree);
            item->setText(0, QStringLiteral("(empty array)"));
            item->setText(1, QStringLiteral("[]"));
        } else {
            for (int index = 0; index < array.size(); ++index) {
                populateJsonTreeItem(tree->invisibleRootItem(), QStringLiteral("[%1]").arg(index), array.at(index));
            }
        }
    } else {
        auto *item = new QTreeWidgetItem(tree);
        item->setText(0, QStringLiteral("value"));
        item->setText(1, scalarToDisplayString(value));
    }

    tree->collapseAll();
    tree->expandToDepth(0);
}

void addOverviewTreeRow(QTreeWidget *tree, const QString &key, const QString &value)
{
    auto *item = new QTreeWidgetItem(tree);
    item->setText(0, key);
    item->setText(1, value);
}

QLabel *createSectionTitle(const QString &title, QWidget *parent)
{
    auto *label = new QLabel(title, parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

void clearLayout(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        if (QLayout *childLayout = item->layout()) {
            clearLayout(childLayout);
            delete childLayout;
        }
        delete item;
    }
}

QString formatDurationLabel(double seconds)
{
    int remainingSeconds = qMax(0, qRound(seconds));
    const int hours = remainingSeconds / 3600;
    remainingSeconds %= 3600;
    const int minutes = remainingSeconds / 60;
    const int wholeSeconds = remainingSeconds % 60;

    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(wholeSeconds, 2, 10, QLatin1Char('0'));
    }

    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(wholeSeconds, 2, 10, QLatin1Char('0'));
}

class MovieExportDialog : public QDialog
{
public:
    MovieExportDialog(const MovieExportSettings &baseSettings,
                      const QImage &sampleImage,
                      int timeLoopSize,
                      int currentTimeFrame,
                      const QString &defaultPath,
                      bool exportRoi,
                      QWidget *parent = nullptr)
        : QDialog(parent)
        , baseSettings_(baseSettings)
        , sampleImage_(sampleImage)
        , timeLoopSize_(timeLoopSize)
    {
        setWindowTitle(exportRoi ? tr("Export ROI Movie") : tr("Export Movie"));
        setModal(true);
        resize(560, 0);

        auto *layout = new QVBoxLayout(this);
        auto *intro = new QLabel(exportRoi
                                     ? tr("Export the current ROI as a rendered MP4. Start/end/skip apply only to the time axis using 0-based frame numbers; all other loop coordinates stay fixed.")
                                     : tr("Export the current rendered frame view as an MP4. Start/end/skip apply only to the time axis using 0-based frame numbers; all other loop coordinates stay fixed."),
                                 this);
        intro->setWordWrap(true);
        layout->addWidget(intro);

        auto *formLayout = new QFormLayout();

        auto *pathRow = new QWidget(this);
        auto *pathLayout = new QHBoxLayout(pathRow);
        pathLayout->setContentsMargins(0, 0, 0, 0);
        outputPathEdit_ = new QLineEdit(defaultPath, pathRow);
        auto *browseButton = new QToolButton(pathRow);
        browseButton->setText(tr("..."));
        pathLayout->addWidget(outputPathEdit_, 1);
        pathLayout->addWidget(browseButton);
        formLayout->addRow(tr("Output"), pathRow);

        startFrameSpin_ = new QSpinBox(this);
        startFrameSpin_->setRange(0, qMax(timeLoopSize_ - 1, 0));
        startFrameSpin_->setValue(std::clamp(currentTimeFrame, 0, qMax(timeLoopSize_ - 1, 0)));
        formLayout->addRow(tr("Start frame (0-based)"), startFrameSpin_);

        endFrameSpin_ = new QSpinBox(this);
        endFrameSpin_->setRange(0, qMax(timeLoopSize_ - 1, 0));
        endFrameSpin_->setValue(qMax(timeLoopSize_ - 1, 0));
        formLayout->addRow(tr("End frame (0-based)"), endFrameSpin_);

        skipFramesSpin_ = new QSpinBox(this);
        skipFramesSpin_->setRange(0, qMax(timeLoopSize_ - 1, 0));
        skipFramesSpin_->setValue(0);
        formLayout->addRow(tr("Skip frames"), skipFramesSpin_);

        fpsSpin_ = new QDoubleSpinBox(this);
        fpsSpin_->setRange(0.1, 240.0);
        fpsSpin_->setDecimals(1);
        fpsSpin_->setSingleStep(1.0);
        fpsSpin_->setValue(baseSettings_.fps);
        formLayout->addRow(tr("FPS"), fpsSpin_);

        outputSizeLabel_ = new QLabel(this);
        frameCountLabel_ = new QLabel(this);
        durationLabel_ = new QLabel(this);
        estimatedSizeLabel_ = new QLabel(this);
        formLayout->addRow(tr("Output size"), outputSizeLabel_);
        formLayout->addRow(tr("Frames"), frameCountLabel_);
        formLayout->addRow(tr("Duration"), durationLabel_);
        formLayout->addRow(tr("Approx. file size"), estimatedSizeLabel_);

        layout->addLayout(formLayout);

        warningLabel_ = new QLabel(this);
        warningLabel_->setWordWrap(true);
        warningLabel_->setStyleSheet(QStringLiteral("color: #c23b3b;"));
        layout->addWidget(warningLabel_);

        auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        continueButton_ = buttonBox->button(QDialogButtonBox::Ok);
        continueButton_->setText(tr("Continue"));
        connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttonBox);

        estimateDebounceTimer_ = new QTimer(this);
        estimateDebounceTimer_->setSingleShot(true);
        estimateDebounceTimer_->setInterval(150);
        connect(estimateDebounceTimer_, &QTimer::timeout, this, [this]() { refreshEstimate(); });

        connect(browseButton, &QToolButton::clicked, this, [this]() {
            const QString selectedPath = QFileDialog::getSaveFileName(this,
                                                                      tr("Save Movie"),
                                                                      currentSettings().outputPath,
                                                                      tr("MP4 Video (*.mp4)"));
            if (!selectedPath.isEmpty()) {
                outputPathEdit_->setText(selectedPath);
            }
        });
        connect(outputPathEdit_, &QLineEdit::textChanged, this, [this]() { updateContinueEnabled(); });
        connect(startFrameSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
            if (endFrameSpin_->value() < value) {
                endFrameSpin_->setValue(value);
            }
            scheduleEstimateRefresh();
        });
        connect(endFrameSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
            if (startFrameSpin_->value() > value) {
                startFrameSpin_->setValue(value);
            }
            scheduleEstimateRefresh();
        });
        connect(skipFramesSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this]() { scheduleEstimateRefresh(); });
        connect(fpsSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this]() { scheduleEstimateRefresh(); });

        refreshEstimate();
    }

    [[nodiscard]] MovieExportSettings currentSettings() const
    {
        MovieExportSettings settings = baseSettings_;
        settings.startFrame = startFrameSpin_->value();
        settings.endFrame = endFrameSpin_->value();
        settings.skipFrames = skipFramesSpin_->value();
        settings.fps = fpsSpin_->value();
        QString outputPath = outputPathEdit_->text().trimmed();
        if (!outputPath.isEmpty()) {
            const QFileInfo outputInfo(outputPath);
            if (outputInfo.suffix().isEmpty()) {
                outputPath += QStringLiteral(".mp4");
            } else if (outputInfo.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) != 0) {
                outputPath = outputInfo.dir().filePath(outputInfo.completeBaseName() + QStringLiteral(".mp4"));
            }
        }
        settings.outputPath = outputPath;
        return settings;
    }

    [[nodiscard]] MovieExportEstimate currentEstimate() const
    {
        return currentEstimate_;
    }

private:
    void refreshEstimate()
    {
        const MovieExportSettings settings = currentSettings();
        currentEstimate_ = estimateMovieExport(settings, sampleImage_);

        outputSizeLabel_->setText(settings.outputSize.isValid()
                                      ? tr("%1 × %2").arg(settings.outputSize.width()).arg(settings.outputSize.height())
                                      : tr("Unavailable"));

        if (currentEstimate_.valid) {
            frameCountLabel_->setText(QString::number(currentEstimate_.frameCount));
            durationLabel_->setText(formatDurationLabel(currentEstimate_.durationSeconds));
            estimatedSizeLabel_->setText(QLocale().formattedDataSize(currentEstimate_.estimatedBytes));
            warningLabel_->clear();
        } else {
            frameCountLabel_->setText(tr("Unavailable"));
            durationLabel_->setText(tr("Unavailable"));
            estimatedSizeLabel_->setText(tr("Unavailable"));
            warningLabel_->setText(currentEstimate_.errorMessage);
        }

        updateContinueEnabled();
    }

    void scheduleEstimateRefresh()
    {
        if (estimateDebounceTimer_) {
            estimateDebounceTimer_->start();
        }
    }

    void updateContinueEnabled()
    {
        continueButton_->setEnabled(currentEstimate_.valid && !currentSettings().outputPath.isEmpty());
    }

    MovieExportSettings baseSettings_;
    QImage sampleImage_;
    int timeLoopSize_ = 0;
    QLineEdit *outputPathEdit_ = nullptr;
    QSpinBox *startFrameSpin_ = nullptr;
    QSpinBox *endFrameSpin_ = nullptr;
    QSpinBox *skipFramesSpin_ = nullptr;
    QDoubleSpinBox *fpsSpin_ = nullptr;
    QLabel *outputSizeLabel_ = nullptr;
    QLabel *frameCountLabel_ = nullptr;
    QLabel *durationLabel_ = nullptr;
    QLabel *estimatedSizeLabel_ = nullptr;
    QLabel *warningLabel_ = nullptr;
    QPushButton *continueButton_ = nullptr;
    QTimer *estimateDebounceTimer_ = nullptr;
    MovieExportEstimate currentEstimate_;
};
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("nd2-viewer"));
    resize(1440, 920);

    buildCentralUi();
    buildMenus();

    infoStatusLabel_ = new QLabel(this);
    zoomStatusLabel_ = new QLabel(tr("Fit"), this);
    pixelStatusLabel_ = new QLabel(this);
    statusBar()->addWidget(infoStatusLabel_, 1);
    statusBar()->addPermanentWidget(zoomStatusLabel_);
    statusBar()->addPermanentWidget(pixelStatusLabel_, 1);

    connect(&controller_, &DocumentController::documentChanged, this, &MainWindow::updateDocumentUi);
    connect(&controller_, &DocumentController::coordinateStateChanged, this, &MainWindow::updateCoordinateUi);
    connect(&controller_, &DocumentController::channelSettingsChanged, this, &MainWindow::updateChannelUi);
    connect(&controller_, &DocumentController::frameReady, this, &MainWindow::updateFrameUi);
    connect(&controller_, &DocumentController::metadataChanged, this, &MainWindow::updateMetadataUi);
    connect(&controller_, &DocumentController::errorOccurred, this, &MainWindow::showErrorMessage);
    connect(&controller_, &DocumentController::busyChanged, this, &MainWindow::updateBusyState);
    connect(&controller_, &DocumentController::statusTextChanged, this, &MainWindow::updateStatusMessage);

    connect(imageViewport_, &ImageViewport::hoveredPixelChanged, this, &MainWindow::updateHoveredPixel);
    connect(imageViewport_, &ImageViewport::zoomChanged, this, &MainWindow::updateZoomLabel);
    connect(imageViewport_, &ImageViewport::saveImageRequested, this, &MainWindow::saveCurrentFrameAs);
    connect(imageViewport_, &ImageViewport::exportRoiRequested, this, &MainWindow::saveCurrentRoiAs);
    connect(imageViewport_, &ImageViewport::exportMovieRequested, this, &MainWindow::exportMovieAs);
    connect(imageViewport_, &ImageViewport::exportRoiMovieRequested, this, &MainWindow::exportRoiMovieAs);

    connect(channelControlsWidget_, &ChannelControlsWidget::channelSettingsChanged,
            &controller_, &DocumentController::setChannelSettings);
    connect(channelControlsWidget_, &ChannelControlsWidget::autoContrastRequested,
            &controller_, &DocumentController::autoContrastChannel);
    connect(channelControlsWidget_, &ChannelControlsWidget::autoContrastAllRequested,
            &controller_, &DocumentController::autoContrastAllChannels);

    updateDocumentUi();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (movieExportInProgress_) {
        QWidget *widget = qobject_cast<QWidget *>(watched);
        const bool belongsToWindow = widget && (widget == this || isAncestorOf(widget));
        if (belongsToWindow) {
            switch (event->type()) {
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonRelease:
            case QEvent::MouseButtonDblClick:
            case QEvent::MouseMove:
            case QEvent::Wheel:
            case QEvent::KeyPress:
            case QEvent::KeyRelease:
            case QEvent::Shortcut:
            case QEvent::ShortcutOverride:
            case QEvent::ContextMenu:
            case QEvent::TouchBegin:
            case QEvent::TouchUpdate:
            case QEvent::TouchEnd:
                return true;
            default:
                break;
            }
        }
    }

    bool shouldCommit = false;
    for (int index = 0; index < loopControls_.size(); ++index) {
        const auto &loopWidgets = loopControls_.at(index);
        if (watched != loopWidgets.slider) {
            continue;
        }

        switch (event->type()) {
        case QEvent::MouseButtonRelease:
        case QEvent::KeyRelease:
        case QEvent::Wheel:
            shouldCommit = true;
            break;
        default:
            break;
        }

        const bool handled = QMainWindow::eventFilter(watched, event);
        if (shouldCommit) {
            commitLoopSliderValue(index);
        }
        return handled;
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (movieExportInProgress_) {
        event->ignore();
        statusBar()->showMessage(tr("Wait for the movie export to finish before closing the window."), 5000);
        return;
    }

    QMainWindow::closeEvent(event);
}

void MainWindow::openFile()
{
    if (movieExportInProgress_) {
        return;
    }

    const QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("Open ND2 File"),
        QString(),
        tr("Nikon ND2 Files (*.nd2);;TIFF Files (*.tif *.tiff);;All Files (*.*)")
    );
    if (fileName.isEmpty()) {
        return;
    }

    controller_.openFile(fileName);
}

void MainWindow::saveCurrentFrameAs()
{
    exportCurrentSelection(ExportScope::Frame);
}

void MainWindow::saveCurrentRoiAs()
{
    exportCurrentSelection(ExportScope::Roi);
}

void MainWindow::exportMovieAs()
{
    exportMovieSelection(ExportScope::Frame);
}

void MainWindow::exportRoiMovieAs()
{
    exportMovieSelection(ExportScope::Roi);
}

void MainWindow::exportCurrentSelection(ExportScope scope)
{
    if (movieExportInProgress_) {
        return;
    }

    const QImage currentImage = controller_.renderedFrame().image;
    const RawFrame &rawFrame = controller_.currentRawFrame();
    if (currentImage.isNull() || !rawFrame.isValid()) {
        return;
    }
    if (scope == ExportScope::Roi && !imageViewport_->hasRoi()) {
        return;
    }

    const ExportMode mode = promptForExportMode(scope);
    if (mode == ExportMode::Cancelled) {
        return;
    }

    QString selectedPath;
    QString dialogTitle;
    QString dialogFilter;
    const QString scopeLabel = scope == ExportScope::Roi ? tr("ROI") : tr("Frame");

    switch (mode) {
    case ExportMode::PreviewPng:
        dialogTitle = tr("Save Rendered %1 Preview").arg(scopeLabel);
        dialogFilter = tr("PNG Image (*.png)");
        selectedPath = QFileDialog::getSaveFileName(this, dialogTitle, buildDefaultFrameSavePath(scope, QStringLiteral(".png")), dialogFilter);
        break;
    case ExportMode::AnalysisTiffs:
        dialogTitle = tr("Choose Base Name for %1 Analysis TIFFs").arg(scopeLabel);
        dialogFilter = tr("TIFF Image (*.tif)");
        selectedPath = QFileDialog::getSaveFileName(this, dialogTitle, buildDefaultFrameSavePath(scope, QStringLiteral(".tif")), dialogFilter);
        break;
    case ExportMode::Bundle:
        dialogTitle = tr("Save Rendered %1 Preview and Channel TIFFs").arg(scopeLabel);
        dialogFilter = tr("PNG Image (*.png)");
        selectedPath = QFileDialog::getSaveFileName(this, dialogTitle, buildDefaultFrameSavePath(scope, QStringLiteral(".png")), dialogFilter);
        break;
    case ExportMode::Cancelled:
        break;
    }

    if (selectedPath.isEmpty()) {
        return;
    }

    QFileInfo targetInfo(selectedPath);
    if (targetInfo.suffix().isEmpty()) {
        selectedPath += (mode == ExportMode::AnalysisTiffs) ? QStringLiteral(".tif") : QStringLiteral(".png");
        targetInfo = QFileInfo(selectedPath);
    }

    const ExportBundleResult exportResult = exportCurrentFrame(selectedPath, mode, scope);
    if (exportResult.previewRequested && !exportResult.previewSaved) {
        QMessageBox::warning(this,
                             tr("Export Failed"),
                             tr("Could not save the rendered %1 preview to:\n%2")
                                 .arg(scope == ExportScope::Roi ? tr("ROI") : tr("frame"),
                                      QDir::toNativeSeparators(selectedPath)));
        return;
    }

    if (!exportResult.failures.isEmpty()) {
        QMessageBox::warning(this,
                             tr("Export Partially Failed"),
                             tr("Some %1 export files could not be saved:\n\n%2")
                                 .arg(scope == ExportScope::Roi ? tr("ROI") : tr("frame"))
                                 .arg(exportResult.failures.join(QStringLiteral("\n"))));
    }

    QString statusMessage;
    switch (mode) {
    case ExportMode::PreviewPng:
        statusMessage = scope == ExportScope::Roi ? tr("Exported ROI preview PNG")
                                                  : tr("Exported rendered preview PNG");
        break;
    case ExportMode::AnalysisTiffs:
        statusMessage = scope == ExportScope::Roi ? tr("Exported %1 ROI channel TIFF(s)").arg(exportResult.channelPaths.size())
                                                  : tr("Exported %1 channel TIFF(s)").arg(exportResult.channelPaths.size());
        break;
    case ExportMode::Bundle:
        statusMessage = scope == ExportScope::Roi ? tr("Exported ROI preview PNG and %1 channel TIFF(s)").arg(exportResult.channelPaths.size())
                                                  : tr("Exported preview PNG and %1 channel TIFF(s)").arg(exportResult.channelPaths.size());
        break;
    case ExportMode::Cancelled:
        break;
    }

    if (!statusMessage.isEmpty()) {
        statusBar()->showMessage(statusMessage, 5000);
    }
}

void MainWindow::exportMovieSelection(ExportScope scope)
{
    if (movieExportInProgress_) {
        return;
    }

    const QImage currentImage = controller_.renderedFrame().image;
    const RawFrame &rawFrame = controller_.currentRawFrame();
    if (currentImage.isNull() || !rawFrame.isValid()) {
        return;
    }
    if (scope == ExportScope::Roi && !imageViewport_->hasRoi()) {
        return;
    }

    const int timeLoopIndex = findTimeLoopIndex();
    if (timeLoopIndex < 0) {
        QMessageBox::information(this,
                                 tr("Movie Export Unavailable"),
                                 tr("This file does not expose a time loop, so movie export is unavailable."));
        return;
    }

    MovieExportSettings settings;
    settings.nd2Path = controller_.currentPath();
    settings.fixedCoordinates = controller_.coordinateState().values;
    settings.channelSettings = controller_.channelSettings();
    settings.timeLoopIndex = timeLoopIndex;
    settings.outputSize = scope == ExportScope::Roi && imageViewport_->hasRoi()
                              ? imageViewport_->roiRect().intersected(QRect(0, 0, rawFrame.width, rawFrame.height)).size()
                              : QSize(rawFrame.width, rawFrame.height);
    settings.roiRect = scope == ExportScope::Roi ? imageViewport_->roiRect().intersected(QRect(0, 0, rawFrame.width, rawFrame.height))
                                                 : QRect();
    settings.fps = 10.0;
    const QImage sampleImage = (scope == ExportScope::Roi && settings.roiRect.isValid() && !settings.roiRect.isEmpty())
                                   ? currentImage.copy(settings.roiRect)
                                   : currentImage;

    const Nd2LoopInfo &timeLoop = controller_.documentInfo().loops.at(timeLoopIndex);
    const int currentTimeFrame = (timeLoopIndex < controller_.coordinateState().values.size())
                                     ? controller_.coordinateState().values.at(timeLoopIndex)
                                     : 0;
    MovieExportDialog dialog(settings,
                             sampleImage,
                             timeLoop.size,
                             currentTimeFrame,
                             buildDefaultMovieSavePath(scope),
                             scope == ExportScope::Roi,
                             this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    settings = dialog.currentSettings();
    const MovieExportEstimate estimate = dialog.currentEstimate();
    if (!estimate.valid) {
        QMessageBox::warning(this,
                             tr("Movie Export Failed"),
                             estimate.errorMessage.isEmpty()
                                 ? tr("The movie export settings are not valid.")
                                 : estimate.errorMessage);
        return;
    }

    startMovieExportPlayback(settings);
}

void MainWindow::updateDocumentUi()
{
    imageViewport_->clearRoi();
    imageViewport_->setImage(controller_.renderedFrame().image);
    rebuildNavigatorControls();
    channelControlsWidget_->setChannels(controller_.documentInfo().channels, controller_.channelSettings());
    updateCoordinateUi();
    updateChannelUi();
    updateStaticMetadataUi();
    updateFrameMetadataUi();
    updateWindowTitle();
    updateInfoLabel();
}

void MainWindow::updateCoordinateUi()
{
    const FrameCoordinateState &state = controller_.coordinateState();
    for (int index = 0; index < loopControls_.size() && index < state.values.size(); ++index) {
        const QSignalBlocker sliderBlocker(loopControls_[index].slider);
        const QSignalBlocker spinBlocker(loopControls_[index].spinBox);
        loopControls_[index].slider->setValue(state.values.at(index));
        loopControls_[index].spinBox->setValue(state.values.at(index));
    }
    updateInfoLabel();
}

void MainWindow::updateChannelUi()
{
    channelControlsWidget_->updateSettings(controller_.channelSettings());
}

void MainWindow::updateFrameUi()
{
    imageViewport_->setImage(controller_.renderedFrame().image);
    updateInfoLabel();
    if (movieExportInProgress_ && movieExportAwaitingFrame_) {
        prepareCurrentMovieExportFrame();
    }
}

void MainWindow::updateMetadataUi()
{
    updateFrameMetadataUi();
}

void MainWindow::updateStaticMetadataUi()
{
    const Nd2DocumentInfo &info = controller_.documentInfo();

    setOverviewContent(info);
    setMetadataContent(attributesWidgets_, documentValue(info.attributesJson), prettyJson(info.attributesJson));
    setMetadataContent(experimentWidgets_, documentValue(info.experimentJson), prettyJson(info.experimentJson));
    setMetadataContent(textInfoWidgets_, documentValue(info.textInfoJson), prettyJson(info.textInfoJson));
}

void MainWindow::updateFrameMetadataUi()
{
    const Nd2DocumentInfo &info = controller_.documentInfo();
    const QJsonDocument metadataDoc = controller_.currentFrameMetadata().isNull() ? info.metadataJson : controller_.currentFrameMetadata();
    const QString metadataRaw = controller_.currentFrameMetadataText().isEmpty() ? prettyJson(info.metadataJson)
                                                                                 : controller_.currentFrameMetadataText();
    setMetadataContent(metadataWidgets_, documentValue(metadataDoc), metadataRaw);
}

void MainWindow::showErrorMessage(const QString &message)
{
    if (message.isEmpty()) {
        return;
    }

    if (movieExportInProgress_) {
        finishMovieExportPlayback(message);
        return;
    }

    statusBar()->showMessage(message, 5000);
    QMessageBox::warning(this, tr("ND2 Viewer"), message);
}

void MainWindow::updateBusyState(bool busy)
{
    if (busy) {
        statusBar()->showMessage(tr("Loading frame…"));
        setCursor(Qt::BusyCursor);
    } else {
        unsetCursor();
    }
}

void MainWindow::updateStatusMessage(const QString &message)
{
    if (!message.isEmpty()) {
        statusBar()->showMessage(message, 3000);
    }
}

void MainWindow::updateHoveredPixel(const QPoint &pixelPosition, bool insideImage)
{
    pixelStatusLabel_->setText(insideImage ? controller_.pixelInfoAt(pixelPosition) : QString());
}

void MainWindow::updateZoomLabel(double zoomFactor, bool fitToWindow)
{
    zoomStatusLabel_->setText(fitToWindow ? tr("Fit") : tr("%1%").arg(zoomFactor * 100.0, 0, 'f', 1));
}

void MainWindow::buildMenus()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    openAction_ = fileMenu->addAction(tr("&Open…"));
    openAction_->setShortcut(QKeySequence::Open);
    connect(openAction_, &QAction::triggered, this, &MainWindow::openFile);

    reloadAction_ = fileMenu->addAction(tr("&Reload Frame"));
    reloadAction_->setShortcut(tr("F5"));
    connect(reloadAction_, &QAction::triggered, &controller_, &DocumentController::reloadCurrentFrame);

    fileMenu->addSeparator();
    quitAction_ = fileMenu->addAction(tr("E&xit"));
    quitAction_->setShortcut(QKeySequence::Quit);
    connect(quitAction_, &QAction::triggered, this, &QWidget::close);

    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    auto *fitAction = viewMenu->addAction(tr("Fit to Window"));
    fitAction->setShortcut(tr("Ctrl+0"));
    connect(fitAction, &QAction::triggered, imageViewport_, &ImageViewport::zoomToFit);

    auto *actualSizeAction = viewMenu->addAction(tr("Actual Size"));
    actualSizeAction->setShortcut(tr("Ctrl+1"));
    connect(actualSizeAction, &QAction::triggered, imageViewport_, &ImageViewport::setActualSize);

    auto *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    auto *drawRoiAction = toolsMenu->addAction(tr("Draw ROI"));
    drawRoiAction->setCheckable(true);
    connect(drawRoiAction, &QAction::toggled, this, [this](bool checked) {
        imageViewport_->setInteractionMode(checked ? ImageViewport::InteractionMode::DrawRoi
                                                   : ImageViewport::InteractionMode::Pan);
    });
}

void MainWindow::buildCentralUi()
{
    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto *mainSplitter = new QSplitter(Qt::Horizontal, central);

    auto *viewerPane = new QWidget(mainSplitter);
    auto *viewerLayout = new QVBoxLayout(viewerPane);
    viewerLayout->setContentsMargins(0, 0, 0, 0);
    viewerLayout->setSpacing(8);

    auto *navigationSection = new QWidget(viewerPane);
    auto *navigationLayout = new QVBoxLayout(navigationSection);
    navigationLayout->setContentsMargins(0, 0, 0, 0);
    navigationLayout->setSpacing(4);
    navigationLayout->addWidget(createSectionTitle(tr("Navigation"), navigationSection));

    auto *navigatorScrollArea = new QScrollArea(navigationSection);
    navigatorScrollArea->setWidgetResizable(true);
    navigatorContainer_ = new QWidget(navigatorScrollArea);
    navigatorRowsLayout_ = new QVBoxLayout(navigatorContainer_);
    navigatorRowsLayout_->setContentsMargins(0, 0, 0, 0);
    navigatorRowsLayout_->setSpacing(6);
    navigatorEmptyLabel_ = new QLabel(tr("Open a file to browse time, z, or position loops."), navigatorContainer_);
    navigatorEmptyLabel_->setWordWrap(true);
    navigatorRowsLayout_->addWidget(navigatorEmptyLabel_);
    navigatorRowsLayout_->addStretch(1);
    navigatorScrollArea->setWidget(navigatorContainer_);
    navigationLayout->addWidget(navigatorScrollArea);

    imageViewport_ = new ImageViewport(viewerPane);

    viewerLayout->addWidget(navigationSection, 0);
    viewerLayout->addWidget(imageViewport_, 1);

    auto *sidebarPane = new QWidget(mainSplitter);
    sidebarPane->setMinimumWidth(320);
    auto *sidebarLayout = new QVBoxLayout(sidebarPane);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(8);

    metadataOverviewTree_ = new QTreeWidget(sidebarPane);
    metadataOverviewTree_->setColumnCount(2);
    metadataOverviewTree_->setHeaderLabels({tr("Key"), tr("Value")});
    metadataOverviewTree_->setRootIsDecorated(true);
    metadataOverviewTree_->setUniformRowHeights(true);
    metadataOverviewTree_->setAlternatingRowColors(true);
    metadataOverviewTree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    metadataOverviewTree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    metadataOverviewTree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);

    auto *overviewSection = new QWidget(sidebarPane);
    auto *overviewLayout = new QVBoxLayout(overviewSection);
    overviewLayout->setContentsMargins(0, 0, 0, 0);
    overviewLayout->setSpacing(4);
    overviewLayout->addWidget(createSectionTitle(tr("Overview"), overviewSection));
    overviewLayout->addWidget(metadataOverviewTree_);

    channelControlsWidget_ = new ChannelControlsWidget(sidebarPane);
    auto *channelsSection = new QWidget(sidebarPane);
    auto *channelsLayout = new QVBoxLayout(channelsSection);
    channelsLayout->setContentsMargins(0, 0, 0, 0);
    channelsLayout->setSpacing(4);
    channelsLayout->addWidget(createSectionTitle(tr("Channels"), channelsSection));
    channelsLayout->addWidget(channelControlsWidget_);

    metadataTabs_ = new QTabWidget(sidebarPane);
    attributesWidgets_ = addMetadataTab(tr("Attributes"));
    experimentWidgets_ = addMetadataTab(tr("Experiment"));
    metadataWidgets_ = addMetadataTab(tr("Metadata"));
    textInfoWidgets_ = addMetadataTab(tr("Text Info"));

    auto *metadataSection = new QWidget(sidebarPane);
    auto *metadataLayout = new QVBoxLayout(metadataSection);
    metadataLayout->setContentsMargins(0, 0, 0, 0);
    metadataLayout->setSpacing(4);
    metadataLayout->addWidget(createSectionTitle(tr("Metadata"), metadataSection));
    metadataLayout->addWidget(metadataTabs_);

    sidebarLayout->addWidget(overviewSection, 0);
    sidebarLayout->addWidget(channelsSection, 0);
    sidebarLayout->addWidget(metadataSection, 1);

    mainSplitter->addWidget(viewerPane);
    mainSplitter->addWidget(sidebarPane);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 0);
    mainSplitter->setSizes({1000, 380});

    layout->addWidget(mainSplitter, 1);
    setCentralWidget(central);
}

void MainWindow::rebuildNavigatorControls()
{
    clearLayout(navigatorRowsLayout_);
    loopControls_.clear();

    const Nd2DocumentInfo &info = controller_.documentInfo();
    if (info.loops.isEmpty()) {
        navigatorEmptyLabel_ = new QLabel(tr("This file has a single frame and no experiment loops."), navigatorContainer_);
        navigatorEmptyLabel_->setWordWrap(true);
        navigatorRowsLayout_->addWidget(navigatorEmptyLabel_);
        navigatorRowsLayout_->addStretch(1);
        return;
    }

    for (int index = 0; index < info.loops.size(); ++index) {
        const Nd2LoopInfo &loop = info.loops.at(index);
        LoopWidgets widgets;
        widgets.row = new QWidget(navigatorContainer_);
        auto *rowLayout = new QHBoxLayout(widgets.row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        widgets.label = new QLabel(QStringLiteral("%1").arg(loop.label), widgets.row);
        widgets.label->setMinimumWidth(84);
        widgets.slider = new QSlider(Qt::Horizontal, widgets.row);
        widgets.slider->setRange(0, qMax(loop.size - 1, 0));
        widgets.slider->setTracking(false);
        widgets.slider->installEventFilter(this);
        widgets.spinBox = new QSpinBox(widgets.row);
        widgets.spinBox->setRange(0, qMax(loop.size - 1, 0));
        widgets.details = new QLabel(QStringLiteral("%1 · %2 steps").arg(loop.type, QString::number(loop.size)), widgets.row);
        widgets.details->setMinimumWidth(120);

        rowLayout->addWidget(widgets.label);
        rowLayout->addWidget(widgets.slider, 1);
        rowLayout->addWidget(widgets.spinBox);
        rowLayout->addWidget(widgets.details);

        navigatorRowsLayout_->addWidget(widgets.row);
        loopControls_.push_back(widgets);

        connect(widgets.slider, &QSlider::sliderMoved, this, [this, index](int value) {
            auto &loopWidgets = loopControls_[index];
            const QSignalBlocker spinBlocker(loopWidgets.spinBox);
            loopWidgets.spinBox->setValue(value);
        });

        connect(widgets.slider, &QSlider::valueChanged, this, [this, index](int value) {
            auto &loopWidgets = loopControls_[index];
            const QSignalBlocker spinBlocker(loopWidgets.spinBox);
            loopWidgets.spinBox->setValue(value);
        });

        connect(widgets.slider, &QSlider::sliderReleased, this, [this, index]() {
            commitLoopSliderValue(index);
        });

        connect(widgets.spinBox, qOverload<int>(&QSpinBox::valueChanged), this, [this, index](int value) {
            auto &loopWidgets = loopControls_[index];
            const QSignalBlocker sliderBlocker(loopWidgets.slider);
            loopWidgets.slider->setValue(value);
            controller_.setCoordinateValue(index, value);
        });
    }

    navigatorRowsLayout_->addStretch(1);
}

void MainWindow::commitLoopSliderValue(int loopIndex)
{
    if (loopIndex < 0 || loopIndex >= loopControls_.size()) {
        return;
    }

    const auto &loopWidgets = loopControls_.at(loopIndex);
    controller_.setCoordinateValue(loopIndex, loopWidgets.slider->sliderPosition());
}

MainWindow::MetadataWidgets MainWindow::addMetadataTab(const QString &title)
{
    auto *page = new QWidget(metadataTabs_);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    MetadataWidgets widgets;
    auto *splitter = new QSplitter(Qt::Vertical, page);
    auto *tree = new QTreeWidget(splitter);
    tree->setColumnCount(2);
    tree->setHeaderLabels({tr("Key"), tr("Value")});
    tree->setRootIsDecorated(true);
    tree->setUniformRowHeights(true);
    tree->setAlternatingRowColors(true);
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    auto *raw = new QPlainTextEdit(splitter);
    raw->setReadOnly(true);

    splitter->addWidget(tree);
    splitter->addWidget(raw);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    layout->addWidget(splitter);

    metadataTabs_->addTab(page, title);
    widgets.tree = tree;
    widgets.raw = raw;
    return widgets;
}

void MainWindow::setMetadataContent(const MetadataWidgets &widgets, const QJsonValue &jsonValue, const QString &rawText)
{
    populateJsonTree(widgets.tree, jsonValue);
    widgets.raw->setPlainText(rawText);
}

void MainWindow::setOverviewContent(const Nd2DocumentInfo &info)
{
    if (!metadataOverviewTree_) {
        return;
    }

    metadataOverviewTree_->clear();

    const QString fileName = controller_.hasDocument()
                                 ? QFileInfo(info.filePath).fileName()
                                 : tr("No file loaded");
    addOverviewTreeRow(metadataOverviewTree_, tr("File"), fileName);
    addOverviewTreeRow(metadataOverviewTree_,
                       tr("Size"),
                       QStringLiteral("%1 × %2").arg(info.frameSize.width()).arg(info.frameSize.height()));
    addOverviewTreeRow(metadataOverviewTree_, tr("Frames"), QString::number(info.sequenceCount));
    addOverviewTreeRow(metadataOverviewTree_, tr("Components"), QString::number(info.componentCount));
    addOverviewTreeRow(metadataOverviewTree_,
                       tr("Pixel Type"),
                       info.pixelDataType.isEmpty() ? tr("Unknown") : info.pixelDataType);

    auto *loopsItem = new QTreeWidgetItem(metadataOverviewTree_);
    loopsItem->setText(0, tr("Loops"));
    if (info.loops.isEmpty()) {
        loopsItem->setText(1, tr("Single frame"));
    } else {
        loopsItem->setText(1, QStringLiteral("[%1 item%2]")
                                  .arg(info.loops.size())
                                  .arg(info.loops.size() == 1 ? QString() : QStringLiteral("s")));
        for (const Nd2LoopInfo &loop : info.loops) {
            auto *loopItem = new QTreeWidgetItem(loopsItem);
            loopItem->setText(0, loop.label);
            loopItem->setText(1, QStringLiteral("%1, %2 steps").arg(loop.type, QString::number(loop.size)));
        }
    }

    metadataOverviewTree_->collapseAll();
    metadataOverviewTree_->expandToDepth(0);
}

MainWindow::ExportMode MainWindow::promptForExportMode(ExportScope scope) const
{
    QDialog dialog(const_cast<MainWindow *>(this));
    dialog.setWindowTitle(scope == ExportScope::Roi ? tr("Export Current ROI") : tr("Export Current Frame"));

    auto *layout = new QVBoxLayout(&dialog);
    auto *introLabel = new QLabel(scope == ExportScope::Roi
                                      ? tr("Choose what to export for the current ROI.")
                                      : tr("Choose what to export for the current frame."),
                                  &dialog);
    introLabel->setWordWrap(true);

    auto *previewButton = new QRadioButton(tr("Rendered Preview (.png)"), &dialog);
    auto *analysisButton = new QRadioButton(tr("Analysis Channels (.tif)"), &dialog);
    auto *bundleButton = new QRadioButton(tr("Export Bundle (Recommended)"), &dialog);
    bundleButton->setChecked(true);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    layout->addWidget(introLabel);
    layout->addWidget(previewButton);
    layout->addWidget(analysisButton);
    layout->addWidget(bundleButton);
    layout->addWidget(buttonBox);

    if (dialog.exec() != QDialog::Accepted) {
        return ExportMode::Cancelled;
    }

    if (previewButton->isChecked()) {
        return ExportMode::PreviewPng;
    }
    if (analysisButton->isChecked()) {
        return ExportMode::AnalysisTiffs;
    }
    return ExportMode::Bundle;
}

MainWindow::ExportBundleResult MainWindow::exportCurrentFrame(const QString &selectedPath,
                                                             ExportMode mode,
                                                             ExportScope scope) const
{
    ExportBundleResult result;

    const QImage previewImage = controller_.renderedFrame().image;
    const RawFrame &rawFrame = controller_.currentRawFrame();
    if (previewImage.isNull() || !rawFrame.isValid()) {
        result.failures << tr("No current frame is available.");
        return result;
    }

    QRect cropRect;
    if (scope == ExportScope::Roi) {
        cropRect = imageViewport_->roiRect().intersected(QRect(0, 0, rawFrame.width, rawFrame.height));
        if (!cropRect.isValid() || cropRect.isEmpty()) {
            result.failures << tr("No ROI is currently available.");
            return result;
        }
    }

    const bool shouldSavePreview = mode == ExportMode::PreviewPng || mode == ExportMode::Bundle;
    const bool shouldSaveChannels = mode == ExportMode::AnalysisTiffs || mode == ExportMode::Bundle;
    result.previewRequested = shouldSavePreview;

    if (shouldSavePreview) {
        const QImage imageToSave = scope == ExportScope::Roi ? previewImage.copy(cropRect) : previewImage;
        if (imageToSave.isNull() || !imageToSave.save(selectedPath)) {
            result.failures << tr("Preview PNG: %1").arg(QDir::toNativeSeparators(selectedPath));
            return result;
        }

        result.previewSaved = true;
        result.previewPath = selectedPath;
    }

    if (shouldSaveChannels) {
        const QFileInfo exportInfo(selectedPath);
        const QString baseStem = exportInfo.completeBaseName();
        const QDir outputDir = exportInfo.dir();
        const Nd2DocumentInfo &info = controller_.documentInfo();
        const int channelCount = qMax(rawFrame.components, 1);

        for (int channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
            QString channelLabel = QStringLiteral("C%1").arg(channelIndex + 1);
            if (channelIndex < info.channels.size() && !info.channels.at(channelIndex).name.isEmpty()) {
                channelLabel += QStringLiteral("_") + sanitizeToken(info.channels.at(channelIndex).name);
            }

            const QString channelPath = outputDir.filePath(QStringLiteral("%1_%2.tif").arg(baseStem, channelLabel));
            QString errorMessage;
            if (!writeChannelTiff(channelPath, rawFrame, channelIndex, &errorMessage, cropRect)) {
                result.failures << tr("Channel %1: %2").arg(channelIndex + 1).arg(errorMessage);
                continue;
            }

            result.channelPaths << channelPath;
        }
    }

    return result;
}

bool MainWindow::writeChannelTiff(const QString &path,
                                  const RawFrame &frame,
                                  int channelIndex,
                                  QString *errorMessage,
                                  const QRect &cropRect) const
{
    if (!frame.isValid()) {
        if (errorMessage) {
            *errorMessage = tr("No raw frame data is available.");
        }
        return false;
    }

    if (channelIndex < 0 || channelIndex >= qMax(frame.components, 1)) {
        if (errorMessage) {
            *errorMessage = tr("Channel index %1 is out of range.").arg(channelIndex);
        }
        return false;
    }

    const QRect sourceRect = cropRect.isValid() && !cropRect.isEmpty()
                                 ? cropRect.intersected(QRect(0, 0, frame.width, frame.height))
                                 : QRect(0, 0, frame.width, frame.height);
    if (!sourceRect.isValid() || sourceRect.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("The ROI is outside the current frame.");
        }
        return false;
    }

#ifdef Q_OS_WIN
    TIFF *tiff = TIFFOpenW(reinterpret_cast<const wchar_t *>(path.utf16()), "w");
#else
    const QByteArray encodedPath = QFile::encodeName(path);
    TIFF *tiff = TIFFOpen(encodedPath.constData(), "w");
#endif
    if (!tiff) {
        if (errorMessage) {
            *errorMessage = tr("Could not open %1 for writing.").arg(QDir::toNativeSeparators(path));
        }
        return false;
    }

    const uint16 bitsPerSample = static_cast<uint16>(frame.bitsPerComponent);
    const uint16 sampleFormat = frame.pixelDataType.compare(QStringLiteral("float"), Qt::CaseInsensitive) == 0
                                    ? SAMPLEFORMAT_IEEEFP
                                    : SAMPLEFORMAT_UINT;
    const int outputWidth = sourceRect.width();
    const int outputHeight = sourceRect.height();
    const tsize_t rowBytes = static_cast<tsize_t>(outputWidth * frame.bytesPerComponent());

    TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, static_cast<uint32>(outputWidth));
    TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, static_cast<uint32>(outputHeight));
    TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, bitsPerSample);
    TIFFSetField(tiff, TIFFTAG_SAMPLEFORMAT, sampleFormat);
    TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
    TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tiff, rowBytes));

    QByteArray rowBuffer(static_cast<qsizetype>(rowBytes), Qt::Uninitialized);
    const int bytesPerComponent = frame.bytesPerComponent();
    const int actualChannelIndex = frame.components == 1 ? 0 : channelIndex;
    const char *frameData = frame.data.constData();

    for (int y = 0; y < outputHeight; ++y) {
        char *rowDestination = rowBuffer.data();
        const int sourceY = sourceRect.y() + y;
        const char *rowSource = frameData + static_cast<qsizetype>(sourceY) * frame.bytesPerLine;
        for (int x = 0; x < outputWidth; ++x) {
            const int sourceX = sourceRect.x() + x;
            const qsizetype sourceOffset = static_cast<qsizetype>((sourceX * frame.components + actualChannelIndex) * bytesPerComponent);
            std::memcpy(rowDestination + static_cast<qsizetype>(x) * bytesPerComponent,
                        rowSource + sourceOffset,
                        static_cast<size_t>(bytesPerComponent));
        }

        if (TIFFWriteScanline(tiff, rowBuffer.data(), static_cast<uint32>(y), 0) < 0) {
            TIFFClose(tiff);
            if (errorMessage) {
                *errorMessage = tr("TIFF write failed for %1.").arg(QDir::toNativeSeparators(path));
            }
            return false;
        }
    }

    TIFFClose(tiff);
    return true;
}

QString MainWindow::buildDefaultFrameSavePath(ExportScope scope, const QString &extension) const
{
    QString directory;
    QString baseName = QStringLiteral("frame");

    if (controller_.hasDocument()) {
        const QFileInfo sourceInfo(controller_.currentPath());
        directory = sourceInfo.absolutePath();
        if (!sourceInfo.completeBaseName().isEmpty()) {
            baseName = sourceInfo.completeBaseName();
        }
    }

    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    }
    if (directory.isEmpty()) {
        directory = QDir::homePath();
    }

    const Nd2DocumentInfo &info = controller_.documentInfo();
    const FrameCoordinateState &coordinates = controller_.coordinateState();
    QStringList nameParts{sanitizeToken(baseName)};
    for (int index = 0; index < info.loops.size() && index < coordinates.values.size(); ++index) {
        const QString label = sanitizeToken(info.loops.at(index).label);
        nameParts << QStringLiteral("%1%2").arg(label, QString::number(coordinates.values.at(index) + 1));
    }
    if (scope == ExportScope::Roi && imageViewport_->hasRoi()) {
        const QRect roi = imageViewport_->roiRect();
        nameParts << QStringLiteral("roi_x%1_y%2_w%3_h%4")
                         .arg(roi.x())
                         .arg(roi.y())
                         .arg(roi.width())
                         .arg(roi.height());
    }

    return QDir(directory).filePath(nameParts.join(QStringLiteral("_")) + extension);
}

QString MainWindow::buildDefaultMovieSavePath(ExportScope scope) const
{
    QString path = buildDefaultFrameSavePath(scope, QStringLiteral(".mp4"));
    QFileInfo info(path);
    return info.dir().filePath(info.completeBaseName() + QStringLiteral("_movie.mp4"));
}

int MainWindow::findTimeLoopIndex() const
{
    const Nd2DocumentInfo &info = controller_.documentInfo();
    for (int index = 0; index < info.loops.size(); ++index) {
        if (info.loops.at(index).type == QStringLiteral("TimeLoop")) {
            return index;
        }
    }

    for (int index = 0; index < info.loops.size(); ++index) {
        if (info.loops.at(index).type == QStringLiteral("NETimeLoop")) {
            return index;
        }
    }

    return -1;
}

void MainWindow::startMovieExportPlayback(const MovieExportSettings &settings)
{
    QMediaFormat format(QMediaFormat::MPEG4);
    format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    format.setAudioCodec(QMediaFormat::AudioCodec::Unspecified);
    if (!format.isSupported(QMediaFormat::Encode)) {
        QMessageBox::warning(this,
                             tr("Movie Export Failed"),
                             tr("MP4/H.264 export is not available in this Qt Multimedia backend."));
        return;
    }

    cleanupMovieExportPlayback();

    movieExportSettings_ = settings;
    movieExportTimeValues_.clear();
    const int step = qMax(movieExportSettings_.frameStep(), 1);
    for (int timeValue = movieExportSettings_.startFrame; timeValue <= movieExportSettings_.endFrame; timeValue += step) {
        movieExportTimeValues_.push_back(timeValue);
    }

    if (movieExportTimeValues_.isEmpty()) {
        QMessageBox::warning(this,
                             tr("Movie Export Failed"),
                             tr("The requested time range does not contain any exportable frames."));
        return;
    }

    movieExportNextFrameIndex_ = 0;
    movieExportEncodedFrameCount_ = 0;
    movieExportAwaitingFrame_ = false;
    movieExportPendingEndOfStream_ = false;
    movieExportEndOfStreamSent_ = false;
    movieVideoFrameInputReady_ = false;
    moviePendingFrame_ = {};

    QVideoFrameFormat videoFrameFormat(movieExportSettings_.outputSize,
                                       QVideoFrameFormat::pixelFormatFromImageFormat(QImage::Format_ARGB32));
    videoFrameFormat.setStreamFrameRate(movieExportSettings_.fps);

    movieCaptureSession_ = new QMediaCaptureSession(this);
    movieRecorder_ = new QMediaRecorder(this);
    movieVideoFrameInput_ = new QVideoFrameInput(videoFrameFormat, this);

    movieCaptureSession_->setRecorder(movieRecorder_);
    movieCaptureSession_->setVideoFrameInput(movieVideoFrameInput_);

    movieRecorder_->setOutputLocation(QUrl::fromLocalFile(movieExportSettings_.outputPath));
    movieRecorder_->setMediaFormat(format);
    movieRecorder_->setEncodingMode(QMediaRecorder::ConstantQualityEncoding);
    movieRecorder_->setQuality(QMediaRecorder::HighQuality);
    movieRecorder_->setVideoResolution(movieExportSettings_.outputSize);
    movieRecorder_->setVideoFrameRate(movieExportSettings_.fps);
    movieRecorder_->setAutoStop(true);

    connect(movieVideoFrameInput_, &QVideoFrameInput::readyToSendVideoFrame, this, [this]() {
        movieVideoFrameInputReady_ = true;
        trySendMovieExportFrame();
    });
    connect(movieRecorder_, &QMediaRecorder::errorOccurred, this,
            [this](QMediaRecorder::Error, const QString &errorString) {
                finishMovieExportPlayback(errorString.isEmpty()
                                              ? tr("The movie export did not complete.")
                                              : errorString);
            });
    connect(movieRecorder_, &QMediaRecorder::recorderStateChanged, this,
            [this](QMediaRecorder::RecorderState state) {
                if (!movieExportInProgress_) {
                    return;
                }
                if (state == QMediaRecorder::RecordingState) {
                    statusBar()->showMessage(tr("Exporting movie live through the viewer…"));
                    requestNextMovieExportFrame();
                } else if (state == QMediaRecorder::StoppedState && movieExportEndOfStreamSent_) {
                    finishMovieExportPlayback();
                }
            });

    setMovieExportUiState(true);
    statusBar()->showMessage(tr("Preparing live movie export…"));
    movieRecorder_->record();
}

void MainWindow::requestNextMovieExportFrame()
{
    if (!movieExportInProgress_ || movieExportAwaitingFrame_ || moviePendingFrame_.isValid()) {
        return;
    }

    if (movieExportNextFrameIndex_ >= movieExportTimeValues_.size()) {
        movieExportPendingEndOfStream_ = true;
        statusBar()->showMessage(tr("Finalizing movie export…"));
        return;
    }

    const int timeValue = movieExportTimeValues_.at(movieExportNextFrameIndex_);
    const FrameCoordinateState &state = controller_.coordinateState();
    if (movieExportSettings_.timeLoopIndex < state.values.size()
        && state.values.at(movieExportSettings_.timeLoopIndex) == timeValue
        && controller_.renderedFrame().isValid()
        && controller_.currentRawFrame().isValid()) {
        prepareCurrentMovieExportFrame();
        return;
    }

    movieExportAwaitingFrame_ = true;
    statusBar()->showMessage(tr("Exporting movie live: moving to frame %1…").arg(timeValue));
    controller_.setCoordinateValue(movieExportSettings_.timeLoopIndex, timeValue);
}

void MainWindow::prepareCurrentMovieExportFrame()
{
    if (!movieExportInProgress_ || movieExportNextFrameIndex_ >= movieExportTimeValues_.size()) {
        return;
    }

    const FrameCoordinateState &state = controller_.coordinateState();
    const int expectedTimeValue = movieExportTimeValues_.at(movieExportNextFrameIndex_);
    if (movieExportSettings_.timeLoopIndex >= state.values.size()
        || state.values.at(movieExportSettings_.timeLoopIndex) != expectedTimeValue) {
        return;
    }

    QImage image = controller_.renderedFrame().image;
    if (movieExportSettings_.roiRect.isValid() && !movieExportSettings_.roiRect.isEmpty()) {
        image = image.copy(movieExportSettings_.roiRect);
    }
    image = image.convertToFormat(QImage::Format_ARGB32);
    if (image.isNull()) {
        finishMovieExportPlayback(tr("The current rendered frame could not be prepared for movie export."));
        return;
    }

    movieExportAwaitingFrame_ = false;
    moviePendingFrame_ = QVideoFrame(image);
    moviePendingFrame_.setStreamFrameRate(movieExportSettings_.fps);
    const qint64 startTimeUs = qRound64((movieExportEncodedFrameCount_ * 1000000.0) / movieExportSettings_.fps);
    const qint64 endTimeUs = qRound64(((movieExportEncodedFrameCount_ + 1) * 1000000.0) / movieExportSettings_.fps);
    moviePendingFrame_.setStartTime(startTimeUs);
    moviePendingFrame_.setEndTime(endTimeUs);
    ++movieExportNextFrameIndex_;
    trySendMovieExportFrame();
}

void MainWindow::trySendMovieExportFrame()
{
    if (!movieExportInProgress_ || !movieVideoFrameInput_ || !movieRecorder_
        || movieRecorder_->recorderState() != QMediaRecorder::RecordingState) {
        return;
    }

    if (!movieVideoFrameInputReady_) {
        return;
    }

    if (moviePendingFrame_.isValid()) {
        if (!movieVideoFrameInput_->sendVideoFrame(moviePendingFrame_)) {
            return;
        }

        movieVideoFrameInputReady_ = false;
        moviePendingFrame_ = {};
        ++movieExportEncodedFrameCount_;
        statusBar()->showMessage(tr("Exporting movie live: frame %1 of %2…")
                                     .arg(movieExportEncodedFrameCount_)
                                     .arg(movieExportTimeValues_.size()));
        requestNextMovieExportFrame();
        return;
    }

    if (movieExportPendingEndOfStream_) {
        if (!movieVideoFrameInput_->sendVideoFrame(QVideoFrame())) {
            return;
        }

        movieVideoFrameInputReady_ = false;
        movieExportPendingEndOfStream_ = false;
        movieExportEndOfStreamSent_ = true;
        statusBar()->showMessage(tr("Finalizing movie export…"));
    }
}

void MainWindow::finishMovieExportPlayback(const QString &errorMessage)
{
    if (!movieExportInProgress_) {
        return;
    }

    const bool success = errorMessage.isEmpty();
    const QString outputPath = movieExportSettings_.outputPath;
    const int encodedFrameCount = movieExportEncodedFrameCount_;

    cleanupMovieExportPlayback();

    if (success) {
        statusBar()->showMessage(tr("Exported movie to %1").arg(QDir::toNativeSeparators(outputPath)), 5000);
        QMessageBox::information(this,
                                 tr("Movie Export Complete"),
                                 tr("Exported %1 frame(s) to:\n%2")
                                     .arg(encodedFrameCount)
                                     .arg(QDir::toNativeSeparators(outputPath)));
    } else {
        QMessageBox::warning(this,
                             tr("Movie Export Failed"),
                             errorMessage);
    }
}

void MainWindow::cleanupMovieExportPlayback()
{
    if (movieVideoFrameInput_) {
        movieVideoFrameInput_->disconnect(this);
    }
    if (movieRecorder_) {
        movieRecorder_->disconnect(this);
        if (movieRecorder_->recorderState() != QMediaRecorder::StoppedState) {
            movieRecorder_->stop();
        }
    }

    if (movieCaptureSession_) {
        movieCaptureSession_->deleteLater();
        movieCaptureSession_ = nullptr;
    }
    if (movieRecorder_) {
        movieRecorder_->deleteLater();
        movieRecorder_ = nullptr;
    }
    if (movieVideoFrameInput_) {
        movieVideoFrameInput_->deleteLater();
        movieVideoFrameInput_ = nullptr;
    }

    moviePendingFrame_ = {};
    movieExportTimeValues_.clear();
    movieExportNextFrameIndex_ = 0;
    movieExportEncodedFrameCount_ = 0;
    movieExportAwaitingFrame_ = false;
    movieExportPendingEndOfStream_ = false;
    movieExportEndOfStreamSent_ = false;
    movieVideoFrameInputReady_ = false;
    movieExportSettings_ = {};
    setMovieExportUiState(false);
}

void MainWindow::setMovieExportUiState(bool active)
{
    movieExportInProgress_ = active;

    if (active) {
        qApp->installEventFilter(this);
        QApplication::setOverrideCursor(Qt::BusyCursor);
    } else {
        qApp->removeEventFilter(this);
        QApplication::restoreOverrideCursor();
    }

    if (openAction_) {
        openAction_->setEnabled(!active);
    }
    if (reloadAction_) {
        reloadAction_->setEnabled(!active);
    }
    if (quitAction_) {
        quitAction_->setEnabled(!active);
    }
}

QString MainWindow::sanitizeToken(const QString &value) const
{
    QString sanitized = value;
    sanitized.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("_"));
    sanitized.replace(QRegularExpression(QStringLiteral("_+")), QStringLiteral("_"));
    sanitized.remove(QRegularExpression(QStringLiteral("^_+|_+$")));
    return sanitized.isEmpty() ? QStringLiteral("frame") : sanitized;
}

void MainWindow::updateWindowTitle()
{
    if (!controller_.hasDocument()) {
        setWindowTitle(tr("nd2-viewer"));
        return;
    }

    setWindowTitle(tr("%1 - nd2-viewer").arg(QFileInfo(controller_.currentPath()).fileName()));
}

void MainWindow::updateInfoLabel()
{
    if (!controller_.hasDocument()) {
        infoStatusLabel_->setText(tr("No file loaded"));
        return;
    }

    const Nd2DocumentInfo &info = controller_.documentInfo();
    QStringList coords;
    for (int index = 0; index < info.loops.size() && index < controller_.coordinateState().values.size(); ++index) {
        coords << QStringLiteral("%1=%2").arg(info.loops.at(index).label).arg(controller_.coordinateState().values.at(index));
    }

    infoStatusLabel_->setText(QStringLiteral("%1  |  %2 × %3  |  %4")
                                  .arg(QFileInfo(controller_.currentPath()).fileName())
                                  .arg(info.frameSize.width())
                                  .arg(info.frameSize.height())
                                  .arg(coords.join(QStringLiteral(", "))));
}
