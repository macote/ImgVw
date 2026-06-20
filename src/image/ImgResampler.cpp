#include "ImgResampler.h"

#include <algorithm>
#include <limits>

namespace
{
constexpr std::uint32_t kWeightOne = 1U << 14U;
constexpr std::uint32_t kHorizontalFractionBits = 8U;

struct FilterSpan
{
    int first_source{};
    int source_count{};
    std::size_t weight_offset{};
};

struct Filter
{
    std::vector<FilterSpan> spans;
    std::vector<std::uint16_t> weights;
};

bool BuildAreaFilter(int source_size, int destination_size, Filter* filter)
{
    if (source_size <= 0 || destination_size <= 0 || destination_size > source_size || filter == nullptr)
    {
        return false;
    }

    filter->spans.clear();
    filter->weights.clear();
    filter->spans.reserve(destination_size);
    filter->weights.reserve(static_cast<std::size_t>(source_size) + static_cast<std::size_t>(destination_size));

    for (int destination_index = 0; destination_index < destination_size; ++destination_index)
    {
        const auto interval_start = static_cast<std::int64_t>(destination_index) * source_size;
        const auto interval_end = static_cast<std::int64_t>(destination_index + 1) * source_size;
        const auto first_source = static_cast<int>(interval_start / destination_size);
        const auto last_source = static_cast<int>((interval_end - 1) / destination_size);
        const auto weight_offset = filter->weights.size();
        std::uint32_t weight_sum{};
        std::int64_t covered{};

        for (int source_index = first_source; source_index <= last_source; ++source_index)
        {
            const auto source_start = static_cast<std::int64_t>(source_index) * destination_size;
            const auto source_end = static_cast<std::int64_t>(source_index + 1) * destination_size;
            const auto overlap = (std::min)(interval_end, source_end) - (std::max)(interval_start, source_start);
            covered += overlap;
            const auto quantized_coverage = static_cast<std::uint32_t>((covered * kWeightOne + source_size / 2) /
                                                                       static_cast<std::int64_t>(source_size));
            const auto weight = quantized_coverage - weight_sum;
            filter->weights.push_back(static_cast<std::uint16_t>(weight));
            weight_sum += weight;
        }

        filter->spans.push_back(
            {first_source, last_source - first_source + 1, static_cast<std::size_t>(weight_offset)});
    }

    return true;
}

std::uint8_t Premultiply(std::uint8_t channel, std::uint8_t alpha)
{
    if (alpha == 255)
    {
        return channel;
    }
    if (alpha == 0)
    {
        return 0;
    }
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(channel) * alpha + 127U) / 255U);
}
} // namespace

bool ImgResampler::DownscaleRgba8(const std::uint8_t* source, std::size_t source_stride, int source_width,
                                  int source_height, int destination_width, int destination_height,
                                  AlphaMode source_alpha_mode, Result* result)
{
    if (result == nullptr)
    {
        return false;
    }
    result->pixels.clear();
    result->stride = 0;

    if (source == nullptr || source_width <= 0 || source_height <= 0 || destination_width <= 0 ||
        destination_height <= 0 || destination_width > source_width || destination_height > source_height ||
        source_stride < static_cast<std::size_t>(source_width) * 4U)
    {
        return false;
    }

    const auto destination_stride = static_cast<std::size_t>(destination_width) * 4U;
    if (destination_stride > (std::numeric_limits<std::size_t>::max)() / static_cast<std::size_t>(destination_height))
    {
        return false;
    }

    Filter horizontal_filter;
    Filter vertical_filter;
    if (!BuildAreaFilter(source_width, destination_width, &horizontal_filter) ||
        !BuildAreaFilter(source_height, destination_height, &vertical_filter))
    {
        return false;
    }

    const auto intermediate_stride = destination_stride;
    if (intermediate_stride > (std::numeric_limits<std::size_t>::max)() / static_cast<std::size_t>(source_height))
    {
        return false;
    }
    const auto intermediate_elements = intermediate_stride * static_cast<std::size_t>(source_height);
    if (intermediate_elements > (std::numeric_limits<std::size_t>::max)() / sizeof(std::uint16_t))
    {
        return false;
    }

    std::vector<std::uint16_t> intermediate(intermediate_elements);
    for (int source_y = 0; source_y < source_height; ++source_y)
    {
        const auto* source_row = source + static_cast<std::size_t>(source_y) * source_stride;
        auto* intermediate_row = intermediate.data() + static_cast<std::size_t>(source_y) * intermediate_stride;

        for (int destination_x = 0; destination_x < destination_width; ++destination_x)
        {
            const auto& span = horizontal_filter.spans[destination_x];
            const auto* weights = horizontal_filter.weights.data() + span.weight_offset;
            std::uint32_t accumulator[4]{};

            for (int source_offset = 0; source_offset < span.source_count; ++source_offset)
            {
                const auto* source_pixel =
                    source_row + static_cast<std::size_t>(span.first_source + source_offset) * 4U;
                const auto weight = weights[source_offset];
                const auto alpha = source_pixel[3];
                if (source_alpha_mode == AlphaMode::Straight)
                {
                    accumulator[0] += Premultiply(source_pixel[0], alpha) * weight;
                    accumulator[1] += Premultiply(source_pixel[1], alpha) * weight;
                    accumulator[2] += Premultiply(source_pixel[2], alpha) * weight;
                }
                else
                {
                    accumulator[0] += source_pixel[0] * weight;
                    accumulator[1] += source_pixel[1] * weight;
                    accumulator[2] += source_pixel[2] * weight;
                }
                accumulator[3] += alpha * weight;
            }

            auto* destination_pixel = intermediate_row + static_cast<std::size_t>(destination_x) * 4U;
            for (int channel = 0; channel < 4; ++channel)
            {
                destination_pixel[channel] =
                    static_cast<std::uint16_t>((accumulator[channel] + (1U << (14U - kHorizontalFractionBits - 1U))) >>
                                               (14U - kHorizontalFractionBits));
            }
        }
    }

    result->pixels.assign(destination_stride * static_cast<std::size_t>(destination_height), 0);
    result->stride = destination_stride;
    for (int destination_y = 0; destination_y < destination_height; ++destination_y)
    {
        const auto& span = vertical_filter.spans[destination_y];
        const auto* weights = vertical_filter.weights.data() + span.weight_offset;
        auto* destination_row = result->pixels.data() + static_cast<std::size_t>(destination_y) * destination_stride;

        for (int destination_x = 0; destination_x < destination_width; ++destination_x)
        {
            std::uint64_t accumulator[4]{};
            for (int source_offset = 0; source_offset < span.source_count; ++source_offset)
            {
                const auto* source_pixel =
                    intermediate.data() +
                    static_cast<std::size_t>(span.first_source + source_offset) * intermediate_stride +
                    static_cast<std::size_t>(destination_x) * 4U;
                const auto weight = weights[source_offset];
                for (int channel = 0; channel < 4; ++channel)
                {
                    accumulator[channel] += static_cast<std::uint64_t>(source_pixel[channel]) * weight;
                }
            }

            auto* destination_pixel = destination_row + static_cast<std::size_t>(destination_x) * 4U;
            for (int channel = 0; channel < 4; ++channel)
            {
                destination_pixel[channel] =
                    static_cast<std::uint8_t>((accumulator[channel] + (1ULL << (14U + kHorizontalFractionBits - 1U))) >>
                                              (14U + kHorizontalFractionBits));
            }
        }
    }

    return true;
}
