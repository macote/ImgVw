#pragma once

#include "ImgItem.h"
#include "turbojpeg_ImgVw.h"
#include <Windows.h>
#include <Gdiplus.h>
#include <string>

class ImgJPEGItem final : public ImgItem
{
public:
    ImgJPEGItem(std::wstring filepath, INT targetwidth, INT targetheight)
        : ImgItem(filepath, targetwidth, targetheight)
    {
        scalingfactors_ = turbojpeg::GetScalingFactors(&scalingfactorcount_);
    }
    ImgJPEGItem(const ImgJPEGItem&) = delete;
    ImgJPEGItem& operator=(const ImgJPEGItem&) = delete;
    void Load();
private:
    INT scalingfactorcount_{};
    tjscalingfactor* scalingfactors_{ nullptr };
    std::string errorstring_;
private:
    INT GetScalingFactorIndex(INT width, INT height, INT targetwidth, INT targetheight) const;
};