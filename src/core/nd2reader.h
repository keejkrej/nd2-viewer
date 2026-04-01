#pragma once

#include "core/nd2types.h"

#include <QMutex>
#include <QString>
#include <QVector>

#include <Nd2ReadSdk.h>

class Nd2Reader
{
public:
    Nd2Reader() = default;
    ~Nd2Reader();

    bool open(const QString &path, QString *errorMessage = nullptr);
    void close();

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString filePath() const;
    [[nodiscard]] const Nd2DocumentInfo &documentInfo() const;
    [[nodiscard]] int sequenceCount() const;

    QVector<int> coordsForSequence(int sequenceIndex, QString *errorMessage = nullptr) const;
    bool sequenceForCoords(const QVector<int> &coords, int *sequenceIndex, QString *errorMessage = nullptr) const;

    RawFrame readFrame(int sequenceIndex, QString *errorMessage = nullptr) const;
    QJsonDocument globalMetadata(QString *errorMessage = nullptr) const;
    QJsonDocument frameMetadata(int sequenceIndex, QString *errorMessage = nullptr) const;
    QString frameMetadataText(int sequenceIndex, QString *errorMessage = nullptr) const;

private:
    static QJsonDocument parseJsonText(const QString &jsonText);
    static QColor parseColorValue(const QJsonValue &value);
    static QVector3D parseVector3(const QJsonValue &value);
    static int firstIntValue(const QJsonObject &object, std::initializer_list<const char *> keys, int fallback = 0);
    static QString firstStringValue(const QJsonObject &object, std::initializer_list<const char *> keys, const QString &fallback = {});
    static QString loopLabel(const QString &type, int index);
    static Nd2DocumentInfo buildFallbackInfo(const QString &path);

    bool loadDocumentInfo(QString *errorMessage);

    mutable QMutex mutex_;
    LIMFILEHANDLE handle_ = nullptr;
    Nd2DocumentInfo info_;
};
