#pragma once

#include "ImgItemFactory.h"
#include <Windows.h>
#include <string>
#include <map>

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
