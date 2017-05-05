#pragma once

#include "ImgItemFactory.h"
#include <Windows.h>
#include <string>
#include <map>

class ImgCache
{
public:
	ImgCache()
	{
	}
	~ImgCache()
	{
		Clear();
	}
	void Clear()
	{
		map_.clear();
	}
	void Add(std::wstring filepath, INT targetwidth, INT targetheight);
	void Remove(std::wstring filepath);
	void set_temppath(std::wstring temppath) { temppath_ = temppath; }
	std::shared_ptr<ImgItem> Get(std::wstring filepath) const;
private:
	std::wstring temppath_;
	std::map<std::wstring, std::shared_ptr<ImgItem>, std::less<std::wstring>> map_;
};

inline void ImgCache::Add(std::wstring filepath, INT targetwidth, INT targetheight)
{
	auto imgitem = ImgItemFactory::Create(filepath, temppath_, targetwidth, targetheight);
	map_.emplace(std::pair<std::wstring, std::shared_ptr<ImgItem>>(filepath, std::move(imgitem)));
}

inline void ImgCache::Remove(std::wstring filepath)
{
	map_.erase(filepath);
}

inline std::shared_ptr<ImgItem> ImgCache::Get(std::wstring filepath) const
{
	std::shared_ptr<ImgItem> imgitem(nullptr);
	auto result = map_.find(filepath);
	if (result != map_.end())
	{
		imgitem = (*result).second;
	}

	return imgitem;
}