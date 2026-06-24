#include "ColorTransform.h"

#include <limits>

namespace
{
class TransformHandle
{
  public:
    TransformHandle(cmsHPROFILE source, cmsUInt32Number source_format, cmsHPROFILE destination,
                    cmsUInt32Number destination_format)
        : transform_(cmsCreateTransform(source, source_format, destination, destination_format, INTENT_PERCEPTUAL, 0))
    {
    }

    ~TransformHandle()
    {
        if (transform_ != nullptr)
        {
            cmsDeleteTransform(transform_);
        }
    }

    TransformHandle(const TransformHandle&) = delete;
    TransformHandle& operator=(const TransformHandle&) = delete;

    bool IsValid() const
    {
        return transform_ != nullptr;
    }

    cmsHTRANSFORM get() const
    {
        return transform_;
    }

  private:
    cmsHTRANSFORM transform_{nullptr};
};
} // namespace

ColorTransformResult ColorTransform::TransformCmyk8ReversedToBgr8(const ColorProfile& source_profile, INT width,
                                                                  INT height, INT stride, INT new_stride, PBYTE* buffer,
                                                                  HANDLE heap)
{
    if (width <= 0 || height <= 0 || stride <= 0 || new_stride <= 0 || buffer == nullptr || *buffer == nullptr ||
        heap == nullptr || heap == INVALID_HANDLE_VALUE)
    {
        return {ColorTransformStatus::InvalidInput};
    }

    if (!source_profile.IsValid())
    {
        return {ColorTransformStatus::MissingSourceProfile};
    }

    if (static_cast<std::size_t>(new_stride) > (std::numeric_limits<std::size_t>::max)() / height)
    {
        return {ColorTransformStatus::InvalidInput};
    }

    auto srgb_profile = ColorProfile::CreateSrgb();
    if (!srgb_profile.IsValid())
    {
        return {ColorTransformStatus::CreateSrgbProfileFailed};
    }

    TransformHandle transform(source_profile.get(), TYPE_CMYK_8_REV, srgb_profile.get(), TYPE_BGR_8);
    if (!transform.IsValid())
    {
        return {ColorTransformStatus::CreateTransformFailed};
    }

    const auto new_buffer_size = static_cast<std::size_t>(new_stride) * height;
    auto new_buffer = reinterpret_cast<PBYTE>(HeapAlloc(heap, 0, new_buffer_size));
    if (new_buffer == nullptr)
    {
        return {ColorTransformStatus::AllocationFailed};
    }

    cmsDoTransformLineStride(transform.get(), *buffer, new_buffer, width, height, stride, new_stride, 0, 0);

    HeapFree(heap, 0, *buffer);
    *buffer = new_buffer;

    return {ColorTransformStatus::Succeeded};
}
