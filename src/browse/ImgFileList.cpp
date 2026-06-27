#include "ImgFileList.h"

#include <algorithm>
#include <iterator>

ImgFileList::ImgFileList() : random_engine_(std::random_device{}()) {}

ImgFileList::ImgFileList(unsigned int randomseed) : random_engine_(randomseed) {}

void ImgFileList::Clear()
{
    files_.clear();
    current_ = files_.end();
    random_order_.clear();
    random_index_ = kRandomIndexPark;
}

bool ImgFileList::Add(const std::wstring& filepath)
{
    const auto result = files_.insert(filepath);
    if (!result.second)
    {
        return false;
    }

    random_order_.push_back(filepath);
    if (current_ == files_.end())
    {
        current_ = files_.begin();
    }

    return true;
}

bool ImgFileList::Empty() const
{
    return files_.empty();
}

std::wstring ImgFileList::CurrentPath() const
{
    return current_ == files_.end() ? std::wstring() : *current_;
}

bool ImgFileList::MoveToNext()
{
    if (files_.empty() || std::next(current_) == files_.end())
    {
        return false;
    }

    ++current_;
    return true;
}

bool ImgFileList::MoveToPrevious()
{
    if (files_.empty() || current_ == files_.begin())
    {
        return false;
    }

    --current_;
    return true;
}

bool ImgFileList::MoveToFirst()
{
    if (files_.empty() || current_ == files_.begin())
    {
        return false;
    }

    current_ = files_.begin();
    return true;
}

bool ImgFileList::MoveToLast()
{
    if (files_.empty() || std::next(current_) == files_.end())
    {
        return false;
    }

    current_ = std::prev(files_.end());
    return true;
}

bool ImgFileList::MoveTo(const std::wstring& filepath)
{
    const auto match = files_.find(filepath);
    if (match == files_.end())
    {
        return false;
    }

    current_ = match;
    return true;
}

bool ImgFileList::MoveToRandom()
{
    if (files_.empty())
    {
        return false;
    }

    if (random_index_ >= random_order_.size())
    {
        std::wstring last;
        if (random_index_ != kRandomIndexPark)
        {
            last = random_order_.back();
        }

        do
        {
            std::shuffle(random_order_.begin(), random_order_.end(), random_engine_);
        } while (random_order_.size() > 1 && last == random_order_.front());

        random_index_ = 0;
    }

    current_ = files_.find(random_order_[random_index_]);
    ++random_index_;
    return current_ != files_.end();
}

bool ImgFileList::RemoveCurrent()
{
    if (current_ == files_.end())
    {
        return false;
    }

    const auto removed = *current_;
    current_ = files_.erase(current_);
    random_order_.erase(std::remove(random_order_.begin(), random_order_.end(), removed), random_order_.end());
    random_index_ = kRandomIndexPark;
    return true;
}

std::vector<std::wstring> ImgFileList::PathsFromCurrent() const
{
    std::vector<std::wstring> paths;
    if (files_.empty())
    {
        return paths;
    }

    const auto start = current_ == files_.end() ? files_.begin() : current_;
    paths.insert(paths.end(), start, files_.end());
    paths.insert(paths.end(), files_.begin(), start);
    return paths;
}
