#pragma once

#include "core/documentreader.h"
#include "core/documenttypes.h"

#include <QObject>
#include <QImage>
#include <QRect>
#include <QString>
#include <QVideoFrame>
#include <QVector>

#include <memory>

struct MovieExportSettings
{
    QString sourcePath;
    QString outputPath;
    QVector<int> fixedCoordinates;
    QVector<ChannelRenderSettings> channelSettings;
    int timeLoopIndex = -1;
    int startFrame = 0;
    int endFrame = 0;
    int step = 1;
    double fps = 10.0;
    QRect roiRect;
    QSize outputSize;

    [[nodiscard]] int frameStep() const
    {
        return qMax(step, 1);
    }
};

struct MovieExportEstimate
{
    bool valid = false;
    int frameCount = 0;
    QSize outputSize;
    double durationSeconds = 0.0;
    qint64 estimatedBytes = 0;
    QString errorMessage;
};

struct MovieExportResult
{
    bool success = false;
    int encodedFrameCount = 0;
    qint64 bytesWritten = 0;
    QString outputPath;
    QString errorMessage;
};

Q_DECLARE_METATYPE(MovieExportSettings)
Q_DECLARE_METATYPE(MovieExportEstimate)
Q_DECLARE_METATYPE(MovieExportResult)

[[nodiscard]] MovieExportEstimate estimateMovieExport(const MovieExportSettings &settings, const QImage &sampleImage);
[[nodiscard]] int movieExportFrameCount(const MovieExportSettings &settings);

class QMediaCaptureSession;
class QMediaRecorder;
class QVideoFrameInput;

class MovieExportWorker : public QObject
{
    Q_OBJECT

public:
    explicit MovieExportWorker(const MovieExportSettings &settings, QObject *parent = nullptr);

public slots:
    void start();

signals:
    void progressChanged(int current, int total, const QString &message);
    void finished(const MovieExportResult &result);

private:
    [[nodiscard]] MovieExportResult validateSettings() const;
    [[nodiscard]] QImage renderFrameImage(int timeValue, QString *errorMessage);
    [[nodiscard]] bool sendPendingFrame();
    void initializeRecorder();
    void finishWithError(const QString &message);
    void finishSuccessfully();
    void handleReadyToSend();

    MovieExportSettings settings_;
    mutable std::unique_ptr<DocumentReader> reader_;
    QVector<ChannelRenderSettings> workingChannelSettings_;
    QVector<int> timeValues_;
    MovieExportResult result_;
    QMediaCaptureSession *captureSession_ = nullptr;
    QMediaRecorder *recorder_ = nullptr;
    QVideoFrameInput *videoFrameInput_ = nullptr;
    QVideoFrame pendingFrame_;
    int nextFrameCursor_ = 0;
    bool pendingEndOfStream_ = false;
    bool endOfStreamSent_ = false;
    bool completionEmitted_ = false;
};
