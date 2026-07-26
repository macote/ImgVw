#include "ImgHEIFItem.h"
#include "ImgItemHelper.h"
#include "ImgResampler.h"

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

struct HeifProgressContext
{
    ImgItem* item{};
    volatile LONG maximum{100};
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

bool ConvertRgbaToBottomUpBgr(const std::vector<std::uint8_t>& embedded_profile, const std::uint8_t* source,
                              std::size_t source_stride, bool premultiplied, int width, int height,
                              std::vector<std::uint8_t>* output, int* output_stride)
{
    const auto destination_stride = PaddedBgrStride(width);
    if (source == nullptr || destination_stride == 0 || source_stride < static_cast<std::size_t>(width) * 4U ||
        static_cast<std::size_t>(destination_stride) >
            (std::numeric_limits<std::size_t>::max)() / static_cast<std::size_t>(height))
    {
        return false;
    }

    output->assign(static_cast<std::size_t>(destination_stride) * height, 0);
    std::vector<std::uint8_t> color_managed(static_cast<std::size_t>(destination_stride) * height, 0);
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

void StartDecodeProgress(heif_progress_step step, int max_progress, void* context)
{
    if (step != heif_progress_step_total)
    {
        return;
    }

    auto progress_context = reinterpret_cast<HeifProgressContext*>(context);
    if (progress_context != nullptr && progress_context->item != nullptr)
    {
        InterlockedExchange(&progress_context->maximum, max_progress > 0 ? max_progress : 100);
        progress_context->item->SetLoadingProgressPercent(0);
    }
}

void UpdateDecodeProgress(heif_progress_step step, int progress, void* context)
{
    if (step != heif_progress_step_total)
    {
        return;
    }

    auto progress_context = reinterpret_cast<HeifProgressContext*>(context);
    if (progress_context != nullptr && progress_context->item != nullptr)
    {
        const auto maximum = InterlockedCompareExchange(&progress_context->maximum, 0, 0);
        progress_context->item->SetLoadingProgressPercent(maximum > 0 ? (progress * 100) / maximum : progress);
    }
}

void EndDecodeProgress(heif_progress_step step, void* context)
{
    if (step != heif_progress_step_total)
    {
        return;
    }

    auto progress_context = reinterpret_cast<HeifProgressContext*>(context);
    if (progress_context != nullptr && progress_context->item != nullptr)
    {
        progress_context->item->SetLoadingProgressPercent(100);
    }
}
} // namespace

ImgHEIFItem::ImgHEIFItem(std::wstring filepath, INT targetwidth, INT targetheight)
    : ImgItem(filepath, targetwidth, targetheight)
{
    SetSupportsLoadingProgress(TRUE);
}

void ImgHEIFItem::Load()
{
    SetStatus(Status::Loading);
    ResetLoadingProgress();
    const LoadCompletion completion(loadedevent_.get());

    try
    {
        const HeifInitialization initialization;
        if (!initialization.initialized())
        {
            SetError(FormatError(L"Initializing libheif", initialization.error()));
            return;
        }

        FileMapView file_map(filepath_, FileMapView::Mode::Read);
        if (file_map.filesize().HighPart != 0 || file_map.filesize().LowPart == 0)
        {
            SetError(L"HEIF file is empty or too large.");
            return;
        }

        std::unique_ptr<heif_context, ContextDeleter> context(heif_context_alloc());
        if (!context)
        {
            SetError(L"Could not allocate a libheif context.");
            return;
        }

        auto error = heif_context_read_from_memory_without_copy(context.get(), file_map.data(),
                                                                file_map.filesize().LowPart, nullptr);
        if (error.code != heif_error_Ok)
        {
            SetError(FormatError(L"Reading HEIF container", error));
            return;
        }

        heif_image_handle* raw_handle{};
        error = heif_context_get_primary_image_handle(context.get(), &raw_handle);
        std::unique_ptr<heif_image_handle, HandleDeleter> handle(raw_handle);
        if (error.code != heif_error_Ok || !handle)
        {
            SetError(FormatError(L"Getting HEIF primary image", error));
            return;
        }

        width_ = heif_image_handle_get_width(handle.get());
        height_ = heif_image_handle_get_height(handle.get());
        if (width_ <= 0 || height_ <= 0)
        {
            SetError(L"HEIF primary image has invalid dimensions.");
            return;
        }

        std::vector<std::uint8_t> embedded_profile;
        const auto has_embedded_profile = ReadEmbeddedRgbProfile(handle.get(), &embedded_profile);
        std::unique_ptr<heif_decoding_options, DecodingOptionsDeleter> decoding_options(heif_decoding_options_alloc());
        if (!decoding_options)
        {
            SetError(L"Could not allocate libheif decoding options.");
            return;
        }
        decoding_options->convert_hdr_to_8bit = 1;
        decoding_options->output_image_nclx_profile_passthrough = has_embedded_profile ? 1 : 0;
        decoding_options->color_conversion_options.preferred_chroma_upsampling_algorithm =
            heif_chroma_upsampling_bilinear;
        decoding_options->color_conversion_options.only_use_preferred_chroma_algorithm = 1;
        HeifProgressContext progress_context{this, 100};
        decoding_options->start_progress = StartDecodeProgress;
        decoding_options->on_progress = UpdateDecodeProgress;
        decoding_options->end_progress = EndDecodeProgress;
        decoding_options->progress_user_data = &progress_context;

        heif_image* raw_image{};
        error = heif_decode_image(handle.get(), &raw_image, heif_colorspace_RGB, heif_chroma_interleaved_RGBA,
                                  decoding_options.get());
        std::unique_ptr<heif_image, ImageDeleter> image(raw_image);
        if (error.code != heif_error_Ok || !image)
        {
            SetError(FormatError(L"Decoding HEIF primary image", error));
            return;
        }

        int display_width{};
        int display_height{};
        if (!ImgItemHelper::CalculateDisplaySize(width_, height_, targetwidth_, targetheight_, &display_width,
                                                 &display_height))
        {
            SetError(L"Could not calculate HEIF display dimensions.");
            return;
        }

        std::size_t source_stride{};
        auto source = heif_image_get_plane_readonly2(image.get(), heif_channel_interleaved, &source_stride);
        auto premultiplied = heif_image_is_premultiplied_alpha(image.get()) != 0;
        ImgResampler::Result resized;
        if (display_width != width_ || display_height != height_)
        {
            const auto alpha_mode =
                premultiplied ? ImgResampler::AlphaMode::Premultiplied : ImgResampler::AlphaMode::Straight;
            if (!ImgResampler::DownscaleRgba8(source, source_stride, width_, height_, display_width, display_height,
                                              alpha_mode, &resized))
            {
                SetError(L"Scaling HEIF primary image failed.");
                return;
            }
            source = resized.pixels.data();
            source_stride = resized.stride;
            premultiplied = true;
        }

        std::vector<std::uint8_t> output;
        int output_stride{};
        if (!ConvertRgbaToBottomUpBgr(embedded_profile, source, source_stride, premultiplied, display_width,
                                      display_height, &output, &output_stride))
        {
            SetError(L"Could not convert HEIF pixels to the display format.");
            return;
        }

        pending_displaybuffer_.WriteData(display_width, display_height, output_stride, output.data());
        SetupDisplayParameters();
    }
    catch (const std::exception& error)
    {
        SetError(Utf8ToWide(error.what()));
    }
    catch (...)
    {
        SetError(L"Unexpected HEIF loading failure.");
    }
}
