#include "EmptyStateLayout.h"
#include "DisplayPresenter.h"
#include "InfoOverlay.h"
#include "OverlayText.h"
#include "WindowGeometry.h"
#include "../support/TestHarness.h"

namespace
{
void TestWindowGeometry()
{
    Check(WindowGeometry::ScaleForDpi(100, 144) == 150, "DPI scaling uses the supplied DPI");
    Check(WindowGeometry::ScaleForDpi(100, 0) == 100, "DPI scaling defaults to 96 DPI");

    const RECT outer{0, 0, 100, 100};
    Check(WindowGeometry::ContainsRect(outer, RECT{20, 20, 80, 80}),
          "rectangle containment accepts an inner rectangle");
    Check(!WindowGeometry::ContainsRect(outer, RECT{-1, 20, 80, 80}),
          "rectangle containment rejects a rectangle outside the left edge");
}

void TestEmptyStateLayout()
{
    EmptyStateLayout::Input input{1000, 800, 96, FALSE, FALSE, 0, 0};
    const auto layout = EmptyStateLayout::Calculate(input);
    Check(layout.panel_width == 460 && layout.panel_height == 96, "empty-state panel uses normal dimensions");
    Check(layout.logo_width == 0 && layout.logo_height == 0, "empty-state layout omits an unavailable logo");
    Check(layout.buttons_top == 424, "empty-state buttons are vertically centered with two rows");

    input.logo_source_width = 100;
    input.logo_source_height = 50;
    const auto logo_layout = EmptyStateLayout::Calculate(input);
    Check(logo_layout.logo_width == 368 && logo_layout.logo_height == 184,
          "empty-state logo preserves its aspect ratio");
    Check(logo_layout.buttons_top == 522, "empty-state logo participates in vertical centering");

    input.no_images = TRUE;
    input.searching_subfolders = TRUE;
    const auto searching_layout = EmptyStateLayout::Calculate(input);
    Check(searching_layout.panel_height == 132, "no-images state uses the taller panel");
    Check(searching_layout.buttons_top == 560, "searching state uses one button row");
}

void TestOverlayText()
{
    Check(OverlayText::FormatByteSize(999) == L"999 B", "byte-size formatting preserves byte units");
    Check(OverlayText::FormatByteSize(1024) == L"1.0 KB", "byte-size formatting scales kilobytes");
    Check(OverlayText::FormatByteSize(104857600) == L"100 MB", "byte-size formatting rounds large values");
    Check(OverlayText::FormatPercent(1, 4) == L"25%", "percentage formatting uses whole percentages");
    Check(OverlayText::FormatPercent(1, 0) == L"0%", "percentage formatting handles an empty total");
    Check(OverlayText::BuildItemInfo(L"image.jpg", true, false, 0) == L"image.jpg",
          "ready image info omits loading progress");
    Check(OverlayText::BuildItemInfo(L"image.jpg", false, true, 42) == L"[42%] image.jpg",
          "loading image info includes progress");
    Check(OverlayText::BuildItemInfo(L"image.jpg", false, false, 42) == L"[0%] image.jpg",
          "non-loading image info uses zero progress");
}

void TestInfoOverlayProgressActions()
{
    const InfoOverlayProgressInput started{true, 10, L"image.jpg", 100, 50, false};
    const auto start_update = InfoOverlay::CalculateProgressUpdate({}, started);
    Check(start_update.actions.arm_progress_timer && !start_update.state.visible,
          "loading progress arms its timer before the debounce expires");

    auto elapsed = started;
    elapsed.current_tick = 150;
    const auto visible_update = InfoOverlay::CalculateProgressUpdate(start_update.state, elapsed);
    Check(visible_update.state.visible && visible_update.actions.invalidate_all,
          "loading progress requests invalidation when the debounce expires");

    InfoOverlayProgressInput stopped{};
    stopped.info_overlay_visible = true;
    const auto stopped_update = InfoOverlay::CalculateProgressUpdate(visible_update.state, stopped);
    Check(stopped_update.actions.cancel_progress_timer && stopped_update.actions.refresh_overlay &&
              stopped_update.actions.invalidate_all,
          "completed loading cancels its timer and refreshes visible overlay content");

    auto changed_path = elapsed;
    changed_path.path = L"next.jpg";
    changed_path.current_tick = 200;
    const auto path_update = InfoOverlay::CalculateProgressUpdate(visible_update.state, changed_path);
    Check(!path_update.state.visible && path_update.state.wait_start_tick == 200,
          "changing paths restarts the loading-progress debounce");
}

void TestDisplayPresentationDecisions()
{
    Check(DisplayPresenter::DecidePresentation(ImgItem::Status::Loading, false, false, true) ==
              DisplayPresentation::WaitingForImage,
          "first paint waits for a loading image");
    Check(DisplayPresenter::DecidePresentation(ImgItem::Status::Loading, false, false, false) ==
              DisplayPresentation::ImageNotReady,
          "later paints expose loading fallback presentation");
    Check(DisplayPresenter::DecidePresentation(ImgItem::Status::Error, false, true, true) ==
              DisplayPresentation::ImageError,
          "image errors take precedence over waiting state");
    Check(DisplayPresenter::DecidePresentation(ImgItem::Status::Ready, true, true, true) ==
              DisplayPresentation::ImageReady,
          "a published frame is ready even while slideshow waiting state clears");

    const RECT overlay{10, 10, 110, 60};
    Check(DisplayPresenter::ShouldDrawOnlyOverlay(true, ImgItem::Status::Ready, overlay, RECT{20, 20, 40, 40}),
          "a partial paint contained by the overlay redraws only the overlay");
    Check(!DisplayPresenter::ShouldDrawOnlyOverlay(true, ImgItem::Status::Ready, overlay, RECT{0, 0, 40, 40}),
          "a paint crossing the overlay boundary redraws full presentation");
}
} // namespace

void RunWindowPresentationTests()
{
    TestWindowGeometry();
    TestEmptyStateLayout();
    TestOverlayText();
    TestInfoOverlayProgressActions();
    TestDisplayPresentationDecisions();
}
