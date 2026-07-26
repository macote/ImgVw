#include "ImgFileList.h"
#include "../support/TestHarness.h"

#include <set>
#include <string>
#include <vector>

namespace
{
void TestEmptyList()
{
    ImgFileList files(1);

    Check(files.Empty(), "new list is empty");
    Check(files.CurrentPath().empty(), "empty list has no current path");
    Check(!files.MoveToNext(), "empty list cannot move next");
    Check(!files.MoveToPrevious(), "empty list cannot move previous");
    Check(!files.MoveToFirst(), "empty list cannot move first");
    Check(!files.MoveToLast(), "empty list cannot move last");
    Check(!files.MoveToRandom(), "empty list cannot move randomly");
    Check(!files.RemoveCurrent(), "empty list cannot remove current item");
}

void TestOrderedNavigation()
{
    ImgFileList files(1);

    Check(files.Add(L"b.jpg"), "first path is added");
    Check(files.Add(L"a.jpg"), "second path is added");
    Check(files.Add(L"c.jpg"), "third path is added");
    Check(!files.Add(L"b.jpg"), "duplicate path is rejected");
    Check(files.CurrentPath() == L"a.jpg", "unmoved list uses sorted first path as current");

    Check(!files.MoveToFirst(), "cannot move to first while already on first");
    Check(files.CurrentPath() == L"a.jpg", "first path uses sorted order");
    Check(!files.MoveToPrevious(), "cannot move before first");
    Check(files.MoveToNext(), "move next succeeds");
    Check(files.CurrentPath() == L"b.jpg", "next path is selected");
    auto progress = files.GetSequentialProgress();
    Check(progress.position == 2 && progress.total == 3, "sequential progress follows sorted navigation");
    progress = files.GetSequentialProgress(L"c.jpg");
    Check(progress.position == 3 && progress.total == 3, "sequential progress can follow a shared cursor path");
    Check(files.MoveToLast(), "move to last succeeds");
    Check(files.CurrentPath() == L"c.jpg", "last path is selected");
    progress = files.GetSequentialProgress();
    Check(progress.position == 3 && progress.total == 3, "sequential progress reaches the final file");
    Check(!files.MoveToNext(), "cannot move after last");
    Check(files.MoveTo(L"a.jpg"), "move to known path succeeds");
    Check(!files.MoveTo(L"missing.jpg"), "move to unknown path fails");
    Check(files.CurrentPath() == L"a.jpg", "failed move preserves current path");
}

void TestFolderGroupedNavigation()
{
    ImgFileList files(1);

    files.Add(L"C:\\images\\example.jpg");
    files.Add(L"C:\\images\\W900\\b.jpg");
    files.Add(L"C:\\images\\IMG1.jpg");
    files.Add(L"C:\\images\\W900\\a.jpg");

    Check(files.CurrentPath() == L"C:\\images\\example.jpg", "root folder sorted first path is current");
    Check(files.MoveToNext(), "move to second root file succeeds");
    Check(files.CurrentPath() == L"C:\\images\\IMG1.jpg", "root files stay together before subfolder files");
    Check(files.MoveToNext(), "move to first subfolder file succeeds");
    Check(files.CurrentPath() == L"C:\\images\\W900\\a.jpg", "subfolder files sort after root files");
    Check(files.MoveToNext(), "move to second subfolder file succeeds");
    Check(files.CurrentPath() == L"C:\\images\\W900\\b.jpg", "subfolder files sort by filename");
}

void TestExplicitNavigationPinsCurrentPath()
{
    ImgFileList files(1);

    files.Add(L"b.jpg");
    files.Add(L"c.jpg");
    Check(files.MoveToLast(), "move to selected item succeeds");
    Check(files.CurrentPath() == L"c.jpg", "selected item is current before later insert");

    files.Add(L"a.jpg");
    Check(files.CurrentPath() == L"c.jpg", "later sorted insert does not replace selected current item");
}

void TestDirectFileSelectionPinsCurrentPath()
{
    ImgFileList files(1);

    files.Add(L"b.jpg");
    Check(files.MoveTo(L"b.jpg"), "directly selected file is selected");
    files.Add(L"a.jpg");

    Check(files.CurrentPath() == L"b.jpg", "later sorted insert does not replace directly selected file");
}

void TestPathsFromCurrent()
{
    ImgFileList files(1);
    files.Add(L"b.jpg");
    files.Add(L"a.jpg");
    files.Add(L"d.jpg");
    files.Add(L"c.jpg");

    Check(files.MoveTo(L"c.jpg"), "move to queue seed start succeeds");
    const auto paths = files.PathsFromCurrent();
    const std::vector<std::wstring> expected = {L"c.jpg", L"d.jpg", L"a.jpg", L"b.jpg"};
    Check(paths == expected, "paths from current start at current item and wrap in browse order");
}

void TestRemoval()
{
    ImgFileList files(1);
    files.Add(L"a.jpg");
    files.Add(L"b.jpg");
    files.Add(L"c.jpg");

    Check(files.MoveTo(L"b.jpg"), "select middle item before removal");
    Check(files.RemoveCurrent(), "remove middle item succeeds");
    Check(files.CurrentPath() == L"c.jpg", "removal selects following item");
    Check(files.RemoveCurrent(), "remove last item succeeds");
    Check(files.CurrentPath().empty(), "removing last item parks at end");
    Check(files.MoveToPrevious(), "can move to previous item after removing last");
    Check(files.CurrentPath() == L"a.jpg", "previous item remains available");
    Check(files.RemoveCurrent(), "remove final item succeeds");
    Check(files.Empty(), "list is empty after removing all items");
}

void TestRandomNavigation()
{
    ImgFileList files(7);
    const std::set<std::wstring> expected = {L"a.jpg", L"b.jpg", L"c.jpg"};
    files.Add(L"a.jpg");
    files.Add(L"b.jpg");
    files.Add(L"c.jpg");

    std::set<std::wstring> firstcycle;
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        Check(files.MoveToRandom(), "random move succeeds");
        firstcycle.insert(files.CurrentPath());
    }

    Check(firstcycle == expected, "random cycle visits every item once");
    const auto last = files.CurrentPath();
    Check(files.MoveToRandom(), "next random cycle starts");
    Check(files.CurrentPath() != last, "random cycles do not repeat the boundary item");
}

void TestRandomNavigationInsertsNewFilesIntoCurrentCycle()
{
    ImgFileList files(7);
    files.Add(L"a.jpg");
    files.Add(L"b.jpg");
    files.Add(L"c.jpg");
    Check(files.MoveTo(L"a.jpg"), "select current file before random insertion test");
    files.BeginRandomCycle();
    files.Add(L"d.jpg");
    files.Add(L"e.jpg");

    const std::set<std::wstring> expected = {L"b.jpg", L"c.jpg", L"d.jpg", L"e.jpg"};
    std::set<std::wstring> remaining;
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        Check(files.MoveToRandom(), "new file is available in the current random cycle");
        Check(files.CurrentPath() != L"a.jpg", "random insertion does not reset consumed paths");
        remaining.insert(files.CurrentPath());
    }

    Check(remaining == expected, "new files are randomly inserted without repeating the active cycle");
}

void TestRandomNavigationReportsCycleProgress()
{
    ImgFileList files(7);
    files.Add(L"a.jpg");
    files.Add(L"b.jpg");
    files.Add(L"c.jpg");
    Check(files.MoveTo(L"a.jpg"), "select current file before random progress test");
    files.BeginRandomCycle();

    auto progress = files.GetRandomProgress();
    Check(progress.position == 1 && progress.total == 3, "random cycle counts the displayed file as consumed");

    Check(files.MoveToRandom(), "advance random cycle before progress update");
    progress = files.GetRandomProgress();
    Check(progress.position == 2 && progress.total == 3, "random cycle progress advances with navigation");

    files.Add(L"d.jpg");
    progress = files.GetRandomProgress();
    Check(progress.position == 2 && progress.total == 4, "random cycle progress includes inserted files");
}

void TestBeginningRandomCycleConsumesCurrentFile()
{
    ImgFileList files(7);
    files.Add(L"a.jpg");
    files.Add(L"b.jpg");
    files.Add(L"c.jpg");
    Check(files.MoveTo(L"b.jpg"), "select current file before beginning random cycle");

    files.BeginRandomCycle();

    std::set<std::wstring> remaining;
    for (std::size_t index = 1; index < files.Size(); ++index)
    {
        Check(files.MoveToRandom(), "fresh random cycle advances past current file");
        Check(files.CurrentPath() != L"b.jpg", "fresh random cycle does not redisplay current file");
        remaining.insert(files.CurrentPath());
    }

    const std::set<std::wstring> expected = {L"a.jpg", L"c.jpg"};
    Check(remaining == expected, "fresh random cycle retains every other file");
}

void TestRandomNavigationPreservesCycleAfterRemoval()
{
    ImgFileList files(7);
    files.Add(L"a.jpg");
    files.Add(L"b.jpg");
    files.Add(L"c.jpg");
    files.Add(L"d.jpg");
    Check(files.MoveTo(L"a.jpg"), "select current file before removal test");
    files.BeginRandomCycle();

    Check(files.MoveToRandom(), "random cycle advances before removal");
    const auto removed = files.CurrentPath();
    Check(files.RemoveCurrent(), "remove consumed random item");

    std::set<std::wstring> remaining;
    for (std::size_t index = 0; index < 2; ++index)
    {
        Check(files.MoveToRandom(), "random cycle continues after removal");
        Check(files.CurrentPath() != L"a.jpg", "consumed current item is not reshuffled after removal");
        Check(files.CurrentPath() != removed, "removed item does not reappear");
        remaining.insert(files.CurrentPath());
    }

    Check(remaining.size() == 2, "remaining random cycle does not repeat an item after removal");
}

void TestRandomNavigationKeepsInsertedFileAfterActiveRemoval()
{
    ImgFileList files(7);
    files.Add(L"a.jpg");
    files.BeginRandomCycle();
    files.Add(L"b.jpg");

    Check(files.RemoveCurrent(), "remove the only active random-cycle item");
    Check(files.MoveToRandom(), "inserted item continues the active random cycle");
    Check(files.CurrentPath() == L"b.jpg", "inserted item remains available after consumed cycle becomes empty");
}

void TestRandomNavigationDoesNotConsumeExcludedFiles()
{
    ImgFileList files(7);
    files.Add(L"a.jpg");
    files.Add(L"b.jpg");
    files.Add(L"c.jpg");
    files.Add(L"d.jpg");
    Check(files.MoveTo(L"a.jpg"), "select current file before exclusion test");
    files.BeginRandomCycle();

    Check(files.MoveToRandomExcluding({L"b.jpg", L"c.jpg"}), "random navigation finds the only eligible file");
    Check(files.CurrentPath() == L"d.jpg", "random navigation skips excluded files");
    Check(!files.MoveToRandomExcluding({L"b.jpg", L"c.jpg", L"d.jpg"}),
          "random navigation holds when no file is eligible");

    Check(files.MoveToRandomExcluding({L"c.jpg", L"d.jpg"}), "previously excluded file remains eligible");
    Check(files.CurrentPath() == L"b.jpg", "rejected random candidate was not consumed");
    Check(files.MoveToRandomExcluding({L"b.jpg", L"d.jpg"}), "last excluded file remains eligible");
    Check(files.CurrentPath() == L"c.jpg", "all unconsumed candidates remain available");
}

void TestClear()
{
    ImgFileList files(1);
    files.Add(L"a.jpg");
    files.Add(L"b.jpg");
    files.MoveToRandom();

    files.Clear();

    Check(files.Empty(), "clear removes all items");
    Check(files.CurrentPath().empty(), "clear resets current path");
    Check(!files.MoveToRandom(), "clear resets random navigation");
}
} // namespace

void RunImgFileListTests()
{
    TestEmptyList();
    TestOrderedNavigation();
    TestFolderGroupedNavigation();
    TestExplicitNavigationPinsCurrentPath();
    TestDirectFileSelectionPinsCurrentPath();
    TestPathsFromCurrent();
    TestRemoval();
    TestRandomNavigation();
    TestRandomNavigationInsertsNewFilesIntoCurrentCycle();
    TestRandomNavigationReportsCycleProgress();
    TestBeginningRandomCycleConsumesCurrentFile();
    TestRandomNavigationPreservesCycleAfterRemoval();
    TestRandomNavigationKeepsInsertedFileAfterActiveRemoval();
    TestRandomNavigationDoesNotConsumeExcludedFiles();
    TestClear();
}
