#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ImgBrowserLoadContext;

using MultiMonitorWindowId = std::uintptr_t;

class MultiMonitorSlideShowCoordinator
{
  public:
    bool RegisterSecondaryWindow(MultiMonitorWindowId window)
    {
        if (window == 0 ||
            std::find(secondary_windows_.begin(), secondary_windows_.end(), window) != secondary_windows_.end())
        {
            return false;
        }

        secondary_windows_.push_back(window);
        return true;
    }

    bool UnregisterSecondaryWindow(MultiMonitorWindowId window)
    {
        const auto match = std::find(secondary_windows_.begin(), secondary_windows_.end(), window);
        if (match == secondary_windows_.end())
        {
            return false;
        }

        secondary_windows_.erase(match);
        if (next_target_index_ >= target_count())
        {
            next_target_index_ = 0;
        }
        return true;
    }

    void Start()
    {
        running_ = true;
        next_target_index_ = 0;
        preloaded_path_count_ = 0;
        sequential_cursor_path_.clear();
    }

    void Stop()
    {
        running_ = false;
        next_target_index_ = 0;
        preloaded_path_count_ = 0;
        sequential_cursor_path_.clear();
    }

    bool running() const
    {
        return running_;
    }

    std::size_t target_count() const
    {
        return 1 + secondary_windows_.size();
    }

    MultiMonitorWindowId TargetAt(MultiMonitorWindowId primary_window, std::size_t index) const
    {
        if (index == 0)
        {
            return primary_window;
        }

        const auto secondary_index = index - 1;
        return secondary_index < secondary_windows_.size() ? secondary_windows_[secondary_index] : 0;
    }

    MultiMonitorWindowId NextTarget(MultiMonitorWindowId primary_window)
    {
        const auto count = target_count();
        const auto target = TargetAt(primary_window, next_target_index_ % count);
        next_target_index_ = (next_target_index_ + 1) % count;
        return target;
    }

    const std::vector<MultiMonitorWindowId>& secondary_windows() const
    {
        return secondary_windows_;
    }

    std::vector<MultiMonitorWindowId> ReleaseSecondaryWindows()
    {
        std::vector<MultiMonitorWindowId> windows;
        windows.swap(secondary_windows_);
        next_target_index_ = 0;
        return windows;
    }

    const std::wstring& sequential_cursor_path() const
    {
        return sequential_cursor_path_;
    }

    void SetSequentialCursorPath(const std::wstring& path)
    {
        sequential_cursor_path_ = path;
    }

    std::size_t preloaded_path_count() const
    {
        return preloaded_path_count_;
    }

    void SetPreloadedPathCount(std::size_t count)
    {
        preloaded_path_count_ = count;
    }

    std::shared_ptr<ImgBrowserLoadContext> FindLoadContext(int width, int height) const
    {
        const auto match = std::find_if(load_contexts_.begin(), load_contexts_.end(),
                                        [width, height](const TargetLoadContext& context) {
                                            return context.width == width && context.height == height;
                                        });
        return match == load_contexts_.end() ? std::shared_ptr<ImgBrowserLoadContext>() : match->context;
    }

    void RememberLoadContext(int width, int height, const std::shared_ptr<ImgBrowserLoadContext>& context)
    {
        if (context == nullptr || width <= 0 || height <= 0)
        {
            return;
        }

        load_contexts_.erase(std::remove_if(load_contexts_.begin(), load_contexts_.end(),
                                            [width, height, &context](const TargetLoadContext& item) {
                                                return item.context == context &&
                                                       (item.width != width || item.height != height);
                                            }),
                             load_contexts_.end());

        const auto match =
            std::find_if(load_contexts_.begin(), load_contexts_.end(), [width, height](const TargetLoadContext& item) {
                return item.width == width && item.height == height;
            });
        if (match == load_contexts_.end())
        {
            load_contexts_.push_back({width, height, context});
        }
        else
        {
            match->context = context;
        }
    }

    void ClearLoadContexts()
    {
        load_contexts_.clear();
    }

  private:
    struct TargetLoadContext
    {
        int width{};
        int height{};
        std::shared_ptr<ImgBrowserLoadContext> context;
    };

    bool running_{};
    std::size_t next_target_index_{};
    std::size_t preloaded_path_count_{};
    std::wstring sequential_cursor_path_;
    std::vector<MultiMonitorWindowId> secondary_windows_;
    std::vector<TargetLoadContext> load_contexts_;
};
