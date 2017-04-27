#pragma once

#include "ImgItem.h"
#include "3rd-party\libjpeg-turbo\turbojpeg.h"
#include <Windows.h>
#include <string>

class ImgJPEGItem : public ImgItem
{
public:
	ImgJPEGItem(std::wstring filepath, std::wstring temppath, INT targetwidth, INT targetheight)
		: ImgItem(filepath, temppath, targetwidth, targetheight)
	{
		scalingfactors_ = tjGetScalingFactors(&scalingfactorcount_);
	}
	void Load();
private:
	INT scalingfactorcount_{};
	tjscalingfactor* scalingfactors_{ nullptr };
	INT GetScalingFactorIndex(INT width, INT height);
};