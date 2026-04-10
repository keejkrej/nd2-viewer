#pragma once

#include "core/documenttypes.h"

#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

class ChannelRowWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChannelRowWidget(QWidget *parent = nullptr);

    void setChannel(const ChannelInfo &channel, const ChannelRenderSettings &settings);
    void setAutoContrastControlsVisible(bool visible);
    void setAutoContrastTuningEnabled(bool enabled);

signals:
    void settingsEdited(const ChannelRenderSettings &settings);
    void autoContrastTuningRequested();

private:
    [[nodiscard]] ChannelRenderSettings currentSettings() const;
    void emitEditedSettings();
    void updateSwatch(const QColor &color);

    bool updating_ = false;
    QCheckBox *enabledCheck_ = nullptr;
    QLabel *nameLabel_ = nullptr;
    QPushButton *colorSwatchButton_ = nullptr;
    QDoubleSpinBox *lowSpinBox_ = nullptr;
    QDoubleSpinBox *highSpinBox_ = nullptr;
    QPushButton *tuneButton_ = nullptr;
    ChannelRenderSettings settings_;
};

class ChannelControlsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChannelControlsWidget(QWidget *parent = nullptr);

    void setChannels(const QVector<ChannelInfo> &channels, const QVector<ChannelRenderSettings> &settings);
    void setAutoContrastControlsVisible(bool visible);
    void setLiveAutoEnabled(bool enabled);
    void setLiveAutoInteractive(bool interactive);
    void setAutoContrastTuningEnabled(bool enabled);
    void updateSettings(const QVector<ChannelRenderSettings> &settings);

signals:
    void channelSettingsChanged(int index, const ChannelRenderSettings &settings);
    void liveAutoChanged(bool enabled);
    void autoContrastTuningRequested(int index);
    void autoContrastAllRequested();

private:
    void clearRows();

    QVBoxLayout *rowsLayout_ = nullptr;
    QCheckBox *liveAutoCheck_ = nullptr;
    QLabel *emptyStateLabel_ = nullptr;
    QPushButton *autoAllButton_ = nullptr;
    QVector<ChannelRowWidget *> rows_;
    QVector<ChannelInfo> channels_;
    bool autoContrastControlsVisible_ = true;
    bool liveAutoEnabled_ = false;
    bool autoContrastTuningEnabled_ = true;
};
