#pragma once

#include "ImgItemFactory.h"
#include <Windows.h>
#include <string>
#include <map>

class ImgCache
{
public:
    ImgCache() { }
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
    void Add(std::wstring filepath, INT targetwidth, INT targetheight);
    void Remove(std::wstring filepath);
    std::shared_ptr<ImgItem> Get(const std::wstring& filepath) const;
private:
    std::map<std::wstring, std::shared_ptr<ImgItem>, std::less<std::wstring>> map_;
};

inline void ImgCache::Add(std::wstring filepath, INT targetwidth, INT targetheight)
{
    const auto imgitem = ImgItemFactory::Create(filepath, targetwidth, targetheight);
    map_.emplace(std::pair<std::wstring, std::shared_ptr<ImgItem>>(filepath, std::move(imgitem)));
}

inline void ImgCache::Remove(std::wstring filepath)
{
    map_.erase(filepath);
}

inline std::shared_ptr<ImgItem> ImgCache::Get(const std::wstring& filepath) const
{
    std::shared_ptr<ImgItem> imgitem(nullptr);
    const auto result = map_.find(filepath);
    if (result != map_.end())
    {
        imgitem = (*result).second;
    }

    return imgitem;
}