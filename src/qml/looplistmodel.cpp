#include "qml/looplistmodel.h"

#include <QtGlobal>

LoopListModel::LoopListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LoopListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : info_.loops.size();
}

QVariant LoopListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= info_.loops.size()) {
        return {};
    }

    const int row = index.row();
    const LoopInfo &loop = info_.loops.at(row);
    const int value = row < state_.values.size() ? state_.values.at(row) : 0;
    const int preview = row < previewValues_.size() ? previewValues_.at(row) : value;

    switch (role) {
    case LabelRole:
        return loop.label;
    case TypeRole:
        return loop.type;
    case ValueRole:
        return value;
    case PreviewValueRole:
        return preview;
    case MaximumRole:
        return qMax(loop.size - 1, 0);
    case DetailsRole:
        return QStringLiteral("%1 · %2 steps").arg(loop.type, QString::number(loop.size));
    case IsTimeRole:
        return row == timeLoopIndex_;
    case LockedRole:
        return row == lockedLoopIndex_;
    default:
        return {};
    }
}

QHash<int, QByteArray> LoopListModel::roleNames() const
{
    return {
        {LabelRole, "label"},
        {TypeRole, "type"},
        {ValueRole, "value"},
        {PreviewValueRole, "previewValue"},
        {MaximumRole, "maximum"},
        {DetailsRole, "details"},
        {IsTimeRole, "isTime"},
        {LockedRole, "locked"},
    };
}

void LoopListModel::setState(const DocumentInfo &info,
                             const FrameCoordinateState &state,
                             int timeLoopIndex,
                             int lockedLoopIndex)
{
    beginResetModel();
    info_ = info;
    state_ = state;
    previewValues_ = state.values;
    previewValues_.resize(info_.loops.size());
    timeLoopIndex_ = timeLoopIndex;
    lockedLoopIndex_ = lockedLoopIndex;
    endResetModel();
}

void LoopListModel::setPreviewValue(int loopIndex, int value)
{
    if (loopIndex < 0 || loopIndex >= previewValues_.size()) {
        return;
    }
    previewValues_[loopIndex] = value;
    const QModelIndex modelIndex = index(loopIndex);
    emit dataChanged(modelIndex, modelIndex, {PreviewValueRole});
}

void LoopListModel::setCommittedValue(int loopIndex, int value)
{
    if (loopIndex < 0 || loopIndex >= state_.values.size()) {
        return;
    }
    state_.values[loopIndex] = value;
    if (loopIndex < previewValues_.size()) {
        previewValues_[loopIndex] = value;
    }
    const QModelIndex modelIndex = index(loopIndex);
    emit dataChanged(modelIndex, modelIndex, {ValueRole, PreviewValueRole});
}
