#include "ImgJPEGDecoder.h"

#include <cstdlib>
#include <cstring>
#include <limits>

namespace
{
constexpr auto kExifMarker = JPEG_APP0 + 1;
constexpr auto kIccMarker = JPEG_APP0 + 2;
constexpr unsigned char kExifSignature[] = {'E', 'x', 'i', 'f', 0, 0};
} // namespace

ImgJPEGDecoder::~ImgJPEGDecoder()
{
    DeleteRowPointers();
    if (created_)
    {
        jpeg_destroy_decompress(&decompressor_);
    }
}

bool ImgJPEGDecoder::Initialize(const unsigned char* data, std::size_t size)
{
    if (created_ || data == nullptr || size == 0 || size > std::numeric_limits<unsigned long>::max())
    {
        error_ = "Invalid JPEG input.";
        return false;
    }

    decompressor_.err = jpeg_std_error(&error_manager_.base);
    error_manager_.base.error_exit = ErrorExit;
    error_manager_.base.emit_message = EmitMessage;
    if (setjmp(error_manager_.jump_buffer) != 0)
    {
        CaptureError();
        return false;
    }

    jpeg_create_decompress(&decompressor_);
    created_ = true;
    jpeg_save_markers(&decompressor_, kExifMarker, 0xFFFF);
    jpeg_save_markers(&decompressor_, kIccMarker, 0xFFFF);
    jpeg_mem_src(&decompressor_, data, static_cast<unsigned long>(size));
    if (jpeg_read_header(&decompressor_, TRUE) != JPEG_HEADER_OK)
    {
        error_ = "Invalid JPEG header.";
        return false;
    }

    width_ = static_cast<int>(decompressor_.image_width);
    height_ = static_cast<int>(decompressor_.image_height);
    is_cmyk_ = decompressor_.jpeg_color_space == JCS_CMYK || decompressor_.jpeg_color_space == JCS_YCCK;
    FindExifMarker();
    ReadIccProfile();
    return true;
}

bool ImgJPEGDecoder::ConfigureOutput(unsigned int scale_numerator, unsigned int scale_denominator, bool cmyk)
{
    if (!created_ || scale_numerator == 0 || scale_denominator == 0)
    {
        error_ = "Invalid JPEG output configuration.";
        return false;
    }

    if (setjmp(error_manager_.jump_buffer) != 0)
    {
        CaptureError();
        return false;
    }

    decompressor_.scale_num = scale_numerator;
    decompressor_.scale_denom = scale_denominator;
    decompressor_.out_color_space = cmyk ? JCS_CMYK : JCS_EXT_BGR;
    decompressor_.dct_method = JDCT_FASTEST;
    jpeg_calc_output_dimensions(&decompressor_);
    output_width_ = static_cast<int>(decompressor_.output_width);
    output_height_ = static_cast<int>(decompressor_.output_height);
    return output_width_ > 0 && output_height_ > 0;
}

bool ImgJPEGDecoder::Decode(unsigned char* buffer, int stride, bool bottom_up, ProgressCallback progress_callback,
                            void* progress_context)
{
    if (!created_ || buffer == nullptr || stride <= 0 || output_width_ <= 0 || output_height_ <= 0)
    {
        error_ = "Invalid JPEG decode buffer.";
        return false;
    }

    DeleteRowPointers();
    row_pointers_ = static_cast<JSAMPROW*>(std::malloc(sizeof(JSAMPROW) * static_cast<std::size_t>(output_height_)));
    if (row_pointers_ == nullptr)
    {
        error_ = "Could not allocate JPEG row pointers.";
        return false;
    }

    for (int row = 0; row < output_height_; ++row)
    {
        const auto output_row = bottom_up ? output_height_ - row - 1 : row;
        row_pointers_[row] = buffer + (static_cast<std::size_t>(output_row) * stride);
    }

    if (setjmp(error_manager_.jump_buffer) != 0)
    {
        CaptureError();
        DeleteRowPointers();
        return false;
    }

    jpeg_start_decompress(&decompressor_);
    if (static_cast<int>(decompressor_.output_width) != output_width_ ||
        static_cast<int>(decompressor_.output_height) != output_height_)
    {
        error_ = "JPEG output dimensions changed unexpectedly.";
        jpeg_abort_decompress(&decompressor_);
        DeleteRowPointers();
        return false;
    }

    if (progress_callback != nullptr)
    {
        progress_callback(0, progress_context);
    }

    while (decompressor_.output_scanline < decompressor_.output_height)
    {
        const auto start_scanline = decompressor_.output_scanline;
        const auto remaining_scanlines = decompressor_.output_height - decompressor_.output_scanline;
        const auto scanline_count = remaining_scanlines > 16 ? 16 : remaining_scanlines;
        jpeg_read_scanlines(&decompressor_, &row_pointers_[decompressor_.output_scanline], scanline_count);
        if (progress_callback != nullptr && decompressor_.output_scanline != start_scanline)
        {
            const auto percent = static_cast<int>((decompressor_.output_scanline * 100) / decompressor_.output_height);
            progress_callback(percent, progress_context);
        }
    }

    jpeg_finish_decompress(&decompressor_);
    DeleteRowPointers();
    return true;
}

void ImgJPEGDecoder::CaptureError()
{
    error_ = error_manager_.message[0] == '\0' ? "JPEG operation failed." : error_manager_.message;
}

void ImgJPEGDecoder::FindExifMarker()
{
    for (auto marker = decompressor_.marker_list; marker != nullptr; marker = marker->next)
    {
        if (marker->marker == kExifMarker && marker->data_length >= sizeof(kExifSignature) &&
            std::memcmp(marker->data, kExifSignature, sizeof(kExifSignature)) == 0)
        {
            exif_data_ = marker->data;
            exif_size_ = marker->data_length;
            return;
        }
    }
}

void ImgJPEGDecoder::ReadIccProfile()
{
    JOCTET* profile{};
    unsigned int profile_size{};
    if (jpeg_read_icc_profile(&decompressor_, &profile, &profile_size))
    {
        icc_profile_.assign(profile, profile + profile_size);
        std::free(profile);
    }
}

void ImgJPEGDecoder::DeleteRowPointers()
{
    std::free(row_pointers_);
    row_pointers_ = nullptr;
}

void ImgJPEGDecoder::ErrorExit(j_common_ptr common)
{
    auto error = reinterpret_cast<ErrorManager*>(common->err);
    (*common->err->format_message)(common, error->message);
    longjmp(error->jump_buffer, 1);
}

void ImgJPEGDecoder::EmitMessage(j_common_ptr common, int message_level)
{
    if (message_level < 0)
    {
        ErrorExit(common);
    }
}
