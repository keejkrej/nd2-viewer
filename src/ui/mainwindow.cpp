#include "ui/mainwindow.h"

#include "ui/channelcontrolswidget.h"
#include "ui/imageviewport.h"

#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QToolBar>
#include <QVBoxLayout>

namespace
{
QString prettyJson(const QJsonDocument &document)
{
    if (document.isNull()) {
        return QStringLiteral("{}");
    }
    return QString::fromUtf8(document.toJson(QJsonDocument::Indented));
}

QString scalarToString(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString().toHtmlEscaped();
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble());
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isNull() || value.isUndefined()) {
        return QStringLiteral("<i>null</i>");
    }
    return QStringLiteral("<i>complex</i>");
}

QString jsonSummaryHtml(const QJsonValue &value, int depth = 0)
{
    if (depth > 4) {
        return QStringLiteral("<i>…</i>");
    }

    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        QString html = QStringLiteral("<table cellspacing='0' cellpadding='4'>");
        int shown = 0;
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (shown++ >= 24) {
                html += QStringLiteral("<tr><td colspan='2'><i>More fields omitted…</i></td></tr>");
                break;
            }
            html += QStringLiteral("<tr><td valign='top'><b>%1</b></td><td>%2</td></tr>")
                        .arg(it.key().toHtmlEscaped(), jsonSummaryHtml(it.value(), depth + 1));
        }
        html += QStringLiteral("</table>");
        return html;
    }

    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        QString html = QStringLiteral("<ol>");
        const int count = std::min(static_cast<int>(array.size()), 12);
        for (int index = 0; index < count; ++index) {
            html += QStringLiteral("<li>%1</li>").arg(jsonSummaryHtml(array.at(index), depth + 1));
        }
        if (array.size() > count) {
            html += QStringLiteral("<li><i>More items omitted…</i></li>");
        }
        html += QStringLiteral("</ol>");
        return html;
    }

    return scalarToString(value);
}

QString documentOverviewHtml(const Nd2DocumentInfo &info)
{
    QString html;
    html += QStringLiteral("<h3>Overview</h3>");
    html += QStringLiteral("<table cellspacing='0' cellpadding='4'>");
    html += QStringLiteral("<tr><td><b>File</b></td><td>%1</td></tr>").arg(QFileInfo(info.filePath).fileName().toHtmlEscaped());
    html += QStringLiteral("<tr><td><b>Size</b></td><td>%1 × %2</td></tr>").arg(info.frameSize.width()).arg(info.frameSize.height());
    html += QStringLiteral("<tr><td><b>Frames</b></td><td>%1</td></tr>").arg(info.sequenceCount);
    html += QStringLiteral("<tr><td><b>Components</b></td><td>%1</td></tr>").arg(info.componentCount);
    html += QStringLiteral("<tr><td><b>Pixel type</b></td><td>%1</td></tr>").arg(info.pixelDataType.toHtmlEscaped());
    html += QStringLiteral("</table>");
    if (!info.loops.isEmpty()) {
        html += QStringLiteral("<h3>Loops</h3><ul>");
        for (const Nd2LoopInfo &loop : info.loops) {
            html += QStringLiteral("<li><b>%1</b> (%2): %3 steps</li>")
                        .arg(loop.label.toHtmlEscaped(), loop.type.toHtmlEscaped())
                        .arg(loop.size);
        }
        html += QStringLiteral("</ul>");
    }
    return html;
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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("nd2-viewer"));
    resize(1440, 920);

    buildCentralUi();
    buildMenus();
    buildDockUi();

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

    connect(channelControlsWidget_, &ChannelControlsWidget::channelSettingsChanged,
            &controller_, &DocumentController::setChannelSettings);
    connect(channelControlsWidget_, &ChannelControlsWidget::autoContrastRequested,
            &controller_, &DocumentController::autoContrastChannel);
    connect(channelControlsWidget_, &ChannelControlsWidget::autoContrastAllRequested,
            &controller_, &DocumentController::autoContrastAllChannels);

    updateDocumentUi();
}

void MainWindow::openFile()
{
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

void MainWindow::updateDocumentUi()
{
    rebuildNavigatorControls();
    channelControlsWidget_->setChannels(controller_.documentInfo().channels, controller_.channelSettings());
    updateCoordinateUi();
    updateChannelUi();
    updateMetadataUi();
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
    updateMetadataUi();
    updateInfoLabel();
}

void MainWindow::updateMetadataUi()
{
    const Nd2DocumentInfo &info = controller_.documentInfo();

    const QString attributesSummary = documentOverviewHtml(info) + jsonSummaryHtml(info.attributesJson.object());
    setMetadataContent(attributesWidgets_, attributesSummary, prettyJson(info.attributesJson));

    const QString experimentSummary = jsonSummaryHtml(info.experimentJson.isArray() ? QJsonValue(info.experimentJson.array())
                                                                                   : QJsonValue(info.experimentJson.object()));
    setMetadataContent(experimentWidgets_, experimentSummary, prettyJson(info.experimentJson));

    const QJsonDocument metadataDoc = controller_.currentFrameMetadata().isNull() ? info.metadataJson : controller_.currentFrameMetadata();
    const QString metadataSummary = QStringLiteral("<h3>Current Frame Metadata</h3>") + jsonSummaryHtml(metadataDoc.object());
    const QString metadataRaw = controller_.currentFrameMetadataText().isEmpty() ? prettyJson(info.metadataJson)
                                                                                 : controller_.currentFrameMetadataText();
    setMetadataContent(metadataWidgets_, metadataSummary, metadataRaw);

    const QString textInfoSummary = jsonSummaryHtml(info.textInfoJson.object());
    setMetadataContent(textInfoWidgets_, textInfoSummary, prettyJson(info.textInfoJson));
}

void MainWindow::showErrorMessage(const QString &message)
{
    if (message.isEmpty()) {
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
    auto *openAction = fileMenu->addAction(tr("&Open…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openFile);

    auto *reloadAction = fileMenu->addAction(tr("&Reload Frame"));
    reloadAction->setShortcut(tr("F5"));
    connect(reloadAction, &QAction::triggered, &controller_, &DocumentController::reloadCurrentFrame);

    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction(tr("E&xit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    auto *fitAction = viewMenu->addAction(tr("Fit to Window"));
    fitAction->setShortcut(tr("Ctrl+0"));
    connect(fitAction, &QAction::triggered, imageViewport_, &ImageViewport::zoomToFit);

    auto *actualSizeAction = viewMenu->addAction(tr("Actual Size"));
    actualSizeAction->setShortcut(tr("Ctrl+1"));
    connect(actualSizeAction, &QAction::triggered, imageViewport_, &ImageViewport::setActualSize);
}

void MainWindow::buildCentralUi()
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto *navigatorGroup = new QGroupBox(tr("Navigation"), central);
    auto *navigatorGroupLayout = new QVBoxLayout(navigatorGroup);
    navigatorGroupLayout->setContentsMargins(8, 8, 8, 8);
    navigatorGroupLayout->setSpacing(6);

    auto *navigatorScrollArea = new QScrollArea(navigatorGroup);
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
    navigatorGroupLayout->addWidget(navigatorScrollArea);

    imageViewport_ = new ImageViewport(central);

    layout->addWidget(navigatorGroup, 0);
    layout->addWidget(imageViewport_, 1);
    setCentralWidget(central);
}

void MainWindow::buildDockUi()
{
    auto *channelsDock = new QDockWidget(tr("Channels"), this);
    channelControlsWidget_ = new ChannelControlsWidget(channelsDock);
    channelsDock->setWidget(channelControlsWidget_);
    addDockWidget(Qt::RightDockWidgetArea, channelsDock);

    auto *metadataDock = new QDockWidget(tr("Metadata"), this);
    metadataTabs_ = new QTabWidget(metadataDock);
    attributesWidgets_ = addMetadataTab(tr("Attributes"));
    experimentWidgets_ = addMetadataTab(tr("Experiment"));
    metadataWidgets_ = addMetadataTab(tr("Metadata"));
    textInfoWidgets_ = addMetadataTab(tr("Text Info"));
    metadataDock->setWidget(metadataTabs_);
    addDockWidget(Qt::RightDockWidgetArea, metadataDock);
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

        connect(widgets.slider, &QSlider::valueChanged, widgets.spinBox, &QSpinBox::setValue);
        connect(widgets.spinBox, qOverload<int>(&QSpinBox::valueChanged), widgets.slider, &QSlider::setValue);
        connect(widgets.spinBox, qOverload<int>(&QSpinBox::valueChanged), this, [this, index](int value) {
            controller_.setCoordinateValue(index, value);
        });
    }

    navigatorRowsLayout_->addStretch(1);
}

MainWindow::MetadataWidgets MainWindow::addMetadataTab(const QString &title)
{
    auto *page = new QWidget(metadataTabs_);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto *splitter = new QSplitter(Qt::Vertical, page);
    auto *summary = new QTextBrowser(splitter);
    summary->setOpenExternalLinks(false);
    auto *raw = new QPlainTextEdit(splitter);
    raw->setReadOnly(true);

    splitter->addWidget(summary);
    splitter->addWidget(raw);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    layout->addWidget(splitter);

    metadataTabs_->addTab(page, title);
    return {summary, raw};
}

void MainWindow::setMetadataContent(const MetadataWidgets &widgets, const QString &summaryHtml, const QString &rawText)
{
    widgets.summary->setHtml(summaryHtml.isEmpty() ? QStringLiteral("<i>No data available.</i>") : summaryHtml);
    widgets.raw->setPlainText(rawText);
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
