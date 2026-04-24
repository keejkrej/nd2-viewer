#pragma once

#include "core/documenttypes.h"

#include <QAbstractListModel>

class LoopListModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        LabelRole = Qt::UserRole + 1,
        TypeRole,
        ValueRole,
        PreviewValueRole,
        MaximumRole,
        DetailsRole,
        IsTimeRole,
        LockedRole
    };

    explicit LoopListModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setState(const DocumentInfo &info, const FrameCoordinateState &state, int timeLoopIndex, int lockedLoopIndex);
    void setPreviewValue(int loopIndex, int value);
    void setCommittedValue(int loopIndex, int value);

private:
    DocumentInfo info_;
    FrameCoordinateState state_;
    QVector<int> previewValues_;
    int timeLoopIndex_ = -1;
    int lockedLoopIndex_ = -1;
};
