#include "EmptyStateLayout.h"
#include "BrowseWindowState.h"
#include "DisplayPresenter.h"
#include "DisplaySession.h"
#include "CursorController.h"
#include "InfoOverlay.h"
#include "InfoOverlayStatsBuilder.h"
#include "InfoOverlayVisibility.h"
#include "MonitorPlacement.h"
#include "MultiMonitorSlideShowCoordinator.h"
#include "SlideShowStateMachine.h"
#include "OverlayText.h"
#include "WindowGeometry.h"
#include "WindowDragController.h"
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

void TestDisplaySession()
{
    DisplaySession session;
    Check(session.first_paint() && session.restore_path().empty(), "display session starts with no selected image");

    session.Select(L"first.jpg");
    Check(session.restore_path() == L"first.jpg", "display session restores the selection before its first paint");
    session.MarkPainted(L"first.jpg");
    session.CompleteFirstPaint();
    session.Select(L"second.jpg");
    Check(!session.first_paint() && session.selected_path() == L"second.jpg" && session.restore_path() == L"first.jpg",
          "display session preserves the last painted image while a newer selection loads");

    session.MarkPainted(L"second.jpg");
    Check(session.restore_path() == L"second.jpg", "display session advances its restore path after painting");
    session.Clear();
    Check(session.selected_path().empty() && session.painted_path().empty(),
          "display session clears selected and painted paths together");
}

void TestBrowseWindowState()
{
    BrowseWindowState state(L"first");
    Check(state.path() == L"first" && !state.browser_initialized() && !state.CanStartSubfolderSearch(),
          "browse window state retains its startup path without assuming browser initialization");

    state.MarkBrowserInitialized();
    Check(state.CanStartSubfolderSearch(), "initialized browse state permits its first recursive search");
    state.EnableSubfolderSearch();
    Check(state.browse_subfolders() && !state.CanStartSubfolderSearch(),
          "browse window state prevents duplicate recursive searches");

    state.OpenPath(L"second");
    Check(state.path() == L"second" && state.browser_initialized() && !state.browse_subfolders() &&
              state.CanStartSubfolderSearch(),
          "opening another path preserves browser initialization and resets recursive browsing");
}

void TestWindowDragController()
{
    WindowDragController controller;
    Check(controller.Begin(POINT{-1500, 100}, RECT{-1600, 0, -600, 800}),
          "window drag begins on a negative-coordinate monitor");
    const auto position = controller.CalculatePosition(POINT{-1400, 250});
    Check(position.x == -1500 && position.y == 150, "window drag preserves the pointer offset across monitors");
    Check(controller.End() && !controller.active(), "window drag ends deterministically");
    Check(!controller.Begin(POINT{}, RECT{10, 10, 10, 20}), "window drag rejects an empty window rectangle");
}

void TestCursorController()
{
    CursorController controller;
    Check(controller.SetVisible(false) == CursorVisibilityAction::Hide,
          "cursor controller requests its initial hidden state");
    Check(controller.SetVisible(false) == CursorVisibilityAction::None,
          "cursor controller does not unbalance repeated hide requests");
    Check(controller.SetCaptured(true) && !controller.SetCaptured(true),
          "cursor controller changes capture state only once");

    controller.OnMouseMove(POINTS{10, 10}, 100);
    const auto moved = controller.OnMouseMove(POINTS{11, 10}, 100);
    Check(moved.visibility == CursorVisibilityAction::Show && moved.arm_idle_timer,
          "mouse activity shows the cursor and arms idle tracking");
    controller.SetVisible(false);
    const auto moved_while_armed = controller.OnMouseMove(POINTS{12, 10}, 125);
    Check(moved_while_armed.visibility == CursorVisibilityAction::Show && !moved_while_armed.arm_idle_timer,
          "mouse activity restores a cursor hidden while idle tracking remains armed");
    const auto early = controller.OnIdleTimer(175, 1000, 100);
    Check(early.arm_idle_timer && early.idle_timer_delay == 50, "cursor idle policy preserves the remaining timeout");
    const auto idle = controller.OnIdleTimer(225, 1000, 100);
    Check(idle.visibility == CursorVisibilityAction::Hide && idle.cancel_idle_timer,
          "cursor idle policy hides once after the timeout");

    Check(controller.SetAutoHideEnabled(false) == CursorVisibilityAction::Show,
          "disabling auto-hide restores a hidden cursor");
    Check(controller.SetVisible(false) == CursorVisibilityAction::None,
          "disabled auto-hide rejects cursor hide requests");
    const auto moved_while_disabled = controller.OnMouseMove(POINTS{13, 10}, 250);
    Check(moved_while_disabled.visibility == CursorVisibilityAction::None && !moved_while_disabled.arm_idle_timer,
          "disabled auto-hide leaves idle tracking disarmed");
    Check(controller.SetAutoHideEnabled(true) == CursorVisibilityAction::None,
          "enabling auto-hide preserves the visible cursor");
    const auto moved_after_enabling = controller.OnMouseMove(POINTS{14, 10}, 275);
    Check(moved_after_enabling.arm_idle_timer, "mouse activity rearms idle tracking after auto-hide is enabled");
}

void TestMonitorPlacement()
{
    const auto bounds = MonitorPlacement::FromRectangle(RECT{-1920, -200, 0, 880});
    Check(bounds.valid && bounds.x == -1920 && bounds.y == -200 && bounds.width == 1920 && bounds.height == 1080,
          "monitor placement preserves negative-coordinate bounds");
    Check(!MonitorPlacement::FromRectangle(RECT{0, 0, 0, 100}).valid, "monitor placement rejects empty bounds");

    MonitorPlacement placement;
    const auto first_monitor = reinterpret_cast<HMONITOR>(static_cast<ULONG_PTR>(1));
    const auto second_monitor = reinterpret_cast<HMONITOR>(static_cast<ULONG_PTR>(2));
    placement.SetCurrent(first_monitor);
    Check(!placement.OnMonitorChanged(first_monitor, false).changed,
          "monitor placement ignores duplicate monitor notifications");
    const auto dragging_transition = placement.OnMonitorChanged(second_monitor, true);
    Check(dragging_transition.changed && !dragging_transition.apply_bounds,
          "monitor placement defers bounds while dragging");
}

void TestSlideShowStateMachine()
{
    SlideShowStateMachine slideshow(1750, 125, 10000, 125);
    Check(slideshow.Start(SlideShowMode::Sequential) && slideshow.OnTimer() == SlideShowNavigation::Sequential,
          "sequential slideshow requests sequential navigation");
    slideshow.OnDisplaySelection(true, false);
    Check(slideshow.waiting_for_image() && slideshow.OnTimer() == SlideShowNavigation::None,
          "slideshow pauses navigation while an image loads");
    Check(slideshow.OnImageReady() && slideshow.OnTimer() == SlideShowNavigation::Sequential,
          "ready notification resumes slideshow navigation");

    slideshow.Stop();
    slideshow.Start(SlideShowMode::Random);
    slideshow.OnDisplaySelection(false, false);
    Check(slideshow.needs_initial_advance(), "empty random selection requests an initial advance");
    Check(slideshow.IncreaseSpeed() && slideshow.interval() == 1625, "slideshow speed increase reduces the interval");
    slideshow.SetInterval(125);
    Check(!slideshow.IncreaseSpeed() && slideshow.interval() == 125, "slideshow interval respects its minimum");
    slideshow.SetInterval(10000);
    Check(!slideshow.DecreaseSpeed() && slideshow.interval() == 10000, "slideshow interval respects its maximum");
    Check(slideshow.Stop() && !slideshow.waiting_for_image() && !slideshow.needs_initial_advance(),
          "stopping slideshow clears transient state");
}

void TestMultiMonitorSlideShowCoordinator()
{
    MultiMonitorSlideShowCoordinator coordinator;
    const MultiMonitorWindowId owner = 1;
    const MultiMonitorWindowId first_secondary = 2;
    const MultiMonitorWindowId second_secondary = 3;

    Check(!coordinator.RegisterSecondaryWindow(0), "multi-monitor slideshow rejects an empty registration");
    Check(coordinator.RegisterSecondaryWindow(first_secondary) &&
              coordinator.RegisterSecondaryWindow(second_secondary) &&
              !coordinator.RegisterSecondaryWindow(first_secondary),
          "multi-monitor slideshow keeps unique stable registrations");

    coordinator.Start();
    Check(coordinator.running() && coordinator.NextTarget(owner) == owner &&
              coordinator.NextTarget(owner) == first_secondary && coordinator.NextTarget(owner) == second_secondary &&
              coordinator.NextTarget(owner) == owner,
          "multi-monitor slideshow advances targets in a deterministic cycle");

    Check(coordinator.UnregisterSecondaryWindow(first_secondary) &&
              !coordinator.UnregisterSecondaryWindow(first_secondary) && coordinator.target_count() == 2,
          "destroyed secondary windows are explicitly unregistered");
    Check(coordinator.NextTarget(owner) == second_secondary && coordinator.NextTarget(owner) == owner,
          "target rotation remains deterministic after a secondary window closes");

    coordinator.SetSequentialCursorPath(L"next.jpg");
    coordinator.SetPreloadedPathCount(42);
    coordinator.Stop();
    Check(!coordinator.running() && coordinator.sequential_cursor_path().empty() &&
              coordinator.preloaded_path_count() == 0,
          "stopping multi-monitor slideshow clears transient navigation state");

    const auto released = coordinator.ReleaseSecondaryWindows();
    Check(released.size() == 1 && released.front() == second_secondary && coordinator.target_count() == 1,
          "owner teardown releases every secondary registration before destruction");
}

void TestInfoOverlayStatsBuilder()
{
    ImgBrowserStats shared_stats;
    shared_stats.found_images = 12;

    ImgBrowserStats first_target;
    first_target.targetwidth = 800;
    first_target.targetheight = 600;
    first_target.loader.queued = 2;
    first_target.sizes.push_back({800, 600, 3, 1, 5, 1, 4096});

    auto duplicate_target = first_target;
    duplicate_target.loader.queued = 99;

    ImgBrowserStats second_target;
    second_target.targetwidth = 1920;
    second_target.targetheight = 1080;
    second_target.loader.free_slots = 4;

    InfoOverlayStatsContext context;
    context.has_free_bytes = true;
    context.free_bytes = 8192;
    context.slideshow_running = true;
    context.random_slideshow = true;
    context.cycle = {3, 12};
    context.current_item_text = L"photo.jpg";

    const auto snapshot =
        InfoOverlayStatsBuilder::Build(shared_stats, {first_target, duplicate_target, second_target}, context);
    Check(snapshot.found_images == 12 && snapshot.targets.size() == 2,
          "overlay stats aggregation preserves shared totals and deduplicates target sizes");
    Check(snapshot.targets[0].queued == 3 && snapshot.targets[0].loader_queued == 2 &&
              snapshot.targets[0].used_bytes == 4096,
          "overlay stats aggregation selects cache and loader data for the active target size");
    Check(snapshot.targets[1].width == 1920 && snapshot.targets[1].free_slots == 4,
          "overlay stats aggregation retains targets without a cache-size entry");
    Check(snapshot.has_free_bytes && snapshot.free_bytes == 8192 && snapshot.slideshow_running &&
              snapshot.random_slideshow && snapshot.cycle_position == 3 && snapshot.cycle_total == 12 &&
              snapshot.current_item_text == L"photo.jpg",
          "overlay stats aggregation copies presentation context");
}

void TestInfoOverlayVisibility()
{
    InfoOverlayVisibility visibility;
    Check(visibility.ToggleFilename(false) && visibility.filename_visible(false),
          "filename overlay can be enabled without the stats chord");
    Check(!visibility.filename_visible(true), "loader stats temporarily suppress the filename overlay");
    visibility.OnStatsRequested(true);
    Check(!visibility.filename_visible(false), "requesting loader stats disables the persistent filename overlay");
    Check(!visibility.ToggleFilename(true), "filename overlay cannot be enabled while loader stats are requested");
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
    TestDisplaySession();
    TestBrowseWindowState();
    TestWindowDragController();
    TestCursorController();
    TestMonitorPlacement();
    TestSlideShowStateMachine();
    TestMultiMonitorSlideShowCoordinator();
    TestInfoOverlayStatsBuilder();
    TestInfoOverlayVisibility();
    TestEmptyStateLayout();
    TestOverlayText();
    TestInfoOverlayProgressActions();
    TestDisplayPresentationDecisions();
}
