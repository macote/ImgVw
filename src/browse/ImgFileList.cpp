#include "ImgFileList.h"

#include <algorithm>
#include <cwctype>
#include <iterator>

namespace
{
std::wstring::size_type FileNameOffset(const std::wstring& path)
{
    const auto separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? 0 : separator + 1;
}

int ComparePathPart(const std::wstring& left, std::wstring::size_type left_offset, std::wstring::size_type left_length,
                    const std::wstring& right, std::wstring::size_type right_offset,
                    std::wstring::size_type right_length)
{
    const auto shared_length = std::min(left_length, right_length);
    for (std::wstring::size_type index = 0; index < shared_length; ++index)
    {
        const auto left_char = left[left_offset + index];
        const auto right_char = right[right_offset + index];
        const auto left_folded = std::towlower(left_char);
        const auto right_folded = std::towlower(right_char);
        if (left_folded != right_folded)
        {
            return left_folded < right_folded ? -1 : 1;
        }
    }

    if (left_length != right_length)
    {
        return left_length < right_length ? -1 : 1;
    }

    for (std::wstring::size_type index = 0; index < shared_length; ++index)
    {
        const auto left_char = left[left_offset + index];
        const auto right_char = right[right_offset + index];
        if (left_char != right_char)
        {
            return left_char < right_char ? -1 : 1;
        }
    }

    return 0;
}
} // namespace

bool ImgFileListPathLess::operator()(const std::wstring& left, const std::wstring& right) const
{
    const auto left_filename_offset = FileNameOffset(left);
    const auto right_filename_offset = FileNameOffset(right);

    const auto folder_compare = ComparePathPart(left, 0, left_filename_offset, right, 0, right_filename_offset);
    if (folder_compare != 0)
    {
        return folder_compare < 0;
    }

    return ComparePathPart(left, left_filename_offset, left.size() - left_filename_offset, right, right_filename_offset,
                           right.size() - right_filename_offset) < 0;
}

ImgFileList::ImgFileList() : random_engine_(std::random_device{}()) {}

ImgFileList::ImgFileList(unsigned int randomseed) : random_engine_(randomseed) {}

void ImgFileList::Clear()
{
    files_.clear();
    current_ = files_.end();
    random_order_.clear();
    random_index_ = kRandomIndexPark;
    current_pinned_ = false;
}

bool ImgFileList::Add(const std::wstring& filepath)
{
    const auto result = files_.insert(filepath);
    if (!result.second)
    {
        return false;
    }

    if (random_index_ == kRandomIndexPark)
    {
        random_order_.push_back(filepath);
    }
    else
    {
        random_order_.push_back(filepath);
        std::uniform_int_distribution<std::size_t> insertion(random_index_, random_order_.size() - 1);
        std::iter_swap(random_order_.begin() + insertion(random_engine_), std::prev(random_order_.end()));
    }
    if (current_ == files_.end() || !current_pinned_)
    {
        current_ = files_.begin();
    }

    return true;
}

bool ImgFileList::Empty() const
{
    return files_.empty();
}

std::size_t ImgFileList::Size() const
{
    return files_.size();
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
    current_pinned_ = true;
    return true;
}

bool ImgFileList::MoveToPrevious()
{
    if (files_.empty() || current_ == files_.begin())
    {
        return false;
    }

    --current_;
    current_pinned_ = true;
    return true;
}

bool ImgFileList::MoveToFirst()
{
    if (files_.empty() || current_ == files_.begin())
    {
        return false;
    }

    current_ = files_.begin();
    current_pinned_ = true;
    return true;
}

bool ImgFileList::MoveToLast()
{
    if (files_.empty() || std::next(current_) == files_.end())
    {
        return false;
    }

    current_ = std::prev(files_.end());
    current_pinned_ = true;
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
    current_pinned_ = true;
    return true;
}

void ImgFileList::BeginRandomCycle()
{
    std::shuffle(random_order_.begin(), random_order_.end(), random_engine_);

    if (current_ == files_.end())
    {
        random_index_ = random_order_.empty() ? kRandomIndexPark : 0;
        return;
    }

    const auto currentpath = *current_;
    const auto match = std::find(random_order_.begin(), random_order_.end(), currentpath);
    if (match != random_order_.end())
    {
        std::iter_swap(random_order_.begin(), match);
        random_index_ = 1;
        return;
    }

    random_index_ = random_order_.empty() ? kRandomIndexPark : 0;
}

bool ImgFileList::MoveToRandom()
{
    return MoveToRandomExcluding({});
}

bool ImgFileList::MoveToRandomExcluding(const std::vector<std::wstring>& excluded)
{
    if (files_.empty())
    {
        return false;
    }

    if (random_index_ >= random_order_.size())
    {
        std::wstring last;
        if (random_index_ != kRandomIndexPark && !random_order_.empty())
        {
            last = random_order_.back();
        }

        std::shuffle(random_order_.begin(), random_order_.end(), random_engine_);
        if (random_order_.size() > 1 && last == random_order_.front())
        {
            std::uniform_int_distribution<std::size_t> replacement(1, random_order_.size() - 1);
            std::iter_swap(random_order_.begin(), random_order_.begin() + replacement(random_engine_));
        }

        random_index_ = 0;
    }

    const auto next = std::find_if(random_order_.begin() + random_index_, random_order_.end(),
                                   [&excluded](const std::wstring& filepath) {
                                       return std::find(excluded.begin(), excluded.end(), filepath) == excluded.end();
                                   });
    if (next == random_order_.end())
    {
        return false;
    }

    std::iter_swap(random_order_.begin() + random_index_, next);
    current_ = files_.find(random_order_[random_index_]);
    ++random_index_;
    if (current_ == files_.end())
    {
        return false;
    }

    current_pinned_ = true;
    return true;
}

ImgFileListRandomProgress ImgFileList::GetRandomProgress() const
{
    if (random_index_ == kRandomIndexPark)
    {
        return {0, random_order_.size()};
    }

    return {std::min(random_index_, random_order_.size()), random_order_.size()};
}

bool ImgFileList::RemoveCurrent()
{
    if (current_ == files_.end())
    {
        return false;
    }

    const auto removed = *current_;
    current_ = files_.erase(current_);
    const auto randommatch = std::find(random_order_.begin(), random_order_.end(), removed);
    if (randommatch != random_order_.end())
    {
        const auto removedindex = static_cast<std::size_t>(std::distance(random_order_.begin(), randommatch));
        random_order_.erase(randommatch);
        if (random_index_ != kRandomIndexPark && removedindex < random_index_)
        {
            --random_index_;
        }
    }
    if (files_.empty())
    {
        current_pinned_ = false;
    }

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
