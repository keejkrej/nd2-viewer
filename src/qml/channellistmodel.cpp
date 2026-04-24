#include "qml/channellistmodel.h"

ChannelListModel::ChannelListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ChannelListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : settings_.size();
}

QVariant ChannelListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= settings_.size()) {
        return {};
    }

    const int row = index.row();
    const ChannelRenderSettings &settings = settings_.at(row);
    const QString fallbackName = QStringLiteral("Channel %1").arg(row + 1);
    const QString name = row < channels_.size() && !channels_.at(row).name.isEmpty() ? channels_.at(row).name : fallbackName;

    switch (role) {
    case NameRole:
        return name;
    case EnabledRole:
        return settings.enabled;
    case ColorRole:
        return settings.color;
    case LowRole:
        return settings.low;
    case HighRole:
        return settings.high;
    case LowPercentileRole:
        return settings.lowPercentile;
    case HighPercentileRole:
        return settings.highPercentile;
    default:
        return {};
    }
}

QHash<int, QByteArray> ChannelListModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {EnabledRole, "enabled"},
        {ColorRole, "color"},
        {LowRole, "low"},
        {HighRole, "high"},
        {LowPercentileRole, "lowPercentile"},
        {HighPercentileRole, "highPercentile"},
    };
}

void ChannelListModel::setChannels(const QVector<ChannelInfo> &channels,
                                   const QVector<ChannelRenderSettings> &settings)
{
    beginResetModel();
    channels_ = channels;
    settings_ = settings;
    endResetModel();
}

void ChannelListModel::setSettings(const QVector<ChannelRenderSettings> &settings)
{
    if (settings_.size() != settings.size()) {
        beginResetModel();
        settings_ = settings;
        endResetModel();
        return;
    }

    settings_ = settings;
    if (!settings_.isEmpty()) {
        emit dataChanged(index(0), index(settings_.size() - 1),
                         {EnabledRole, ColorRole, LowRole, HighRole, LowPercentileRole, HighPercentileRole});
    }
}
