#include <OpenHome/Av/Scd/Sender/Demo/DirScanner.h>
#include <OpenHome/Av/Scd/Sender/Demo/WavSender.h>

#include <string>
#include <filesystem>

using namespace OpenHome;
using namespace OpenHome::Scd;
using namespace OpenHome::Scd::Sender;
using namespace OpenHome::Scd::Sender::Demo;

void DirScanner::Run(std::string& aPath, IScdSupply& aSupply)
{
    for (auto& p: std::experimental::filesystem::v1::directory_iterator(aPath)) {
        std::cout << p.path().filename() << std::endl;
        WavSender sender(p, aSupply);
        sender.Run();
    }
}
