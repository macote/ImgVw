#pragma once

#include "ImgBrowser.h"
#include "InfoOverlay.h"

#include <algorithm>
#include <string>
#include <vector>

struct InfoOverlayStatsContext
{
    bool has_free_bytes{};
    ULONGLONG free_bytes{};
    bool slideshow_running{};
    bool random_slideshow{};
    ImgFileListProgress cycle;
    std::wstring current_item_text;
};

class InfoOverlayStatsBuilder final
{
  public:
    static InfoOverlayStatsSnapshot Build(const ImgBrowserStats& shared_stats,
                                          const std::vector<ImgBrowserStats>& target_stats,
                                          const InfoOverlayStatsContext& context)
    {
        InfoOverlayStatsSnapshot snapshot;
        snapshot.found_images = shared_stats.found_images;
        for (const auto& stats : target_stats)
        {
            const auto duplicate = std::find_if(
                snapshot.targets.begin(), snapshot.targets.end(), [&stats](const InfoOverlayTargetStats& target) {
                    return target.width == stats.targetwidth && target.height == stats.targetheight;
                });
            if (duplicate != snapshot.targets.end())
            {
                continue;
            }

            ImgCacheSizeStats size_stats;
            size_stats.targetwidth = stats.targetwidth;
            size_stats.targetheight = stats.targetheight;
            const auto size =
                std::find_if(stats.sizes.begin(), stats.sizes.end(), [&stats](const ImgCacheSizeStats& item) {
                    return item.targetwidth == stats.targetwidth && item.targetheight == stats.targetheight;
                });
            if (size != stats.sizes.end())
            {
                size_stats = *size;
            }

            snapshot.targets.push_back({size_stats.targetwidth, size_stats.targetheight, size_stats.queued,
                                        size_stats.loading, size_stats.ready, size_stats.error,
                                        size_stats.temp_file_bytes, stats.loader.queued, stats.loader.free_slots,
                                        stats.loader.maximum_slots});
        }

        snapshot.has_free_bytes = context.has_free_bytes;
        snapshot.free_bytes = context.free_bytes;
        snapshot.slideshow_running = context.slideshow_running;
        snapshot.random_slideshow = context.random_slideshow;
        snapshot.cycle_position = context.cycle.position;
        snapshot.cycle_total = context.cycle.total;
        snapshot.current_item_text = context.current_item_text;
        return snapshot;
    }
};
