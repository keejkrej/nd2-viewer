#include "ui/channelcontrolswidget.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
ChannelInfo fallbackChannelInfo(int index, const QString &name)
{
    ChannelInfo channel;
    channel.index = index;
    channel.name = name;
    return channel;
}

QIcon settingsIcon(const QWidget *widget)
{
    constexpr int iconSize = 16;
    QPixmap pixmap(iconSize, iconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(iconSize / 2.0, iconSize / 2.0);

    QPen pen(widget->palette().buttonText().color());
    pen.setWidthF(1.4);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    constexpr int spokeCount = 8;
    for (int index = 0; index < spokeCount; ++index) {
        painter.save();
        painter.rotate(index * (360.0 / spokeCount));
        painter.drawLine(QPointF(0.0, -6.0), QPointF(0.0, -4.0));
        painter.restore();
    }

    painter.drawEllipse(QPointF(0.0, 0.0), 4.2, 4.2);
    painter.drawEllipse(QPointF(0.0, 0.0), 1.6, 1.6);

    return QIcon(pixmap);
}
} // namespace

ChannelRowWidget::ChannelRowWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(8);
    enabledCheck_ = new QCheckBox(this);
    enabledCheck_->setChecked(true);
    colorSwatchButton_ = new QPushButton(this);
    colorSwatchButton_->setFixedSize(18, 18);
    colorSwatchButton_->setFlat(true);
    colorSwatchButton_->setToolTip(tr("Choose channel color"));
    nameLabel_ = new QLabel(tr("Channel"), this);
    nameLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    autoCheck_ = new QCheckBox(tr("Live auto"), this);
    tuneButton_ = new QPushButton(this);
    tuneButton_->setIcon(settingsIcon(this));
    tuneButton_->setToolTip(tr("Tune live auto percentiles"));
    tuneButton_->setAccessibleName(tr("Tune live auto"));
    tuneButton_->setFixedSize(28, 28);
    tuneButton_->setIconSize(QSize(16, 16));
    tuneButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    autoButton_ = new QPushButton(tr("Auto now"), this);
    autoButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    headerLayout->addWidget(enabledCheck_);
    headerLayout->addWidget(colorSwatchButton_);
    headerLayout->addWidget(nameLabel_);
    headerLayout->addWidget(autoCheck_);
    headerLayout->addWidget(tuneButton_);
    headerLayout->addWidget(autoButton_);

    auto *rangeLayout = new QHBoxLayout();
    rangeLayout->setSpacing(6);
    auto *lowLabel = new QLabel(tr("Low"), this);
    auto *highLabel = new QLabel(tr("High"), this);
    lowSpinBox_ = new QDoubleSpinBox(this);
    highSpinBox_ = new QDoubleSpinBox(this);
    for (QDoubleSpinBox *spinBox : {lowSpinBox_, highSpinBox_}) {
        spinBox->setDecimals(6);
        spinBox->setRange(-1.0e18, 1.0e18);
        spinBox->setSingleStep(1.0);
        spinBox->setKeyboardTracking(false);
    }

    rangeLayout->addWidget(lowLabel);
    rangeLayout->addWidget(lowSpinBox_, 1);
    rangeLayout->addWidget(highLabel);
    rangeLayout->addWidget(highSpinBox_, 1);

    mainLayout->addLayout(headerLayout);
    mainLayout->addLayout(rangeLayout);

    connect(enabledCheck_, &QCheckBox::toggled, this, [this]() {
        if (!updating_) {
            emitEditedSettings();
        }
    });

    connect(autoCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        if (updating_) {
            return;
        }

        emitEditedSettings();
        if (checked) {
            emit autoContrastRequested();
        }
    });

    const auto onSpinBoxChanged = [this]() {
        if (updating_) {
            return;
        }

        if (autoCheck_->isChecked()) {
            autoCheck_->setChecked(false);
            return;
        }
        emitEditedSettings();
    };

    connect(lowSpinBox_, &QDoubleSpinBox::valueChanged, this, [onSpinBoxChanged](double) { onSpinBoxChanged(); });
    connect(highSpinBox_, &QDoubleSpinBox::valueChanged, this, [onSpinBoxChanged](double) { onSpinBoxChanged(); });

    connect(autoButton_, &QPushButton::clicked, this, [this]() { emit autoContrastRequested(); });
    connect(tuneButton_, &QPushButton::clicked, this, [this]() { emit autoContrastTuningRequested(); });
    connect(colorSwatchButton_, &QPushButton::clicked, this, [this]() {
        const QColor selected = QColorDialog::getColor(settings_.color, this, tr("Choose Channel Color"));
        if (!selected.isValid() || selected == settings_.color) {
            return;
        }

        settings_.color = selected;
        updateSwatch(selected);
        if (!updating_) {
            emitEditedSettings();
        }
    });
}

void ChannelRowWidget::setChannel(const ChannelInfo &channel, const ChannelRenderSettings &settings)
{
    updating_ = true;
    settings_ = settings;

    enabledCheck_->setChecked(settings.enabled);
    autoCheck_->setChecked(settings.autoContrast);
    nameLabel_->setText(channel.name.isEmpty() ? tr("Channel %1").arg(channel.index + 1) : channel.name);
    updateSwatch(settings.color);
    lowSpinBox_->setValue(settings.low);
    highSpinBox_->setValue(settings.high);

    updating_ = false;
}

void ChannelRowWidget::setAutoContrastControlsVisible(bool visible)
{
    autoCheck_->setVisible(visible);
    tuneButton_->setVisible(visible);
    autoButton_->setVisible(visible);
}

void ChannelRowWidget::setLiveAutoInteractive(bool interactive)
{
    autoCheck_->setEnabled(interactive);
}

ChannelRenderSettings ChannelRowWidget::currentSettings() const
{
    ChannelRenderSettings settings = settings_;
    settings.enabled = enabledCheck_->isChecked();
    settings.autoContrast = autoCheck_->isChecked();
    settings.low = lowSpinBox_->value();
    settings.high = std::max(highSpinBox_->value(), settings.low + 1.0e-9);
    settings.color = settings_.color;
    return settings;
}

void ChannelRowWidget::emitEditedSettings()
{
    emit settingsEdited(currentSettings());
}

void ChannelRowWidget::updateSwatch(const QColor &color)
{
    colorSwatchButton_->setStyleSheet(QStringLiteral("QPushButton { border: 1px solid palette(mid); background-color: %1; }").arg(color.name()));
}

ChannelControlsWidget::ChannelControlsWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    autoAllButton_ = new QPushButton(tr("Auto Contrast All"), this);
    emptyStateLabel_ = new QLabel(tr("Open a file to inspect channels."), this);
    emptyStateLabel_->setWordWrap(true);
    rowsLayout_ = new QVBoxLayout();
    rowsLayout_->setSpacing(8);
    rowsLayout_->addWidget(emptyStateLabel_);
    rowsLayout_->addStretch(1);

    mainLayout->addWidget(autoAllButton_);
    mainLayout->addLayout(rowsLayout_, 1);

    connect(autoAllButton_, &QPushButton::clicked, this, &ChannelControlsWidget::autoContrastAllRequested);
}

void ChannelControlsWidget::setChannels(const QVector<ChannelInfo> &channels, const QVector<ChannelRenderSettings> &settings)
{
    clearRows();
    channels_ = channels;

    if (channels.isEmpty() || settings.isEmpty()) {
        emptyStateLabel_->show();
        return;
    }

    emptyStateLabel_->hide();
    for (int index = 0; index < settings.size(); ++index) {
        auto *row = new ChannelRowWidget(this);
        const ChannelInfo channel = index < channels.size() ? channels.at(index)
                                                            : fallbackChannelInfo(index, tr("Channel %1").arg(index + 1));
        row->setChannel(channel, settings.at(index));
        row->setAutoContrastControlsVisible(autoContrastControlsVisible_);
        rowsLayout_->insertWidget(rowsLayout_->count() - 1, row);
        rows_.push_back(row);

        connect(row, &ChannelRowWidget::settingsEdited, this, [this, index](const ChannelRenderSettings &rowSettings) {
            emit channelSettingsChanged(index, rowSettings);
        });
        connect(row, &ChannelRowWidget::autoContrastRequested, this, [this, index]() {
            emit autoContrastRequested(index);
        });
        connect(row, &ChannelRowWidget::autoContrastTuningRequested, this, [this, index]() {
            emit autoContrastTuningRequested(index);
        });
    }
}

void ChannelControlsWidget::setAutoContrastControlsVisible(bool visible)
{
    autoContrastControlsVisible_ = visible;
    autoAllButton_->setVisible(visible);
    for (ChannelRowWidget *row : std::as_const(rows_)) {
        row->setAutoContrastControlsVisible(visible);
    }
}

void ChannelControlsWidget::setLiveAutoInteractive(bool interactive)
{
    for (ChannelRowWidget *row : std::as_const(rows_)) {
        row->setLiveAutoInteractive(interactive);
    }
}

void ChannelControlsWidget::updateSettings(const QVector<ChannelRenderSettings> &settings)
{
    if (settings.size() != rows_.size()) {
        setChannels(channels_, settings);
        return;
    }

    for (int index = 0; index < rows_.size(); ++index) {
        const ChannelInfo channel = index < channels_.size() ? channels_.at(index)
                                                             : fallbackChannelInfo(index, tr("Channel %1").arg(index + 1));
        rows_.at(index)->setChannel(channel, settings.at(index));
    }
}

void ChannelControlsWidget::clearRows()
{
    for (ChannelRowWidget *row : std::as_const(rows_)) {
        rowsLayout_->removeWidget(row);
        row->deleteLater();
    }
    rows_.clear();
}
