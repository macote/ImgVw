#include "ImgJPEGItem.h"
#include "ImgItemHelper.h"

void ImgJPEGItem::Load()
{
    status_ = Status::Loading;

    tjhandle jpegdecompressor{ nullptr };
    PBYTE buffer{ nullptr };

    try
    {
        FileMapView jpegfilemap(filepath_, FileMapView::Mode::Read);

        if (jpegfilemap.filesize().HighPart > 0)
        {
            status_ = Status::Error;
            goto done;
        }

        jpegdecompressor = turbojpeg::InitDecompress();
        if (jpegdecompressor == NULL)
        {
            status_ = Status::Error;
            goto done;
        }

        turbojpeg::SaveMarkers(jpegdecompressor, EXIF_MARKER);
        turbojpeg::SaveMarkers(jpegdecompressor, ICC_MARKER);

        INT jpegSubsamp, jpegColorspace;
        if (turbojpeg::DecompressHeader(jpegdecompressor, jpegfilemap.data(), jpegfilemap.filesize().LowPart,
            &width_, &height_, &jpegSubsamp, &jpegColorspace, TJFLAG_NOCLEANUP))
        {
            errorstring_ = turbojpeg::GetErrorStr();
            status_ = Status::Error;
            goto done;
        }

        INT pixelformat = TJPF_BGR;
        if (jpegColorspace == TJCS::TJCS_CMYK || jpegColorspace == TJCS::TJCS_YCCK)
        {
            pixelformat = TJPF_CMYK;
            PBYTE iccprofiledata;
            INT iccprofiledatabytecount;
            if (turbojpeg::ReadICCProfile(jpegdecompressor, &iccprofiledata, &iccprofiledatabytecount))
            {
                OpenICCProfile(iccprofiledata, iccprofiledatabytecount);
                free(iccprofiledata);
            }
        }

        Gdiplus::RotateFlipType rotateflip{ Gdiplus::RotateNoneFlipNone };
        PBYTE exifdata;
        const auto exifdatabytecount = turbojpeg::LocateEXIFSegment(jpegdecompressor, &exifdata);
        if (exifdatabytecount > 0)
        {
            rotateflip = ImgItemHelper::GetRotateFlipTypeFromExifOrientation(
                ImgItemHelper::GetExifOrientationFromData(exifdata, exifdatabytecount));
        }

        turbojpeg::AbortDecompress(jpegdecompressor);

        INT targetwidth{}, targetheight{};
        if (rotateflip == Gdiplus::Rotate90FlipNone || rotateflip == Gdiplus::Rotate270FlipNone
            || rotateflip == Gdiplus::Rotate90FlipX || rotateflip == Gdiplus::Rotate270FlipX)
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
        INT decompresswidth{}, decompressheight{};
        const auto scalingfactorindex = GetScalingFactorIndex(width_, height_, targetwidth, targetheight);
        if (scalingfactorindex >= scalingfactorcount_)
        {
            resize = TRUE;
        }
        else
        {
            decompresswidth = TJSCALED(width_, scalingfactors_[scalingfactorindex]);
            decompressheight = TJSCALED(height_, scalingfactors_[scalingfactorindex]);
            const auto widthdiff = targetwidth - decompresswidth;
            const auto heightdiff = targetheight - decompressheight;

            if ((widthdiff >= heightdiff && heightdiff > (kResizePercentThreshold * targetheight))
                || (heightdiff >= widthdiff && widthdiff > (kResizePercentThreshold * targetwidth)))
            {
                resize = TRUE;
            }
        }

        if (resize)
        {
            decompresswidth = TJSCALED(width_, scalingfactors_[scalingfactorindex - 1]);
            decompressheight = TJSCALED(height_, scalingfactors_[scalingfactorindex - 1]);
        }

        auto stride = TJPAD(decompresswidth * tjPixelSize[pixelformat]);
        const auto buffersize = stride * decompressheight;
        buffer = reinterpret_cast<PBYTE>(HeapAlloc(heap_, 0, buffersize));

        INT decompressflags{ TJFLAG_FASTDCT };
        if (!resize)
        {
            decompressflags |= TJFLAG_BOTTOMUP;
        }

        turbojpeg::SkipMarkers(jpegdecompressor, EXIF_MARKER);
        turbojpeg::SkipMarkers(jpegdecompressor, ICC_MARKER);

        if (turbojpeg::Decompress(jpegdecompressor, jpegfilemap.data(), jpegfilemap.filesize().LowPart, buffer,
            decompresswidth, stride, decompressheight, pixelformat, decompressflags))
        {
            errorstring_ = turbojpeg::GetErrorStr();
            status_ = Status::Error;
            goto done;
        }

        if (pixelformat == TJPF_CMYK)
        {
            const auto newstride = TJPAD(decompresswidth * tjPixelSize[TJPF_BGR]);
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

    if (jpegdecompressor != nullptr)
    {
        turbojpeg::Destroy(jpegdecompressor);
    }

    if (buffer != nullptr)
    {
        HeapFree(heap_, 0, buffer);
    }

    CloseICCProfile();

    SetEvent(loadedevent_);
}

INT ImgJPEGItem::GetScalingFactorIndex(INT width, INT height, INT targetwidth, INT targetheight) const
{
    INT i{}, scaledwidth{}, scaledheight{};

    while (i < scalingfactorcount_)
    {
        scaledwidth = TJSCALED(width, scalingfactors_[i]);
        scaledheight = TJSCALED(height, scalingfactors_[i]);
        if (scaledwidth <= targetwidth && scaledheight <= targetheight)
        {
            break;
        }

        ++i;
    }

    return i;
}