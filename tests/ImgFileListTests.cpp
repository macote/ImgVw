#include "ImgFileList.h"
#include "ImgLoader.h"

#include <iostream>
#include <set>
#include <string>

namespace
{
int failures = 0;

void Check(bool condition, const char* description)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << description << '\n';
        ++failures;
    }
}

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
    Check(files.CurrentPath() == L"b.jpg", "first collected path remains current");

    Check(files.MoveToFirst(), "move to first succeeds");
    Check(files.CurrentPath() == L"a.jpg", "first path uses sorted order");
    Check(!files.MoveToPrevious(), "cannot move before first");
    Check(files.MoveToNext(), "move next succeeds");
    Check(files.CurrentPath() == L"b.jpg", "next path is selected");
    Check(files.MoveToLast(), "move to last succeeds");
    Check(files.CurrentPath() == L"c.jpg", "last path is selected");
    Check(!files.MoveToNext(), "cannot move after last");
    Check(files.MoveTo(L"a.jpg"), "move to known path succeeds");
    Check(!files.MoveTo(L"missing.jpg"), "move to unknown path fails");
    Check(files.CurrentPath() == L"a.jpg", "failed move preserves current path");
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

void TestLoaderShutdown()
{
    for (int iteration = 0; iteration < 10; ++iteration)
    {
        ImgLoader loader;
        loader.StopLoading();
        loader.StopLoading();
    }
}
} // namespace

int main()
{
    TestEmptyList();
    TestOrderedNavigation();
    TestRemoval();
    TestRandomNavigation();
    TestClear();
    TestLoaderShutdown();

    if (failures != 0)
    {
        std::cerr << failures << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "All tests passed.\n";
    return 0;
}
