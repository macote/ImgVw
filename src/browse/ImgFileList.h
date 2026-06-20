#pragma once

#include <cstddef>
#include <random>
#include <set>
#include <string>
#include <vector>

class ImgFileList final
{
  public:
    static constexpr std::size_t kRandomIndexPark = static_cast<std::size_t>(-1);

    ImgFileList();
    explicit ImgFileList(unsigned int randomseed);

    void Clear();
    bool Add(const std::wstring& filepath);
    bool Empty() const;
    std::wstring CurrentPath() const;
    bool MoveToNext();
    bool MoveToPrevious();
    bool MoveToFirst();
    bool MoveToLast();
    bool MoveTo(const std::wstring& filepath);
    bool MoveToRandom();
    bool RemoveCurrent();

  private:
    std::set<std::wstring> files_;
    std::set<std::wstring>::iterator current_{files_.end()};
    std::vector<std::wstring> random_order_;
    std::size_t random_index_{kRandomIndexPark};
    std::mt19937 random_engine_;
};
