#include "ImgFormatResolver.h"

#include "ImgFormatDetector.h"
#include "ImgHeaderProbe.h"
#include "ImgItemHelper.h"

ImgItem::Format ImgFormatResolver::Resolve(const std::wstring& filepath)
{
    const auto extensionformat = ImgItemHelper::GetImgFormatFromExtension(filepath);
    if (extensionformat == ImgItem::Format::Unsupported)
    {
        return ImgItem::Format::Unsupported;
    }

    const auto probe = ImgHeaderProbe::ReadPrefix(filepath);
    if (probe.Succeeded())
    {
        const auto detectedformat =
            ImgFormatDetector::ToImgItemFormat(ImgFormatDetector::Detect(probe.bytes.data(), probe.bytes.size()));
        if (detectedformat != ImgItem::Format::Unsupported)
        {
            return detectedformat;
        }
    }

    return extensionformat;
}
