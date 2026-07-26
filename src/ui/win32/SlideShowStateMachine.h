#pragma once

#include <Windows.h>

enum class SlideShowMode
{
    Sequential,
    Random,
};

enum class SlideShowNavigation
{
    None,
    Sequential,
    Random,
};

class SlideShowStateMachine final
{
  public:
    SlideShowStateMachine(UINT initial_interval, UINT minimum_interval, UINT maximum_interval, UINT interval_step)
        : interval_(initial_interval), minimum_interval_(minimum_interval), maximum_interval_(maximum_interval),
          interval_step_(interval_step)
    {
    }

    bool Start(SlideShowMode mode)
    {
        mode_ = mode;
        const auto changed = !running_;
        running_ = true;
        return changed;
    }

    bool Stop()
    {
        const auto changed = running_;
        running_ = false;
        waiting_for_image_ = false;
        needs_initial_advance_ = false;
        return changed;
    }

    SlideShowNavigation OnTimer() const
    {
        if (!running_ || waiting_for_image_)
        {
            return SlideShowNavigation::None;
        }
        return mode_ == SlideShowMode::Random ? SlideShowNavigation::Random : SlideShowNavigation::Sequential;
    }

    void OnDisplaySelection(bool has_item, bool ready_or_error)
    {
        waiting_for_image_ = has_item && !ready_or_error;
        needs_initial_advance_ = !has_item && running_ && mode_ == SlideShowMode::Random;
        if (has_item)
        {
            needs_initial_advance_ = false;
        }
    }

    bool OnImageReady()
    {
        if (!waiting_for_image_)
        {
            return false;
        }
        waiting_for_image_ = false;
        return true;
    }

    bool IncreaseSpeed()
    {
        if (interval_ <= minimum_interval_)
        {
            return false;
        }
        interval_ = interval_ - minimum_interval_ < interval_step_ ? minimum_interval_ : interval_ - interval_step_;
        return true;
    }

    bool DecreaseSpeed()
    {
        if (interval_ >= maximum_interval_)
        {
            return false;
        }
        interval_ = maximum_interval_ - interval_ < interval_step_ ? maximum_interval_ : interval_ + interval_step_;
        return true;
    }

    bool running() const
    {
        return running_;
    }
    bool random() const
    {
        return mode_ == SlideShowMode::Random;
    }
    bool waiting_for_image() const
    {
        return waiting_for_image_;
    }
    bool needs_initial_advance() const
    {
        return needs_initial_advance_;
    }
    UINT interval() const
    {
        return interval_;
    }
    void SetMode(SlideShowMode mode)
    {
        mode_ = mode;
    }
    void SetWaitingForImage(bool waiting)
    {
        waiting_for_image_ = waiting;
    }
    void SetNeedsInitialAdvance(bool needed)
    {
        needs_initial_advance_ = needed;
    }
    void SetInterval(UINT interval)
    {
        interval_ = interval;
    }

  private:
    bool running_{};
    SlideShowMode mode_{SlideShowMode::Sequential};
    bool waiting_for_image_{};
    bool needs_initial_advance_{};
    UINT interval_{};
    UINT minimum_interval_{};
    UINT maximum_interval_{};
    UINT interval_step_{};
};
