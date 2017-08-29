#pragma once

#include "ImgItem.h"
#include "turbojpeg_ImgVw.h"
#include "3rd-party\easyexif\exif.h"
#include "3rd-party\Little-CMS\lcms2.h"
#include <Windows.h>
#include <Gdiplus.h>
#include <string>

class ImgJPEGItem : public ImgItem
{
public:
    ImgJPEGItem(std::wstring filepath, std::wstring temppath, INT targetwidth, INT targetheight)
        : ImgItem(filepath, temppath, targetwidth, targetheight)
    {
        scalingfactors_ = turbojpeg::GetScalingFactors(&scalingfactorcount_);
    }
    void Load();
private:
    INT scalingfactorcount_{};
    tjscalingfactor* scalingfactors_{ nullptr };
    std::string errorstring_;
    INT GetScalingFactorIndex(INT width, INT height, INT targetwidth, INT targetheight);
};