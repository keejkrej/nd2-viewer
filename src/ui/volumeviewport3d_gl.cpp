#include "ui/volumeviewport3d_gl.h"

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QString>
#include <QtGlobal>
#include <QtMath>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

namespace
{
constexpr int kMaxVolumeChannels = 8;
constexpr int kVolumeTextureUnitBase = 1;

int effectiveBitsForVolume(const RawVolume &volume)
{
    if (volume.bitsPerComponent > 0) {
        return volume.bitsPerComponent;
    }
    const int bytes = volume.bytesPerComponent();
    if (bytes == 2) {
        return 16;
    }
    if (bytes == 1) {
        return 8;
    }
    return 16;
}

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
    vec3 texCoord = mix(uTexMin, uTexMax, localCoord);
    texCoord.y = uTexMin.y + uTexMax.y - texCoord.y;
    vTexCoord = texCoord;
    vec3 objectPosition = mix(uBoundsMin, uBoundsMax, localCoord);
    gl_Position = uMvp * vec4(objectPosition, 1.0);
}
)";

const char *kFragmentShaderSource = R"(#version 330 core
in vec3 vTexCoord;

uniform sampler2DArray uTexture0;
uniform sampler2DArray uTexture1;
uniform sampler2DArray uTexture2;
uniform sampler2DArray uTexture3;
uniform sampler2DArray uTexture4;
uniform sampler2DArray uTexture5;
uniform sampler2DArray uTexture6;
uniform sampler2DArray uTexture7;
uniform vec4 uChannelColor[8];
uniform vec2 uChannelRange[8];
uniform int uChannelCount;
uniform int uVolumeDepth;
uniform float uChannelSampleScale[8];

out vec4 fragColor;

float layerFromTexZ(float zNorm)
{
    if (uVolumeDepth <= 1) {
        return 0.0;
    }
    return zNorm * float(uVolumeDepth - 1);
}

float sampleChannel(int channelIndex, vec3 position)
{
    vec3 coord = vec3(position.x, position.y, layerFromTexZ(position.z));
    if (channelIndex == 0) return texture(uTexture0, coord).r;
    if (channelIndex == 1) return texture(uTexture1, coord).r;
    if (channelIndex == 2) return texture(uTexture2, coord).r;
    if (channelIndex == 3) return texture(uTexture3, coord).r;
    if (channelIndex == 4) return texture(uTexture4, coord).r;
    if (channelIndex == 5) return texture(uTexture5, coord).r;
    if (channelIndex == 6) return texture(uTexture6, coord).r;
    return texture(uTexture7, coord).r;
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

        float value = sampleChannel(channelIndex, vTexCoord) * uChannelSampleScale[channelIndex];
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

constexpr std::array<float, 12> kSliceVertices = {
    0.0f, 0.0f,
    1.0f, 1.0f,
    1.0f, 0.0f,
    0.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f,
};

QVector<float> channelDataAsFloat32ForTexture(const RawVolume &volume, int channelIndex)
{
    const qsizetype n = volume.voxelCount();
    QVector<float> out(static_cast<int>(n));
    float *dst = out.data();
    const char *srcBase = volume.channelData.at(channelIndex).constData();
    const int bpc = volume.bytesPerComponent();

    if (volume.pixelDataType.compare(QStringLiteral("float"), Qt::CaseInsensitive) == 0 && bpc == 4) {
        std::memcpy(dst, srcBase, static_cast<size_t>(n) * sizeof(float));
        return out;
    }
    if (bpc == 1) {
        const double denom = std::pow(2.0, effectiveBitsForVolume(volume)) - 1.0;
        for (qsizetype i = 0; i < n; ++i) {
            dst[i] = static_cast<float>(static_cast<unsigned char>(srcBase[i]) / denom);
        }
        return out;
    }
    if (bpc == 2) {
        const int normBits = effectiveBitsForVolume(volume);
        const double denom = std::pow(2.0, normBits) - 1.0;
        for (qsizetype i = 0; i < n; ++i) {
            quint16 v = 0;
            std::memcpy(&v, srcBase + i * 2, sizeof(v));
            dst[i] = static_cast<float>(static_cast<double>(v) / denom);
        }
        return out;
    }
    if (bpc == 4) {
        const auto *src = reinterpret_cast<const quint32 *>(srcBase);
        for (qsizetype i = 0; i < n; ++i) {
            dst[i] = static_cast<float>(src[i]);
        }
        return out;
    }
    std::fill(dst, dst + n, 0.0f);
    return out;
}
}

VolumeViewport3DBackendGl::VolumeViewport3DBackendGl(QWidget *parent)
    : QOpenGLWidget(parent)
{
    channelSampleScale_.fill(1.0f);
    setMinimumSize(500, 400);
}

VolumeViewport3DBackendGl::~VolumeViewport3DBackendGl()
{
    makeCurrent();
    clearTextures();
    delete vertexArray_;
    delete vertexBuffer_;
    delete program_;
    doneCurrent();
}

QWidget *VolumeViewport3DBackendGl::widget()
{
    return this;
}

void VolumeViewport3DBackendGl::setVolume(const RawVolume &volume, const QVector<ChannelRenderSettings> &channelSettings)
{
    volume_ = volume;
    channelSettings_ = channelSettings;
    texturesDirty_ = true;
    pendingAutoFit_ = true;
    lastError_.clear();
    renderSummary_ = QStringLiteral("%1 × %2 × %3").arg(volume_.width).arg(volume_.height).arg(volume_.depth);
    qInfo("3D OpenGL viewport volume set: %dx%dx%d components=%d bits=%d type=%s",
          volume_.width,
          volume_.height,
          volume_.depth,
          volume_.components,
          volume_.bitsPerComponent,
          qPrintable(volume_.pixelDataType));
    resetView();
}

void VolumeViewport3DBackendGl::setChannelSettings(const QVector<ChannelRenderSettings> &channelSettings)
{
    channelSettings_ = channelSettings;
    update();
}

void VolumeViewport3DBackendGl::resetView()
{
    yawDegrees_ = -35.0f;
    pitchDegrees_ = 25.0f;
    fitToVolume();
}

void VolumeViewport3DBackendGl::fitToVolume()
{
    if (texturesDirty_ || !volume_.isValid()) {
        pendingAutoFit_ = true;
    } else {
        distance_ = fittedDistanceForCurrentBounds();
    }
    update();
}

QString VolumeViewport3DBackendGl::lastError() const
{
    return lastError_;
}

QString VolumeViewport3DBackendGl::renderSummary() const
{
    return renderSummary_;
}

void VolumeViewport3DBackendGl::initializeGL()
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
        qWarning("3D OpenGL shader setup failed: %s", qPrintable(program_->log()));
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
}

void VolumeViewport3DBackendGl::resizeGL(int width, int height)
{
    glViewport(0, 0, width, height);
}

void VolumeViewport3DBackendGl::paintGL()
{
    glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!volume_.isValid()) {
        return;
    }

    ensureTextures();

    if (pendingAutoFit_ && !texturesDirty_) {
        distance_ = fittedDistanceForCurrentBounds();
        pendingAutoFit_ = false;
    }

    QMatrix4x4 projection;
    projection.perspective(45.0f, float(width()) / std::max(1, height()), 0.1f, 20.0f);

    QMatrix4x4 view;
    const QVector3D cameraPosition = cameraPositionObject();
    view.lookAt(cameraPosition, QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 1.0f, 0.0f));

    QMatrix4x4 mvp = projection * view;

    if (shaderReady_ && !textures_.isEmpty()) {
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
        program_->setUniformValue("uVolumeDepth", volume_.depth);
        program_->setUniformValueArray("uChannelSampleScale",
                                       channelSampleScale_.data(),
                                       VolumeViewport3DBackendGl::kVolumeChannelUniforms,
                                       1);

        QVector4D channelColors[kMaxVolumeChannels];
        QVector2D channelRanges[kMaxVolumeChannels];
        const float intNormalization = static_cast<float>(std::pow(2.0, effectiveBitsForVolume(volume_)) - 1.0);
        for (int index = 0; index < kMaxVolumeChannels; ++index) {
            if (index < channelSettings_.size() && index < textures_.size() && channelSettings_.at(index).enabled) {
                const ChannelRenderSettings &settings = channelSettings_.at(index);
                channelColors[index] = QVector4D(settings.color.redF(), settings.color.greenF(), settings.color.blueF(), 1.0f);
                float low = static_cast<float>(settings.low);
                float high = static_cast<float>(settings.high);
                if (volume_.pixelDataType.compare(QStringLiteral("float"), Qt::CaseInsensitive) != 0 && volume_.bytesPerComponent() <= 2) {
                    low /= intNormalization;
                    high /= intNormalization;
                }
                channelRanges[index] = QVector2D(low, high);
            } else {
                channelColors[index] = QVector4D(0.0f, 0.0f, 0.0f, 0.0f);
                channelRanges[index] = QVector2D(0.0f, 1.0f);
            }
        }

        program_->setUniformValueArray("uChannelColor", channelColors, kMaxVolumeChannels);
        program_->setUniformValueArray("uChannelRange", channelRanges, kMaxVolumeChannels);

        program_->setUniformValue("uTexture0", kVolumeTextureUnitBase);
        program_->setUniformValue("uTexture1", kVolumeTextureUnitBase + 1);
        program_->setUniformValue("uTexture2", kVolumeTextureUnitBase + 2);
        program_->setUniformValue("uTexture3", kVolumeTextureUnitBase + 3);
        program_->setUniformValue("uTexture4", kVolumeTextureUnitBase + 4);
        program_->setUniformValue("uTexture5", kVolumeTextureUnitBase + 5);
        program_->setUniformValue("uTexture6", kVolumeTextureUnitBase + 6);
        program_->setUniformValue("uTexture7", kVolumeTextureUnitBase + 7);

        for (int index = 0; index < textures_.size() && index < kMaxVolumeChannels; ++index) {
            textures_.at(index)->bind(static_cast<uint>(kVolumeTextureUnitBase + index),
                                      QOpenGLTexture::DontResetTextureUnit);
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
            textures_.at(index)->release(static_cast<uint>(kVolumeTextureUnitBase + index),
                                         QOpenGLTexture::DontResetTextureUnit);
        }

        program_->release();
    }
}

void VolumeViewport3DBackendGl::mousePressEvent(QMouseEvent *event)
{
    lastMousePosition_ = event->pos();
    event->accept();
}

void VolumeViewport3DBackendGl::mouseMoveEvent(QMouseEvent *event)
{
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

void VolumeViewport3DBackendGl::wheelEvent(QWheelEvent *event)
{
    const double steps = event->angleDelta().y() / 120.0;
    distance_ = std::clamp(distance_ * std::pow(0.85f, static_cast<float>(steps)), 1.2f, 8.0f);
    update();
    event->accept();
}

void VolumeViewport3DBackendGl::updateContentBoundsFromVolume()
{
    if (!volume_.isValid()) {
        contentBoundsMin_ = QVector3D(-0.6f, -0.6f, -0.2f);
        contentBoundsMax_ = QVector3D(0.6f, 0.6f, 0.2f);
        contentTexMin_ = QVector3D(0.0f, 0.0f, 0.0f);
        contentTexMax_ = QVector3D(1.0f, 1.0f, 1.0f);
        return;
    }

    const QVector3D scale = normalizedScale();
    auto axisValue = [](int index, int size) -> float {
        if (size <= 1) {
            return 0.0f;
        }
        return (static_cast<float>(index) / static_cast<float>(size - 1)) - 0.5f;
    };
    auto corner = [&](int ix, int iy, int iz) {
        return QVector3D(axisValue(ix, volume_.width) * scale.x(),
                         -axisValue(iy, volume_.height) * scale.y(),
                         axisValue(iz, volume_.depth) * scale.z());
    };

    const int w = volume_.width;
    const int h = volume_.height;
    const int d = volume_.depth;
    const int ix0 = 0;
    const int ix1 = std::max(0, w - 1);
    const int iy0 = 0;
    const int iy1 = std::max(0, h - 1);
    const int iz0 = 0;
    const int iz1 = std::max(0, d - 1);

    QVector3D mn(std::numeric_limits<float>::max(),
                 std::numeric_limits<float>::max(),
                 std::numeric_limits<float>::max());
    QVector3D mx(std::numeric_limits<float>::lowest(),
                 std::numeric_limits<float>::lowest(),
                 std::numeric_limits<float>::lowest());

    for (int ix : {ix0, ix1}) {
        for (int iy : {iy0, iy1}) {
            for (int iz : {iz0, iz1}) {
                const QVector3D c = corner(ix, iy, iz);
                mn.setX(std::min(mn.x(), c.x()));
                mn.setY(std::min(mn.y(), c.y()));
                mn.setZ(std::min(mn.z(), c.z()));
                mx.setX(std::max(mx.x(), c.x()));
                mx.setY(std::max(mx.y(), c.y()));
                mx.setZ(std::max(mx.z(), c.z()));
            }
        }
    }

    contentBoundsMin_ = mn;
    contentBoundsMax_ = mx;
    contentTexMin_ = QVector3D(0.0f, 0.0f, 0.0f);
    contentTexMax_ = QVector3D(1.0f, 1.0f, 1.0f);
}

void VolumeViewport3DBackendGl::ensureTextures()
{
    if (!texturesDirty_ || !volume_.isValid()) {
        return;
    }

    clearTextures();
    qInfo("3D OpenGL texture upload starting: channels=%d bytesPerComponent=%d voxelCount=%lld",
          effectiveChannelCount(),
          volume_.bytesPerComponent(),
          static_cast<long long>(volume_.voxelCount()));

    GLint maxTexSize = 4096;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
    GLint maxArrayLayers = 2048;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxArrayLayers);
    if (volume_.width > maxTexSize || volume_.height > maxTexSize || volume_.depth > maxArrayLayers) {
        lastError_ = tr("Volume %1×%2×%3 exceeds texture limits (max 2D size %4, max array layers %5).")
                         .arg(volume_.width)
                         .arg(volume_.height)
                         .arg(volume_.depth)
                         .arg(maxTexSize)
                         .arg(maxArrayLayers);
        qWarning("%s", qPrintable(lastError_));
        texturesDirty_ = false;
        return;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    channelSampleScale_.fill(1.0f);

    const int channelCount = effectiveChannelCount();
    const int w = volume_.width;
    const int h = volume_.height;
    const int bpc = volume_.bytesPerComponent();
    const bool useFloat32Texture = volume_.pixelDataType.compare(QStringLiteral("float"), Qt::CaseInsensitive) == 0 && bpc == 4;

    for (int index = 0; index < channelCount; ++index) {
        auto *texture = new QOpenGLTexture(QOpenGLTexture::Target2DArray);
        texture->create();
        texture->setWrapMode(QOpenGLTexture::ClampToEdge);
        texture->setMinificationFilter(QOpenGLTexture::Linear);
        texture->setMagnificationFilter(QOpenGLTexture::Linear);
        texture->setAutoMipMapGenerationEnabled(false);
        texture->setMipLevels(1);
        texture->setMipBaseLevel(0);
        texture->setMipMaxLevel(0);
        texture->setSize(w, h);
        texture->setLayers(volume_.depth);

        if (useFloat32Texture) {
            const QVector<float> floatTexels = channelDataAsFloat32ForTexture(volume_, index);
#if defined(Q_OS_MACOS)
            float mx = 0.0f;
            for (float v : floatTexels) {
                mx = std::max(mx, v);
            }
            if (!(mx > 0.0f) || !std::isfinite(mx)) {
                mx = 1.0f;
            }
            channelSampleScale_[static_cast<size_t>(index)] = mx;
            QVector<quint16> u16Norm(floatTexels.size());
            const double scale = 65535.0 / static_cast<double>(mx);
            for (qsizetype i = 0; i < floatTexels.size(); ++i) {
                u16Norm[static_cast<int>(i)] = static_cast<quint16>(
                    std::clamp(std::lround(static_cast<double>(floatTexels[i]) * scale), 0L, 65535L));
            }
            texture->setFormat(QOpenGLTexture::R16_UNorm);
            texture->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::UInt16);
            texture->setData(0,
                             0,
                             0,
                             w,
                             h,
                             volume_.depth,
                             QOpenGLTexture::Red,
                             QOpenGLTexture::UInt16,
                             u16Norm.constData());
#else
            texture->setFormat(QOpenGLTexture::R32F);
            texture->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::Float32);
            texture->setData(0,
                             0,
                             0,
                             w,
                             h,
                             volume_.depth,
                             QOpenGLTexture::Red,
                             QOpenGLTexture::Float32,
                             floatTexels.constData());
#endif
        } else if (bpc == 1) {
            const QByteArray &raw = volume_.channelData.at(index);
            texture->setFormat(QOpenGLTexture::R8_UNorm);
            texture->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::UInt8);
            texture->setData(0,
                             0,
                             0,
                             w,
                             h,
                             volume_.depth,
                             QOpenGLTexture::Red,
                             QOpenGLTexture::UInt8,
                             raw.constData());
        } else if (bpc == 2) {
            const QByteArray &raw = volume_.channelData.at(index);
            texture->setFormat(QOpenGLTexture::R16_UNorm);
            texture->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::UInt16);
            texture->setData(0,
                             0,
                             0,
                             w,
                             h,
                             volume_.depth,
                             QOpenGLTexture::Red,
                             QOpenGLTexture::UInt16,
                             raw.constData());
        } else {
            const QVector<float> floatTexels = channelDataAsFloat32ForTexture(volume_, index);
            texture->setFormat(QOpenGLTexture::R32F);
            texture->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::Float32);
            texture->setData(0,
                             0,
                             0,
                             w,
                             h,
                             volume_.depth,
                             QOpenGLTexture::Red,
                             QOpenGLTexture::Float32,
                             floatTexels.constData());
        }

        const GLenum glErr = glGetError();
        if (glErr != GL_NO_ERROR) {
            lastError_ = tr("OpenGL error after 3D texture upload (channel %1): 0x%2")
                             .arg(index)
                             .arg(QString::number(glErr, 16));
            qWarning("%s", qPrintable(lastError_));
            delete texture;
            clearTextures();
            texturesDirty_ = false;
            return;
        }

        textures_.push_back(texture);
    }

    texturesDirty_ = false;
    updateContentBoundsFromVolume();
    qInfo("3D OpenGL texture upload complete.");
}

void VolumeViewport3DBackendGl::clearTextures()
{
    qDeleteAll(textures_);
    textures_.clear();
}

float VolumeViewport3DBackendGl::fittedDistanceForCurrentBounds() const
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

QVector3D VolumeViewport3DBackendGl::normalizedScale() const
{
    const QVector3D spacing = volume_.voxelSpacing;
    QVector3D scale(volume_.width * spacing.x(),
                    volume_.height * spacing.y(),
                    volume_.depth * spacing.z());
    const float maximumComponent = std::max({scale.x(), scale.y(), scale.z(), 1.0f});
    scale /= maximumComponent;

    if (qFuzzyCompare(spacing.x(), 1.0f) && qFuzzyCompare(spacing.y(), 1.0f) && qFuzzyCompare(spacing.z(), 1.0f)) {
        scale.setZ(std::max(scale.z(), 0.25f));
    }

    return scale;
}

QVector3D VolumeViewport3DBackendGl::cameraPositionObject() const
{
    const float yaw = qDegreesToRadians(yawDegrees_);
    const float pitch = qDegreesToRadians(pitchDegrees_);
    return {
        distance_ * std::cos(pitch) * std::sin(yaw),
        distance_ * std::sin(pitch),
        distance_ * std::cos(pitch) * std::cos(yaw)
    };
}

int VolumeViewport3DBackendGl::effectiveChannelCount() const
{
    return std::min({volume_.components, static_cast<int>(channelSettings_.size()), kMaxVolumeChannels});
}
