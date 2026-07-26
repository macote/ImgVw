#pragma once

class InfoOverlayVisibility final
{
  public:
    bool ToggleFilename(bool stats_requested)
    {
        filename_enabled_ = stats_requested ? false : !filename_enabled_;
        return filename_enabled_;
    }

    void OnStatsRequested(bool requested)
    {
        if (requested)
        {
            filename_enabled_ = false;
        }
    }

    bool filename_visible(bool stats_requested) const
    {
        return filename_enabled_ && !stats_requested;
    }

  private:
    bool filename_enabled_{};
};
