#include <OpenHome/Private/Standard.h>
#include <OpenHome/Av/Scd/Sender/Demo/DirScanner.h>
#include <OpenHome/Av/Scd/Sender/Demo/WavSender.h>

#include <string>

// VS19 depreciates the experimental filesystem header and causes a warning. Warnings as errors causes this to fail the build
// VS build codes from here: https://learn.microsoft.com/en-us/cpp/preprocessor/predefined-macros
#if defined(_MSC_VER) && _MSC_VER >= 1920
#include <filesystem>
#if _HAS_CXX17  // VS19 also enforces that C++ 17 or later MUST be used in order to access the filesystem header
#define BUILD_DIR_SCANNER
#endif // _HAS_CXX17
#else
#include <experimental/filesystem>
#define BUILD_DIR_SCANNER
using namespace std::experimental;
#endif

using namespace OpenHome;
using namespace OpenHome::Scd;
using namespace OpenHome::Scd::Sender;
using namespace OpenHome::Scd::Sender::Demo;

using namespace std;

void DirScanner::Run(string& aPath, IScdSupply& aSupply)
{
#if !defined(BUILD_DIR_SCANNER)
    (void)aPath;
    (void)aSupply;

    ASSERT_VA(false, "%s", "Use of std::filesystem with MSVC requires C++ 17\n");
#else
     filesystem::path path(aPath);
    for (auto& p: filesystem::directory_iterator(path)) {
        auto filename = p.path().string();
        cout << filename << endl;
        WavSender sender(filename, aSupply);
        sender.Run();
    }
#endif // defined(BUILD_DIR_SCANNER)
}
