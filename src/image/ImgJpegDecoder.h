#pragma once

#include <csetjmp>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include <jpeglib.h>

class ImgJpegDecoder final
{
  public:
    ImgJpegDecoder() = default;
    ~ImgJpegDecoder();
    ImgJpegDecoder(const ImgJpegDecoder&) = delete;
    ImgJpegDecoder& operator=(const ImgJpegDecoder&) = delete;

    bool Initialize(const unsigned char* data, std::size_t size);
    bool ConfigureOutput(unsigned int scale_numerator, unsigned int scale_denominator, bool cmyk);
    bool Decode(unsigned char* buffer, int stride, bool bottom_up);

    int width() const
    {
        return width_;
    }
    int height() const
    {
        return height_;
    }
    int output_width() const
    {
        return output_width_;
    }
    int output_height() const
    {
        return output_height_;
    }
    bool is_cmyk() const
    {
        return is_cmyk_;
    }
    const unsigned char* exif_data() const
    {
        return exif_data_;
    }
    std::size_t exif_size() const
    {
        return exif_size_;
    }
    const std::vector<unsigned char>& icc_profile() const
    {
        return icc_profile_;
    }
    const std::string& error() const
    {
        return error_;
    }

  private:
    struct ErrorManager
    {
        jpeg_error_mgr base{};
        jmp_buf jump_buffer{};
        char message[JMSG_LENGTH_MAX]{};
    };

    jpeg_decompress_struct decompressor_{};
    ErrorManager error_manager_{};
    bool created_{};
    int width_{};
    int height_{};
    int output_width_{};
    int output_height_{};
    bool is_cmyk_{};
    const unsigned char* exif_data_{};
    std::size_t exif_size_{};
    std::vector<unsigned char> icc_profile_;
    std::string error_;
    JSAMPROW* row_pointers_{};

    void CaptureError();
    void FindExifMarker();
    void ReadIccProfile();
    void DeleteRowPointers();
    static void ErrorExit(j_common_ptr common);
    static void EmitMessage(j_common_ptr common, int message_level);
};
