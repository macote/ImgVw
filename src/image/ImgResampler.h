#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class ImgResampler
{
  public:
    enum class AlphaMode
    {
        Straight,
        Premultiplied
    };

    struct Result
    {
        std::vector<std::uint8_t> pixels;
        std::size_t stride{};
    };

    static bool DownscaleRgba8(const std::uint8_t* source, std::size_t source_stride, int source_width,
                               int source_height, int destination_width, int destination_height,
                               AlphaMode source_alpha_mode, Result* result);
};
