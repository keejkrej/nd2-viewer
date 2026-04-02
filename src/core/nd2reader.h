#pragma once

#include "core/documentreader.h"

#include <QMutex>
#include <QString>

#include <Nd2ReadSdk.h>

class Nd2Reader : public DocumentReader
{
public:
    Nd2Reader() = default;
    ~Nd2Reader() override;

    bool open(const QString &path, QString *errorMessage = nullptr) override;
    void close() override;

    [[nodiscard]] bool isOpen() const override;
    [[nodiscard]] QString filePath() const override;
    [[nodiscard]] const DocumentInfo &documentInfo() const override;
    [[nodiscard]] int sequenceCount() const override;

    bool sequenceForCoords(const QVector<int> &coords, int *sequenceIndex, QString *errorMessage = nullptr) const override;

    RawFrame readFrame(int sequenceIndex, QString *errorMessage = nullptr) const override;
    MetadataSection frameMetadataSection(int sequenceIndex, QString *errorMessage = nullptr) const override;

private:
    static QJsonDocument parseJsonText(const QString &jsonText);
    static QColor parseColorValue(const QJsonValue &value);
    static QVector3D parseVector3(const QJsonValue &value);
    static int firstIntValue(const QJsonObject &object, std::initializer_list<const char *> keys, int fallback = 0);
    static QString firstStringValue(const QJsonObject &object, std::initializer_list<const char *> keys, const QString &fallback = {});
    static QString loopLabel(const QString &type, int index);
    static DocumentInfo buildFallbackInfo(const QString &path);
    static MetadataSection metadataSection(const QString &title, const QJsonDocument &document, const QString &rawText = {});

    bool loadDocumentInfo(QString *errorMessage);
    QString frameMetadataText(int sequenceIndex, QString *errorMessage = nullptr) const;

    mutable QMutex mutex_;
    LIMFILEHANDLE handle_ = nullptr;
    DocumentInfo info_;
};
