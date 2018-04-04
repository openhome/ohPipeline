#include "WavSender.h"
#include <OpenHome/Defines.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/OptionParser.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Net/Core/OhNet.h>
#include <OpenHome/Media/Tests/Cdecl.h>
#include <OpenHome/Av/Scd/Sender/Demo/DirScanner.h>
#include <OpenHome/Av/Scd/ScdMsg.h>
#include <OpenHome/Av/Scd/Sender/ScdSupply.h>
#include <OpenHome/Av/Scd/Sender/ScdServer.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Net/Odp/CpDeviceOdp.h>
#include <OpenHome/Net/Core/FunctorCpDevice.h>
#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Debug-ohMediaPlayer.h>
#include <Generated/CpAvOpenhomeOrgTransport1.h>

#include <stdlib.h>
#include <filesystem>

using namespace OpenHome;
using namespace OpenHome::Net;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Scd;
using namespace OpenHome::Scd::Sender;
using namespace OpenHome::Scd::Sender::Demo;

class DummySupply : public IScdSupply
{
public:
    void OutputMetadataDidl(const std::string& /*aUri*/, const std::string& /*aMetadata*/) override {}
    void OutputMetadataOh(const OpenHomeMetadata& /*aMetadata*/) override {}
    void OutputFormat(TUint aBitDepth, TUint aSampleRate, TUint aNumChannels,
                      TUint /*aBitRate*/, TUint64 /*aSampleStart*/, TUint64 /*aSamplesTotal*/,
                      TBool /*aSeekable*/, TBool /*aLossless*/, TBool /*aLive*/,
                      TBool /*aBroadcastAllowed*/, const std::string& /*aCodecName*/)
    {
        printf("  Format: bitDepth=%u, sampleRate=%u, numChannels=%u\n", aBitDepth, aSampleRate, aNumChannels);
    }
    void OutputAudio(const TByte* /*aData*/, TUint /*aBytes*/) {}
    void OutputMetatextDidl(const std::string& /*aMetatext*/) {}
    void OutputMetatextOh(const OpenHomeMetadata& /*aMetatext*/) {}
    void OutputHalt() {}
};

class DeviceListHandler
{
public:
    DeviceListHandler(const Brx& aSelectedRoom, Endpoint aScdEndpoint);
    ~DeviceListHandler();
    void Added(CpDevice& aDevice);
    void Removed(CpDevice& aDevice);
private:
    void PrintDeviceInfo(const char* aPrologue, const CpDevice& aDevice);
    void PrintDeviceInfoLocked(const char* aPrologue, const CpDevice& aDevice);
private:
    Mutex iLock;
    Brn iSelectedRoom;
    Net::CpProxyAvOpenhomeOrgTransport1* iCpTransport;
    Endpoint iScdEndpoint;
};

static const Brn kScdModePrefix("uri=scd://");

DeviceListHandler::DeviceListHandler(const Brx& aSelectedRoom, Endpoint aScdEndpoint)
    : iLock("DLLM")
    , iSelectedRoom(aSelectedRoom)
    , iCpTransport(nullptr)
    , iScdEndpoint(aScdEndpoint)
{
}

DeviceListHandler::~DeviceListHandler()
{
    if (iCpTransport != nullptr) {
        delete iCpTransport;
    }
}

void DeviceListHandler::Added(CpDevice& aDevice)
{
    iLock.Wait();
    PrintDeviceInfoLocked("Added", aDevice);
    if (iSelectedRoom.Bytes() > 0) {
        Brh val;
        aDevice.GetAttribute("Odp.FriendlyName", val);
        if (val.Split(0, iSelectedRoom.Bytes()) == iSelectedRoom) {
            if (iCpTransport != nullptr) {
                delete iCpTransport;
            }
            iCpTransport = new CpProxyAvOpenhomeOrgTransport1(aDevice);
            Bwh mode(kScdModePrefix.Bytes() + Endpoint::kMaxEndpointBytes);
            mode.Replace(kScdModePrefix);
            iScdEndpoint.AppendEndpoint(mode);
            Log::Print("SCD play (%.*s) on %.*s\n", PBUF(mode), PBUF(iSelectedRoom));
            iCpTransport->SyncPlayAs(Brn("SCD"), mode);
        }
    }
    iLock.Signal();
}

void DeviceListHandler::Removed(CpDevice& aDevice)
{
    PrintDeviceInfo("Removed", aDevice);
}

void DeviceListHandler::PrintDeviceInfo(const char* aPrologue, const CpDevice& aDevice)
{
    iLock.Wait();
    PrintDeviceInfoLocked(aPrologue, aDevice);
    iLock.Signal();
}

void DeviceListHandler::PrintDeviceInfoLocked(const char* aPrologue, const CpDevice& aDevice)
{
    Brh name;
    aDevice.GetAttribute("Odp.FriendlyName", name);
    Brh loc;
    aDevice.GetAttribute("Odp.Location", loc);
    Print("ODP Device %s: UDN %.*s (%.*s, %.*s)\n", aPrologue, PBUF(aDevice.Udn()), PBUF(name), PBUF(loc));
}


int CDECL main(int aArgc, char* aArgv[])
{
#ifdef _WIN32
    char* noErrDlgs = getenv("NO_ERROR_DIALOGS");
    if (noErrDlgs != nullptr && strcmp(noErrDlgs, "1") == 0) {
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    }
#endif // _WIN32

    // Parse options.
    OptionParser parser;
    OptionUint optionAdapter("-a", "--adapter", 0, "[0...n] Adpater index to use");
    OptionString optionRoom("-r", "--room", Brn(""), "optionRoom to send SCD audio");
    OptionString optionDir("-d", "--dir", Brn("c:\\TestAudio\\CodecStress"), "Directory to search for WAV files");
    parser.AddOption(&optionDir);
    parser.AddOption(&optionAdapter);
    parser.AddOption(&optionRoom);
    if (!parser.Parse(aArgc, aArgv) || parser.HelpDisplayed()) {
        return 1;
    }

    auto initParams = Net::InitialisationParams::Create();
    initParams->SetDvEnableBonjour("WavSenderMain", true);
    auto lib = new Net::Library(initParams);
    auto subnetList = lib->CreateSubnetList();
    auto subnet = (*subnetList)[optionAdapter.Value()]->Subnet();
    Net::Library::DestroySubnetList(subnetList);
    auto cpStack = lib->StartCp(subnet);

    {
        Debug::AddLevel(Debug::kScd);
        Debug::AddLevel(Debug::kOdp);

        // app goes here
        ScdMsgFactory factory(1,   // Ready
                              0,   // MetadataDidl
                              5,   // MetadataOh,
                              5,   // Format,
                              5,   // FormatDsd,
                              100, // AudioOut,
                              0,   // AudioIn,
                              0,   // MetatextDidl,
                              5,   // MetatextOh,
                              1,   // Halt,
                              1,   // Disconnect,
                              0,   // Seek,
                              0    // Skip
                          );
        ScdSupply supply(factory);
        ScdServer server(lib->Env(), supply, factory);
        Endpoint::EndpointBuf buf;
        server.Endpoint().AppendEndpoint(buf);
        Log::Print("SCD Sender running on %s\n", buf.Ptr());
        std::string path(optionDir.CString());

        DeviceListHandler logger(optionRoom.Value(), server.Endpoint());
        FunctorCpDevice added = MakeFunctorCpDevice(logger, &DeviceListHandler::Added);
        FunctorCpDevice removed = MakeFunctorCpDevice(logger, &DeviceListHandler::Removed);
        CpDeviceListOdpAll* deviceList = new CpDeviceListOdpAll(*cpStack, added, removed);

        //DummySupply supply;
        DirScanner::Run(path, supply);

        delete deviceList;
    }

    delete lib;

    return 0;
}
