#pragma once

#include "ImageDispatcher.h"
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
    ImgCache()
    {
        if (!InitializeCriticalSectionAndSpinCount(&criticalsection_, 0x00000400))
        {
            // TODO: handle error
        }
    }
    ~ImgCache()
    {
        Clear();
        DeleteCriticalSection(&criticalsection_);
    }
    ImgCache(const ImgCache&) = delete;
    ImgCache& operator=(const ImgCache&) = delete;
    void Clear()
    {
        EnterCriticalSection(&criticalsection_);
        map_.clear();
        LeaveCriticalSection(&criticalsection_);
    }
    std::shared_ptr<ImgItem> Add(std::wstring filepath, INT targetwidth, INT targetheight, ImgItem::Format imgformat);
    void Remove(std::wstring filepath);
    std::shared_ptr<ImgItem> Get(const std::wstring& filepath, INT targetwidth, INT targetheight) const;
    std::vector<ImgCacheSizeStats> GetSizeStats() const;

  private:
    std::map<ImgCacheKey, std::shared_ptr<ImgItem>> map_;
    mutable CRITICAL_SECTION criticalsection_;
};

inline std::shared_ptr<ImgItem> ImgCache::Add(std::wstring filepath, INT targetwidth, INT targetheight,
                                              ImgItem::Format imgformat)
{
    EnterCriticalSection(&criticalsection_);
    const ImgCacheKey key{filepath, targetwidth, targetheight};
    const auto existing = map_.find(key);
    if (existing != map_.end())
    {
        const auto imgitem = existing->second;
        LeaveCriticalSection(&criticalsection_);
        return imgitem;
    }

    const auto imgitem = ImageDispatcher::Create(filepath, targetwidth, targetheight, imgformat);
    map_.emplace(std::make_pair(key, imgitem));
    LeaveCriticalSection(&criticalsection_);
    return imgitem;
}

inline void ImgCache::Remove(std::wstring filepath)
{
    EnterCriticalSection(&criticalsection_);
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
    LeaveCriticalSection(&criticalsection_);
}

inline std::shared_ptr<ImgItem> ImgCache::Get(const std::wstring& filepath, INT targetwidth, INT targetheight) const
{
    EnterCriticalSection(&criticalsection_);
    std::shared_ptr<ImgItem> imgitem(nullptr);
    const ImgCacheKey key{filepath, targetwidth, targetheight};
    const auto result = map_.find(key);
    if (result != map_.end())
    {
        imgitem = (*result).second;
    }

    LeaveCriticalSection(&criticalsection_);
    return imgitem;
}

inline std::vector<ImgCacheSizeStats> ImgCache::GetSizeStats() const
{
    EnterCriticalSection(&criticalsection_);
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

    LeaveCriticalSection(&criticalsection_);
    return stats;
}
