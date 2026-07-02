#pragma once

#include "ImgItemFactory.h"
#include <Windows.h>
#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

struct ImgCacheSizeStats
{
    INT targetwidth{};
    INT targetheight{};
    std::size_t queued{};
    std::size_t loading{};
    std::size_t ready{};
    std::size_t error{};
    unsigned long long temp_file_bytes{};
};

struct ImgCacheKey
{
    std::wstring filepath;
    INT targetwidth{};
    INT targetheight{};

    bool operator<(const ImgCacheKey& other) const
    {
        if (filepath < other.filepath)
        {
            return true;
        }
        if (other.filepath < filepath)
        {
            return false;
        }
        if (targetwidth < other.targetwidth)
        {
            return true;
        }
        if (other.targetwidth < targetwidth)
        {
            return false;
        }

        return targetheight < other.targetheight;
    }
};

class ImgCache
{
  public:
    ImgCache() {}
    ~ImgCache()
    {
        Clear();
    }
    ImgCache(const ImgCache&) = delete;
    ImgCache& operator=(const ImgCache&) = delete;
    void Clear()
    {
        map_.clear();
    }
    std::shared_ptr<ImgItem> Add(std::wstring filepath, INT targetwidth, INT targetheight);
    std::shared_ptr<ImgItem> Add(std::wstring filepath, INT targetwidth, INT targetheight, ImgItem::Format imgformat);
    void Remove(std::wstring filepath);
    std::shared_ptr<ImgItem> Get(const std::wstring& filepath, INT targetwidth, INT targetheight) const;
    std::vector<ImgCacheSizeStats> GetSizeStats() const;

  private:
    std::map<ImgCacheKey, std::shared_ptr<ImgItem>> map_;
};

inline std::shared_ptr<ImgItem> ImgCache::Add(std::wstring filepath, INT targetwidth, INT targetheight)
{
    return Add(filepath, targetwidth, targetheight, ImgItemFactory::ResolveFormat(filepath));
}

inline std::shared_ptr<ImgItem> ImgCache::Add(std::wstring filepath, INT targetwidth, INT targetheight,
                                              ImgItem::Format imgformat)
{
    const ImgCacheKey key{filepath, targetwidth, targetheight};
    const auto existing = map_.find(key);
    if (existing != map_.end())
    {
        return existing->second;
    }

    const auto imgitem = ImgItemFactory::Create(filepath, targetwidth, targetheight, imgformat);
    map_.emplace(std::make_pair(key, imgitem));
    return imgitem;
}

inline void ImgCache::Remove(std::wstring filepath)
{
    auto item = map_.begin();
    while (item != map_.end())
    {
        if (item->first.filepath == filepath)
        {
            map_.erase(item++);
        }
        else
        {
            ++item;
        }
    }
}

inline std::shared_ptr<ImgItem> ImgCache::Get(const std::wstring& filepath, INT targetwidth, INT targetheight) const
{
    std::shared_ptr<ImgItem> imgitem(nullptr);
    const ImgCacheKey key{filepath, targetwidth, targetheight};
    const auto result = map_.find(key);
    if (result != map_.end())
    {
        imgitem = (*result).second;
    }

    return imgitem;
}

inline std::vector<ImgCacheSizeStats> ImgCache::GetSizeStats() const
{
    std::vector<ImgCacheSizeStats> stats;
    for (const auto& item : map_)
    {
        const auto& key = item.first;
        auto match = std::find_if(stats.begin(), stats.end(), [&key](const ImgCacheSizeStats& candidate) {
            return candidate.targetwidth == key.targetwidth && candidate.targetheight == key.targetheight;
        });
        if (match == stats.end())
        {
            ImgCacheSizeStats size_stats;
            size_stats.targetwidth = key.targetwidth;
            size_stats.targetheight = key.targetheight;
            stats.push_back(size_stats);
            match = stats.end() - 1;
        }

        switch (item.second->status())
        {
        case ImgItem::Status::Queued:
            ++match->queued;
            break;
        case ImgItem::Status::Loading:
            ++match->loading;
            break;
        case ImgItem::Status::Ready:
            ++match->ready;
            match->temp_file_bytes += item.second->displaybuffersize();
            break;
        case ImgItem::Status::Error:
            ++match->error;
            break;
        }
    }

    return stats;
}
