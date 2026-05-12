#include "core/segmentation.h"

#include <QObject>

#include <itkConnectedComponentImageFilter.h>
#include <itkImage.h>
#include <itkImageRegionConstIterator.h>
#include <itkImageRegionIterator.h>
#include <itkLiThresholdImageFilter.h>
#include <itkOtsuThresholdImageFilter.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>

namespace
{
using FloatImage2D = itk::Image<float, 2>;
using FloatImage3D = itk::Image<float, 3>;
using MaskImage2D = itk::Image<unsigned char, 2>;
using MaskImage3D = itk::Image<unsigned char, 3>;
using LabelImage2D = itk::Image<quint32, 2>;
using LabelImage3D = itk::Image<quint32, 3>;

int effectiveFrameComponentIndex(const RawFrame &frame, int channelIndex)
{
    if (channelIndex < 0 || !frame.isValid()) {
        return -1;
    }
    if (frame.components == 1) {
        return 0;
    }
    return channelIndex < frame.components ? channelIndex : -1;
}

int effectiveVolumeChannelIndex(const RawVolume &volume, int channelIndex)
{
    if (channelIndex < 0 || !volume.isValid()) {
        return -1;
    }
    if (volume.components == 1) {
        return 0;
    }
    return channelIndex < volume.components ? channelIndex : -1;
}

double sampleFrameValue(const RawFrame &frame, int x, int y, int component)
{
    const int bytesPerComponent = frame.bytesPerComponent();
    const qsizetype rowOffset = static_cast<qsizetype>(y) * frame.bytesPerLine;
    const qsizetype pixelOffset = static_cast<qsizetype>((x * frame.components + component) * bytesPerComponent);
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

double sampleVolumeValue(const RawVolume &volume, qsizetype voxelIndex, int channelIndex)
{
    const int bytesPerComponent = volume.bytesPerComponent();
    const char *pixel = volume.channelData.at(channelIndex).constData() + voxelIndex * bytesPerComponent;

    switch (bytesPerComponent) {
    case 1:
        return static_cast<unsigned char>(pixel[0]);
    case 2: {
        quint16 value = 0;
        std::memcpy(&value, pixel, sizeof(value));
        return value;
    }
    case 4: {
        if (volume.pixelDataType.compare(QStringLiteral("float"), Qt::CaseInsensitive) == 0) {
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

float sanitizedFloat(double value)
{
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    return static_cast<float>(std::clamp(value, 0.0, static_cast<double>(std::numeric_limits<float>::max())));
}

template<typename TImage>
typename TImage::Pointer allocateImage(const typename TImage::SizeType &size)
{
    typename TImage::IndexType start;
    start.Fill(0);

    typename TImage::RegionType region;
    region.SetIndex(start);
    region.SetSize(size);

    typename TImage::Pointer image = TImage::New();
    image->SetRegions(region);
    image->Allocate();
    image->FillBuffer(0);
    return image;
}

FloatImage2D::Pointer createFloatImage2D(const RawFrame &frame,
                                         int component,
                                         double *minimumValue,
                                         double *maximumValue)
{
    FloatImage2D::SizeType size;
    size[0] = static_cast<itk::SizeValueType>(frame.width);
    size[1] = static_cast<itk::SizeValueType>(frame.height);
    FloatImage2D::Pointer image = allocateImage<FloatImage2D>(size);

    double minimum = std::numeric_limits<double>::max();
    double maximum = std::numeric_limits<double>::lowest();
    itk::ImageRegionIterator<FloatImage2D> it(image, image->GetLargestPossibleRegion());
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            const double value = sampleFrameValue(frame, x, y, component);
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
            it.Set(sanitizedFloat(value));
            ++it;
        }
    }

    *minimumValue = minimum;
    *maximumValue = maximum;
    return image;
}

FloatImage3D::Pointer createFloatImage3D(const RawVolume &volume,
                                         int channelIndex,
                                         double *minimumValue,
                                         double *maximumValue)
{
    FloatImage3D::SizeType size;
    size[0] = static_cast<itk::SizeValueType>(volume.width);
    size[1] = static_cast<itk::SizeValueType>(volume.height);
    size[2] = static_cast<itk::SizeValueType>(volume.depth);
    FloatImage3D::Pointer image = allocateImage<FloatImage3D>(size);
    FloatImage3D::SpacingType spacing;
    spacing[0] = volume.voxelSpacing.x();
    spacing[1] = volume.voxelSpacing.y();
    spacing[2] = volume.voxelSpacing.z();
    image->SetSpacing(spacing);

    double minimum = std::numeric_limits<double>::max();
    double maximum = std::numeric_limits<double>::lowest();
    itk::ImageRegionIterator<FloatImage3D> it(image, image->GetLargestPossibleRegion());
    const qsizetype voxelCount = volume.voxelCount();
    for (qsizetype index = 0; index < voxelCount; ++index) {
        const double value = sampleVolumeValue(volume, index, channelIndex);
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
        it.Set(sanitizedFloat(value));
        ++it;
    }

    *minimumValue = minimum;
    *maximumValue = maximum;
    return image;
}

template<typename TInputImage, typename TOutputImage>
double calculateItkThreshold(const TInputImage *image, SegmentationThresholdMethod method)
{
    if (method == SegmentationThresholdMethod::Li) {
        using Filter = itk::LiThresholdImageFilter<TInputImage, TOutputImage>;
        typename Filter::Pointer filter = Filter::New();
        filter->SetInput(image);
        filter->SetInsideValue(0);
        filter->SetOutsideValue(1);
        filter->Update();
        return filter->GetThreshold();
    }

    using Filter = itk::OtsuThresholdImageFilter<TInputImage, TOutputImage>;
    typename Filter::Pointer filter = Filter::New();
    filter->SetInput(image);
    filter->SetInsideValue(0);
    filter->SetOutsideValue(1);
    filter->Update();
    return filter->GetThreshold();
}

MaskImage2D::Pointer createMaskImage(const SegmentationMask2D &mask)
{
    MaskImage2D::SizeType size;
    size[0] = static_cast<itk::SizeValueType>(mask.width);
    size[1] = static_cast<itk::SizeValueType>(mask.height);
    MaskImage2D::Pointer image = allocateImage<MaskImage2D>(size);

    itk::ImageRegionIterator<MaskImage2D> it(image, image->GetLargestPossibleRegion());
    const auto *source = reinterpret_cast<const unsigned char *>(mask.data.constData());
    for (qsizetype index = 0; index < mask.data.size(); ++index) {
        it.Set(source[index] != 0 ? 1 : 0);
        ++it;
    }
    return image;
}

MaskImage3D::Pointer createMaskImage(const SegmentationMask3D &mask)
{
    MaskImage3D::SizeType size;
    size[0] = static_cast<itk::SizeValueType>(mask.width);
    size[1] = static_cast<itk::SizeValueType>(mask.height);
    size[2] = static_cast<itk::SizeValueType>(mask.depth);
    MaskImage3D::Pointer image = allocateImage<MaskImage3D>(size);
    MaskImage3D::SpacingType spacing;
    spacing[0] = mask.voxelSpacing.x();
    spacing[1] = mask.voxelSpacing.y();
    spacing[2] = mask.voxelSpacing.z();
    image->SetSpacing(spacing);

    itk::ImageRegionIterator<MaskImage3D> it(image, image->GetLargestPossibleRegion());
    const auto *source = reinterpret_cast<const unsigned char *>(mask.data.constData());
    for (qsizetype index = 0; index < mask.data.size(); ++index) {
        it.Set(source[index] != 0 ? 1 : 0);
        ++it;
    }
    return image;
}

template<typename TLabelImage>
quint32 copyLabels(const TLabelImage *image, QVector<quint32> *labels)
{
    const typename TLabelImage::RegionType region = image->GetLargestPossibleRegion();
    itk::ImageRegionConstIterator<TLabelImage> it(image, region);
    quint32 maximumLabel = 0;
    for (qsizetype index = 0; index < labels->size() && !it.IsAtEnd(); ++index, ++it) {
        const quint32 label = it.Get();
        (*labels)[index] = label;
        maximumLabel = std::max(maximumLabel, label);
    }
    return maximumLabel;
}
} // namespace

SegmentationThresholdResult SegmentationProcessor::calculateThreshold2D(const RawFrame &frame,
                                                                        int channelIndex,
                                                                        SegmentationThresholdMethod method)
{
    SegmentationThresholdResult result;
    if (!frame.isValid()) {
        result.errorMessage = QObject::tr("No valid raw 2D frame is available for segmentation.");
        return result;
    }

    const int component = effectiveFrameComponentIndex(frame, channelIndex);
    if (component < 0) {
        result.errorMessage = QObject::tr("The selected channel is not present in the current raw frame.");
        return result;
    }

    try {
        double minimum = 0.0;
        double maximum = 0.0;
        const FloatImage2D::Pointer image = createFloatImage2D(frame, component, &minimum, &maximum);
        result.threshold = calculateItkThreshold<FloatImage2D, MaskImage2D>(image.GetPointer(), method);
        result.minimumValue = minimum;
        result.maximumValue = maximum;
        result.success = true;
    } catch (const itk::ExceptionObject &ex) {
        result.errorMessage = QObject::tr("ITK thresholding failed: %1").arg(QString::fromUtf8(ex.GetDescription()));
    } catch (const std::exception &ex) {
        result.errorMessage = QObject::tr("Thresholding failed: %1").arg(QString::fromUtf8(ex.what()));
    } catch (...) {
        result.errorMessage = QObject::tr("Thresholding failed with an unknown error.");
    }
    return result;
}

SegmentationThresholdResult SegmentationProcessor::calculateThreshold3D(const RawVolume &volume,
                                                                        int channelIndex,
                                                                        SegmentationThresholdMethod method)
{
    SegmentationThresholdResult result;
    if (!volume.isValid()) {
        result.errorMessage = QObject::tr("No valid 3D volume is available for segmentation.");
        return result;
    }

    const int dataIndex = effectiveVolumeChannelIndex(volume, channelIndex);
    if (dataIndex < 0 || dataIndex >= volume.channelData.size()) {
        result.errorMessage = QObject::tr("The selected channel is not present in the cached volume.");
        return result;
    }

    try {
        double minimum = 0.0;
        double maximum = 0.0;
        const FloatImage3D::Pointer image = createFloatImage3D(volume, dataIndex, &minimum, &maximum);
        result.threshold = calculateItkThreshold<FloatImage3D, MaskImage3D>(image.GetPointer(), method);
        result.minimumValue = minimum;
        result.maximumValue = maximum;
        result.success = true;
    } catch (const itk::ExceptionObject &ex) {
        result.errorMessage = QObject::tr("ITK thresholding failed: %1").arg(QString::fromUtf8(ex.GetDescription()));
    } catch (const std::exception &ex) {
        result.errorMessage = QObject::tr("Thresholding failed: %1").arg(QString::fromUtf8(ex.what()));
    } catch (...) {
        result.errorMessage = QObject::tr("Thresholding failed with an unknown error.");
    }
    return result;
}

SegmentationMask2D SegmentationProcessor::binarize2D(const RawFrame &frame, int channelIndex, double threshold)
{
    SegmentationMask2D mask;
    if (!frame.isValid()) {
        return mask;
    }

    const int component = effectiveFrameComponentIndex(frame, channelIndex);
    if (component < 0) {
        return mask;
    }

    mask.width = frame.width;
    mask.height = frame.height;
    mask.data.resize(static_cast<qsizetype>(frame.width) * frame.height);
    auto *target = reinterpret_cast<unsigned char *>(mask.data.data());
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            target[static_cast<qsizetype>(y) * frame.width + x] =
                sampleFrameValue(frame, x, y, component) >= threshold ? 1 : 0;
        }
    }
    return mask;
}

SegmentationMask3D SegmentationProcessor::binarize3D(const RawVolume &volume, int channelIndex, double threshold)
{
    SegmentationMask3D mask;
    if (!volume.isValid()) {
        return mask;
    }

    const int dataIndex = effectiveVolumeChannelIndex(volume, channelIndex);
    if (dataIndex < 0 || dataIndex >= volume.channelData.size()) {
        return mask;
    }

    mask.width = volume.width;
    mask.height = volume.height;
    mask.depth = volume.depth;
    mask.voxelSpacing = volume.voxelSpacing;
    mask.data.resize(volume.voxelCount());
    auto *target = reinterpret_cast<unsigned char *>(mask.data.data());
    for (qsizetype index = 0; index < volume.voxelCount(); ++index) {
        target[index] = sampleVolumeValue(volume, index, dataIndex) >= threshold ? 1 : 0;
    }
    return mask;
}

SegmentationLabels2D SegmentationProcessor::labelConnectedComponents(const SegmentationMask2D &mask)
{
    SegmentationLabels2D result;
    result.width = mask.width;
    result.height = mask.height;
    if (!mask.isValid()) {
        result.errorMessage = QObject::tr("No valid binary mask is available for connected-component labeling.");
        return result;
    }

    try {
        using Filter = itk::ConnectedComponentImageFilter<MaskImage2D, LabelImage2D>;
        Filter::Pointer filter = Filter::New();
        filter->SetInput(createMaskImage(mask));
        filter->SetFullyConnected(false);
        filter->Update();

        result.labels.resize(mask.width * mask.height);
        result.componentCount = copyLabels(filter->GetOutput(), &result.labels);
        result.success = true;
    } catch (const itk::ExceptionObject &ex) {
        result.errorMessage = QObject::tr("ITK connected-component labeling failed: %1").arg(QString::fromUtf8(ex.GetDescription()));
    } catch (const std::exception &ex) {
        result.errorMessage = QObject::tr("Connected-component labeling failed: %1").arg(QString::fromUtf8(ex.what()));
    } catch (...) {
        result.errorMessage = QObject::tr("Connected-component labeling failed with an unknown error.");
    }
    return result;
}

SegmentationLabels3D SegmentationProcessor::labelConnectedComponents(const SegmentationMask3D &mask)
{
    SegmentationLabels3D result;
    result.width = mask.width;
    result.height = mask.height;
    result.depth = mask.depth;
    result.voxelSpacing = mask.voxelSpacing;
    if (!mask.isValid()) {
        result.errorMessage = QObject::tr("No valid 3D binary mask is available for connected-component labeling.");
        return result;
    }

    try {
        using Filter = itk::ConnectedComponentImageFilter<MaskImage3D, LabelImage3D>;
        Filter::Pointer filter = Filter::New();
        filter->SetInput(createMaskImage(mask));
        filter->SetFullyConnected(false);
        filter->Update();

        result.labels.resize(mask.voxelCount());
        result.componentCount = copyLabels(filter->GetOutput(), &result.labels);
        result.success = true;
    } catch (const itk::ExceptionObject &ex) {
        result.errorMessage = QObject::tr("ITK connected-component labeling failed: %1").arg(QString::fromUtf8(ex.GetDescription()));
    } catch (const std::exception &ex) {
        result.errorMessage = QObject::tr("Connected-component labeling failed: %1").arg(QString::fromUtf8(ex.what()));
    } catch (...) {
        result.errorMessage = QObject::tr("Connected-component labeling failed with an unknown error.");
    }
    return result;
}
