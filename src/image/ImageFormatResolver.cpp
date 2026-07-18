#include "ImageFormatResolver.h"

#include "ImageFormatDetector.h"
#include "ImageHeaderProbe.h"
#include "ImgItemHelper.h"

ImgItem::Format ImageFormatResolver::Resolve(const std::wstring& filepath)
{
    const auto extensionformat = ImgItemHelper::GetImgFormatFromExtension(filepath);
    if (extensionformat == ImgItem::Format::Unsupported)
    {
        return ImgItem::Format::Unsupported;
    }

    const auto probe = ImageHeaderProbe::ReadPrefix(filepath);
    if (probe.Succeeded())
    {
        const auto detectedformat =
            ImageFormatDetector::ToImgItemFormat(ImageFormatDetector::Detect(probe.bytes.data(), probe.bytes.size()));
        if (detectedformat != ImgItem::Format::Unsupported)
        {
            return detectedformat;
        }
    }

    return extensionformat;
}
