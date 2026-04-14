#include "ui/mainwindow.h"

#include "ui/autocontrasttuningdialog.h"
#include "ui/channelcontrolswidget.h"
#include "ui/imageviewport.h"
#include "ui/volumeviewport3d.h"
#include "core/documentreaderfactory.h"
#include "core/framerenderer.h"
#include "core/volumeutils.h"

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
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLibraryInfo>
#include <QLocale>
#include <QLineEdit>
#include <QMenuBar>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSplitter>
#include <QStatusBar>
#include <QStandardPaths>
#include <QSignalBlocker>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoFrameFormat>
#include <QVideoFrameInput>

#include <QtConcurrent>

#include <tiffio.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <optional>

namespace
{
bool hasQtFfmpegMediaPlugin(const QStringList &libraryPaths)
{
    for (const QString &basePath : libraryPaths) {
        const QDir multimediaDir(basePath + QStringLiteral("/multimedia"));
        const QStringList entries = multimediaDir.entryList(QDir::Files);
        for (const QString &entry : entries) {
            if (entry.contains(QStringLiteral("ffmpeg"), Qt::CaseInsensitive)
                && entry.contains(QStringLiteral("mediaplugin"), Qt::CaseInsensitive)) {
                return true;
            }
        }
    }

    return false;
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

} // namespace

class FileInfoDialog final : public QDialog
{
public:
    explicit FileInfoDialog(QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(tr("File Info"));
        setModal(true);
        resize(960, 720);

        auto *layout = new QVBoxLayout(this);
        auto *splitter = new QSplitter(Qt::Vertical, this);

        auto *overviewSection = new QWidget(splitter);
        auto *overviewLayout = new QVBoxLayout(overviewSection);
        overviewLayout->setContentsMargins(0, 0, 0, 0);
        overviewLayout->setSpacing(6);
        overviewLayout->addWidget(createSectionTitle(tr("Overview"), overviewSection));

        overviewTree_ = new QTreeWidget(overviewSection);
        overviewTree_->setColumnCount(2);
        overviewTree_->setHeaderLabels({tr("Key"), tr("Value")});
        overviewTree_->setRootIsDecorated(true);
        overviewTree_->setUniformRowHeights(true);
        overviewTree_->setAlternatingRowColors(true);
        overviewTree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        overviewTree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        overviewTree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
        overviewLayout->addWidget(overviewTree_);

        auto *metadataSection = new QWidget(splitter);
        auto *metadataLayout = new QVBoxLayout(metadataSection);
        metadataLayout->setContentsMargins(0, 0, 0, 0);
        metadataLayout->setSpacing(6);
        metadataLayout->addWidget(createSectionTitle(tr("Metadata"), metadataSection));

        metadataTabs_ = new QTabWidget(metadataSection);
        metadataLayout->addWidget(metadataTabs_);

        splitter->addWidget(overviewSection);
        splitter->addWidget(metadataSection);
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        splitter->setSizes({220, 480});
        layout->addWidget(splitter, 1);

        auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
        connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttonBox);
    }

    void refresh(const DocumentInfo &info, const MetadataSection &frameMetadata, bool hasDocument)
    {
        setOverviewContent(info, hasDocument);
        rebuildMetadataTabs(info, frameMetadata);
    }

private:
    struct MetadataPaneWidgets
    {
        QTreeWidget *tree = nullptr;
        QPlainTextEdit *raw = nullptr;
    };

    MetadataPaneWidgets addMetadataTab(const QString &title)
    {
        MetadataPaneWidgets widgets;

        auto *page = new QWidget(metadataTabs_);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        auto *splitter = new QSplitter(Qt::Horizontal, page);
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

    void rebuildMetadataTabs(const DocumentInfo &info, const MetadataSection &frameMetadata)
    {
        while (metadataTabs_->count() > 0) {
            QWidget *page = metadataTabs_->widget(0);
            metadataTabs_->removeTab(0);
            delete page;
        }
        metadataSectionWidgets_.clear();

        metadataSectionWidgets_.reserve(info.metadataSections.size());
        for (const MetadataSection &section : info.metadataSections) {
            metadataSectionWidgets_.push_back(addMetadataTab(section.title));
        }
        frameMetadataWidgets_ = addMetadataTab(tr("Frame Metadata"));

        for (int index = 0; index < metadataSectionWidgets_.size() && index < info.metadataSections.size(); ++index) {
            const MetadataSection &section = info.metadataSections.at(index);
            setMetadataContent(metadataSectionWidgets_.at(index), section.treeValue, section.rawText);
        }
        setMetadataContent(frameMetadataWidgets_, frameMetadata.treeValue, frameMetadata.rawText);
    }

    void setMetadataContent(const MetadataPaneWidgets &widgets, const QJsonValue &jsonValue, const QString &rawText)
    {
        populateJsonTree(widgets.tree, jsonValue);
        widgets.raw->setPlainText(rawText);
    }

    void setOverviewContent(const DocumentInfo &info, bool hasDocument)
    {
        overviewTree_->clear();

        const QString fileName = hasDocument ? QFileInfo(info.filePath).fileName() : tr("No file loaded");
        addOverviewTreeRow(overviewTree_, tr("File"), fileName);
        addOverviewTreeRow(overviewTree_,
                           tr("Size"),
                           QStringLiteral("%1 × %2").arg(info.frameSize.width()).arg(info.frameSize.height()));
        addOverviewTreeRow(overviewTree_, tr("Frames"), QString::number(info.sequenceCount));
        addOverviewTreeRow(overviewTree_, tr("Components"), QString::number(info.componentCount));
        addOverviewTreeRow(overviewTree_,
                           tr("Pixel Type"),
                           info.pixelDataType.isEmpty() ? tr("Unknown") : info.pixelDataType);

        auto *loopsItem = new QTreeWidgetItem(overviewTree_);
        loopsItem->setText(0, tr("Loops"));
        if (info.loops.isEmpty()) {
            loopsItem->setText(1, tr("Single frame"));
        } else {
            loopsItem->setText(1, QStringLiteral("[%1 item%2]")
                                      .arg(info.loops.size())
                                      .arg(info.loops.size() == 1 ? QString() : QStringLiteral("s")));
            for (const LoopInfo &loop : info.loops) {
                auto *loopItem = new QTreeWidgetItem(loopsItem);
                loopItem->setText(0, loop.label);
                loopItem->setText(1, QStringLiteral("%1, %2 steps").arg(loop.type, QString::number(loop.size)));
            }
        }

        overviewTree_->collapseAll();
        overviewTree_->expandToDepth(0);
    }

    QTreeWidget *overviewTree_ = nullptr;
    QTabWidget *metadataTabs_ = nullptr;
    QVector<MetadataPaneWidgets> metadataSectionWidgets_;
    MetadataPaneWidgets frameMetadataWidgets_;
};

namespace
{
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
                                     ? tr("Export the current ROI as a rendered MP4. Start, end, and step apply only to the time axis using 0-based frame numbers; all other loop coordinates stay fixed.")
                                     : tr("Export the current rendered frame view as an MP4. Start, end, and step apply only to the time axis using 0-based frame numbers; all other loop coordinates stay fixed."),
                                 this);
        intro->setWordWrap(true);
        layout->addWidget(intro);

        auto *formLayout = new QFormLayout();

        startFrameSpin_ = new QSpinBox(this);
        startFrameSpin_->setRange(0, qMax(timeLoopSize_ - 1, 0));
        startFrameSpin_->setValue(0);
        formLayout->addRow(tr("Start frame (0-based)"), startFrameSpin_);

        endFrameSpin_ = new QSpinBox(this);
        endFrameSpin_->setRange(0, qMax(timeLoopSize_ - 1, 0));
        endFrameSpin_->setValue(qMax(timeLoopSize_ - 1, 0));
        formLayout->addRow(tr("End frame (0-based)"), endFrameSpin_);

        stepSpin_ = new QSpinBox(this);
        stepSpin_->setRange(1, qMax(timeLoopSize_, 1));
        stepSpin_->setValue(qMax(baseSettings_.step, 1));
        formLayout->addRow(tr("Step"), stepSpin_);

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
        connect(stepSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this]() { scheduleEstimateRefresh(); });
        connect(fpsSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this]() { scheduleEstimateRefresh(); });

        refreshEstimate();
    }

    [[nodiscard]] MovieExportSettings currentSettings() const
    {
        MovieExportSettings settings = baseSettings_;
        settings.startFrame = startFrameSpin_->value();
        settings.endFrame = endFrameSpin_->value();
        settings.step = stepSpin_->value();
        settings.fps = fpsSpin_->value();
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
        continueButton_->setEnabled(currentEstimate_.valid);
    }

    MovieExportSettings baseSettings_;
    QImage sampleImage_;
    int timeLoopSize_ = 0;
    QSpinBox *startFrameSpin_ = nullptr;
    QSpinBox *endFrameSpin_ = nullptr;
    QSpinBox *stepSpin_ = nullptr;
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
    pixelStatusLabel_ = new QLabel(this);
    statusBar()->addWidget(infoStatusLabel_, 1);
    statusBar()->addPermanentWidget(pixelStatusLabel_, 1);

    connect(&controller_, &DocumentController::documentChanged, this, &MainWindow::updateDocumentUi);
    connect(&controller_, &DocumentController::coordinateStateChanged, this, &MainWindow::updateCoordinateUi);
    connect(&controller_, &DocumentController::coordinateStateChanged, this, &MainWindow::maybeReloadVolumeForNonZCoordinateChange);
    connect(&controller_, &DocumentController::channelSettingsChanged, this, &MainWindow::updateChannelUi);
    connect(&controller_, &DocumentController::frameReady, this, &MainWindow::updateFrameUi);
    connect(&controller_, &DocumentController::metadataChanged, this, &MainWindow::updateMetadataUi);
    connect(&controller_, &DocumentController::errorOccurred, this, &MainWindow::showErrorMessage);
    connect(&controller_, &DocumentController::busyChanged, this, &MainWindow::updateBusyState);
    connect(&controller_, &DocumentController::statusTextChanged, this, &MainWindow::updateStatusMessage);

    connect(imageViewport_, &ImageViewport::hoveredPixelChanged, this, &MainWindow::updateHoveredPixel);
    connect(imageViewport_, &ImageViewport::saveImageRequested, this, &MainWindow::saveCurrentFrameAs);
    connect(imageViewport_, &ImageViewport::exportRoiRequested, this, &MainWindow::saveCurrentRoiAs);
    connect(imageViewport_, &ImageViewport::exportMovieRequested, this, &MainWindow::exportMovieAs);
    connect(imageViewport_, &ImageViewport::exportRoiMovieRequested, this, &MainWindow::exportRoiMovieAs);

    connect(channelControlsWidget_, &ChannelControlsWidget::channelSettingsChanged,
            &controller_, qOverload<int, const ChannelRenderSettings &>(&DocumentController::setChannelSettings));
    connect(channelControlsWidget_, &ChannelControlsWidget::liveAutoChanged,
            this, &MainWindow::setLiveAutoForAllChannels);
    connect(channelControlsWidget_, &ChannelControlsWidget::autoContrastTuningRequested,
            this, &MainWindow::openAutoContrastTuningDialog);
    connect(channelControlsWidget_, &ChannelControlsWidget::autoContrastAllRequested,
            this, &MainWindow::autoContrastAllForActiveView);

    connect(view2dButton_, &QPushButton::clicked, this, [this]() { setVolumeViewActive(false); });
    connect(view3dButton_, &QPushButton::clicked, this, [this]() { setVolumeViewActive(true); });
    connect(&volumeWatcher_, &QFutureWatcher<VolumeLoadResult>::finished, this, &MainWindow::handleVolumeLoadFinished);

    updateDocumentUi();

    // Warm the native VTK widget after the main window is already shown so the
    // first switch into 3D does not visibly recreate the top-level window.
    QTimer::singleShot(0, this, [this]() {
        ensureVolumeViewport();
    });
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

    if (volumeWatcher_.isRunning()) {
        volumeWatcher_.waitForFinished();
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
        tr("Open Microscopy File"),
        QString(),
        tr("Microscopy Files (*.nd2 *.czi);;Nikon ND2 Files (*.nd2);;Zeiss CZI Files (*.czi);;All Files (*.*)")
    );
    if (fileName.isEmpty()) {
        return;
    }

    controller_.openFile(fileName);
}

void MainWindow::saveCurrentFrameAs()
{
    if (isVolumeViewActive()) {
        exportCurrentVolumeFrame();
        return;
    }

    exportCurrentSelection(ExportScope::Frame);
}

void MainWindow::saveCurrentRoiAs()
{
    exportCurrentSelection(ExportScope::Roi);
}

void MainWindow::exportMovieAs()
{
    if (isVolumeViewActive()) {
        exportVolumeMovie();
        return;
    }

    exportMovieSelection(ExportScope::Frame);
}

void MainWindow::exportRoiMovieAs()
{
    exportMovieSelection(ExportScope::Roi);
}

void MainWindow::showFileInfoDialog()
{
    if (!fileInfoDialog_) {
        fileInfoDialog_ = new FileInfoDialog(this);
    }

    updateFileInfoDialog();
    fileInfoDialog_->exec();
}

void MainWindow::updateFileInfoDialog()
{
    if (!fileInfoDialog_) {
        return;
    }

    fileInfoDialog_->refresh(controller_.documentInfo(),
                             controller_.currentFrameMetadataSection(),
                             controller_.hasDocument());
}

void MainWindow::setVolumeViewActive(bool active)
{
    if (active && !hasUsableZStack()) {
        return;
    }

    if (active) {
        ensureVolumeViewport();
    }

    applyVolumeViewMode(active);
    updateViewModeButtons();
}

void MainWindow::triggerViewAction()
{
    if (isVolumeViewActive()) {
        if (volumeViewport_) {
            volumeViewport_->resetView();
        }
        return;
    }

    if (imageViewport_) {
        imageViewport_->zoomToFit();
    }
}

void MainWindow::updateViewModeButtons()
{
    const bool volumeActive = isVolumeViewActive();
    const bool hasDocument = controller_.hasDocument();
    const bool hasImage = imageViewport_ && imageViewport_->hasImage();
    const bool volumeReady = volumeViewport_ && volumeViewportHasVolume_ && cachedVolume_.isValid();

    if (view2dButton_) {
        const QSignalBlocker blocker(view2dButton_);
        view2dButton_->setChecked(!volumeActive);
        view2dButton_->setEnabled(!movieExportInProgress_ && hasDocument);
    }
    if (view3dButton_) {
        const QSignalBlocker blocker(view3dButton_);
        view3dButton_->setChecked(volumeActive);
        view3dButton_->setEnabled(!movieExportInProgress_ && hasUsableZStack());
    }
    if (viewActionButton_) {
        viewActionButton_->setVisible(hasDocument);
        viewActionButton_->setText(volumeActive ? tr("Reset") : tr("Fit"));
        viewActionButton_->setToolTip(volumeActive
                                          ? tr("Restore the default 3D camera angle and refit the volume.")
                                          : tr("Fit the current image to the viewport once."));
        viewActionButton_->setEnabled(!movieExportInProgress_ && (volumeActive ? volumeReady : hasImage));
    }
}

void MainWindow::openAutoContrastTuningDialog(int channelIndex)
{
    if (isVolumeViewActive()) {
        statusBar()->showMessage(tr("Live auto percentile tuning is only available in 2D mode."), 5000);
        return;
    }

    const QVector<ChannelRenderSettings> settings = controller_.channelSettings();
    if (channelIndex < 0 || channelIndex >= settings.size()) {
        return;
    }

    const RawFrame &rawFrame = controller_.currentRawFrame();
    if (!rawFrame.isValid()) {
        statusBar()->showMessage(tr("Load a frame before tuning live auto contrast."), 5000);
        return;
    }

    const ChannelAutoContrastAnalysis analysis = FrameRenderer::analyzeChannel(rawFrame, channelIndex);
    const QString description = tr("The histogram uses a sampled snapshot of the current frame.");

    if (!analysis.isValid()) {
        QMessageBox::warning(this,
                             tr("Live Auto"),
                             tr("A histogram could not be prepared for the current frame."));
        return;
    }

    const DocumentInfo &info = controller_.documentInfo();
    const QString channelName = (channelIndex < info.channels.size() && !info.channels.at(channelIndex).name.isEmpty())
                                    ? info.channels.at(channelIndex).name
                                    : tr("Channel %1").arg(channelIndex + 1);
    const ChannelRenderSettings originalSettings = settings.at(channelIndex);

    AutoContrastTuningDialog dialog(channelName, description, analysis, originalSettings, this);
    dialog.setPreviewCallback([this, channelIndex](const ChannelRenderSettings &previewSettings) {
        controller_.setChannelSettings(channelIndex, previewSettings);
    });

    if (dialog.exec() == QDialog::Accepted) {
        controller_.setChannelSettings(channelIndex, dialog.currentSettings());
        return;
    }

    controller_.setChannelSettings(channelIndex, originalSettings);
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
    stopTimePlayback();

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
    settings.sourcePath = controller_.currentPath();
    settings.fixedCoordinates = controller_.coordinateState().values;
    settings.channelSettings = controller_.channelSettings();
    settings.liveAutoEnabled = controller_.liveAutoEnabled();
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

    const LoopInfo &timeLoop = controller_.documentInfo().loops.at(timeLoopIndex);
    MovieExportDialog dialog(settings,
                             sampleImage,
                             timeLoop.size,
                             scope == ExportScope::Roi,
                             this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    settings = dialog.currentSettings();
    QString selectedPath = QFileDialog::getSaveFileName(this,
                                                        tr("Save Movie"),
                                                        buildDefaultMovieSavePath(scope, settings),
                                                        tr("MP4 Video (*.mp4)"));
    if (selectedPath.isEmpty()) {
        return;
    }

    QFileInfo outputInfo(selectedPath);
    if (outputInfo.suffix().isEmpty()) {
        selectedPath += QStringLiteral(".mp4");
    } else if (outputInfo.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) != 0) {
        selectedPath = outputInfo.dir().filePath(outputInfo.completeBaseName() + QStringLiteral(".mp4"));
    }
    settings.outputPath = selectedPath;

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

void MainWindow::exportCurrentVolumeFrame()
{
    if (movieExportInProgress_ || !isVolumeViewActive() || !cachedVolume_.isValid() || !volumeViewport_) {
        return;
    }

    const QImage image = captureCurrentVolumeImage();
    if (image.isNull()) {
        QMessageBox::warning(this,
                             tr("Export Failed"),
                             tr("The current 3D view could not be captured."));
        return;
    }

    QString selectedPath = QFileDialog::getSaveFileName(this,
                                                        tr("Save 3D Frame"),
                                                        buildDefaultFrameSavePath(ExportScope::Frame, QStringLiteral(".png")),
                                                        tr("PNG Image (*.png)"));
    if (selectedPath.isEmpty()) {
        return;
    }

    QFileInfo outputInfo(selectedPath);
    if (outputInfo.suffix().isEmpty()) {
        selectedPath += QStringLiteral(".png");
    } else if (outputInfo.suffix().compare(QStringLiteral("png"), Qt::CaseInsensitive) != 0) {
        selectedPath = outputInfo.dir().filePath(outputInfo.completeBaseName() + QStringLiteral(".png"));
    }

    if (!image.save(selectedPath, "PNG")) {
        QMessageBox::warning(this,
                             tr("Export Failed"),
                             tr("The current 3D view could not be written to:\n%1")
                                 .arg(QDir::toNativeSeparators(selectedPath)));
        return;
    }

    statusBar()->showMessage(tr("Exported 3D frame to %1").arg(QDir::toNativeSeparators(selectedPath)), 5000);
}

void MainWindow::exportVolumeMovie()
{
    if (movieExportInProgress_ || !isVolumeViewActive() || !cachedVolume_.isValid() || !volumeViewport_) {
        return;
    }

    stopTimePlayback();

    const int timeLoopIndex = findTimeLoopIndex();
    if (timeLoopIndex < 0) {
        QMessageBox::information(this,
                                 tr("Movie Export Unavailable"),
                                 tr("This file does not expose a time loop, so movie export is unavailable."));
        return;
    }

    const QImage sampleImage = captureCurrentVolumeImage();
    if (sampleImage.isNull()) {
        QMessageBox::warning(this,
                             tr("Movie Export Failed"),
                             tr("The current 3D view could not be captured for export."));
        return;
    }

    MovieExportSettings settings;
    settings.sourcePath = controller_.currentPath();
    settings.fixedCoordinates = controller_.coordinateState().values;
    settings.channelSettings = controller_.channelSettings();
    settings.liveAutoEnabled = false;
    settings.timeLoopIndex = timeLoopIndex;
    settings.outputSize = sampleImage.size();
    settings.fps = 10.0;

    const LoopInfo &timeLoop = controller_.documentInfo().loops.at(timeLoopIndex);
    MovieExportDialog dialog(settings, sampleImage, timeLoop.size, false, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    settings = dialog.currentSettings();
    settings.liveAutoEnabled = false;

    QString selectedPath = QFileDialog::getSaveFileName(this,
                                                        tr("Save 3D Movie"),
                                                        buildDefaultMovieSavePath(ExportScope::Frame, settings),
                                                        tr("MP4 Video (*.mp4)"));
    if (selectedPath.isEmpty()) {
        return;
    }

    QFileInfo outputInfo(selectedPath);
    if (outputInfo.suffix().isEmpty()) {
        selectedPath += QStringLiteral(".mp4");
    } else if (outputInfo.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) != 0) {
        selectedPath = outputInfo.dir().filePath(outputInfo.completeBaseName() + QStringLiteral(".mp4"));
    }
    settings.outputPath = selectedPath;

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
    cachedVolume_ = {};
    volumeViewportHasVolume_ = false;
    volumeLoadGeneration_ = 0;
    documentZLoopIndex_ = VolumeUtils::findZLoopIndex(controller_.documentInfo());

    imageViewport_->clearRoi();
    imageViewport_->setImage(controller_.renderedFrame().image);
    if (imageViewport_->hasImage()) {
        imageViewport_->zoomToFit();
    }
    rebuildNavigatorControls();

    const bool hasZ = hasUsableZStack();
    viewerStack_->setCurrentIndex(0);
    channelControlsWidget_->setChannels(controller_.documentInfo().channels, controller_.channelSettings());
    channelControlsWidget_->setLiveAutoEnabled(controller_.liveAutoEnabled());
    channelControlsWidget_->setLiveAutoInteractive(!isVolumeViewActive());
    channelControlsWidget_->setAutoContrastTuningEnabled(!isVolumeViewActive());
    updateCoordinateUi();
    updateChannelUi();
    updateStaticMetadataUi();
    updateFrameMetadataUi();
    updateWindowTitle();
    updateInfoLabel();
    applyZLoopNavigatorLock();

    if (viewModeControl_) {
        viewModeControl_->setVisible(hasZ);
    }
    updateViewModeButtons();
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
    applyZLoopNavigatorLock();
}

void MainWindow::updateChannelUi()
{
    channelControlsWidget_->updateSettings(controller_.channelSettings());
    channelControlsWidget_->setLiveAutoEnabled(controller_.liveAutoEnabled());
    syncVolumeViewportChannelSettings();
}

void MainWindow::updateFrameUi()
{
    const bool hadImage = imageViewport_->hasImage();
    imageViewport_->setImage(controller_.renderedFrame().image);
    if (!hadImage && imageViewport_->hasImage() && !isVolumeViewActive()) {
        imageViewport_->zoomToFit();
    }
    updateViewModeButtons();
    updateInfoLabel();
    if (!isVolumeViewActive() && timePlaybackActive_ && timePlaybackAwaitingFrame_) {
        completeTimePlaybackStep();
    }
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
    updateFileInfoDialog();
}

void MainWindow::updateFrameMetadataUi()
{
    updateFileInfoDialog();
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
    QMessageBox::warning(this, tr("nd2-viewer"), message);
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

void MainWindow::buildMenus()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    openAction_ = fileMenu->addAction(tr("&Open…"));
    openAction_->setShortcut(QKeySequence::Open);
    connect(openAction_, &QAction::triggered, this, &MainWindow::openFile);

    reloadAction_ = fileMenu->addAction(tr("&Reload Frame"));
    reloadAction_->setShortcut(tr("F5"));
    connect(reloadAction_, &QAction::triggered, &controller_, &DocumentController::reloadCurrentFrame);

    fileInfoAction_ = fileMenu->addAction(tr("File &Info…"));
    fileInfoAction_->setShortcut(tr("Ctrl+I"));
    connect(fileInfoAction_, &QAction::triggered, this, &MainWindow::showFileInfoDialog);

    fileMenu->addSeparator();
    auto *exportMenu = fileMenu->addMenu(tr("&Export"));

    auto *exportCurrentFrameAction = exportMenu->addAction(tr("Frame..."));
    connect(exportCurrentFrameAction, &QAction::triggered, this, &MainWindow::saveCurrentFrameAs);

    auto *exportMovieAction = exportMenu->addAction(tr("Movie..."));
    connect(exportMovieAction, &QAction::triggered, this, &MainWindow::exportMovieAs);

    auto *exportCurrentRoiAction = exportMenu->addAction(tr("ROI Frame..."));
    connect(exportCurrentRoiAction, &QAction::triggered, this, &MainWindow::saveCurrentRoiAs);

    auto *exportRoiMovieAction = exportMenu->addAction(tr("ROI Movie..."));
    connect(exportRoiMovieAction, &QAction::triggered, this, &MainWindow::exportRoiMovieAs);

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
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto *navigationSection = new QWidget(central);
    auto *navigationLayout = new QVBoxLayout(navigationSection);
    navigationLayout->setContentsMargins(0, 0, 0, 0);
    navigationLayout->setSpacing(4);
    navigationLayout->addWidget(createSectionTitle(tr("Navigation"), navigationSection));

    navigatorContainer_ = new QWidget(navigationSection);
    navigatorRowsLayout_ = new QVBoxLayout(navigatorContainer_);
    navigatorRowsLayout_->setContentsMargins(0, 0, 0, 0);
    navigatorRowsLayout_->setSpacing(6);
    navigatorEmptyLabel_ = new QLabel(QString(), navigatorContainer_);
    navigatorEmptyLabel_->setWordWrap(true);
    navigatorRowsLayout_->addWidget(navigatorEmptyLabel_);
    navigatorRowsLayout_->addStretch(1);
    navigationLayout->addWidget(navigatorContainer_);

    auto *contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(8);

    auto *canvasPane = new QWidget(central);
    auto *canvasLayout = new QVBoxLayout(canvasPane);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->setSpacing(4);
    canvasLayout->addWidget(createSectionTitle(tr("Viewer"), canvasPane));

    auto *viewModeRow = new QWidget(canvasPane);
    auto *viewModeLayout = new QHBoxLayout(viewModeRow);
    viewModeLayout->setContentsMargins(0, 0, 0, 0);
    viewModeLayout->setSpacing(8);

    viewModeControl_ = new QWidget(viewModeRow);
    viewModeControl_->setObjectName(QStringLiteral("viewModeSwitch"));
    viewModeControl_->setVisible(false);
    viewModeControl_->setStyleSheet(QStringLiteral(
        "#viewModeSwitch {"
        " border: 1px solid palette(mid);"
        " border-radius: 11px;"
        " background: palette(base);"
        "}"
        "#viewModeSwitch QPushButton {"
        " border: 0;"
        " border-radius: 10px;"
        " padding: 6px 14px;"
        "}"
        "#viewModeSwitch QPushButton:checked {"
        " background: palette(highlight);"
        " color: palette(highlighted-text);"
        " font-weight: 600;"
        "}"));
    auto *viewModeSwitchLayout = new QHBoxLayout(viewModeControl_);
    viewModeSwitchLayout->setContentsMargins(2, 2, 2, 2);
    viewModeSwitchLayout->setSpacing(2);

    view2dButton_ = new QPushButton(tr("2D"), viewModeControl_);
    view2dButton_->setCheckable(true);
    view2dButton_->setAutoExclusive(true);
    view3dButton_ = new QPushButton(tr("3D"), viewModeControl_);
    view3dButton_->setCheckable(true);
    view3dButton_->setAutoExclusive(true);
    viewModeSwitchLayout->addWidget(view2dButton_);
    viewModeSwitchLayout->addWidget(view3dButton_);
    viewModeLayout->addWidget(viewModeControl_);

    viewActionButton_ = new QPushButton(tr("Fit"), viewModeRow);
    viewActionButton_->setEnabled(false);
    viewActionButton_->setVisible(false);
    viewActionButton_->setToolTip(tr("Fit the current image to the viewport once."));
    viewModeLayout->addWidget(viewActionButton_);
    viewModeLayout->addStretch(1);

    imageViewport_ = new ImageViewport(canvasPane);

    volumePage_ = new QWidget(canvasPane);
    auto *volumeOuterLayout = new QVBoxLayout(volumePage_);
    volumeOuterLayout->setContentsMargins(0, 0, 0, 0);
    volumeOuterLayout->setSpacing(0);

    connect(viewActionButton_, &QPushButton::clicked, this, &MainWindow::triggerViewAction);

    viewerStack_ = new QStackedWidget(canvasPane);
    viewerStack_->addWidget(imageViewport_);
    viewerStack_->addWidget(volumePage_);

    canvasLayout->addWidget(viewModeRow, 0);
    canvasLayout->addWidget(viewerStack_, 1);

    auto *channelsSection = new QWidget(central);
    auto *channelsLayout = new QVBoxLayout(channelsSection);
    channelsLayout->setContentsMargins(0, 0, 0, 0);
    channelsLayout->setSpacing(4);
    channelControlsWidget_ = new ChannelControlsWidget(channelsSection);
    channelsLayout->addWidget(createSectionTitle(tr("Channels"), channelsSection));
    channelsLayout->addWidget(channelControlsWidget_);
    channelsSection->setMinimumWidth(320);
    channelsSection->setMaximumWidth(420);

    contentLayout->addWidget(canvasPane, 1);
    contentLayout->addWidget(channelsSection, 0);

    layout->addWidget(navigationSection, 0);
    layout->addLayout(contentLayout, 1);
    setCentralWidget(central);
}

void MainWindow::rebuildNavigatorControls()
{
    stopTimePlayback();
    timePlaybackButton_ = nullptr;
    timePlaybackLoopIndex_ = -1;
    clearLayout(navigatorRowsLayout_);
    loopControls_.clear();

    if (!controller_.hasDocument()) {
        navigatorEmptyLabel_ = new QLabel(QString(), navigatorContainer_);
        navigatorEmptyLabel_->setWordWrap(true);
        navigatorRowsLayout_->addWidget(navigatorEmptyLabel_);
        navigatorRowsLayout_->addStretch(1);
        return;
    }

    const DocumentInfo &info = controller_.documentInfo();
    if (info.loops.isEmpty()) {
        navigatorEmptyLabel_ = new QLabel(tr("This file has a single frame and no experiment loops."), navigatorContainer_);
        navigatorEmptyLabel_->setWordWrap(true);
        navigatorRowsLayout_->addWidget(navigatorEmptyLabel_);
        navigatorRowsLayout_->addStretch(1);
        return;
    }

    const int timeLoopIndex = findTimeLoopIndex();

    for (int index = 0; index < info.loops.size(); ++index) {
        const LoopInfo &loop = info.loops.at(index);
        LoopWidgets widgets;
        widgets.row = new QWidget(navigatorContainer_);
        auto *rowLayout = new QHBoxLayout(widgets.row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        widgets.label = new QLabel(QStringLiteral("%1").arg(loop.label), widgets.row);
        widgets.label->setMinimumWidth(84);
        QToolButton *playButton = nullptr;
        if (index == timeLoopIndex) {
            playButton = new QToolButton(widgets.row);
            playButton->setAutoRaise(true);
            playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
            playButton->setToolTip(tr("Choose a playback step and start the time loop."));
            timePlaybackButton_ = playButton;
            timePlaybackLoopIndex_ = index;
        }
        widgets.slider = new QSlider(Qt::Horizontal, widgets.row);
        widgets.slider->setRange(0, qMax(loop.size - 1, 0));
        widgets.slider->setTracking(false);
        widgets.slider->installEventFilter(this);
        widgets.spinBox = new QSpinBox(widgets.row);
        widgets.spinBox->setRange(0, qMax(loop.size - 1, 0));
        widgets.details = new QLabel(QStringLiteral("%1 · %2 steps").arg(loop.type, QString::number(loop.size)), widgets.row);
        widgets.details->setMinimumWidth(120);

        rowLayout->addWidget(widgets.label);
        if (playButton) {
            rowLayout->addWidget(playButton);
        }
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

        if (playButton) {
            connect(playButton, &QToolButton::clicked, this, &MainWindow::handleTimePlaybackButton);
        }
    }

    navigatorRowsLayout_->addStretch(1);

    applyZLoopNavigatorLock();
}

void MainWindow::commitLoopSliderValue(int loopIndex)
{
    if (loopIndex < 0 || loopIndex >= loopControls_.size()) {
        return;
    }

    const auto &loopWidgets = loopControls_.at(loopIndex);
    controller_.setCoordinateValue(loopIndex, loopWidgets.slider->sliderPosition());
}

void MainWindow::handleTimePlaybackButton()
{
    if (timePlaybackActive_) {
        stopTimePlayback();
        return;
    }

    if (movieExportInProgress_ || !controller_.hasDocument() || timePlaybackLoopIndex_ < 0) {
        return;
    }

    const DocumentInfo &info = controller_.documentInfo();
    if (timePlaybackLoopIndex_ >= info.loops.size()) {
        return;
    }

    const int maximumStep = qMax(info.loops.at(timePlaybackLoopIndex_).size, 1);
    bool accepted = false;
    const int selectedStep = QInputDialog::getInt(this,
                                                  tr("Time Playback"),
                                                  tr("Speed up / frame step"),
                                                  qBound(1, timePlaybackStep_, maximumStep),
                                                  1,
                                                  maximumStep,
                                                  1,
                                                  &accepted);
    if (!accepted) {
        return;
    }

    timePlaybackStep_ = selectedStep;
    startTimePlayback();
}

void MainWindow::startTimePlayback()
{
    if (movieExportInProgress_ || !controller_.hasDocument() || timePlaybackLoopIndex_ < 0) {
        stopTimePlayback();
        return;
    }

    const DocumentInfo &info = controller_.documentInfo();
    if (timePlaybackLoopIndex_ >= info.loops.size()) {
        stopTimePlayback();
        return;
    }

    const int lastFrame = info.loops.at(timePlaybackLoopIndex_).size - 1;
    timePlaybackTimeValues_ = buildTimeFrameValues(0, lastFrame, qMax(timePlaybackStep_, 1));
    if (timePlaybackTimeValues_.isEmpty()) {
        stopTimePlayback();
        return;
    }

    const QVector<int> coordinateValues = controller_.coordinateState().values;
    const int currentTimeValue = (timePlaybackLoopIndex_ < coordinateValues.size()) ? coordinateValues.at(timePlaybackLoopIndex_) : 0;
    const int currentIndex = timePlaybackTimeValues_.indexOf(currentTimeValue);
    if (currentIndex >= 0) {
        timePlaybackNextFrameIndex_ = (currentIndex + 1) % timePlaybackTimeValues_.size();
    } else {
        timePlaybackNextFrameIndex_ = 0;
    }

    if (!timePlaybackTimer_) {
        timePlaybackTimer_ = new QTimer(this);
        connect(timePlaybackTimer_, &QTimer::timeout, this, &MainWindow::advanceTimePlayback);
    }
    timePlaybackTimer_->setInterval(100);
    timePlaybackAwaitingFrame_ = false;
    timePlaybackActive_ = true;
    if (timePlaybackButton_) {
        timePlaybackButton_->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
        timePlaybackButton_->setToolTip(tr("Stop time loop playback."));
    }
    statusBar()->showMessage(tr("Playing time loop with step %1…").arg(timePlaybackStep_));
    timePlaybackTimer_->start();
}

void MainWindow::stopTimePlayback()
{
    const bool wasActive = timePlaybackActive_;
    timePlaybackActive_ = false;
    timePlaybackAwaitingFrame_ = false;
    timePlaybackNextFrameIndex_ = 0;
    timePlaybackTimeValues_.clear();

    if (timePlaybackTimer_) {
        timePlaybackTimer_->stop();
    }
    if (timePlaybackButton_) {
        timePlaybackButton_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        timePlaybackButton_->setToolTip(tr("Choose a playback step and start the time loop."));
    }

    if (wasActive) {
        statusBar()->showMessage(tr("Time playback paused."), 2000);
    }
}

void MainWindow::advanceTimePlayback()
{
    if (!timePlaybackActive_ || timePlaybackAwaitingFrame_ || timePlaybackLoopIndex_ < 0 || timePlaybackTimeValues_.isEmpty()) {
        return;
    }

    if (timePlaybackNextFrameIndex_ >= timePlaybackTimeValues_.size()) {
        timePlaybackNextFrameIndex_ = 0;
    }

    const int nextTimeValue = timePlaybackTimeValues_.at(timePlaybackNextFrameIndex_);
    const QVector<int> coordinateValues = controller_.coordinateState().values;
    if (timePlaybackLoopIndex_ < coordinateValues.size() && coordinateValues.at(timePlaybackLoopIndex_) == nextTimeValue) {
        timePlaybackNextFrameIndex_ = (timePlaybackNextFrameIndex_ + 1) % timePlaybackTimeValues_.size();
        return;
    }

    timePlaybackAwaitingFrame_ = true;
    controller_.setCoordinateValue(timePlaybackLoopIndex_, nextTimeValue);
}

void MainWindow::completeTimePlaybackStep()
{
    timePlaybackAwaitingFrame_ = false;
    if (!timePlaybackTimeValues_.isEmpty()) {
        timePlaybackNextFrameIndex_ = (timePlaybackNextFrameIndex_ + 1) % timePlaybackTimeValues_.size();
    }
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
        const DocumentInfo &info = controller_.documentInfo();
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

    const DocumentInfo &info = controller_.documentInfo();
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

QString MainWindow::buildDefaultMovieSavePath(ExportScope scope, const MovieExportSettings &settings) const
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

    const DocumentInfo &info = controller_.documentInfo();
    QStringList nameParts{sanitizeToken(baseName)};
    for (int index = 0; index < info.loops.size() && index < settings.fixedCoordinates.size(); ++index) {
        if (index == settings.timeLoopIndex) {
            continue;
        }

        const QString label = sanitizeToken(info.loops.at(index).label);
        nameParts << QStringLiteral("%1%2").arg(label, QString::number(settings.fixedCoordinates.at(index) + 1));
    }
    if (scope == ExportScope::Roi && imageViewport_->hasRoi()) {
        const QRect roi = imageViewport_->roiRect();
        nameParts << QStringLiteral("roi_x%1_y%2_w%3_h%4")
                         .arg(roi.x())
                         .arg(roi.y())
                         .arg(roi.width())
                         .arg(roi.height());
    }

    nameParts << QStringLiteral("movie_start%1_end%2_step%3")
                     .arg(settings.startFrame)
                     .arg(settings.endFrame)
                     .arg(settings.frameStep());

    return QDir(directory).filePath(nameParts.join(QStringLiteral("_")) + QStringLiteral(".mp4"));
}

int MainWindow::findTimeLoopIndex() const
{
    const DocumentInfo &info = controller_.documentInfo();
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

bool MainWindow::isVolumeViewActive() const
{
    return viewerStack_ && viewerStack_->currentIndex() == 1;
}

bool MainWindow::hasUsableZStack() const
{
    return controller_.hasDocument() && VolumeUtils::findZLoopIndex(controller_.documentInfo()) >= 0;
}

bool MainWindow::volumeMatchesCurrentFixedCoordinates() const
{
    if (!cachedVolume_.isValid()) {
        return false;
    }

    const int z = documentZLoopIndex_;
    if (z < 0) {
        return false;
    }

    const QVector<int> &current = controller_.coordinateState().values;
    const QVector<int> &fixed = cachedVolume_.fixedCoordinates.values;
    const int loopCount = controller_.documentInfo().loops.size();

    for (int i = 0; i < loopCount; ++i) {
        if (i == z) {
            continue;
        }

        const int c = i < current.size() ? current.at(i) : 0;
        const int f = i < fixed.size() ? fixed.at(i) : 0;
        if (c != f) {
            return false;
        }
    }

    return true;
}

void MainWindow::applyZLoopNavigatorLock()
{
    const int z = documentZLoopIndex_;
    if (z < 0 || z >= loopControls_.size()) {
        return;
    }

    const bool lock = isVolumeViewActive();
    loopControls_[z].slider->setEnabled(!lock);
    loopControls_[z].spinBox->setEnabled(!lock);
}

void MainWindow::syncVolumeViewportChannelSettings()
{
    if (!isVolumeViewActive() || !cachedVolume_.isValid() || !volumeViewport_) {
        return;
    }

    volumeViewport_->setChannelSettings(controller_.channelSettings());
}

void MainWindow::ensureVolumeViewport()
{
    if (volumeViewport_ || !volumePage_) {
        return;
    }

    auto *volumeOuterLayout = qobject_cast<QVBoxLayout *>(volumePage_->layout());
    if (!volumeOuterLayout) {
        volumeOuterLayout = new QVBoxLayout(volumePage_);
        volumeOuterLayout->setContentsMargins(0, 0, 0, 0);
        volumeOuterLayout->setSpacing(0);
    }

    volumeViewport_ = new VolumeViewport3D(volumePage_);
    volumeOuterLayout->addWidget(volumeViewport_, 1);
}

void MainWindow::setLiveAutoForAllChannels(bool enabled)
{
    controller_.setLiveAutoEnabled(enabled);

    if (enabled) {
        autoContrastAllForActiveView();
    }
}

void MainWindow::applyVolumeViewMode(bool volumeViewActive)
{
    if (!controller_.hasDocument()) {
        return;
    }

    if (volumeViewActive && !hasUsableZStack()) {
        return;
    }

    if (volumeViewActive) {
        ensureVolumeViewport();
    }

    viewerStack_->setCurrentIndex(volumeViewActive ? 1 : 0);
    channelControlsWidget_->setLiveAutoInteractive(!volumeViewActive);
    channelControlsWidget_->setAutoContrastTuningEnabled(!volumeViewActive);
    applyZLoopNavigatorLock();

    if (volumeViewActive) {
        if (volumeMatchesCurrentFixedCoordinates() && cachedVolume_.isValid()) {
            if (volumeViewportHasVolume_) {
                volumeViewport_->setChannelSettings(controller_.channelSettings());
            } else {
                volumeViewport_->setVolume(cachedVolume_, controller_.channelSettings());
                volumeViewportHasVolume_ = true;
            }
            if (!volumeViewport_->lastError().isEmpty()) {
                QMessageBox::warning(this, tr("3D View"), volumeViewport_->lastError());
            }
        } else {
            startVolumeLoad();
        }
    }

    updateViewModeButtons();
}

void MainWindow::startVolumeLoad()
{
    if (!controller_.hasDocument() || !hasUsableZStack() || !isVolumeViewActive()) {
        return;
    }

    ensureVolumeViewport();

    if (volumeViewport_ && cachedVolume_.isValid()) {
        pendingVolumeCameraState_ = volumeViewport_->cameraState();
    } else {
        pendingVolumeCameraState_ = {};
    }

    ++volumeLoadGeneration_;
    const int generation = volumeLoadGeneration_;

    statusBar()->showMessage(tr("Loading 3D volume…"));
    updateViewModeButtons();

    const QString path = controller_.currentPath();
    const DocumentInfo info = controller_.documentInfo();
    const FrameCoordinateState coords = controller_.coordinateState();
    const QVector<ChannelRenderSettings> seed =
        (movieExportInProgress_ && movieExportVolumeView_ && !movieExportFrozenChannelSettings_.isEmpty())
            ? movieExportFrozenChannelSettings_
            : controller_.channelSettings();
    const DocumentReaderOptions readerOptions = (movieExportInProgress_ && movieExportVolumeView_)
                                                    ? movieExportReaderOptions()
                                                    : DocumentReaderOptions{};

    volumeWatcher_.setFuture(QtConcurrent::run([path, info, coords, seed, generation, readerOptions]() {
        VolumeLoadResult result = VolumeLoader::load(path, info, coords, seed, readerOptions);
        result.loadRequestId = generation;
        return result;
    }));
}

void MainWindow::handleVolumeLoadFinished()
{
    const VolumeLoadResult result = volumeWatcher_.result();
    if (result.loadRequestId != volumeLoadGeneration_) {
        return;
    }

    if (!isVolumeViewActive()) {
        if (result.success && result.volume.isValid()) {
            cachedVolume_ = result.volume;
            volumeViewportHasVolume_ = false;
        }
        return;
    }

    if (!result.success || !result.volume.isValid()) {
        qWarning("3D volume load failed: %s", qPrintable(result.error));
        if (movieExportInProgress_ && movieExportVolumeView_) {
            finishMovieExportPlayback(result.error.isEmpty()
                                          ? tr("The 3D volume could not be prepared for export.")
                                          : result.error);
            return;
        }
        if (timePlaybackActive_) {
            stopTimePlayback();
        }
        QMessageBox::warning(this,
                             tr("3D View"),
                             result.error.isEmpty() ? tr("The 3D volume could not be prepared.") : result.error);
        viewerStack_->setCurrentIndex(0);
        channelControlsWidget_->setLiveAutoInteractive(true);
        channelControlsWidget_->setAutoContrastTuningEnabled(true);
        applyZLoopNavigatorLock();
        cachedVolume_ = {};
        volumeViewportHasVolume_ = false;
        updateViewModeButtons();
        return;
    }

    cachedVolume_ = result.volume;
    const QVector<ChannelRenderSettings> channelSettings =
        (movieExportInProgress_ && movieExportVolumeView_ && !movieExportFrozenChannelSettings_.isEmpty())
            ? movieExportFrozenChannelSettings_
            : controller_.channelSettings();
    volumeViewport_->setVolume(cachedVolume_, channelSettings);
    volumeViewportHasVolume_ = true;
    if (movieExportInProgress_ && movieExportVolumeView_) {
        volumeViewport_->setCameraState(movieExportFrozenCameraState_);
    } else if (pendingVolumeCameraState_.valid) {
        volumeViewport_->setCameraState(pendingVolumeCameraState_);
    }
    pendingVolumeCameraState_ = {};
    statusBar()->showMessage(tr("Loaded 3D volume %1").arg(volumeViewport_->renderSummary()), 3000);
    updateViewModeButtons();
    if (timePlaybackActive_ && timePlaybackAwaitingFrame_) {
        completeTimePlaybackStep();
    }

    if (movieExportInProgress_ && movieExportVolumeView_ && movieExportAwaitingFrame_) {
        volumeViewport_->setCameraState(movieExportFrozenCameraState_);

        QImage image = captureCurrentVolumeImage();
        if (image.isNull()) {
            finishMovieExportPlayback(tr("The current 3D view could not be captured for movie export."));
            return;
        }
        if (image.size() != movieExportSettings_.outputSize) {
            image = image.scaled(movieExportSettings_.outputSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        image = image.convertToFormat(QImage::Format_ARGB32);
        if (image.isNull()) {
            finishMovieExportPlayback(tr("The current 3D view could not be prepared for movie export."));
            return;
        }

        ++movieExportNextFrameIndex_;
        consumePreparedMovieExportImage(image);
        return;
    }

    if (!volumeViewport_->lastError().isEmpty()) {
        QMessageBox::warning(this, tr("3D View"), volumeViewport_->lastError());
    }
}

void MainWindow::maybeReloadVolumeForNonZCoordinateChange()
{
    if (movieExportRestoringState_) {
        return;
    }

    if (!isVolumeViewActive()) {
        return;
    }

    if (!cachedVolume_.isValid() || !volumeMatchesCurrentFixedCoordinates()) {
        startVolumeLoad();
    }
}

void MainWindow::autoContrastChannelForActiveView(int channelIndex)
{
    if (isVolumeViewActive() && cachedVolume_.isValid()) {
        const QVector<ChannelRenderSettings> settings = controller_.channelSettings();
        if (channelIndex < 0 || channelIndex >= settings.size()) {
            return;
        }

        ChannelRenderSettings channelSettings = settings.at(channelIndex);
        if (!FrameRenderer::applyAutoContrastToChannelFromZSlices(cachedVolume_, channelIndex, channelSettings)) {
            return;
        }

        controller_.setChannelSettings(channelIndex, channelSettings);
        syncVolumeViewportChannelSettings();
        return;
    }

    controller_.autoContrastChannel(channelIndex);
}

void MainWindow::autoContrastAllForActiveView()
{
    if (isVolumeViewActive() && cachedVolume_.isValid()) {
        const QVector<ChannelRenderSettings> settings = controller_.channelSettings();
        bool anyChanged = false;
        for (int channelIndex = 0; channelIndex < settings.size(); ++channelIndex) {
            ChannelRenderSettings channelSettings = settings.at(channelIndex);
            const bool channelChanged = FrameRenderer::applyAutoContrastToChannelFromZSlices(cachedVolume_, channelIndex,
                                                                                             channelSettings);

            if (channelChanged) {
                controller_.setChannelSettings(channelIndex, channelSettings);
                anyChanged = true;
            }
        }

        if (anyChanged) {
            syncVolumeViewportChannelSettings();
        }
        return;
    }

    controller_.autoContrastAllChannels();
}

void MainWindow::startMovieExportPlayback(const MovieExportSettings &settings)
{
    stopTimePlayback();

    const QString backendUnsupportedReason = movieExportBackendUnsupportedReason();
    if (!backendUnsupportedReason.isEmpty()) {
        QMessageBox::warning(this, tr("Movie Export Failed"), backendUnsupportedReason);
        return;
    }

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
    movieExportDocumentInfo_ = controller_.documentInfo();
    movieExportTimeValues_ = buildTimeFrameValues(movieExportSettings_);
    movieExportVolumeView_ = isVolumeViewActive() && volumeViewport_ && cachedVolume_.isValid();
    movieExportReadIssueLog_ = std::make_shared<ReadIssueLog>();
    movieExportOriginalChannelSettings_ = controller_.channelSettings();
    movieExportOriginalLiveAutoEnabled_ = controller_.liveAutoEnabled();
    movieExportOriginalTimeValue_ =
        (movieExportSettings_.timeLoopIndex >= 0 && movieExportSettings_.timeLoopIndex < controller_.coordinateState().values.size())
            ? controller_.coordinateState().values.at(movieExportSettings_.timeLoopIndex)
            : -1;

    if (movieExportTimeValues_.isEmpty()) {
        QMessageBox::warning(this,
                             tr("Movie Export Failed"),
                             tr("The requested time range does not contain any exportable frames."));
        return;
    }
    if (movieExportSettings_.timeLoopIndex < 0
        || movieExportSettings_.timeLoopIndex >= controller_.coordinateState().values.size()) {
        QMessageBox::warning(this,
                             tr("Movie Export Failed"),
                             tr("The selected time loop is unavailable for movie export."));
        movieExportSettings_ = {};
        movieExportDocumentInfo_ = {};
        movieExportTimeValues_.clear();
        movieExportReadIssueLog_.reset();
        movieExportOriginalChannelSettings_.clear();
        movieExportOriginalLiveAutoEnabled_ = false;
        movieExportOriginalTimeValue_ = -1;
        return;
    }

    movieExportNextFrameIndex_ = 0;
    movieExportEncodedFrameCount_ = 0;
    movieExportAwaitingFrame_ = false;
    movieExportPendingEndOfStream_ = false;
    movieExportEndOfStreamSent_ = false;
    movieVideoFrameInputReady_ = false;
    moviePendingFrame_ = {};

    if (movieExportVolumeView_) {
        movieExportFrozenChannelSettings_ = movieExportSettings_.channelSettings;
        movieExportSettings_.liveAutoEnabled = false;
        movieExportSettings_.channelSettings = movieExportFrozenChannelSettings_;
        movieExportFrozenCameraState_ = volumeViewport_->cameraState();
        movieExportOriginalCameraState_ = movieExportFrozenCameraState_;
        movieExportOriginalVolume_ = cachedVolume_;
    } else {
        movieExportFrozenChannelSettings_ = controller_.channelSettings();
        movieExportSettings_.liveAutoEnabled = false;
        movieExportSettings_.channelSettings = movieExportFrozenChannelSettings_;
    }
    controller_.setReadOptions(movieExportReaderOptions());
    controller_.setLiveAutoEnabled(false);

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
                    statusBar()->showMessage(movieExportVolumeView_
                                                 ? tr("Exporting 3D movie through the viewer…")
                                                 : tr("Exporting movie live through the viewer…"));
                    movieVideoFrameInputReady_ = true;
                    if (moviePendingFrame_.isValid()) {
                        trySendMovieExportFrame();
                    } else {
                        requestNextMovieExportFrame();
                    }
                } else if (state == QMediaRecorder::StoppedState && movieExportEndOfStreamSent_) {
                    finishMovieExportPlayback();
                }
            });

    setMovieExportUiState(true);
    statusBar()->showMessage(movieExportVolumeView_
                                 ? tr("Preparing 3D movie export…")
                                 : tr("Preparing live movie export…"));
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
    statusBar()->showMessage((movieExportVolumeView_ ? tr("Exporting 3D movie: preparing frame %1…")
                                                     : tr("Exporting movie live: preparing frame %1…"))
                                 .arg(timeValue));
    movieExportAwaitingFrame_ = true;

    const FrameCoordinateState &currentState = controller_.coordinateState();
    const bool alreadyVisible =
        movieExportSettings_.timeLoopIndex >= 0 && movieExportSettings_.timeLoopIndex < currentState.values.size()
        && currentState.values.at(movieExportSettings_.timeLoopIndex) == timeValue;
    if (alreadyVisible) {
        if (movieExportVolumeView_) {
            if (!cachedVolume_.isValid() || !volumeMatchesCurrentFixedCoordinates()) {
                startVolumeLoad();
                return;
            }

            volumeViewport_->setCameraState(movieExportFrozenCameraState_);
            QImage image = captureCurrentVolumeImage();
            if (image.isNull()) {
                finishMovieExportPlayback(tr("The current 3D view could not be captured for movie export."));
                return;
            }
            if (image.size() != movieExportSettings_.outputSize) {
                image = image.scaled(movieExportSettings_.outputSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
            image = image.convertToFormat(QImage::Format_ARGB32);
            if (image.isNull()) {
                finishMovieExportPlayback(tr("The current 3D view could not be prepared for movie export."));
                return;
            }

            ++movieExportNextFrameIndex_;
            consumePreparedMovieExportImage(image);
            return;
        }

        if (controller_.renderedFrame().image.isNull()) {
            controller_.reloadCurrentFrame();
            return;
        }

        prepareCurrentMovieExportFrame();
        return;
    }

    controller_.setCoordinateValue(movieExportSettings_.timeLoopIndex, timeValue);
}

DocumentReaderOptions MainWindow::movieExportReaderOptions() const
{
    DocumentReaderOptions options;
    options.failurePolicy = ReadFailurePolicy::SubstituteBlack;
    options.issueLog = movieExportReadIssueLog_;
    return options;
}

QString MainWindow::movieExportBackendUnsupportedReason() const
{
    const QString selectedBackend = qEnvironmentVariable("QT_MEDIA_BACKEND");
    if (selectedBackend.compare(QStringLiteral("ffmpeg"), Qt::CaseInsensitive) == 0) {
        return {};
    }

    QStringList libraryPaths = QCoreApplication::libraryPaths();
    const QString qtPluginPath = QLibraryInfo::path(QLibraryInfo::PluginsPath);
    if (!qtPluginPath.isEmpty() && !libraryPaths.contains(qtPluginPath)) {
        libraryPaths.push_back(qtPluginPath);
    }

    if (hasQtFfmpegMediaPlugin(libraryPaths)) {
        return tr("Movie export requires Qt's FFmpeg multimedia backend because it uses QVideoFrameInput. "
                  "Set QT_MEDIA_BACKEND=ffmpeg before creating the application.");
    }

    return tr("Movie export requires Qt's FFmpeg multimedia backend because it uses QVideoFrameInput. "
              "This Qt installation does not provide the FFmpeg multimedia plugin.");
}

void MainWindow::prepareCurrentMovieExportFrame()
{
    if (!movieExportInProgress_ || movieExportVolumeView_ || movieExportNextFrameIndex_ >= movieExportTimeValues_.size()) {
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
    if (image.size() != movieExportSettings_.outputSize) {
        image = image.scaled(movieExportSettings_.outputSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    image = image.convertToFormat(QImage::Format_ARGB32);
    if (image.isNull()) {
        finishMovieExportPlayback(tr("The current rendered frame could not be prepared for movie export."));
        return;
    }

    ++movieExportNextFrameIndex_;
    consumePreparedMovieExportImage(image);
}

void MainWindow::consumePreparedMovieExportImage(const QImage &image)
{
    movieExportAwaitingFrame_ = false;

    moviePendingFrame_ = QVideoFrame(image);
    moviePendingFrame_.setStreamFrameRate(movieExportSettings_.fps);
    const qint64 startTimeUs = qRound64((movieExportEncodedFrameCount_ * 1000000.0) / movieExportSettings_.fps);
    const qint64 endTimeUs = qRound64(((movieExportEncodedFrameCount_ + 1) * 1000000.0) / movieExportSettings_.fps);
    moviePendingFrame_.setStartTime(startTimeUs);
    moviePendingFrame_.setEndTime(endTimeUs);
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
        statusBar()->showMessage((movieExportVolumeView_
                                      ? tr("Exporting 3D movie: frame %1 of %2…")
                                      : tr("Exporting movie live: frame %1 of %2…"))
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
    const bool volumeView = movieExportVolumeView_;
    const QVector<ReadIssue> issues = movieExportReadIssueLog_ ? movieExportReadIssueLog_->snapshot() : QVector<ReadIssue>{};
    QString warningReportPath;
    QString warningReportError;
    if (success && !issues.isEmpty()) {
        const QString reportPath = buildMovieExportWarningReportPath(outputPath);
        if (writeMovieExportWarningReport(reportPath, movieExportSettings_, volumeView, issues, &warningReportError)) {
            warningReportPath = reportPath;
        }
    }

    cleanupMovieExportPlayback();

    if (success) {
        statusBar()->showMessage(tr("Exported movie to %1").arg(QDir::toNativeSeparators(outputPath)), 5000);
        QString message = tr("Exported %1 frame(s) to:\n%2")
                              .arg(encodedFrameCount)
                              .arg(QDir::toNativeSeparators(outputPath));
        if (!warningReportPath.isEmpty()) {
            message += tr("\n\nSome frames or slices were substituted with black. A warning report was written to:\n%1")
                           .arg(QDir::toNativeSeparators(warningReportPath));
        } else if (!warningReportError.isEmpty()) {
            message += tr("\n\nSome frames or slices were substituted with black, but the warning report could not be written:\n%1")
                           .arg(warningReportError);
        }
        QMessageBox::information(this,
                                 tr("Movie Export Complete"),
                                 message);
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

    ++volumeLoadGeneration_;
    setMovieExportUiState(false);
    controller_.setReadOptions({});
    controller_.setLiveAutoEnabled(movieExportOriginalLiveAutoEnabled_);
    if (!movieExportOriginalChannelSettings_.isEmpty()) {
        controller_.setChannelSettings(movieExportOriginalChannelSettings_);
    }
    restoreVolumeViewportAfterMovieExport();

    moviePendingFrame_ = {};
    movieExportTimeValues_.clear();
    movieExportNextFrameIndex_ = 0;
    movieExportEncodedFrameCount_ = 0;
    movieExportAwaitingFrame_ = false;
    movieExportPendingEndOfStream_ = false;
    movieExportEndOfStreamSent_ = false;
    movieVideoFrameInputReady_ = false;
    movieExportReadIssueLog_.reset();
    movieExportSettings_ = {};
    movieExportDocumentInfo_ = {};
}

void MainWindow::restoreVolumeViewportAfterMovieExport()
{
    movieExportRestoringState_ = true;
    if (movieExportOriginalTimeValue_ >= 0 && movieExportSettings_.timeLoopIndex >= 0) {
        controller_.setCoordinateValue(movieExportSettings_.timeLoopIndex, movieExportOriginalTimeValue_);
    }

    if (!movieExportVolumeView_ || !volumeViewport_) {
        movieExportRestoringState_ = false;
        movieExportVolumeView_ = false;
        movieExportFrozenChannelSettings_.clear();
        movieExportOriginalChannelSettings_.clear();
        movieExportOriginalLiveAutoEnabled_ = false;
        movieExportOriginalTimeValue_ = -1;
        movieExportFrozenCameraState_ = {};
        movieExportOriginalVolume_ = {};
        movieExportOriginalCameraState_ = {};
        return;
    }

    if (movieExportOriginalVolume_.isValid()) {
        volumeViewport_->setVolume(movieExportOriginalVolume_, controller_.channelSettings());
        volumeViewportHasVolume_ = true;
        volumeViewport_->setCameraState(movieExportOriginalCameraState_);
    } else {
        volumeViewportHasVolume_ = false;
    }
    movieExportRestoringState_ = false;
    movieExportVolumeView_ = false;
    movieExportFrozenChannelSettings_.clear();
    movieExportOriginalChannelSettings_.clear();
    movieExportOriginalLiveAutoEnabled_ = false;
    movieExportOriginalTimeValue_ = -1;
    movieExportFrozenCameraState_ = {};
    movieExportOriginalVolume_ = {};
    movieExportOriginalCameraState_ = {};
    updateViewModeButtons();
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
    if (fileInfoAction_) {
        fileInfoAction_->setEnabled(!active);
    }
    if (quitAction_) {
        quitAction_->setEnabled(!active);
    }
    if (timePlaybackButton_) {
        timePlaybackButton_->setEnabled(!active);
    }
    updateViewModeButtons();
}

QImage MainWindow::captureCurrentVolumeImage() const
{
    if (!volumeViewport_) {
        return {};
    }

    return volumeViewport_->captureImage();
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

    const DocumentInfo &info = controller_.documentInfo();
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
