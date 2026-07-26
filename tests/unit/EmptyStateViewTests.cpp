#include "EmptyStateView.h"
#include "../support/TestHarness.h"

namespace
{
void TestEmptyStateViewState()
{
    EmptyStateView view;
    Check(!view.IsVisible(), "empty-state view starts hidden");

    view.Show(L"No image", false, false);
    Check(view.IsVisible() && !view.IsNoImages() && view.message() == L"No image",
          "empty-state view tracks an initial empty message");

    view.Show(L"No supported images", true, true);
    Check(view.IsNoImages() && view.message() == L"No supported images",
          "empty-state view tracks the no-images message");

    view.ShowSearchingSubfolders();
    Check(view.IsSearchingSubfolders() && view.message() == L"Searching subfolders for supported images...",
          "empty-state view exposes the searching state");

    view.Hide();
    Check(!view.IsVisible() && view.message().empty(), "hiding empty-state view clears its message");
}
} // namespace

void RunEmptyStateViewTests()
{
    TestEmptyStateViewState();
}
