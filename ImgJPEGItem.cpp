#include "ImgJPEGItem.h"
#include "ImgBitmap.h"
#include "ImgItemHelper.h"
#include <memory>

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
        if (turbojpeg::DecompressHeader(jpegdecompressor, jpegfilemap.view(), jpegfilemap.filesize().LowPart,
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
        INT exifdatabytecount = turbojpeg::LocateEXIFSegment(jpegdecompressor, &exifdata);
        if (exifdatabytecount > 0)
        {
            rotateflip = ImgItemHelper::GetRotateFlipTypeFromExifOrientation(
                ImgItemHelper::GetExifOrientationFromData(exifdata, exifdatabytecount));
        }

        turbojpeg::AbortDecompress(jpegdecompressor);

        INT targetwidth, targetheight;
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
        if (width_ > targetwidth || height_ > targetheight)
        {
            auto scalingfactorindex = GetScalingFactorIndex(width_, height_, targetwidth, targetheight);
            if (scalingfactorindex >= scalingfactorcount_)
            {
                resize = TRUE;
            }
            else
            {
                displaywidth_ = TJSCALED(width_, scalingfactors_[scalingfactorindex]);
                displayheight_ = TJSCALED(height_, scalingfactors_[scalingfactorindex]);
                auto widthdiff = targetwidth - displaywidth_;
                auto heightdiff = targetheight - displayheight_;

                if ((widthdiff >= heightdiff && heightdiff > (kResizePercentThreshold * targetheight))
                    || (heightdiff >= widthdiff && widthdiff > (kResizePercentThreshold * targetwidth)))
                {
                    resize = TRUE;
                }
            }

            if (resize)
            {
                displaywidth_ = TJSCALED(width_, scalingfactors_[scalingfactorindex - 1]);
                displayheight_ = TJSCALED(height_, scalingfactors_[scalingfactorindex - 1]);
            }
        }
        else
        {
            displaywidth_ = width_;
            displayheight_ = height_;
        }

        stride_ = TJPAD(displaywidth_ * tjPixelSize[pixelformat]);
        buffersize_ = stride_ * displayheight_;
        buffer = (PBYTE)HeapAlloc(heap_, 0, buffersize_);

        INT decompressflags{ TJFLAG_FASTDCT };
        if (!resize)
        {
            decompressflags |= TJFLAG_BOTTOMUP;
        }

        turbojpeg::SkipMarkers(jpegdecompressor, EXIF_MARKER);
        turbojpeg::SkipMarkers(jpegdecompressor, ICC_MARKER);

        if (turbojpeg::Decompress(jpegdecompressor, jpegfilemap.view(), jpegfilemap.filesize().LowPart, buffer,
            displaywidth_, stride_, displayheight_, pixelformat, decompressflags))
        {
            errorstring_ = turbojpeg::GetErrorStr();
            status_ = Status::Error;
            goto done;
        }

        if (pixelformat == TJPF_CMYK)
        {
            if (iccprofile_ == nullptr)
            {
                // TODO: determine if CMYK images can be supported without an ICC profile
                status_ = Status::Error;
                goto done;
            }

            TranformCMYK8ColorsToBGR8(&buffer, TJPAD(tjPixelSize[TJPF_BGR] * displaywidth_));
        }

        if (resize)
        {
            ImgItemHelper::ResizeAndRotate24bppRGBImage(*this, displaywidth_, displayheight_, buffer,
                targetwidth, targetheight, rotateflip);
        }
        else if (rotateflip != Gdiplus::RotateNoneFlipNone)
        {
            ImgItemHelper::Rotate24bppRGBImage(*this, displaywidth_, displayheight_, buffer, rotateflip);
        }
        else
        {
            WriteTempFile(buffer, buffersize_);
        }
    }
    catch (...)
    {
        status_ = Status::Error;
        goto done;
    }

    SetupRGBColorsBITMAPINFO(24, displaywidth_, displayheight_);

    offsetx_ = (targetwidth_ - displaywidth_) / 2;
    offsety_ = (targetheight_ - displayheight_) / 2;

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

INT ImgJPEGItem::GetScalingFactorIndex(INT width, INT height, INT targetwidth, INT targetheight)
{
    INT i{}, scaledwidth, scaledheight;

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