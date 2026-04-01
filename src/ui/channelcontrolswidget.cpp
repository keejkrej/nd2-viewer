#include "ui/channelcontrolswidget.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
Nd2ChannelInfo fallbackChannelInfo(int index, const QString &name)
{
    Nd2ChannelInfo channel;
    channel.index = index;
    channel.name = name;
    return channel;
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
    colorSwatch_ = new QLabel(this);
    colorSwatch_->setFixedSize(18, 18);
    colorSwatch_->setFrameShape(QFrame::Box);
    colorSwatch_->setFrameShadow(QFrame::Plain);
    nameLabel_ = new QLabel(tr("Channel"), this);
    nameLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    autoCheck_ = new QCheckBox(tr("Live auto"), this);
    autoButton_ = new QPushButton(tr("Auto now"), this);
    autoButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    headerLayout->addWidget(enabledCheck_);
    headerLayout->addWidget(colorSwatch_);
    headerLayout->addWidget(nameLabel_);
    headerLayout->addWidget(autoCheck_);
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

    connect(autoButton_, &QPushButton::clicked, this, [this]() {
        if (!autoCheck_->isChecked()) {
            autoCheck_->setChecked(true);
            return;
        }
        emit autoContrastRequested();
    });
}

void ChannelRowWidget::setChannel(const Nd2ChannelInfo &channel, const ChannelRenderSettings &settings)
{
    updating_ = true;

    enabledCheck_->setChecked(settings.enabled);
    autoCheck_->setChecked(settings.autoContrast);
    nameLabel_->setText(channel.name.isEmpty() ? tr("Channel %1").arg(channel.index + 1) : channel.name);
    updateSwatch(settings.color);
    lowSpinBox_->setValue(settings.low);
    highSpinBox_->setValue(settings.high);

    updating_ = false;
}

ChannelRenderSettings ChannelRowWidget::currentSettings() const
{
    ChannelRenderSettings settings;
    settings.enabled = enabledCheck_->isChecked();
    settings.autoContrast = autoCheck_->isChecked();
    settings.low = lowSpinBox_->value();
    settings.high = std::max(highSpinBox_->value(), settings.low + 1.0e-9);
    settings.color = colorSwatch_->palette().color(QPalette::Window);
    return settings;
}

void ChannelRowWidget::emitEditedSettings()
{
    emit settingsEdited(currentSettings());
}

void ChannelRowWidget::updateSwatch(const QColor &color)
{
    QPalette palette = colorSwatch_->palette();
    palette.setColor(QPalette::Window, color);
    colorSwatch_->setAutoFillBackground(true);
    colorSwatch_->setPalette(palette);
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

void ChannelControlsWidget::setChannels(const QVector<Nd2ChannelInfo> &channels, const QVector<ChannelRenderSettings> &settings)
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
        const Nd2ChannelInfo channel = index < channels.size() ? channels.at(index)
                                                               : fallbackChannelInfo(index, tr("Channel %1").arg(index + 1));
        row->setChannel(channel, settings.at(index));
        rowsLayout_->insertWidget(rowsLayout_->count() - 1, row);
        rows_.push_back(row);

        connect(row, &ChannelRowWidget::settingsEdited, this, [this, index](const ChannelRenderSettings &rowSettings) {
            emit channelSettingsChanged(index, rowSettings);
        });
        connect(row, &ChannelRowWidget::autoContrastRequested, this, [this, index]() {
            emit autoContrastRequested(index);
        });
    }
}

void ChannelControlsWidget::updateSettings(const QVector<ChannelRenderSettings> &settings)
{
    if (settings.size() != rows_.size()) {
        setChannels(channels_, settings);
        return;
    }

    for (int index = 0; index < rows_.size(); ++index) {
        const Nd2ChannelInfo channel = index < channels_.size() ? channels_.at(index)
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
