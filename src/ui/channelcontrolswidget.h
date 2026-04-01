#pragma once

#include "core/nd2types.h"

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

    void setChannel(const Nd2ChannelInfo &channel, const ChannelRenderSettings &settings);

signals:
    void settingsEdited(const ChannelRenderSettings &settings);
    void autoContrastRequested();

private:
    [[nodiscard]] ChannelRenderSettings currentSettings() const;
    void emitEditedSettings();
    void updateSwatch(const QColor &color);

    bool updating_ = false;
    QCheckBox *enabledCheck_ = nullptr;
    QCheckBox *autoCheck_ = nullptr;
    QLabel *nameLabel_ = nullptr;
    QLabel *colorSwatch_ = nullptr;
    QDoubleSpinBox *lowSpinBox_ = nullptr;
    QDoubleSpinBox *highSpinBox_ = nullptr;
    QPushButton *autoButton_ = nullptr;
};

class ChannelControlsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChannelControlsWidget(QWidget *parent = nullptr);

    void setChannels(const QVector<Nd2ChannelInfo> &channels, const QVector<ChannelRenderSettings> &settings);
    void updateSettings(const QVector<ChannelRenderSettings> &settings);

signals:
    void channelSettingsChanged(int index, const ChannelRenderSettings &settings);
    void autoContrastRequested(int index);
    void autoContrastAllRequested();

private:
    void clearRows();

    QVBoxLayout *rowsLayout_ = nullptr;
    QLabel *emptyStateLabel_ = nullptr;
    QPushButton *autoAllButton_ = nullptr;
    QVector<ChannelRowWidget *> rows_;
    QVector<Nd2ChannelInfo> channels_;
};
