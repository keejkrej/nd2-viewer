#include "ui/volumeviewport3d.h"

#include <QtConcurrent>

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace
{
constexpr int kMaxVolumeChannels = 8;

struct PointVertex
{
    float x;
    float y;
    float z;
    float r;
    float g;
    float b;
    float a;
};

const char *kVertexShaderSource = R"(#version 330 core
layout(location = 0) in vec2 aPosition;

uniform mat4 uMvp;
uniform vec3 uBoundsMin;
uniform vec3 uBoundsMax;
uniform vec3 uTexMin;
uniform vec3 uTexMax;
uniform float uSliceLerp;

out vec3 vTexCoord;

void main()
{
    vec3 localCoord = vec3(aPosition, uSliceLerp);
    // Image rows are stored top-down, but the 3D world uses +Y as up, so flip
    // texture Y here to keep the smooth volume path aligned with the point cloud.
    vec3 texCoord = mix(uTexMin, uTexMax, localCoord);
    texCoord.y = uTexMin.y + uTexMax.y - texCoord.y;
    vTexCoord = texCoord;
    vec3 objectPosition = mix(uBoundsMin, uBoundsMax, localCoord);
    gl_Position = uMvp * vec4(objectPosition, 1.0);
}
)";

const char *kFragmentShaderSource = R"(#version 330 core
in vec3 vTexCoord;

uniform sampler3D uTexture0;
uniform sampler3D uTexture1;
uniform sampler3D uTexture2;
uniform sampler3D uTexture3;
uniform sampler3D uTexture4;
uniform sampler3D uTexture5;
uniform sampler3D uTexture6;
uniform sampler3D uTexture7;
uniform vec4 uChannelColor[8];
uniform vec2 uChannelRange[8];
uniform int uChannelCount;

out vec4 fragColor;

float sampleChannel(int channelIndex, vec3 position)
{
    if (channelIndex == 0) return texture(uTexture0, position).r;
    if (channelIndex == 1) return texture(uTexture1, position).r;
    if (channelIndex == 2) return texture(uTexture2, position).r;
    if (channelIndex == 3) return texture(uTexture3, position).r;
    if (channelIndex == 4) return texture(uTexture4, position).r;
    if (channelIndex == 5) return texture(uTexture5, position).r;
    if (channelIndex == 6) return texture(uTexture6, position).r;
    return texture(uTexture7, position).r;
}

void main()
{
    if (any(lessThan(vTexCoord, vec3(0.0))) || any(greaterThan(vTexCoord, vec3(1.0)))) {
        discard;
    }

    vec3 color = vec3(0.0);
    float alpha = 0.0;
    for (int channelIndex = 0; channelIndex < uChannelCount; ++channelIndex) {
        if (uChannelColor[channelIndex].a <= 0.0) {
            continue;
        }

        float value = sampleChannel(channelIndex, vTexCoord);
        vec2 range = uChannelRange[channelIndex];
        float scaled = clamp((value - range.x) / max(range.y - range.x, 1e-6), 0.0, 1.0);
        color += uChannelColor[channelIndex].rgb * scaled;
        alpha = max(alpha, scaled);
    }

    if (alpha <= 0.0) {
        discard;
    }

    fragColor = vec4(clamp(color, vec3(0.0), vec3(1.0)), alpha);
}
)";

const char *kPointVertexShaderSource = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

uniform mat4 uMvp;
uniform float uPointSize;

out vec4 vColor;

void main()
{
    gl_Position = uMvp * vec4(aPosition, 1.0);
    gl_PointSize = uPointSize;
    vColor = aColor;
}
)";

const char *kPointFragmentShaderSource = R"(#version 330 core
in vec4 vColor;

out vec4 fragColor;

void main()
{
    vec2 pointCoord = gl_PointCoord * 2.0 - vec2(1.0);
    float radius2 = dot(pointCoord, pointCoord);
    if (radius2 > 1.0) {
        discard;
    }
    float falloff = exp(-radius2 * 3.5);
    fragColor = vec4(vColor.rgb * falloff, vColor.a * falloff);
}
)";

constexpr std::array<float, 12> kSliceVertices = {
    0.0f, 0.0f,
    1.0f, 1.0f,
    1.0f, 0.0f,
    0.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f,
};

QString defaultSelectionPrompt()
{
    return QObject::tr("Rotate to a view, click Add View, then draw a contour around the nucleus.");
}

QString singleChannelRequiredPrompt()
{
    return QObject::tr("3D Select requires exactly one enabled channel.");
}

double sampleVolumeValue(const RawVolume &volume, int channelIndex, qsizetype voxelIndex)
{
    if (!volume.isValid() || channelIndex < 0 || channelIndex >= volume.components || voxelIndex < 0 || voxelIndex >= volume.voxelCount()) {
        return 0.0;
    }

    const int bytesPerComponent = volume.bytesPerComponent();
    const char *voxel = volume.channelData.at(channelIndex).constData() + (voxelIndex * bytesPerComponent);
    switch (bytesPerComponent) {
    case 1:
        return static_cast<unsigned char>(voxel[0]);
    case 2: {
        quint16 value = 0;
        std::memcpy(&value, voxel, sizeof(value));
        return value;
    }
    case 4: {
        if (volume.pixelDataType.compare(QStringLiteral("float"), Qt::CaseInsensitive) == 0) {
            float value = 0.0f;
            std::memcpy(&value, voxel, sizeof(value));
            return value;
        }
        quint32 value = 0;
        std::memcpy(&value, voxel, sizeof(value));
        return value;
    }
    default:
        return 0.0;
    }
}

int pointSamplingStep(const RawVolume &volume)
{
    const double totalVoxels = static_cast<double>(volume.voxelCount());
    return std::max(2, static_cast<int>(std::cbrt(totalVoxels / 950000.0)));
}

float jitterFromIndex(int x, int y, int z, int salt)
{
    quint32 seed = 2166136261u;
    seed = (seed ^ static_cast<quint32>(x + salt * 17)) * 16777619u;
    seed = (seed ^ static_cast<quint32>(y + salt * 31)) * 16777619u;
    seed = (seed ^ static_cast<quint32>(z + salt * 47)) * 16777619u;
    return (static_cast<float>(seed & 0xFFFFu) / 65535.0f) - 0.5f;
}

QImage rasterizedContourMask(const QSize &size, const QPolygonF &contour)
{
    if (!size.isValid() || contour.size() < 3) {
        return {};
    }

    QImage image(size, QImage::Format_Alpha8);
    image.fill(0);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawPolygon(contour);
    return image;
}

VolumeViewport3D::SelectionMask3D emptySelectionMask(const RawVolume &volume)
{
    VolumeViewport3D::SelectionMask3D mask;
    if (!volume.isValid()) {
        return mask;
    }

    mask.width = volume.width;
    mask.height = volume.height;
    mask.depth = volume.depth;
    mask.voxels.fill(0, static_cast<int>(volume.voxelCount()));
    return mask;
}

QVector3D voxelObjectPositionForConstraint(const RawVolume &volume,
                                           int x,
                                           int y,
                                           int z,
                                           const QVector3D &boundsMin,
                                           const QVector3D &boundsMax,
                                           const QVector3D &texMin,
                                           const QVector3D &texMax)
{
    const float texX = volume.width <= 1 ? 0.5f : static_cast<float>(x) / static_cast<float>(volume.width - 1);
    const float texY = volume.height <= 1 ? 0.5f : static_cast<float>(y) / static_cast<float>(volume.height - 1);
    const float texZ = volume.depth <= 1 ? 0.5f : static_cast<float>(z) / static_cast<float>(volume.depth - 1);

    const auto remap = [](float texCoord, float minimum, float maximum, bool invert) {
        const float denominator = std::max(maximum - minimum, 1.0e-6f);
        if (invert) {
            return std::clamp((maximum - texCoord) / denominator, 0.0f, 1.0f);
        }
        return std::clamp((texCoord - minimum) / denominator, 0.0f, 1.0f);
    };

    const QVector3D localCoord(remap(texX, texMin.x(), texMax.x(), false),
                               remap(texY, texMin.y(), texMax.y(), true),
                               remap(texZ, texMin.z(), texMax.z(), false));

    const auto interpolate = [](float minimum, float maximum, float fraction) {
        return minimum + ((maximum - minimum) * fraction);
    };

    return {
        interpolate(boundsMin.x(), boundsMax.x(), localCoord.x()),
        interpolate(boundsMin.y(), boundsMax.y(), localCoord.y()),
        interpolate(boundsMin.z(), boundsMax.z(), localCoord.z())
    };
}

VolumeViewport3D::SelectionMask3D buildProjectionMask(const RawVolume &volume, const VolumeViewport3D::ProjectionConstraint &constraint)
{
    VolumeViewport3D::SelectionMask3D mask = emptySelectionMask(volume);
    if (!mask.isValid() || !constraint.isValid()) {
        return mask;
    }

    const uchar *maskBits = constraint.maskImage.constBits();
    const int bytesPerLine = constraint.maskImage.bytesPerLine();
    const int width = constraint.viewportSize.width();
    const int height = constraint.viewportSize.height();

    for (int z = 0; z < volume.depth; ++z) {
        for (int y = 0; y < volume.height; ++y) {
            for (int x = 0; x < volume.width; ++x) {
                const QVector3D objectPosition = voxelObjectPositionForConstraint(volume,
                                                                                  x,
                                                                                  y,
                                                                                  z,
                                                                                  constraint.boundsMin,
                                                                                  constraint.boundsMax,
                                                                                  constraint.texMin,
                                                                                  constraint.texMax);
                const QVector4D clipPosition = constraint.modelViewProjection * QVector4D(objectPosition, 1.0f);
                if (clipPosition.w() <= 1.0e-6f) {
                    continue;
                }

                const QVector3D ndc(clipPosition.x() / clipPosition.w(),
                                    clipPosition.y() / clipPosition.w(),
                                    clipPosition.z() / clipPosition.w());
                if (ndc.x() < -1.0f || ndc.x() > 1.0f || ndc.y() < -1.0f || ndc.y() > 1.0f || ndc.z() < -1.0f || ndc.z() > 1.0f) {
                    continue;
                }

                const int pixelX = std::clamp(static_cast<int>(std::lround((ndc.x() * 0.5f + 0.5f) * static_cast<float>(width - 1))),
                                              0,
                                              width - 1);
                const int pixelY = std::clamp(static_cast<int>(std::lround((1.0f - (ndc.y() * 0.5f + 0.5f)) * static_cast<float>(height - 1))),
                                              0,
                                              height - 1);
                if (maskBits[pixelY * bytesPerLine + pixelX] == 0) {
                    continue;
                }

                const qsizetype voxelIndex = (static_cast<qsizetype>(z) * volume.width * volume.height)
                                           + (static_cast<qsizetype>(y) * volume.width)
                                           + x;
                mask.voxels[static_cast<int>(voxelIndex)] = 1;
                ++mask.selectedVoxelCount;
            }
        }
    }

    return mask;
}

VolumeViewport3D::SelectionMask3D intersectMasks(const VolumeViewport3D::SelectionMask3D &left,
                                                 const VolumeViewport3D::SelectionMask3D &right)
{
    if (!left.isValid()) {
        return right;
    }
    if (!right.isValid()) {
        return left;
    }

    VolumeViewport3D::SelectionMask3D result = left;
    result.selectedVoxelCount = 0;
    for (int index = 0; index < result.voxels.size(); ++index) {
        const char value = (left.voxels.at(index) != 0 && right.voxels.at(index) != 0) ? 1 : 0;
        result.voxels[index] = value;
        result.selectedVoxelCount += value != 0 ? 1 : 0;
    }
    return result;
}

QString selectionWarningForCounts(qsizetype voxelCount, int acceptedViewCount)
{
    if (acceptedViewCount < 2) {
        return {};
    }

    if (voxelCount <= 0) {
        return QObject::tr("Views do not agree. The intersection is empty.");
    }

    if (voxelCount < 256) {
        return QObject::tr("Views barely agree. The intersection is very small.");
    }

    return {};
}

VolumeViewport3D::SelectionComputationResult computeSelectionFromConstraints(const RawVolume &volume,
                                                                             const QVector<VolumeViewport3D::ProjectionConstraint> &constraints)
{
    VolumeViewport3D::SelectionComputationResult result;
    if (!volume.isValid() || constraints.isEmpty()) {
        result.success = true;
        return result;
    }

    VolumeViewport3D::SelectionMask3D fusedMask;
    for (int index = 0; index < constraints.size(); ++index) {
        const auto latestMask = buildProjectionMask(volume, constraints.at(index));
        if (!latestMask.isValid()) {
            return result;
        }

        if (index == 0) {
            fusedMask = latestMask;
        } else {
            fusedMask = intersectMasks(fusedMask, latestMask);
        }

        if (index == constraints.size() - 1) {
            result.latestMask = latestMask;
        }
    }

    result.fusedMask = fusedMask;
    result.warning = selectionWarningForCounts(fusedMask.selectedVoxelCount, constraints.size());
    result.success = true;
    return result;
}
}

VolumeViewport3D::VolumeViewport3D(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(500, 400);
    selectionStatusText_ = defaultSelectionPrompt();
    connect(&selectionWatcher_, &QFutureWatcher<SelectionComputationResult>::finished, this, &VolumeViewport3D::handleSelectionComputationFinished);
}

VolumeViewport3D::~VolumeViewport3D()
{
    if (selectionWatcher_.isRunning()) {
        selectionWatcher_.waitForFinished();
    }

    makeCurrent();
    clearTextures();
    clearPointCloud();
    delete pointArray_;
    delete pointBuffer_;
    delete pointProgram_;
    delete vertexArray_;
    delete vertexBuffer_;
    delete program_;
    doneCurrent();
}

QString VolumeViewport3D::lastError() const
{
    return lastError_;
}

void VolumeViewport3D::beginProjectionConstraint()
{
    if (!volume_.isValid() || selectionWatcher_.isRunning()) {
        return;
    }

    if (activeSelectionChannelCount() != 1) {
        contourCaptureActive_ = false;
        contourDrawing_ = false;
        pendingContour_.clear();
        selectionStatusText_ = singleChannelRequiredPrompt();
        selectionStatusWarning_ = true;
        update();
        emit selectionStateChanged();
        return;
    }

    contourCaptureActive_ = true;
    contourDrawing_ = false;
    pendingContour_.clear();
    selectionStatusText_ = tr("Draw a closed contour around the nucleus, then click Accept Contour.");
    selectionStatusWarning_ = false;
    update();
    emit selectionStateChanged();
}

void VolumeViewport3D::acceptPendingConstraint()
{
    if (!hasPendingConstraint() || selectionWatcher_.isRunning()) {
        return;
    }

    if (activeSelectionChannelCount() != 1) {
        contourCaptureActive_ = false;
        contourDrawing_ = false;
        pendingContour_.clear();
        selectionStatusText_ = singleChannelRequiredPrompt();
        selectionStatusWarning_ = true;
        emit selectionStateChanged();
        update();
        return;
    }

    const ProjectionConstraint constraint = capturePendingConstraint();
    if (!constraint.isValid()) {
        selectionStatusText_ = tr("Draw a larger closed contour before accepting.");
        selectionStatusWarning_ = true;
        emit selectionStateChanged();
        update();
        return;
    }

    pendingSelectionConstraints_ = acceptedConstraints_;
    pendingSelectionConstraints_.push_back(constraint);
    contourCaptureActive_ = false;
    contourDrawing_ = false;
    pendingContour_.clear();
    startSelectionComputation(pendingSelectionConstraints_);
}

void VolumeViewport3D::undoLastConstraint()
{
    if (selectionWatcher_.isRunning() || acceptedConstraints_.isEmpty()) {
        return;
    }

    pendingSelectionConstraints_ = acceptedConstraints_;
    pendingSelectionConstraints_.removeLast();
    contourCaptureActive_ = false;
    contourDrawing_ = false;
    pendingContour_.clear();

    if (pendingSelectionConstraints_.isEmpty()) {
        acceptedConstraints_.clear();
        latestMask_ = {};
        fusedMask_ = {};
        latestOverlaySamples_.clear();
        fusedOverlaySamples_.clear();
        selectionStatusText_ = defaultSelectionPrompt();
        selectionStatusWarning_ = false;
        emit selectionStateChanged();
        update();
        return;
    }

    startSelectionComputation(pendingSelectionConstraints_);
}

void VolumeViewport3D::clearSelection()
{
    if (selectionWatcher_.isRunning()) {
        return;
    }

    contourCaptureActive_ = false;
    contourDrawing_ = false;
    pendingContour_.clear();
    acceptedConstraints_.clear();
    pendingSelectionConstraints_.clear();
    latestMask_ = {};
    fusedMask_ = {};
    latestOverlaySamples_.clear();
    fusedOverlaySamples_.clear();
    selectionStatusText_ = defaultSelectionPrompt();
    selectionStatusWarning_ = false;
    emit selectionStateChanged();
    update();
}

bool VolumeViewport3D::isSelectionBusy() const
{
    return selectionWatcher_.isRunning();
}

bool VolumeViewport3D::hasPendingConstraint() const
{
    return contourCaptureActive_ && !contourDrawing_ && pendingContour_.size() >= 3;
}

bool VolumeViewport3D::hasSelection() const
{
    return fusedMask_.selectedVoxelCount > 0;
}

int VolumeViewport3D::acceptedConstraintCount() const
{
    return acceptedConstraints_.size();
}

QString VolumeViewport3D::selectionStatusText() const
{
    return selectionStatusText_.isEmpty() ? defaultSelectionPrompt() : selectionStatusText_;
}

bool VolumeViewport3D::selectionStatusWarning() const
{
    return selectionStatusWarning_;
}

void VolumeViewport3D::setSelectionOverlayOpacity(qreal opacity)
{
    const qreal clampedOpacity = std::clamp(opacity, 0.05, 1.0);
    if (qFuzzyCompare(selectionOverlayOpacity_, clampedOpacity)) {
        return;
    }

    selectionOverlayOpacity_ = clampedOpacity;
    update();
}

qreal VolumeViewport3D::selectionOverlayOpacity() const
{
    return selectionOverlayOpacity_;
}

void VolumeViewport3D::setVolume(const RawVolume &volume, const QVector<ChannelRenderSettings> &channelSettings)
{
    volume_ = volume;
    channelSettings_ = channelSettings;
    texturesDirty_ = true;
    pointCloudDirty_ = true;
    pendingAutoFit_ = true;
    contourCaptureActive_ = false;
    contourDrawing_ = false;
    pendingContour_.clear();
    acceptedConstraints_.clear();
    pendingSelectionConstraints_.clear();
    latestMask_ = {};
    fusedMask_ = {};
    latestOverlaySamples_.clear();
    fusedOverlaySamples_.clear();
    selectionStatusText_ = defaultSelectionPrompt();
    selectionStatusWarning_ = false;
    qInfo("3D viewport volume set: %dx%dx%d components=%d bits=%d type=%s",
          volume_.width,
          volume_.height,
          volume_.depth,
          volume_.components,
          volume_.bitsPerComponent,
          qPrintable(volume_.pixelDataType));
    resetView();
    emit selectionStateChanged();
}

void VolumeViewport3D::setChannelSettings(const QVector<ChannelRenderSettings> &channelSettings)
{
    channelSettings_ = channelSettings;
    pointCloudDirty_ = true;
    if (!selectionWatcher_.isRunning()) {
        if (activeSelectionChannelCount() != 1) {
            contourCaptureActive_ = false;
            contourDrawing_ = false;
            pendingContour_.clear();
            if (!acceptedConstraints_.isEmpty() || !hasSelection()) {
                selectionStatusText_ = singleChannelRequiredPrompt();
                selectionStatusWarning_ = true;
            }
        } else if (!contourCaptureActive_ && acceptedConstraints_.isEmpty() && !hasSelection()) {
            selectionStatusText_ = defaultSelectionPrompt();
            selectionStatusWarning_ = false;
        }
        emit selectionStateChanged();
    }
    update();
}

void VolumeViewport3D::resetView()
{
    yawDegrees_ = -35.0f;
    pitchDegrees_ = 25.0f;
    fitToVolume();
}

void VolumeViewport3D::fitToVolume()
{
    if (pointCloudDirty_) {
        pendingAutoFit_ = true;
    } else {
        distance_ = fittedDistanceForCurrentBounds();
    }
    update();
}

void VolumeViewport3D::setRenderMode(RenderMode mode)
{
    if (renderMode_ == mode) {
        return;
    }

    renderMode_ = mode;
    update();
}

VolumeViewport3D::RenderMode VolumeViewport3D::renderMode() const
{
    return renderMode_;
}

void VolumeViewport3D::initializeGL()
{
    initializeOpenGLFunctions();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendEquation(GL_MAX);
    glBlendFunc(GL_ONE, GL_ONE);
    qInfo("3D OpenGL context: renderer=%s vendor=%s version=%s",
          reinterpret_cast<const char *>(glGetString(GL_RENDERER)),
          reinterpret_cast<const char *>(glGetString(GL_VENDOR)),
          reinterpret_cast<const char *>(glGetString(GL_VERSION)));

    program_ = new QOpenGLShaderProgram(this);
    const bool vertexOk = program_->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShaderSource);
    const bool fragmentOk = program_->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShaderSource);
    const bool linkOk = vertexOk && fragmentOk && program_->link();
    shaderReady_ = linkOk;
    if (!linkOk) {
        lastError_ = program_->log();
        qWarning("3D shader setup failed: %s", qPrintable(program_->log()));
        return;
    }

    vertexArray_ = new QOpenGLVertexArrayObject(this);
    vertexArray_->create();
    vertexArray_->bind();

    vertexBuffer_ = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    vertexBuffer_->create();
    vertexBuffer_->bind();
    vertexBuffer_->allocate(kSliceVertices.data(), static_cast<int>(kSliceVertices.size() * sizeof(float)));

    program_->bind();
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    program_->release();

    vertexBuffer_->release();
    vertexArray_->release();

    pointProgram_ = new QOpenGLShaderProgram(this);
    const bool pointVertexOk = pointProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, kPointVertexShaderSource);
    const bool pointFragmentOk = pointProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, kPointFragmentShaderSource);
    pointShaderReady_ = pointVertexOk && pointFragmentOk && pointProgram_->link();
    if (!pointShaderReady_) {
        const QString log = pointProgram_->log();
        lastError_ = lastError_.isEmpty() ? log : lastError_ + QStringLiteral("\n") + log;
        qWarning("3D point shader setup failed: %s", qPrintable(log));
        return;
    }

    pointArray_ = new QOpenGLVertexArrayObject(this);
    pointArray_->create();
    pointArray_->bind();

    pointBuffer_ = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    pointBuffer_->create();
    pointBuffer_->bind();
    pointBuffer_->setUsagePattern(QOpenGLBuffer::DynamicDraw);

    pointProgram_->bind();
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PointVertex), nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(PointVertex), reinterpret_cast<const void *>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
    pointProgram_->release();

    pointBuffer_->release();
    pointArray_->release();
}

void VolumeViewport3D::resizeGL(int width, int height)
{
    glViewport(0, 0, width, height);
}

void VolumeViewport3D::paintGL()
{
    glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!volume_.isValid()) {
        return;
    }

    ensurePointCloud();
    ensureTextures();
    const QMatrix4x4 mvp = currentModelViewProjection();

    const bool drawPoints = renderMode_ == RenderMode::Hybrid || renderMode_ == RenderMode::Points;
    const bool drawSmoothSlices = renderMode_ == RenderMode::Hybrid || renderMode_ == RenderMode::Smooth;

    if (drawPoints && pointShaderReady_ && pointBuffer_ && pointArray_ && pointCount_ > 0) {
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glDisable(GL_DEPTH_TEST);

        pointProgram_->bind();
        pointProgram_->setUniformValue("uMvp", mvp);
        const float pointSize = std::clamp(4200.0f / std::max(volume_.width, volume_.height), 2.2f, 4.8f);
        pointProgram_->setUniformValue("uPointSize", pointSize);
        pointArray_->bind();
        glDrawArrays(GL_POINTS, 0, pointCount_);
        pointArray_->release();
        pointProgram_->release();

        glDisable(GL_PROGRAM_POINT_SIZE);
    }

    if (drawSmoothSlices && shaderReady_ && !textures_.isEmpty()) {
        glEnable(GL_BLEND);
        glBlendEquation(GL_MAX);
        glBlendFunc(GL_ONE, GL_ONE);

        program_->bind();
        program_->setUniformValue("uMvp", mvp);
        program_->setUniformValue("uBoundsMin", contentBoundsMin_);
        program_->setUniformValue("uBoundsMax", contentBoundsMax_);
        program_->setUniformValue("uTexMin", contentTexMin_);
        program_->setUniformValue("uTexMax", contentTexMax_);
        const int stepCount = std::clamp(std::max(volume_.depth * 2, 96), 96, 320);
        program_->setUniformValue("uChannelCount", effectiveChannelCount());

        QVector4D channelColors[kMaxVolumeChannels];
        QVector2D channelRanges[kMaxVolumeChannels];
        for (int index = 0; index < kMaxVolumeChannels; ++index) {
            if (index < channelSettings_.size() && index < textures_.size() && channelSettings_.at(index).enabled) {
                const ChannelRenderSettings &settings = channelSettings_.at(index);
                channelColors[index] = QVector4D(settings.color.redF(), settings.color.greenF(), settings.color.blueF(), 1.0f);
                float low = static_cast<float>(settings.low);
                float high = static_cast<float>(settings.high);
                if (volume_.pixelDataType.compare(QStringLiteral("float"), Qt::CaseInsensitive) != 0 && volume_.bytesPerComponent() <= 2) {
                    const float normalization = std::pow(2.0f, static_cast<float>(volume_.bitsPerComponent)) - 1.0f;
                    low /= normalization;
                    high /= normalization;
                }
                channelRanges[index] = QVector2D(low, high);
            } else {
                channelColors[index] = QVector4D(0.0f, 0.0f, 0.0f, 0.0f);
                channelRanges[index] = QVector2D(0.0f, 1.0f);
            }
        }

        program_->setUniformValueArray("uChannelColor", channelColors, kMaxVolumeChannels);
        program_->setUniformValueArray("uChannelRange", channelRanges, kMaxVolumeChannels);

        program_->setUniformValue("uTexture0", 0);
        program_->setUniformValue("uTexture1", 1);
        program_->setUniformValue("uTexture2", 2);
        program_->setUniformValue("uTexture3", 3);
        program_->setUniformValue("uTexture4", 4);
        program_->setUniformValue("uTexture5", 5);
        program_->setUniformValue("uTexture6", 6);
        program_->setUniformValue("uTexture7", 7);

        for (int index = 0; index < textures_.size() && index < kMaxVolumeChannels; ++index) {
            glActiveTexture(GL_TEXTURE0 + index);
            textures_.at(index)->bind();
        }

        vertexArray_->bind();
        const int sliceCount = stepCount;
        for (int sliceIndex = 0; sliceIndex < sliceCount; ++sliceIndex) {
            const float sliceLerp = sliceCount == 1
                                        ? 0.5f
                                        : (static_cast<float>(sliceIndex) / static_cast<float>(sliceCount - 1));
            program_->setUniformValue("uSliceLerp", sliceLerp);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        vertexArray_->release();

        for (int index = 0; index < textures_.size() && index < kMaxVolumeChannels; ++index) {
            textures_.at(index)->release();
        }

        program_->release();
    }
}

void VolumeViewport3D::paintEvent(QPaintEvent *event)
{
    QOpenGLWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    drawOverlay(painter);
}

void VolumeViewport3D::mousePressEvent(QMouseEvent *event)
{
    if (contourCaptureActive_ && !selectionWatcher_.isRunning() && event->button() == Qt::LeftButton) {
        contourDrawing_ = true;
        pendingContour_.clear();
        pendingContour_ << event->position();
        update();
        emit selectionStateChanged();
        event->accept();
        return;
    }

    lastMousePosition_ = event->pos();
    event->accept();
}

void VolumeViewport3D::mouseMoveEvent(QMouseEvent *event)
{
    if (contourDrawing_) {
        const QPointF position = event->position();
        if (pendingContour_.isEmpty() || QLineF(pendingContour_.last(), position).length() >= 2.0) {
            pendingContour_ << position;
            update();
        }
        event->accept();
        return;
    }

    if (!(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint delta = event->pos() - lastMousePosition_;
    lastMousePosition_ = event->pos();
    yawDegrees_ += delta.x() * 0.45f;
    pitchDegrees_ = std::clamp(pitchDegrees_ + delta.y() * 0.35f, -85.0f, 85.0f);
    update();
    event->accept();
}

void VolumeViewport3D::mouseReleaseEvent(QMouseEvent *event)
{
    if (contourDrawing_ && event->button() == Qt::LeftButton) {
        contourDrawing_ = false;
        if (pendingContour_.size() < 3) {
            pendingContour_.clear();
            selectionStatusText_ = tr("Contour too small. Draw a closed contour around the nucleus.");
            selectionStatusWarning_ = true;
        } else {
            selectionStatusText_ = tr("Contour captured. Click Accept Contour to build the 3D beam.");
            selectionStatusWarning_ = false;
        }
        emit selectionStateChanged();
        update();
        event->accept();
        return;
    }

    QOpenGLWidget::mouseReleaseEvent(event);
}

void VolumeViewport3D::wheelEvent(QWheelEvent *event)
{
    const double steps = event->angleDelta().y() / 120.0;
    distance_ = std::clamp(distance_ * std::pow(0.85f, static_cast<float>(steps)), 1.2f, 8.0f);
    update();
    event->accept();
}

QMatrix4x4 VolumeViewport3D::currentModelViewProjection() const
{
    QMatrix4x4 projection;
    projection.perspective(45.0f, float(width()) / std::max(1, height()), 0.1f, 20.0f);

    QMatrix4x4 view;
    view.lookAt(cameraPositionObject(), QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 1.0f, 0.0f));
    return projection * view;
}

VolumeViewport3D::ProjectionConstraint VolumeViewport3D::capturePendingConstraint() const
{
    ProjectionConstraint constraint;
    if (!hasPendingConstraint()) {
        return constraint;
    }

    constraint.viewportSize = size();
    constraint.contour = pendingContour_;
    constraint.maskImage = rasterizedContourMask(constraint.viewportSize, pendingContour_);
    constraint.modelViewProjection = currentModelViewProjection();
    constraint.boundsMin = contentBoundsMin_;
    constraint.boundsMax = contentBoundsMax_;
    constraint.texMin = contentTexMin_;
    constraint.texMax = contentTexMax_;
    return constraint;
}

QVector3D VolumeViewport3D::voxelObjectPosition(int x,
                                                int y,
                                                int z,
                                                const QVector3D &boundsMin,
                                                const QVector3D &boundsMax,
                                                const QVector3D &texMin,
                                                const QVector3D &texMax) const
{
    return voxelObjectPositionForConstraint(volume_, x, y, z, boundsMin, boundsMax, texMin, texMax);
}

void VolumeViewport3D::rebuildOverlaySamples()
{
    latestOverlaySamples_.clear();
    fusedOverlaySamples_.clear();
    rebuildOverlaySamplesForMask(latestMask_, &latestOverlaySamples_, 2500);
    rebuildOverlaySamplesForMask(fusedMask_, &fusedOverlaySamples_, 4000);
}

void VolumeViewport3D::rebuildOverlaySamplesForMask(const SelectionMask3D &mask,
                                                    QVector<QVector3D> *samples,
                                                    int targetSampleCount) const
{
    if (!samples || !mask.isValid() || mask.selectedVoxelCount <= 0) {
        return;
    }

    const double density = std::max(1.0, static_cast<double>(mask.selectedVoxelCount) / std::max(targetSampleCount, 1));
    const int step = std::max(1, static_cast<int>(std::cbrt(density)));
    samples->reserve(std::min(targetSampleCount, static_cast<int>(mask.selectedVoxelCount)));

    const auto isSelected = [&mask](int x, int y, int z) {
        if (x < 0 || y < 0 || z < 0 || x >= mask.width || y >= mask.height || z >= mask.depth) {
            return false;
        }

        const qsizetype voxelIndex = (static_cast<qsizetype>(z) * mask.width * mask.height)
                                   + (static_cast<qsizetype>(y) * mask.width)
                                   + x;
        return mask.voxels.at(static_cast<int>(voxelIndex)) != 0;
    };

    const auto isSurfaceVoxel = [&isSelected](int x, int y, int z) {
        return !isSelected(x - 1, y, z)
            || !isSelected(x + 1, y, z)
            || !isSelected(x, y - 1, z)
            || !isSelected(x, y + 1, z)
            || !isSelected(x, y, z - 1)
            || !isSelected(x, y, z + 1);
    };

    for (int z = 0; z < mask.depth; z += step) {
        for (int y = 0; y < mask.height; y += step) {
            for (int x = 0; x < mask.width; x += step) {
                const qsizetype voxelIndex = (static_cast<qsizetype>(z) * mask.width * mask.height)
                                           + (static_cast<qsizetype>(y) * mask.width)
                                           + x;
                if (mask.voxels.at(static_cast<int>(voxelIndex)) == 0) {
                    continue;
                }
                if (!isSurfaceVoxel(x, y, z)) {
                    continue;
                }

                samples->push_back(voxelObjectPosition(static_cast<int>(x),
                                                      static_cast<int>(y),
                                                      static_cast<int>(z),
                                                      contentBoundsMin_,
                                                      contentBoundsMax_,
                                                      contentTexMin_,
                                                      contentTexMax_));
                if (samples->size() >= targetSampleCount) {
                    return;
                }
            }
        }
    }
}

void VolumeViewport3D::startSelectionComputation(const QVector<ProjectionConstraint> &constraints)
{
    if (!volume_.isValid() || constraints.isEmpty() || selectionWatcher_.isRunning()) {
        return;
    }

    pendingSelectionConstraints_ = constraints;
    selectionStatusText_ = constraints.size() == 1
                               ? tr("Building the first 3D beam…")
                               : tr("Intersecting %1 view constraints…").arg(constraints.size());
    selectionStatusWarning_ = false;
    emit selectionStateChanged();

    const RawVolume volume = volume_;
    selectionWatcher_.setFuture(QtConcurrent::run([volume, constraints]() {
        return computeSelectionFromConstraints(volume, constraints);
    }));
}

void VolumeViewport3D::handleSelectionComputationFinished()
{
    const SelectionComputationResult result = selectionWatcher_.result();
    if (!result.success) {
        selectionStatusText_ = tr("The contour selection could not be reconstructed.");
        selectionStatusWarning_ = true;
        emit selectionStateChanged();
        update();
        return;
    }

    acceptedConstraints_ = pendingSelectionConstraints_;
    latestMask_ = result.latestMask;
    fusedMask_ = result.fusedMask;
    rebuildOverlaySamples();

    if (!result.warning.isEmpty()) {
        selectionStatusText_ = result.warning;
        selectionStatusWarning_ = true;
    } else if (acceptedConstraints_.size() == 1) {
        selectionStatusText_ = tr("1 view captured. Rotate and add another view to refine the nucleus.");
        selectionStatusWarning_ = false;
    } else {
        selectionStatusText_ = tr("%1 views fused.").arg(acceptedConstraints_.size());
        selectionStatusWarning_ = false;
    }

    emit selectionStateChanged();
    update();
}

void VolumeViewport3D::ensurePointCloud()
{
    if (!pointCloudDirty_ || !pointBuffer_ || !pointShaderReady_ || !volume_.isValid()) {
        return;
    }

    QVector<PointVertex> points;
    points.reserve(600000);

    const QVector3D scale = normalizedScale();
    const int step = pointSamplingStep(volume_);
    const int channelCount = effectiveChannelCount();
    const float threshold = 0.03f;

    auto coordinateAt = [](int index, int size) {
        if (size <= 1) {
            return 0.0f;
        }
        return (static_cast<float>(index) / static_cast<float>(size - 1)) - 0.5f;
    };

    QVector3D minimumBounds(std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max());
    QVector3D maximumBounds(std::numeric_limits<float>::lowest(),
                            std::numeric_limits<float>::lowest(),
                            std::numeric_limits<float>::lowest());
    QVector3D minimumTex(1.0f, 1.0f, 1.0f);
    QVector3D maximumTex(0.0f, 0.0f, 0.0f);

    for (int z = 0; z < volume_.depth; z += step) {
        for (int y = 0; y < volume_.height; y += step) {
            for (int x = 0; x < volume_.width; x += step) {
                const qsizetype voxelIndex = (static_cast<qsizetype>(z) * volume_.width * volume_.height)
                                             + (static_cast<qsizetype>(y) * volume_.width)
                                             + x;

                QVector3D rgb(0.0f, 0.0f, 0.0f);
                float alpha = 0.0f;
                for (int channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
                    if (!channelSettings_.at(channelIndex).enabled) {
                        continue;
                    }

                    const ChannelRenderSettings &settings = channelSettings_.at(channelIndex);
                    const double rawValue = sampleVolumeValue(volume_, channelIndex, voxelIndex);
                    const double denominator = std::max(settings.high - settings.low, 1e-6);
                    const float scaled = static_cast<float>(std::clamp((rawValue - settings.low) / denominator, 0.0, 1.0));
                    if (scaled <= 0.0f) {
                        continue;
                    }

                    const float visual = std::pow(scaled, 0.26f);
                    rgb += QVector3D(settings.color.redF(), settings.color.greenF(), settings.color.blueF()) * visual;
                    alpha = std::max(alpha, visual);
                }

                if (alpha < threshold) {
                    continue;
                }

                PointVertex point;
                const float jitterScaleX = step > 1 ? (scale.x() / std::max(volume_.width - 1, 1)) * (step * 0.35f) : 0.0f;
                const float jitterScaleY = step > 1 ? (scale.y() / std::max(volume_.height - 1, 1)) * (step * 0.35f) : 0.0f;
                const float jitterScaleZ = step > 1 ? (scale.z() / std::max(volume_.depth - 1, 1)) * (step * 0.35f) : 0.0f;
                point.x = coordinateAt(x, volume_.width) * scale.x() + jitterFromIndex(x, y, z, 1) * jitterScaleX;
                point.y = -coordinateAt(y, volume_.height) * scale.y() + jitterFromIndex(x, y, z, 2) * jitterScaleY;
                point.z = coordinateAt(z, volume_.depth) * scale.z() + jitterFromIndex(x, y, z, 3) * jitterScaleZ;
                point.r = std::min(rgb.x(), 1.0f);
                point.g = std::min(rgb.y(), 1.0f);
                point.b = std::min(rgb.z(), 1.0f);
                point.a = std::clamp(alpha * 0.14f, 0.03f, 0.24f);
                points.push_back(point);

                minimumBounds.setX(std::min(minimumBounds.x(), point.x));
                minimumBounds.setY(std::min(minimumBounds.y(), point.y));
                minimumBounds.setZ(std::min(minimumBounds.z(), point.z));
                maximumBounds.setX(std::max(maximumBounds.x(), point.x));
                maximumBounds.setY(std::max(maximumBounds.y(), point.y));
                maximumBounds.setZ(std::max(maximumBounds.z(), point.z));

                const QVector3D texCoord(volume_.width <= 1 ? 0.5f : static_cast<float>(x) / static_cast<float>(volume_.width - 1),
                                         volume_.height <= 1 ? 0.5f : static_cast<float>(y) / static_cast<float>(volume_.height - 1),
                                         volume_.depth <= 1 ? 0.5f : static_cast<float>(z) / static_cast<float>(volume_.depth - 1));
                minimumTex.setX(std::min(minimumTex.x(), texCoord.x()));
                minimumTex.setY(std::min(minimumTex.y(), texCoord.y()));
                minimumTex.setZ(std::min(minimumTex.z(), texCoord.z()));
                maximumTex.setX(std::max(maximumTex.x(), texCoord.x()));
                maximumTex.setY(std::max(maximumTex.y(), texCoord.y()));
                maximumTex.setZ(std::max(maximumTex.z(), texCoord.z()));
            }
        }
    }

    float fitScale = 1.0f;
    QVector3D fittedMinimumBounds = minimumBounds;
    QVector3D fittedMaximumBounds = maximumBounds;
    if (!points.isEmpty()) {
        const QVector3D center = 0.5f * (minimumBounds + maximumBounds);
        const QVector3D extent = maximumBounds - minimumBounds;
        const float longestAxis = std::max({extent.x(), extent.y(), extent.z(), 1.0e-6f});
        fitScale = 1.2f / longestAxis;
        fittedMinimumBounds = QVector3D(std::numeric_limits<float>::max(),
                                        std::numeric_limits<float>::max(),
                                        std::numeric_limits<float>::max());
        fittedMaximumBounds = QVector3D(std::numeric_limits<float>::lowest(),
                                        std::numeric_limits<float>::lowest(),
                                        std::numeric_limits<float>::lowest());
        for (PointVertex &point : points) {
            point.x = (point.x - center.x()) * fitScale;
            point.y = (point.y - center.y()) * fitScale;
            point.z = (point.z - center.z()) * fitScale;
            fittedMinimumBounds.setX(std::min(fittedMinimumBounds.x(), point.x));
            fittedMinimumBounds.setY(std::min(fittedMinimumBounds.y(), point.y));
            fittedMinimumBounds.setZ(std::min(fittedMinimumBounds.z(), point.z));
            fittedMaximumBounds.setX(std::max(fittedMaximumBounds.x(), point.x));
            fittedMaximumBounds.setY(std::max(fittedMaximumBounds.y(), point.y));
            fittedMaximumBounds.setZ(std::max(fittedMaximumBounds.z(), point.z));
        }
        contentBoundsMin_ = fittedMinimumBounds;
        contentBoundsMax_ = fittedMaximumBounds;
        contentTexMin_ = minimumTex;
        contentTexMax_ = maximumTex;
    } else {
        contentBoundsMin_ = QVector3D(-0.6f, -0.6f, -0.2f);
        contentBoundsMax_ = QVector3D(0.6f, 0.6f, 0.2f);
        contentTexMin_ = QVector3D(0.0f, 0.0f, 0.0f);
        contentTexMax_ = QVector3D(1.0f, 1.0f, 1.0f);
    }

    if (pendingAutoFit_) {
        distance_ = fittedDistanceForCurrentBounds();
        pendingAutoFit_ = false;
    }

    pointArray_->bind();
    pointBuffer_->bind();
    pointBuffer_->allocate(points.constData(), static_cast<int>(points.size() * sizeof(PointVertex)));
    pointBuffer_->release();
    pointArray_->release();

    pointCount_ = points.size();
    pointCloudDirty_ = false;
}

void VolumeViewport3D::ensureTextures()
{
    if (!texturesDirty_ || !volume_.isValid()) {
        return;
    }

    clearTextures();
    qInfo("3D texture upload starting: channels=%d bytesPerComponent=%d voxelCount=%lld",
          effectiveChannelCount(),
          volume_.bytesPerComponent(),
          static_cast<long long>(volume_.voxelCount()));

    const int channelCount = effectiveChannelCount();
    for (int index = 0; index < channelCount; ++index) {
        auto *texture = new QOpenGLTexture(QOpenGLTexture::Target3D);
        texture->create();
        texture->setWrapMode(QOpenGLTexture::ClampToEdge);
        texture->setMinificationFilter(QOpenGLTexture::Linear);
        texture->setMagnificationFilter(QOpenGLTexture::Linear);
        texture->setSize(volume_.width, volume_.height, volume_.depth);

        if (volume_.bytesPerComponent() == 1) {
            texture->setFormat(QOpenGLTexture::R8_UNorm);
            texture->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::UInt8);
            texture->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, volume_.channelData.at(index).constData());
        } else if (volume_.bytesPerComponent() == 2) {
            texture->setFormat(QOpenGLTexture::R16_UNorm);
            texture->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::UInt16);
            texture->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt16, volume_.channelData.at(index).constData());
        } else {
            texture->setFormat(QOpenGLTexture::R32F);
            texture->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::Float32);
            if (volume_.pixelDataType.compare(QStringLiteral("float"), Qt::CaseInsensitive) == 0) {
                texture->setData(QOpenGLTexture::Red, QOpenGLTexture::Float32, volume_.channelData.at(index).constData());
            } else {
                QVector<float> converted(static_cast<int>(volume_.voxelCount()));
                const quint32 *source = reinterpret_cast<const quint32 *>(volume_.channelData.at(index).constData());
                for (int voxelIndex = 0; voxelIndex < converted.size(); ++voxelIndex) {
                    converted[voxelIndex] = static_cast<float>(source[voxelIndex]);
                }
                texture->setData(QOpenGLTexture::Red, QOpenGLTexture::Float32, converted.constData());
            }
        }

        textures_.push_back(texture);
    }

    texturesDirty_ = false;
    qInfo("3D texture upload complete.");
}

void VolumeViewport3D::clearTextures()
{
    qDeleteAll(textures_);
    textures_.clear();
}

void VolumeViewport3D::clearPointCloud()
{
    pointCount_ = 0;
    pendingAutoFit_ = false;
    contentBoundsMin_ = QVector3D(-0.6f, -0.6f, -0.2f);
    contentBoundsMax_ = QVector3D(0.6f, 0.6f, 0.2f);
    contentTexMin_ = QVector3D(0.0f, 0.0f, 0.0f);
    contentTexMax_ = QVector3D(1.0f, 1.0f, 1.0f);
}

float VolumeViewport3D::fittedDistanceForCurrentBounds() const
{
    const QVector3D extent = contentBoundsMax_ - contentBoundsMin_;
    const float radius = std::max(0.2f, 0.5f * extent.length());
    const float halfFovRadians = qDegreesToRadians(45.0f * 0.5f);
    const float distanceForSphere = radius / std::sin(std::max(halfFovRadians, 0.15f));
    const float aspect = static_cast<float>(width()) / static_cast<float>(std::max(1, height()));
    const float halfHeight = std::max(extent.y() * 0.5f, 0.15f);
    const float halfWidth = std::max(extent.x() * 0.5f, 0.15f);
    const float distanceForHeight = halfHeight / std::tan(std::max(halfFovRadians, 0.15f));
    const float horizontalHalfFov = std::atan(std::tan(halfFovRadians) * std::max(aspect, 0.5f));
    const float distanceForWidth = halfWidth / std::tan(std::max(horizontalHalfFov, 0.2f));
    return std::clamp(std::max({distanceForSphere, distanceForHeight, distanceForWidth}) * 1.15f, 1.1f, 8.0f);
}

QVector3D VolumeViewport3D::normalizedScale() const
{
    const QVector3D spacing = volume_.voxelSpacing;
    QVector3D scale(volume_.width * spacing.x(),
                    volume_.height * spacing.y(),
                    volume_.depth * spacing.z());
    const float maximumComponent = std::max({scale.x(), scale.y(), scale.z(), 1.0f});
    scale /= maximumComponent;

    // Many microscopy files omit z calibration. When that happens, a 1000x1000x84
    // stack becomes almost paper-thin and is effectively invisible in the first-pass
    // 3D view, so keep a visible minimum slab thickness.
    if (qFuzzyCompare(spacing.x(), 1.0f) && qFuzzyCompare(spacing.y(), 1.0f) && qFuzzyCompare(spacing.z(), 1.0f)) {
        scale.setZ(std::max(scale.z(), 0.25f));
    }

    return scale;
}

QVector3D VolumeViewport3D::cameraPositionObject() const
{
    const float yaw = qDegreesToRadians(yawDegrees_);
    const float pitch = qDegreesToRadians(pitchDegrees_);
    return {
        distance_ * std::cos(pitch) * std::sin(yaw),
        distance_ * std::sin(pitch),
        distance_ * std::cos(pitch) * std::cos(yaw)
    };
}

int VolumeViewport3D::effectiveChannelCount() const
{
    return std::min({volume_.components, static_cast<int>(channelSettings_.size()), kMaxVolumeChannels});
}

int VolumeViewport3D::activeSelectionChannelCount() const
{
    int enabledCount = 0;
    for (const ChannelRenderSettings &settings : channelSettings_) {
        enabledCount += settings.enabled ? 1 : 0;
    }
    return enabledCount;
}

QColor VolumeViewport3D::selectionOverlayColor() const
{
    for (const ChannelRenderSettings &settings : channelSettings_) {
        if (!settings.enabled) {
            continue;
        }

        QColor overlay = QColor::fromRgbF(1.0 - settings.color.redF(),
                                          1.0 - settings.color.greenF(),
                                          1.0 - settings.color.blueF());
        if (overlay.lightnessF() < 0.35) {
            overlay = overlay.lighter(160);
        }
        if (overlay.saturationF() < 0.35) {
            overlay.setHsvF(static_cast<float>(std::fmod(settings.color.hsvHueF() + 0.5, 1.0)), 0.85f, 1.0f);
        }
        return overlay;
    }

    return QColor(255, 220, 120);
}

void VolumeViewport3D::drawOverlay(QPainter &painter)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor overlayColor = selectionOverlayColor();
    const qreal fusedOpacity = selectionOverlayOpacity_;
    const qreal latestOpacity = std::clamp(fusedOpacity * 0.35, 0.08, 0.45);
    const qreal pendingFillOpacity = std::clamp(fusedOpacity * 0.25, 0.04, 0.25);
    QColor pendingStrokeColor = overlayColor;
    pendingStrokeColor.setAlphaF(fusedOpacity);

    if (acceptedConstraints_.size() > 1) {
        drawProjectedMask(painter, latestOverlaySamples_, overlayColor, 3, latestOpacity);
    }
    drawProjectedMask(painter, fusedOverlaySamples_, overlayColor, 4, fusedOpacity);

    if (!pendingContour_.isEmpty()) {
        QPainterPath path;
        path.addPolygon(pendingContour_);
        painter.setPen(QPen(pendingStrokeColor, 2.0));
        QColor fillColor = overlayColor;
        fillColor.setAlphaF(pendingFillOpacity);
        painter.fillPath(path, fillColor);
        painter.drawPath(path);
    }

    const QString overlayText = contourCaptureActive_
                                    ? tr("Contour mode: drag around the nucleus, then click Accept Contour.")
                                    : tr("3D Select: use Add View to add another contour constraint.");
    QRectF textRect = painter.boundingRect(QRectF(0, 0, width() - 24.0, 64.0),
                                           Qt::TextWordWrap,
                                           overlayText);
    textRect.moveLeft(12.0);
    textRect.moveBottom(height() - 12.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 130));
    painter.drawRoundedRect(textRect.adjusted(-8.0, -6.0, 8.0, 6.0), 8.0, 8.0);
    painter.setPen(QColor(240, 240, 240));
    painter.drawText(textRect, Qt::TextWordWrap, overlayText);

    painter.restore();
}

void VolumeViewport3D::drawProjectedMask(QPainter &painter,
                                         const QVector<QVector3D> &voxels,
                                         const QColor &color,
                                         int pixelRadius,
                                         qreal opacity) const
{
    if (voxels.isEmpty() || !volume_.isValid()) {
        return;
    }

    const QMatrix4x4 mvp = currentModelViewProjection();
    QVector<QPointF> points;
    points.reserve(voxels.size());

    for (const QVector3D &objectPosition : voxels) {
        const QVector4D clipPosition = mvp * QVector4D(objectPosition, 1.0f);
        if (clipPosition.w() <= 1.0e-6f) {
            continue;
        }

        const QVector3D ndc(clipPosition.x() / clipPosition.w(),
                            clipPosition.y() / clipPosition.w(),
                            clipPosition.z() / clipPosition.w());
        if (ndc.x() < -1.0f || ndc.x() > 1.0f || ndc.y() < -1.0f || ndc.y() > 1.0f || ndc.z() < -1.0f || ndc.z() > 1.0f) {
            continue;
        }

        points.push_back(QPointF((ndc.x() * 0.5f + 0.5f) * static_cast<float>(width()),
                                 (1.0f - (ndc.y() * 0.5f + 0.5f)) * static_cast<float>(height())));
    }

    if (points.isEmpty()) {
        return;
    }

    QColor projectedColor = color;
    projectedColor.setAlphaF(opacity);
    QPen pen(projectedColor);
    pen.setCapStyle(Qt::RoundCap);
    pen.setWidth(pixelRadius);
    painter.setPen(pen);
    painter.drawPoints(points.constData(), points.size());
}
