#include "core/deconvolution.h"

#include "core/framerenderer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRect>

#include <onnxruntime_cxx_api.h>

#include <itkImage.h>
#include <itkImageRegionConstIterator.h>
#include <itkImageRegionIterator.h>
#include <itkMacro.h>
#include <itkRichardsonLucyDeconvolutionImageFilter.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using ItkImage2D = itk::Image<float, 2>;

std::basic_string<ORTCHAR_T> toOrtPath(const QString &path)
{
#ifdef _WIN32
    return QFileInfo(path).absoluteFilePath().toStdWString();
#else
    return QFile::encodeName(QFileInfo(path).absoluteFilePath()).toStdString();
#endif
}

double sampleValue(const RawFrame &frame, int x, int y, int component)
{
    const int bytesPerComponent = frame.bytesPerComponent();
    const qsizetype pixelOffset = static_cast<qsizetype>((x * frame.components + component) * bytesPerComponent);
    const qsizetype rowOffset = static_cast<qsizetype>(y) * frame.bytesPerLine;
    const char *pixel = frame.data.constData() + rowOffset + pixelOffset;

    switch (bytesPerComponent) {
    case 1:
        return static_cast<unsigned char>(pixel[0]);
    case 2: {
        quint16 value = 0;
        std::memcpy(&value, pixel, sizeof(value));
        return value;
    }
    case 4: {
        if (frame.pixelDataType.compare(QStringLiteral("float"), Qt::CaseInsensitive) == 0) {
            float value = 0.0f;
            std::memcpy(&value, pixel, sizeof(value));
            return value;
        }
        quint32 value = 0;
        std::memcpy(&value, pixel, sizeof(value));
        return value;
    }
    default:
        return 0.0;
    }
}

float sanitizedInputValue(double value)
{
    if (!std::isfinite(value) || value < 0.0) {
        return 0.0f;
    }

    return static_cast<float>(std::min<double>(value, std::numeric_limits<float>::max()));
}

float sanitizedOutputValue(float value)
{
    if (!std::isfinite(value) || value < 0.0f) {
        return 0.0f;
    }

    return value;
}

ItkImage2D::Pointer createImageFromRawComponent(const RawFrame &frame, int component)
{
    ItkImage2D::IndexType start;
    start.Fill(0);

    ItkImage2D::SizeType size;
    size[0] = static_cast<itk::SizeValueType>(frame.width);
    size[1] = static_cast<itk::SizeValueType>(frame.height);

    ItkImage2D::RegionType region;
    region.SetIndex(start);
    region.SetSize(size);

    ItkImage2D::Pointer image = ItkImage2D::New();
    image->SetRegions(region);
    image->Allocate();
    image->FillBuffer(0.0f);

    itk::ImageRegionIterator<ItkImage2D> it(image, region);
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            it.Set(sanitizedInputValue(sampleValue(frame, x, y, component)));
            ++it;
        }
    }

    return image;
}

int selectedComponentIndex(const RawFrame &frame, int channelIndex)
{
    if (channelIndex < 0 || !frame.isValid()) {
        return -1;
    }

    if (frame.components == 1) {
        return 0;
    }

    return channelIndex < frame.components ? channelIndex : -1;
}

ChannelRenderSettings selectedRenderSettings(const QVector<ChannelRenderSettings> &settings, int channelIndex)
{
    ChannelRenderSettings selected;
    if (channelIndex >= 0 && channelIndex < settings.size()) {
        selected = settings.at(channelIndex);
    }
    selected.enabled = true;
    return selected;
}

QVector<ChannelRenderSettings> singleChannelRenderSettings(const QVector<ChannelRenderSettings> &settings,
                                                           int channelIndex,
                                                           double low,
                                                           double high)
{
    ChannelRenderSettings selected = selectedRenderSettings(settings, channelIndex);
    selected.low = low;
    selected.high = high > low ? high : low + 1.0e-9;
    return {selected};
}

ItkImage2D::Pointer createGaussianPsf(const DeconvolutionSettings &settings)
{
    const int radius = std::clamp(settings.kernelRadiusPixels, 1, 31);
    const double sigma = std::clamp(settings.gaussianSigmaPixels, 0.2, 10.0);
    const int width = radius * 2 + 1;

    ItkImage2D::IndexType start;
    start.Fill(0);

    ItkImage2D::SizeType size;
    size.Fill(static_cast<itk::SizeValueType>(width));

    ItkImage2D::RegionType region;
    region.SetIndex(start);
    region.SetSize(size);

    ItkImage2D::Pointer psf = ItkImage2D::New();
    psf->SetRegions(region);
    psf->Allocate();
    psf->FillBuffer(0.0f);

    double sum = 0.0;
    itk::ImageRegionIterator<ItkImage2D> it(psf, region);
    while (!it.IsAtEnd()) {
        const ItkImage2D::IndexType index = it.GetIndex();
        const double dx = static_cast<double>(index[0] - radius);
        const double dy = static_cast<double>(index[1] - radius);
        const double value = std::exp(-(dx * dx + dy * dy) / (2.0 * sigma * sigma));
        it.Set(static_cast<float>(value));
        sum += value;
        ++it;
    }

    if (sum > 0.0) {
        itk::ImageRegionIterator<ItkImage2D> normalizeIt(psf, region);
        while (!normalizeIt.IsAtEnd()) {
            normalizeIt.Set(static_cast<float>(normalizeIt.Get() / sum));
            ++normalizeIt;
        }
    }

    return psf;
}

RawFrame croppedRawFrame(const RawFrame &frame, const QRect &cropRect)
{
    RawFrame crop;

    const QRect bounded = cropRect.intersected(QRect(0, 0, frame.width, frame.height));
    if (!frame.isValid() || !bounded.isValid() || bounded.isEmpty()) {
        return crop;
    }

    crop.sequenceIndex = frame.sequenceIndex;
    crop.width = bounded.width();
    crop.height = bounded.height();
    crop.bitsPerComponent = frame.bitsPerComponent;
    crop.components = frame.components;
    crop.pixelDataType = frame.pixelDataType;
    crop.bytesPerLine = static_cast<qsizetype>(crop.width) * crop.components * crop.bytesPerComponent();
    crop.data.resize(crop.bytesPerLine * crop.height);

    const qsizetype sourcePixelOffset = static_cast<qsizetype>(bounded.x()) * frame.components * frame.bytesPerComponent();
    const qsizetype copiedRowBytes = crop.bytesPerLine;
    for (int y = 0; y < crop.height; ++y) {
        const char *sourceRow = frame.data.constData() + static_cast<qsizetype>(bounded.y() + y) * frame.bytesPerLine + sourcePixelOffset;
        char *targetRow = crop.data.data() + static_cast<qsizetype>(y) * crop.bytesPerLine;
        std::memcpy(targetRow, sourceRow, static_cast<size_t>(copiedRowBytes));
    }

    return crop;
}

RawFrame createFloatSingleChannelFrame(int sequenceIndex, int width, int height)
{
    RawFrame frame;
    frame.sequenceIndex = sequenceIndex;
    frame.width = width;
    frame.height = height;
    frame.bitsPerComponent = 32;
    frame.components = 1;
    frame.pixelDataType = QStringLiteral("float");
    frame.bytesPerLine = static_cast<qsizetype>(width) * frame.bytesPerComponent();
    frame.data.resize(frame.bytesPerLine * height);
    frame.data.fill('\0');
    return frame;
}

ItkImage2D::Pointer deconvolveComponent(const RawFrame &frame, int component, const ItkImage2D *psf, int iterations)
{
    using Filter = itk::RichardsonLucyDeconvolutionImageFilter<ItkImage2D, ItkImage2D, ItkImage2D>;

    Filter::Pointer filter = Filter::New();
    filter->SetInput(createImageFromRawComponent(frame, component));
    filter->SetKernelImage(psf);
    filter->SetNumberOfIterations(static_cast<unsigned int>(std::clamp(iterations, 1, 100)));
    filter->Update();

    ItkImage2D::Pointer output = filter->GetOutput();
    output->DisconnectPipeline();
    return output;
}

void copyComponentToFloatRawFrame(const ItkImage2D *image, RawFrame &frame, int component)
{
    const ItkImage2D::RegionType region = image->GetLargestPossibleRegion();
    itk::ImageRegionConstIterator<ItkImage2D> it(image, region);

    const int bytesPerComponent = frame.bytesPerComponent();
    for (int y = 0; y < frame.height; ++y) {
        char *row = frame.data.data() + static_cast<qsizetype>(y) * frame.bytesPerLine;
        for (int x = 0; x < frame.width; ++x) {
            const float value = sanitizedOutputValue(it.Get());
            char *pixel = row + static_cast<qsizetype>((x * frame.components + component) * bytesPerComponent);
            std::memcpy(pixel, &value, sizeof(value));
            ++it;
        }
    }
}

void copyComponentToFloatRawFrame(const std::vector<float> &values, RawFrame &frame)
{
    if (values.size() < static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height)) {
        return;
    }

    for (int y = 0; y < frame.height; ++y) {
        char *row = frame.data.data() + static_cast<qsizetype>(y) * frame.bytesPerLine;
        for (int x = 0; x < frame.width; ++x) {
            const float value = sanitizedOutputValue(values.at(static_cast<size_t>(y) * frame.width + x));
            std::memcpy(row + static_cast<qsizetype>(x) * frame.bytesPerComponent(), &value, sizeof(value));
        }
    }
}

int nextMultipleOfFour(int value)
{
    return ((value + 3) / 4) * 4;
}

std::vector<float> normalizedComponentTensor(const RawFrame &frame, int component, int paddedWidth, int paddedHeight)
{
    std::vector<float> values(static_cast<size_t>(paddedWidth) * paddedHeight, 0.0f);
    std::vector<float> source(static_cast<size_t>(frame.width) * frame.height, 0.0f);

    double minimum = std::numeric_limits<double>::max();
    double maximum = std::numeric_limits<double>::lowest();
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            double value = sampleValue(frame, x, y, component);
            if (!std::isfinite(value)) {
                value = 0.0;
            }
            source[static_cast<size_t>(y) * frame.width + x] = static_cast<float>(value);
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
    }

    const double range = maximum - minimum;
    for (int y = 0; y < paddedHeight; ++y) {
        const int sourceY = std::min(y, frame.height - 1);
        for (int x = 0; x < paddedWidth; ++x) {
            const int sourceX = std::min(x, frame.width - 1);
            double normalized = 0.0;
            if (range > 0.0 && std::isfinite(range)) {
                const float rawValue = source[static_cast<size_t>(sourceY) * frame.width + sourceX];
                normalized = (static_cast<double>(rawValue) - minimum) / range;
            }
            values[static_cast<size_t>(y) * paddedWidth + x] = static_cast<float>(std::clamp(normalized, 0.0, 1.0));
        }
    }

    return values;
}

size_t tensorElementCount(const std::vector<int64_t> &shape)
{
    for (const int64_t dimension : shape) {
        if (dimension <= 0) {
            throw std::runtime_error("DeBCR ONNX output tensor has an invalid dimension.");
        }
    }

    return static_cast<size_t>(
        std::accumulate(shape.begin(), shape.end(), int64_t{1}, std::multiplies<int64_t>()));
}

std::vector<float> runDebcrOnnx(const QString &modelPath,
                                const std::vector<float> &input,
                                int paddedWidth,
                                int paddedHeight,
                                int outputWidth,
                                int outputHeight)
{
    if (!QFileInfo::exists(modelPath)) {
        throw std::runtime_error(QStringLiteral("DeBCR ONNX model was not found at '%1'.").arg(modelPath).toStdString());
    }

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "nd2-viewer-debcr");
    Ort::SessionOptions sessionOptions;
    sessionOptions.SetIntraOpNumThreads(1);
    sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

    const auto ortModelPath = toOrtPath(modelPath);
    Ort::Session session(env, ortModelPath.c_str(), sessionOptions);

    std::vector<int64_t> inputShape{1, 1, paddedHeight, paddedWidth};
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(memoryInfo,
                                                             const_cast<float *>(input.data()),
                                                             input.size(),
                                                             inputShape.data(),
                                                             inputShape.size());

    const char *inputNames[] = {"x0"};
    const char *outputNames[] = {"pred"};
    auto outputs = session.Run(Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);
    if (outputs.empty() || !outputs.front().IsTensor()) {
        throw std::runtime_error("DeBCR ONNX model did not return a tensor output named 'pred'.");
    }

    const float *outputData = outputs.front().GetTensorData<float>();
    const std::vector<int64_t> outputShape = outputs.front().GetTensorTypeAndShapeInfo().GetShape();
    if (outputShape.size() < 2) {
        throw std::runtime_error("DeBCR ONNX output tensor has an invalid shape.");
    }

    const int64_t tensorHeight = outputShape.at(outputShape.size() - 2);
    const int64_t tensorWidth = outputShape.at(outputShape.size() - 1);
    if (tensorWidth < outputWidth || tensorHeight < outputHeight) {
        throw std::runtime_error("DeBCR ONNX output tensor is smaller than the requested image.");
    }

    const size_t outputCount = tensorElementCount(outputShape);
    const size_t minimumCount = static_cast<size_t>(tensorWidth) * static_cast<size_t>(tensorHeight);
    if (outputCount < minimumCount) {
        throw std::runtime_error("DeBCR ONNX output tensor has inconsistent shape metadata.");
    }

    std::vector<float> cropped(static_cast<size_t>(outputWidth) * outputHeight, 0.0f);
    for (int y = 0; y < outputHeight; ++y) {
        for (int x = 0; x < outputWidth; ++x) {
            cropped[static_cast<size_t>(y) * outputWidth + x] =
                sanitizedOutputValue(outputData[static_cast<size_t>(y) * static_cast<size_t>(tensorWidth) + x]);
        }
    }

    return cropped;
}
} // namespace

QString DeconvolutionProcessor::defaultDebcrModelPath()
{
    const QString modelFileName = QStringLiteral("debcr_LM_2D_CARE.onnx");
#ifdef Q_OS_MACOS
    const QString appDirPath = QCoreApplication::applicationDirPath();
    const QDir appDir(appDirPath);
    const QString bundleResourcePath = appDir.absoluteFilePath(QStringLiteral("../Resources/models/%1").arg(modelFileName));
    if (QFileInfo::exists(bundleResourcePath)) {
        return QFileInfo(bundleResourcePath).absoluteFilePath();
    }
#endif
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QFileInfo(appDir.absoluteFilePath(QStringLiteral("models/%1").arg(modelFileName))).absoluteFilePath();
}

DeconvolutionResult DeconvolutionProcessor::run2D(const RawFrame &frame,
                                                  const FrameCoordinateState &coordinates,
                                                  const QVector<ChannelRenderSettings> &channelSettings,
                                                  const DeconvolutionSettings &settings)
{
    DeconvolutionResult result;

    if (!frame.isValid()) {
        result.errorMessage = QObject::tr("No valid raw 2D frame is available for deconvolution.");
        return result;
    }

    if (channelSettings.isEmpty()) {
        result.errorMessage = QObject::tr("No channel settings are available for rendering the deconvolution result.");
        return result;
    }

    if (settings.channelIndex < 0 || settings.channelIndex >= channelSettings.size()) {
        result.errorMessage = QObject::tr("The selected channel is not available.");
        return result;
    }

    const int selectedComponent = selectedComponentIndex(frame, settings.channelIndex);
    if (selectedComponent < 0) {
        result.errorMessage = QObject::tr("The selected channel is not present in the current raw frame.");
        return result;
    }

    try {
        const QRect frameRect(0, 0, frame.width, frame.height);
        QRect roiRect = settings.roiRect.intersected(frameRect);
        if (settings.useRoi && (!roiRect.isValid() || roiRect.isEmpty())) {
            result.errorMessage = QObject::tr("The selected ROI is outside the current frame.");
            return result;
        }

        const int cropMargin =
            settings.useRoi && settings.method == DeconvolutionMethod::Classical ? std::clamp(settings.kernelRadiusPixels, 1, 31) : 0;
        const QRect expandedRoiRect = settings.useRoi
                                          ? roiRect.adjusted(-cropMargin, -cropMargin, cropMargin, cropMargin).intersected(frameRect)
                                          : frameRect;
        const RawFrame sourceFrame = settings.useRoi ? croppedRawFrame(frame, expandedRoiRect) : frame;
        if (!sourceFrame.isValid()) {
            result.errorMessage = settings.useRoi
                                      ? QObject::tr("The selected ROI could not be cropped from the current frame.")
                                      : QObject::tr("No valid raw 2D frame is available for deconvolution.");
            return result;
        }

        RawFrame deconvolvedFrame = createFloatSingleChannelFrame(sourceFrame.sequenceIndex, sourceFrame.width, sourceFrame.height);
        QVector<ChannelRenderSettings> renderSettings;

        if (settings.method == DeconvolutionMethod::DeepLearning) {
            const int paddedWidth = nextMultipleOfFour(sourceFrame.width);
            const int paddedHeight = nextMultipleOfFour(sourceFrame.height);
            const std::vector<float> input = normalizedComponentTensor(sourceFrame, selectedComponent, paddedWidth, paddedHeight);
            const std::vector<float> output = runDebcrOnnx(settings.modelPath,
                                                           input,
                                                           paddedWidth,
                                                           paddedHeight,
                                                           sourceFrame.width,
                                                           sourceFrame.height);
            copyComponentToFloatRawFrame(output, deconvolvedFrame);
            renderSettings = singleChannelRenderSettings(channelSettings, settings.channelIndex, 0.0, 1.0);
        } else {
            const ItkImage2D::Pointer psf = createGaussianPsf(settings);
            const ItkImage2D::Pointer output = deconvolveComponent(sourceFrame, selectedComponent, psf.GetPointer(), settings.iterations);
            copyComponentToFloatRawFrame(output.GetPointer(), deconvolvedFrame, 0);
            const ChannelRenderSettings selected = selectedRenderSettings(channelSettings, settings.channelIndex);
            renderSettings = singleChannelRenderSettings(channelSettings, settings.channelIndex, selected.low, selected.high);
        }

        result.image = FrameRenderer::render(deconvolvedFrame, coordinates, renderSettings).image;
        if (settings.useRoi && !result.image.isNull()) {
            const QRect trimRect(roiRect.x() - expandedRoiRect.x(),
                                 roiRect.y() - expandedRoiRect.y(),
                                 roiRect.width(),
                                 roiRect.height());
            result.image = result.image.copy(trimRect);
        }
        if (result.image.isNull()) {
            result.errorMessage = QObject::tr("The deconvolved frame could not be rendered.");
            return result;
        }

        result.success = true;
        return result;
    } catch (const itk::ExceptionObject &ex) {
        result.errorMessage = QObject::tr("ITK deconvolution failed: %1").arg(QString::fromUtf8(ex.GetDescription()));
    } catch (const Ort::Exception &ex) {
        result.errorMessage = QObject::tr("DeBCR ONNX inference failed: %1").arg(QString::fromUtf8(ex.what()));
    } catch (const std::exception &ex) {
        result.errorMessage = QObject::tr("Deconvolution failed: %1").arg(QString::fromUtf8(ex.what()));
    } catch (...) {
        result.errorMessage = QObject::tr("Deconvolution failed with an unknown error.");
    }

    return result;
}
