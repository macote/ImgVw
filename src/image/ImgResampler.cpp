#include "ImgResampler.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <new>

namespace
{
constexpr std::uint32_t kWeightOne = 1U << 14U;
constexpr std::uint32_t kHorizontalFractionBits = 8U;
constexpr int kMaximumDimension = 32768;
constexpr std::size_t kMaximumWorkingSetBytes = std::size_t{512} * 1024U * 1024U;

bool TryMultiply(std::size_t left, std::size_t right, std::size_t* result)
{
    if (result == nullptr || (right != 0 && left > (std::numeric_limits<std::size_t>::max)() / right))
    {
        return false;
    }

    *result = left * right;
    return true;
}

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
    const auto source_count = static_cast<std::size_t>(source_size);
    const auto destination_count = static_cast<std::size_t>(destination_size);
    if (source_count > kMaximumDimension || destination_count > kMaximumDimension ||
        source_count > (std::numeric_limits<std::size_t>::max)() - destination_count)
    {
        return false;
    }

    filter->spans.reserve(destination_count);
    filter->weights.reserve(source_count + destination_count);

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
        destination_height <= 0 || source_width > kMaximumDimension || source_height > kMaximumDimension ||
        destination_width > source_width || destination_height > source_height)
    {
        return false;
    }

    std::size_t minimum_source_stride{};
    std::size_t destination_stride{};
    std::size_t destination_bytes{};
    std::size_t last_source_row_offset{};
    if (!TryMultiply(static_cast<std::size_t>(source_width), 4U, &minimum_source_stride) ||
        source_stride < minimum_source_stride ||
        !TryMultiply(static_cast<std::size_t>(destination_width), 4U, &destination_stride) ||
        !TryMultiply(destination_stride, static_cast<std::size_t>(destination_height), &destination_bytes) ||
        destination_bytes > kMaximumWorkingSetBytes ||
        !TryMultiply(source_stride, static_cast<std::size_t>(source_height - 1), &last_source_row_offset) ||
        last_source_row_offset > (std::numeric_limits<std::size_t>::max)() - minimum_source_stride)
    {
        return false;
    }

    std::size_t intermediate_elements{};
    std::size_t intermediate_bytes{};
    if (!TryMultiply(destination_stride, static_cast<std::size_t>(source_height), &intermediate_elements) ||
        !TryMultiply(intermediate_elements, sizeof(std::uint16_t), &intermediate_bytes) ||
        intermediate_bytes > kMaximumWorkingSetBytes - destination_bytes)
    {
        return false;
    }

    try
    {
        Filter horizontal_filter;
        Filter vertical_filter;
        if (!BuildAreaFilter(source_width, destination_width, &horizontal_filter) ||
            !BuildAreaFilter(source_height, destination_height, &vertical_filter))
        {
            return false;
        }

        std::vector<std::uint16_t> intermediate(intermediate_elements);
        for (int source_y = 0; source_y < source_height; ++source_y)
        {
            const auto* source_row = source + static_cast<std::size_t>(source_y) * source_stride;
            auto* intermediate_row = intermediate.data() + static_cast<std::size_t>(source_y) * destination_stride;

            for (int destination_x = 0; destination_x < destination_width; ++destination_x)
            {
                const auto& span = horizontal_filter.spans[destination_x];
                const auto* weights = horizontal_filter.weights.data() + span.weight_offset;
                std::uint32_t accumulator[4]{};

                for (int source_index_offset = 0; source_index_offset < span.source_count; ++source_index_offset)
                {
                    const auto* source_pixel =
                        source_row + static_cast<std::size_t>(span.first_source + source_index_offset) * 4U;
                    const auto weight = weights[source_index_offset];
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
                    destination_pixel[channel] = static_cast<std::uint16_t>(
                        (accumulator[channel] + (1U << (14U - kHorizontalFractionBits - 1U))) >>
                        (14U - kHorizontalFractionBits));
                }
            }
        }

        result->pixels.assign(destination_bytes, 0);
        result->stride = destination_stride;
        for (int destination_y = 0; destination_y < destination_height; ++destination_y)
        {
            const auto& span = vertical_filter.spans[destination_y];
            const auto* weights = vertical_filter.weights.data() + span.weight_offset;
            auto* destination_row =
                result->pixels.data() + static_cast<std::size_t>(destination_y) * destination_stride;

            for (int destination_x = 0; destination_x < destination_width; ++destination_x)
            {
                std::uint64_t accumulator[4]{};
                for (int source_index_offset = 0; source_index_offset < span.source_count; ++source_index_offset)
                {
                    const auto* source_pixel =
                        intermediate.data() +
                        static_cast<std::size_t>(span.first_source + source_index_offset) * destination_stride +
                        static_cast<std::size_t>(destination_x) * 4U;
                    const auto weight = weights[source_index_offset];
                    for (int channel = 0; channel < 4; ++channel)
                    {
                        accumulator[channel] += static_cast<std::uint64_t>(source_pixel[channel]) * weight;
                    }
                }

                auto* destination_pixel = destination_row + static_cast<std::size_t>(destination_x) * 4U;
                for (int channel = 0; channel < 4; ++channel)
                {
                    destination_pixel[channel] = static_cast<std::uint8_t>(
                        (accumulator[channel] + (1ULL << (14U + kHorizontalFractionBits - 1U))) >>
                        (14U + kHorizontalFractionBits));
                }
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        result->pixels.clear();
        result->stride = 0;
        return false;
    }

    return true;
}
