#include "ImgJPEGItem.h"
#include "ImgBitmap.h"
#include "ImgItemHelper.h"
#include "3rd-party\easyexif\exif.h"
#include <memory>

void ImgJPEGItem::Load()
{
    status_ = Status::Loading;
    tjhandle jpegdecompressor = NULL;
    INT pixelformat = TJPF_BGR;
    INT decompressflags{ TJFLAG_FASTDCT };
    INT targetwidth, targetheight;
    PBYTE buffer = NULL;
    BOOL resize = FALSE;
    Gdiplus::RotateFlipType rotateflip = Gdiplus::RotateNoneFlipNone;

    try
    {
        FileMapView jpegfilemap(filepath_, FileMapView::Mode::Read);

        if (jpegfilemap.filesize().HighPart > 0)
        {
            status_ = Status::Error;
            goto done;
        }

        jpegdecompressor = tjInitDecompress();
        if (jpegdecompressor == NULL)
        {
            status_ = Status::Error;
            goto done;
        }

        easyexif::EXIFInfo result;
        if (result.parseFrom(jpegfilemap.view(), jpegfilemap.filesize().LowPart, TRUE) == 0)
        {
            switch (result.Orientation)
            {
            case 3:
                rotateflip = Gdiplus::Rotate180FlipNone;
                break;
            case 6:
                rotateflip = Gdiplus::Rotate270FlipNone;
                break;
            case 8:
                rotateflip = Gdiplus::Rotate90FlipNone;
                break;
            default:
                break;
            }
        }

        INT jpegSubsamp, jpegColorspace;
        if (tjDecompressHeader3(jpegdecompressor, jpegfilemap.view(), jpegfilemap.filesize().LowPart, &width_, &height_, &jpegSubsamp, &jpegColorspace))
        {
            errorstring_ = tjGetErrorStr();
            status_ = Status::Error;
            goto done;
        }

        if (jpegColorspace == TJCS::TJCS_CMYK || jpegColorspace == TJCS::TJCS_YCCK)
        {
            pixelformat = TJPF_CMYK;
        }

        if (rotateflip == Gdiplus::Rotate90FlipNone || rotateflip == Gdiplus::Rotate270FlipNone)
        {
            targetwidth = targetheight_;
            targetheight = targetwidth_;
        }
        else
        {
            targetwidth = targetwidth_;
            targetheight = targetheight_;
        }

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

        if (!resize)
        {
            decompressflags |= TJFLAG_BOTTOMUP;
        }

        if (tjDecompress2(jpegdecompressor, jpegfilemap.view(), jpegfilemap.filesize().LowPart, buffer, displaywidth_, stride_, displayheight_, pixelformat, decompressflags))
        {
            errorstring_ = tjGetErrorStr();
            status_ = Status::Error;
            goto done;
        }

        if (pixelformat == TJPF_CMYK)
        {
            // TODO: support CMYK JPEG images
            status_ = Status::Error;
            goto done;
        }

        if (resize)
        {
            ImgItemHelper::ResizeAndRotate24bppRGBImage(*this, displaywidth_, displayheight_, buffer, targetwidth, targetheight,
                rotateflip);
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

    if (buffer != NULL)
    {
        HeapFree(heap_, 0, buffer);
    }

    if (jpegdecompressor != NULL)
    {
        tjDestroy(jpegdecompressor);
    }

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