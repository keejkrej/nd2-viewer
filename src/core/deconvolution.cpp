#include "core/deconvolution.h"

#include "core/framerenderer.h"

#include <QObject>

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

namespace
{
using ItkImage2D = itk::Image<float, 2>;

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

bool channelEnabledForComponent(const RawFrame &frame, const QVector<ChannelRenderSettings> &settings, int component)
{
    if (frame.components == 1) {
        for (const ChannelRenderSettings &channel : settings) {
            if (channel.enabled) {
                return true;
            }
        }
        return false;
    }

    return component >= 0 && component < settings.size() && settings.at(component).enabled;
}
} // namespace

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

    try {
        RawFrame deconvolvedFrame;
        deconvolvedFrame.sequenceIndex = frame.sequenceIndex;
        deconvolvedFrame.width = frame.width;
        deconvolvedFrame.height = frame.height;
        deconvolvedFrame.bitsPerComponent = 32;
        deconvolvedFrame.components = frame.components;
        deconvolvedFrame.pixelDataType = QStringLiteral("float");
        deconvolvedFrame.bytesPerLine = static_cast<qsizetype>(frame.width) * frame.components * deconvolvedFrame.bytesPerComponent();
        deconvolvedFrame.data.resize(deconvolvedFrame.bytesPerLine * frame.height);
        deconvolvedFrame.data.fill('\0');

        const ItkImage2D::Pointer psf = createGaussianPsf(settings);
        for (int component = 0; component < frame.components; ++component) {
            if (!channelEnabledForComponent(frame, channelSettings, component)) {
                continue;
            }

            const ItkImage2D::Pointer output = deconvolveComponent(frame, component, psf.GetPointer(), settings.iterations);
            copyComponentToFloatRawFrame(output.GetPointer(), deconvolvedFrame, component);
        }

        result.image = FrameRenderer::render(deconvolvedFrame, coordinates, channelSettings).image;
        if (result.image.isNull()) {
            result.errorMessage = QObject::tr("The deconvolved frame could not be rendered.");
            return result;
        }

        result.success = true;
        return result;
    } catch (const itk::ExceptionObject &ex) {
        result.errorMessage = QObject::tr("ITK deconvolution failed: %1").arg(QString::fromUtf8(ex.GetDescription()));
    } catch (const std::exception &ex) {
        result.errorMessage = QObject::tr("Deconvolution failed: %1").arg(QString::fromUtf8(ex.what()));
    } catch (...) {
        result.errorMessage = QObject::tr("Deconvolution failed with an unknown error.");
    }

    return result;
}
