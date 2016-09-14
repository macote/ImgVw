#pragma once

#include "ImgItem.h"
#include <Windows.h>
#include <string>
#include <vector>
#include <map>
#include <memory>

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
	std::shared_ptr<ImgItem> Get(std::wstring filepath) const;
private:
	std::map<std::wstring, std::shared_ptr<ImgItem>, std::less<std::wstring>> map_;
};

inline void ImgCache::Add(std::wstring filepath, INT targetwidth, INT targetheight)
{
	auto imgitem = std::make_shared<ImgItem>(filepath, targetwidth, targetheight);
	map_.emplace(std::pair<std::wstring, std::shared_ptr<ImgItem>>(filepath, std::move(imgitem)));
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
