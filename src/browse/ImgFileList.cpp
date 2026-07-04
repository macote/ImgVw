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
}

bool ImgFileListPathLess::operator()(const std::wstring& left, const std::wstring& right) const
{
    const auto left_filename_offset = FileNameOffset(left);
    const auto right_filename_offset = FileNameOffset(right);

    const auto folder_compare =
        ComparePathPart(left, 0, left_filename_offset, right, 0, right_filename_offset);
    if (folder_compare != 0)
    {
        return folder_compare < 0;
    }

    return ComparePathPart(left, left_filename_offset, left.size() - left_filename_offset, right,
                           right_filename_offset, right.size() - right_filename_offset) < 0;
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

    random_order_.push_back(filepath);
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
    if (current_ == files_.end())
    {
        return false;
    }

    current_pinned_ = true;
    return true;
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
