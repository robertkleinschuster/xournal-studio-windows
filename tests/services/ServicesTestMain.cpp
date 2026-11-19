// The services' tests run on a thread with a single-threaded apartment, like the app's UI thread: IFileOperation
// requires one, Windows.Data.Json needs an apartment.

#include <windows.h>
#include <objbase.h>

#include <gtest/gtest.h>

#include <cstdio>

int main(int argc, char** argv)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr))
    {
        std::fprintf(stderr, "CoInitializeEx: 0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }
    testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    CoUninitialize();
    return result;
}
