#pragma once

#include <cstddef>
#include <random>
#include <set>
#include <string>
#include <vector>

struct ImgFileListPathLess
{
    bool operator()(const std::wstring& left, const std::wstring& right) const;
};

struct ImgFileListRandomProgress
{
    std::size_t position{};
    std::size_t total{};
};

class ImgFileList final
{
  public:
    static constexpr std::size_t kRandomIndexPark = static_cast<std::size_t>(-1);

    ImgFileList();
    explicit ImgFileList(unsigned int randomseed);

    void Clear();
    bool Add(const std::wstring& filepath);
    bool Empty() const;
    std::size_t Size() const;
    std::wstring CurrentPath() const;
    bool MoveToNext();
    bool MoveToPrevious();
    bool MoveToFirst();
    bool MoveToLast();
    bool MoveTo(const std::wstring& filepath);
    void BeginRandomCycle();
    bool MoveToRandom();
    bool MoveToRandomExcluding(const std::vector<std::wstring>& excluded);
    ImgFileListRandomProgress GetRandomProgress() const;
    bool RemoveCurrent();
    std::vector<std::wstring> PathsFromCurrent() const;

  private:
    std::set<std::wstring, ImgFileListPathLess> files_;
    std::set<std::wstring, ImgFileListPathLess>::iterator current_{files_.end()};
    std::vector<std::wstring> random_order_;
    std::size_t random_index_{kRandomIndexPark};
    std::mt19937 random_engine_;
    bool current_pinned_{};
};
