#include "ImgHEIFItem.h"

#include <libheif/heif.h>
#include <lcms2.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr std::size_t kMaximumIccProfileSize = 16U * 1024U * 1024U;

struct ContextDeleter
{
    void operator()(heif_context* context) const
    {
        heif_context_free(context);
    }
};

struct HandleDeleter
{
    void operator()(heif_image_handle* handle) const
    {
        heif_image_handle_release(handle);
    }
};

struct ImageDeleter
{
    void operator()(heif_image* image) const
    {
        heif_image_release(image);
    }
};

struct DecodingOptionsDeleter
{
    void operator()(heif_decoding_options* options) const
    {
        heif_decoding_options_free(options);
    }
};

class HeifInitialization final
{
  public:
    HeifInitialization()
    {
        error_ = heif_init(nullptr);
        initialized_ = error_.code == heif_error_Ok;
    }

    ~HeifInitialization()
    {
        if (initialized_)
        {
            heif_deinit();
        }
    }

    HeifInitialization(const HeifInitialization&) = delete;
    HeifInitialization& operator=(const HeifInitialization&) = delete;

    bool initialized() const
    {
        return initialized_;
    }

    heif_error error() const
    {
        return error_;
    }

  private:
    heif_error error_{};
    bool initialized_{};
};

class LoadCompletion final
{
  public:
    explicit LoadCompletion(HANDLE event) : event_(event) {}

    ~LoadCompletion()
    {
        SetEvent(event_);
    }

    LoadCompletion(const LoadCompletion&) = delete;
    LoadCompletion& operator=(const LoadCompletion&) = delete;

  private:
    HANDLE event_;
};

std::wstring Utf8ToWide(const char* text)
{
    if (text == nullptr || text[0] == '\0')
    {
        return {};
    }

    const auto length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (length <= 1)
    {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, &result[0], length);
    result.resize(static_cast<std::size_t>(length - 1));
    return result;
}

std::wstring FormatError(const wchar_t* operation, const heif_error& error)
{
    std::wstring message(operation);
    message += L" failed (";
    message += std::to_wstring(static_cast<int>(error.code));
    message += L", ";
    message += std::to_wstring(static_cast<int>(error.subcode));
    message += L")";

    const auto detail = Utf8ToWide(error.message);
    if (!detail.empty())
    {
        message += L": ";
        message += detail;
    }

    return message;
}

int PaddedBgrStride(int width)
{
    if (width <= 0 || width > ((std::numeric_limits<int>::max)() - 3) / 3)
    {
        return 0;
    }

    return ((width * 3) + 3) & ~3;
}

bool CalculateDisplaySize(int width, int height, int target_width, int target_height, int* output_width,
                          int* output_height)
{
    if (width <= 0 || height <= 0 || target_width <= 0 || target_height <= 0 || output_width == nullptr ||
        output_height == nullptr)
    {
        return false;
    }

    *output_width = width;
    *output_height = height;
    if (width <= target_width && height <= target_height)
    {
        return true;
    }

    const auto width_scale = static_cast<double>(target_width) / width;
    const auto height_scale = static_cast<double>(target_height) / height;
    const auto scale = (std::min)(width_scale, height_scale);
    *output_width = (std::max)(1, static_cast<int>(width * scale));
    *output_height = (std::max)(1, static_cast<int>(height * scale));
    return true;
}

bool ReadEmbeddedRgbProfile(const heif_image_handle* handle, std::vector<std::uint8_t>* profile_data)
{
    const auto profile_size = heif_image_handle_get_raw_color_profile_size(handle);
    if (profile_data == nullptr || profile_size == 0 || profile_size > kMaximumIccProfileSize)
    {
        return false;
    }

    profile_data->resize(profile_size);
    const auto profile_error = heif_image_handle_get_raw_color_profile(handle, profile_data->data());
    if (profile_error.code != heif_error_Ok)
    {
        profile_data->clear();
        return false;
    }

    const auto source_profile =
        cmsOpenProfileFromMem(profile_data->data(), static_cast<cmsUInt32Number>(profile_data->size()));
    if (source_profile == nullptr || cmsGetColorSpace(source_profile) != cmsSigRgbData)
    {
        if (source_profile != nullptr)
        {
            cmsCloseProfile(source_profile);
        }
        profile_data->clear();
        return false;
    }

    cmsCloseProfile(source_profile);
    return true;
}

bool TransformEmbeddedRgbProfile(const std::vector<std::uint8_t>& profile_data, const std::uint8_t* source,
                                 std::size_t source_stride, int width, int height, std::uint8_t* destination,
                                 int destination_stride)
{
    if (profile_data.empty())
    {
        return false;
    }

    const auto source_profile =
        cmsOpenProfileFromMem(profile_data.data(), static_cast<cmsUInt32Number>(profile_data.size()));
    if (source_profile == nullptr)
    {
        return false;
    }

    const auto destination_profile = cmsCreate_sRGBProfile();
    if (destination_profile == nullptr)
    {
        cmsCloseProfile(source_profile);
        return false;
    }

    const auto transform =
        cmsCreateTransform(source_profile, TYPE_RGBA_8, destination_profile, TYPE_BGR_8, INTENT_PERCEPTUAL, 0);
    if (transform == nullptr)
    {
        cmsCloseProfile(destination_profile);
        cmsCloseProfile(source_profile);
        return false;
    }

    cmsDoTransformLineStride(transform, source, destination, static_cast<cmsUInt32Number>(width),
                             static_cast<cmsUInt32Number>(height), static_cast<cmsUInt32Number>(source_stride),
                             static_cast<cmsUInt32Number>(destination_stride), 0, 0);
    cmsDeleteTransform(transform);
    cmsCloseProfile(destination_profile);
    cmsCloseProfile(source_profile);
    return true;
}

bool ConvertRgbaToBottomUpBgr(const std::vector<std::uint8_t>& embedded_profile, heif_image* image, int width,
                              int height, std::vector<std::uint8_t>* output, int* output_stride)
{
    std::size_t source_stride{};
    const auto source = heif_image_get_plane_readonly2(image, heif_channel_interleaved, &source_stride);
    const auto destination_stride = PaddedBgrStride(width);
    if (source == nullptr || destination_stride == 0 || source_stride < static_cast<std::size_t>(width) * 4U ||
        static_cast<std::size_t>(destination_stride) >
            (std::numeric_limits<std::size_t>::max)() / static_cast<std::size_t>(height))
    {
        return false;
    }

    output->assign(static_cast<std::size_t>(destination_stride) * height, 0);
    std::vector<std::uint8_t> color_managed(static_cast<std::size_t>(destination_stride) * height, 0);
    const auto premultiplied = heif_image_is_premultiplied_alpha(image) != 0;
    const auto* transform_source = source;
    auto transform_source_stride = source_stride;
    std::vector<std::uint8_t> unpremultiplied;
    if (premultiplied && !embedded_profile.empty())
    {
        transform_source_stride = static_cast<std::size_t>(width) * 4U;
        unpremultiplied.resize(transform_source_stride * height);
        for (int row_index = 0; row_index < height; ++row_index)
        {
            const auto source_row = source + static_cast<std::size_t>(row_index) * source_stride;
            auto destination_row =
                unpremultiplied.data() + static_cast<std::size_t>(row_index) * transform_source_stride;
            for (int x = 0; x < width; ++x)
            {
                const auto source_pixel = source_row + static_cast<std::size_t>(x) * 4U;
                auto destination_pixel = destination_row + static_cast<std::size_t>(x) * 4U;
                const auto alpha = source_pixel[3];
                for (int channel = 0; channel < 3; ++channel)
                {
                    destination_pixel[channel] =
                        alpha == 0 ? 0
                                   : static_cast<std::uint8_t>(
                                         (std::min)(255U, (source_pixel[channel] * 255U + alpha / 2U) / alpha));
                }
                destination_pixel[3] = alpha;
            }
        }
        transform_source = unpremultiplied.data();
    }
    const auto color_transform_applied =
        TransformEmbeddedRgbProfile(embedded_profile, transform_source, transform_source_stride, width, height,
                                    color_managed.data(), destination_stride);

    for (int source_row_index = 0; source_row_index < height; ++source_row_index)
    {
        const auto source_row = source + static_cast<std::size_t>(source_row_index) * source_stride;
        auto destination_row =
            output->data() + static_cast<std::size_t>(height - source_row_index - 1) * destination_stride;
        const auto managed_row = color_managed.data() + static_cast<std::size_t>(source_row_index) * destination_stride;

        for (int x = 0; x < width; ++x)
        {
            const auto source_pixel = source_row + static_cast<std::size_t>(x) * 4U;
            auto destination_pixel = destination_row + static_cast<std::size_t>(x) * 3U;
            const auto alpha = source_pixel[3];

            if (color_transform_applied)
            {
                const auto managed_pixel = managed_row + static_cast<std::size_t>(x) * 3U;
                for (int channel = 0; channel < 3; ++channel)
                {
                    destination_pixel[channel] =
                        static_cast<std::uint8_t>((managed_pixel[channel] * alpha + 127U) / 255U);
                }
            }
            else
            {
                const std::uint8_t bgr[] = {source_pixel[2], source_pixel[1], source_pixel[0]};
                for (int channel = 0; channel < 3; ++channel)
                {
                    destination_pixel[channel] =
                        premultiplied ? bgr[channel] : static_cast<std::uint8_t>((bgr[channel] * alpha + 127U) / 255U);
                }
            }
        }
    }

    *output_stride = destination_stride;
    return true;
}
} // namespace

void ImgHEIFItem::Load()
{
    status_ = Status::Loading;
    const LoadCompletion completion(loadedevent_);

    try
    {
        const HeifInitialization initialization;
        if (!initialization.initialized())
        {
            errormessage_ = FormatError(L"Initializing libheif", initialization.error());
            status_ = Status::Error;
            return;
        }

        FileMapView file_map(filepath_, FileMapView::Mode::Read);
        if (file_map.filesize().HighPart != 0 || file_map.filesize().LowPart == 0)
        {
            errormessage_ = L"HEIF file is empty or too large.";
            status_ = Status::Error;
            return;
        }

        std::unique_ptr<heif_context, ContextDeleter> context(heif_context_alloc());
        if (!context)
        {
            errormessage_ = L"Could not allocate a libheif context.";
            status_ = Status::Error;
            return;
        }

        auto error = heif_context_read_from_memory_without_copy(context.get(), file_map.data(),
                                                                file_map.filesize().LowPart, nullptr);
        if (error.code != heif_error_Ok)
        {
            errormessage_ = FormatError(L"Reading HEIF container", error);
            status_ = Status::Error;
            return;
        }

        heif_image_handle* raw_handle{};
        error = heif_context_get_primary_image_handle(context.get(), &raw_handle);
        std::unique_ptr<heif_image_handle, HandleDeleter> handle(raw_handle);
        if (error.code != heif_error_Ok || !handle)
        {
            errormessage_ = FormatError(L"Getting HEIF primary image", error);
            status_ = Status::Error;
            return;
        }

        width_ = heif_image_handle_get_width(handle.get());
        height_ = heif_image_handle_get_height(handle.get());
        if (width_ <= 0 || height_ <= 0)
        {
            errormessage_ = L"HEIF primary image has invalid dimensions.";
            status_ = Status::Error;
            return;
        }

        std::vector<std::uint8_t> embedded_profile;
        const auto has_embedded_profile = ReadEmbeddedRgbProfile(handle.get(), &embedded_profile);
        std::unique_ptr<heif_decoding_options, DecodingOptionsDeleter> decoding_options(heif_decoding_options_alloc());
        if (!decoding_options)
        {
            errormessage_ = L"Could not allocate libheif decoding options.";
            status_ = Status::Error;
            return;
        }
        decoding_options->convert_hdr_to_8bit = 1;
        decoding_options->output_image_nclx_profile_passthrough = has_embedded_profile ? 1 : 0;

        heif_image* raw_image{};
        error = heif_decode_image(handle.get(), &raw_image, heif_colorspace_RGB, heif_chroma_interleaved_RGBA,
                                  decoding_options.get());
        std::unique_ptr<heif_image, ImageDeleter> image(raw_image);
        if (error.code != heif_error_Ok || !image)
        {
            errormessage_ = FormatError(L"Decoding HEIF primary image", error);
            status_ = Status::Error;
            return;
        }

        int display_width{};
        int display_height{};
        if (!CalculateDisplaySize(width_, height_, targetwidth_, targetheight_, &display_width, &display_height))
        {
            errormessage_ = L"Could not calculate HEIF display dimensions.";
            status_ = Status::Error;
            return;
        }

        if (display_width != width_ || display_height != height_)
        {
            heif_image* raw_scaled_image{};
            error = heif_image_scale_image(image.get(), &raw_scaled_image, display_width, display_height, nullptr);
            std::unique_ptr<heif_image, ImageDeleter> scaled_image(raw_scaled_image);
            if (error.code != heif_error_Ok || !scaled_image)
            {
                errormessage_ = FormatError(L"Scaling HEIF primary image", error);
                status_ = Status::Error;
                return;
            }

            image = std::move(scaled_image);
        }

        std::vector<std::uint8_t> output;
        int output_stride{};
        if (!ConvertRgbaToBottomUpBgr(embedded_profile, image.get(), display_width, display_height, &output,
                                      &output_stride))
        {
            errormessage_ = L"Could not convert HEIF pixels to the display format.";
            status_ = Status::Error;
            return;
        }

        displaybuffer_.WriteData(display_width, display_height, output_stride, output.data());
        SetupDisplayParameters();
        status_ = Status::Ready;
    }
    catch (const std::exception& error)
    {
        errormessage_ = Utf8ToWide(error.what());
        status_ = Status::Error;
    }
    catch (...)
    {
        errormessage_ = L"Unexpected HEIF loading failure.";
        status_ = Status::Error;
    }
}
