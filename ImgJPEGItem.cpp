#include "ImgJPEGItem.h"
#include "ImgBitmap.h"
#include "ImgItemHelper.h"
#include <memory>

void ImgJPEGItem::Load()
{
	status_ = Status::Loading;
	tjhandle jpegdecompressor = NULL;
	INT decompressflags{ TJFLAG_FASTDCT };
	PBYTE buffer = NULL;
	BOOL resize = FALSE;

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

		INT jpegSubsamp, jpegColorspace;
		if (tjDecompressHeader3(jpegdecompressor, jpegfilemap.view(), jpegfilemap.filesize().LowPart, &width_, &height_, &jpegSubsamp, &jpegColorspace))
		{
			status_ = Status::Error;
			goto done;
		}

		if (jpegColorspace == TJCS::TJCS_CMYK || jpegColorspace == TJCS::TJCS_YCCK)
		{
			// TODO: determine if it should be supported
			status_ = Status::Error;
			goto done;
		}

		if (width_ > targetwidth_ || height_ > targetheight_)
		{
			auto scalingfactorindex = GetScalingFactorIndex(width_, height_);
			if (scalingfactorindex >= scalingfactorcount_)
			{
				resize = TRUE;
			}
			else
			{
				displaywidth_ = TJSCALED(width_, scalingfactors_[scalingfactorindex]);
				displayheight_ = TJSCALED(height_, scalingfactors_[scalingfactorindex]);
				auto widthdiff = targetwidth_ - displaywidth_;
				auto heightdiff = targetheight_ - displayheight_;

				if ((widthdiff >= heightdiff && heightdiff > (kResizePercentThreshold * targetheight_))
					|| (heightdiff >= widthdiff && widthdiff > (kResizePercentThreshold * targetwidth_)))
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

		stride_ = TJPAD(displaywidth_ * tjPixelSize[TJPF_RGB]);
		buffersize_ = stride_ * displayheight_;

		buffer = (PBYTE)HeapAlloc(heap_, 0, buffersize_);

		if (!resize)
		{
			decompressflags |= TJFLAG_BOTTOMUP;
		}

		if (tjDecompress2(jpegdecompressor, jpegfilemap.view(), jpegfilemap.filesize().LowPart, buffer, displaywidth_, stride_, displayheight_, TJPF_BGR, decompressflags))
		{
			status_ = Status::Error;
			goto done;
		}

		if (resize)
		{
			ImgItemHelper::Resize24bppRGBImage(displaywidth_, displayheight_, buffer, targetwidth_, targetheight_,
				[&](Gdiplus::BitmapData& data)
			{
				buffersize_ = data.Height * data.Stride;
				displaywidth_ = data.Width;
				displayheight_ = data.Height;
				this->WriteTempFile((PBYTE)data.Scan0, buffersize_);
			});
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

INT ImgJPEGItem::GetScalingFactorIndex(INT width, INT height)
{
	INT i{}, scaledwidth, scaledheight;

	while (i < scalingfactorcount_)
	{
		scaledwidth = TJSCALED(width, scalingfactors_[i]);
		scaledheight = TJSCALED(height, scalingfactors_[i]);
		if (scaledwidth <= targetwidth_ && scaledheight <= targetheight_)
		{
			break;
		}

		++i;
	}

	return i;
}