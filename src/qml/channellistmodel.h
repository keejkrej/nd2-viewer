#pragma once

#include "core/documenttypes.h"

#include <QAbstractListModel>

class ChannelListModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        NameRole = Qt::UserRole + 1,
        EnabledRole,
        ColorRole,
        LowRole,
        HighRole,
        LowPercentileRole,
        HighPercentileRole
    };

    explicit ChannelListModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setChannels(const QVector<ChannelInfo> &channels, const QVector<ChannelRenderSettings> &settings);
    void setSettings(const QVector<ChannelRenderSettings> &settings);

private:
    QVector<ChannelInfo> channels_;
    QVector<ChannelRenderSettings> settings_;
};
