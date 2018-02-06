#include <OpenHome/Av/Scd/Sender/Demo/DirScanner.h>
#include <OpenHome/Av/Scd/Sender/Demo/WavSender.h>

#include <string>
#include <experimental/filesystem>

using namespace OpenHome;
using namespace OpenHome::Scd;
using namespace OpenHome::Scd::Sender;
using namespace OpenHome::Scd::Sender::Demo;

using namespace std;

void DirScanner::Run(string& aPath, IScdSupply& aSupply)
{
    experimental::filesystem::path path(aPath);
    for (auto& p: experimental::filesystem::v1::directory_iterator(path)) {
        auto filename = p.path().string();
        cout << filename << endl;
        WavSender sender(filename, aSupply);
        sender.Run();
    }
}
