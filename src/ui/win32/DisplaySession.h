#pragma once

#include <string>

class DisplaySession final
{
  public:
    bool first_paint() const
    {
        return first_paint_;
    }

    void CompleteFirstPaint()
    {
        first_paint_ = false;
    }

    const std::wstring& selected_path() const
    {
        return selected_path_;
    }

    const std::wstring& painted_path() const
    {
        return painted_path_;
    }

    const std::wstring& restore_path() const
    {
        return painted_path_.empty() ? selected_path_ : painted_path_;
    }

    void Select(const std::wstring& path)
    {
        selected_path_ = path;
    }

    void MarkPainted(const std::wstring& path)
    {
        painted_path_ = path;
    }

    void Clear()
    {
        selected_path_.clear();
        painted_path_.clear();
    }

  private:
    bool first_paint_{true};
    std::wstring selected_path_;
    std::wstring painted_path_;
};
