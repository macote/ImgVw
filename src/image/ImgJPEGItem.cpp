#include "ImgJPEGItem.h"
#include "ImgItemHelper.h"

#include <array>
#include <cstddef>

namespace
{
struct ScalingFactor
{
    unsigned int numerator;
    unsigned int denominator;
};

constexpr std::array<ScalingFactor, 16> kScalingFactors = {
    ScalingFactor{2, 1}, ScalingFactor{15, 8}, ScalingFactor{7, 4}, ScalingFactor{13, 8},
    ScalingFactor{3, 2}, ScalingFactor{11, 8}, ScalingFactor{5, 4}, ScalingFactor{9, 8},
    ScalingFactor{1, 1}, ScalingFactor{7, 8},  ScalingFactor{3, 4}, ScalingFactor{5, 8},
    ScalingFactor{1, 2}, ScalingFactor{3, 8},  ScalingFactor{1, 4}, ScalingFactor{1, 8}};

int ScaleDimension(int dimension, const ScalingFactor& factor)
{
    return ((dimension * static_cast<int>(factor.numerator)) + static_cast<int>(factor.denominator) - 1) /
           static_cast<int>(factor.denominator);
}

std::size_t GetScalingFactorIndex(int width, int height, int target_width, int target_height)
{
    std::size_t index{};
    while (index < kScalingFactors.size())
    {
        if (ScaleDimension(width, kScalingFactors[index]) <= target_width &&
            ScaleDimension(height, kScalingFactors[index]) <= target_height)
        {
            break;
        }

        ++index;
    }

    return index;
}

int PaddedStride(int width, int component_count)
{
    return ((width * component_count) + 3) & ~3;
}
} // namespace

void ImgJPEGItem::Load()
{
    status_ = Status::Loading;
    PBYTE buffer{nullptr};

    try
    {
        FileMapView jpegfilemap(filepath_, FileMapView::Mode::Read);
        if (jpegfilemap.filesize().HighPart > 0)
        {
            status_ = Status::Error;
            goto done;
        }

        ImgJpegDecoder decoder;
        if (!decoder.Initialize(jpegfilemap.data(), jpegfilemap.filesize().LowPart))
        {
            errorstring_ = decoder.error();
            status_ = Status::Error;
            goto done;
        }

        width_ = decoder.width();
        height_ = decoder.height();
        if (decoder.is_cmyk() && !decoder.icc_profile().empty())
        {
            OpenICCProfile(decoder.icc_profile().data(), static_cast<UINT>(decoder.icc_profile().size()));
        }

        Gdiplus::RotateFlipType rotateflip{Gdiplus::RotateNoneFlipNone};
        if (decoder.exif_data() != nullptr)
        {
            rotateflip = ImgItemHelper::GetRotateFlipTypeFromExifOrientation(
                ImgItemHelper::GetExifOrientationFromData(decoder.exif_data(), static_cast<UINT>(decoder.exif_size())));
        }

        INT targetwidth{}, targetheight{};
        if (rotateflip == Gdiplus::Rotate90FlipNone || rotateflip == Gdiplus::Rotate270FlipNone ||
            rotateflip == Gdiplus::Rotate90FlipX || rotateflip == Gdiplus::Rotate270FlipX)
        {
            targetwidth = targetheight_;
            targetheight = targetwidth_;
        }
        else
        {
            targetwidth = targetwidth_;
            targetheight = targetheight_;
        }

        BOOL resize = FALSE;
        auto scalingfactorindex = GetScalingFactorIndex(width_, height_, targetwidth, targetheight);
        if (scalingfactorindex >= kScalingFactors.size())
        {
            resize = TRUE;
        }
        else
        {
            const auto scaledwidth = ScaleDimension(width_, kScalingFactors[scalingfactorindex]);
            const auto scaledheight = ScaleDimension(height_, kScalingFactors[scalingfactorindex]);
            const auto widthdiff = targetwidth - scaledwidth;
            const auto heightdiff = targetheight - scaledheight;

            if ((widthdiff >= heightdiff && heightdiff > (kResizePercentThreshold * targetheight)) ||
                (heightdiff >= widthdiff && widthdiff > (kResizePercentThreshold * targetwidth)))
            {
                resize = TRUE;
            }
        }

        if (resize && scalingfactorindex > 0)
        {
            --scalingfactorindex;
        }
        else
        {
            resize = FALSE;
        }

        const auto& scalingfactor = kScalingFactors[scalingfactorindex];
        if (!decoder.ConfigureOutput(scalingfactor.numerator, scalingfactor.denominator, decoder.is_cmyk()))
        {
            errorstring_ = decoder.error();
            status_ = Status::Error;
            goto done;
        }

        const auto decompresswidth = decoder.output_width();
        const auto decompressheight = decoder.output_height();
        auto stride = PaddedStride(decompresswidth, decoder.is_cmyk() ? 4 : 3);
        const auto buffersize = static_cast<std::size_t>(stride) * decompressheight;
        buffer = reinterpret_cast<PBYTE>(HeapAlloc(heap_, 0, buffersize));
        if (buffer == nullptr)
        {
            errorstring_ = "Could not allocate JPEG output buffer.";
            status_ = Status::Error;
            goto done;
        }

        if (!decoder.Decode(buffer, stride, true))
        {
            errorstring_ = decoder.error();
            status_ = Status::Error;
            goto done;
        }

        if (decoder.is_cmyk())
        {
            const auto newstride = PaddedStride(decompresswidth, 3);
            if (!TranformCMYK8ColorsToBGR8(decompresswidth, decompressheight, stride, newstride, &buffer))
            {
                status_ = Status::Error;
                goto done;
            }

            stride = newstride;
        }

        if (resize)
        {
            displaybuffer_ = ImgItemHelper::ResizeAndRotate24bppRGBImage(decompresswidth, decompressheight, buffer,
                                                                         targetwidth, targetheight, rotateflip);
        }
        else if (rotateflip != Gdiplus::RotateNoneFlipNone)
        {
            displaybuffer_ = ImgItemHelper::Rotate24bppRGBImage(decompresswidth, decompressheight, buffer, rotateflip);
        }
        else
        {
            displaybuffer_.WriteData(decompresswidth, decompressheight, stride, buffer);
        }
    }
    catch (...)
    {
        status_ = Status::Error;
        goto done;
    }

    SetupDisplayParameters();
    status_ = Status::Ready;

done:
    if (buffer != nullptr)
    {
        HeapFree(heap_, 0, buffer);
    }

    CloseICCProfile();
    SetEvent(loadedevent_);
}
