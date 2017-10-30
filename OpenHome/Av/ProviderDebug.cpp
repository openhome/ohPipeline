#include <OpenHome/Av/ProviderDebug.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Optional.h>
#include <OpenHome/Av/Logger.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Net;

ProviderDebug::ProviderDebug(DvDevice& aDevice, RingBufferLogger& aLogger, Optional<ILogPoster> aLogPoster, DebugManager& aDebugManager)
    : DvProviderAvOpenhomeOrgDebug1(aDevice)
    , iLogger(aLogger)
    , iLogPoster(aLogPoster)
    , iDebugManager(aDebugManager)
{
    EnableActionGetLog();
    EnableActionSendLog();
    EnableActionDebugTest();
}

void ProviderDebug::GetLog(IDvInvocation& aInvocation, IDvInvocationResponseString& aLog)
{
    aInvocation.StartResponse();
    iLogger.Read(aLog);
    aLog.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderDebug::SendLog(IDvInvocation& aInvocation, const Brx& aData)
{
    if (!iLogPoster.Ok()) {
        aInvocation.Error(801, Brn("Not supported"));
    }
    iLogPoster.Unwrap().SendLog(iLogger, aData);
    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderDebug::DebugTest(IDvInvocation& aInvocation, const Brx& aaDebugType, const Brx& aaDebugInput, IDvInvocationResponseString& aaDebugInfo, IDvInvocationResponseBool& aaDebugResult)
{
    aInvocation.StartResponse();

    WriterAscii writer(aaDebugInfo);

    if (aaDebugType == Brn("help")) {
        writer.Write(Brn("forceassert (input: none)"));
        writer.Write(Brn(" "));
        writer.WriteNewline(); // can't get this to work
        iDebugManager.Test(aaDebugType, aaDebugInput, writer);
        aaDebugInfo.WriteFlush();
        aaDebugResult.Write(true);
        aInvocation.EndResponse();
    }
    else {
        writer.Write(Brn("Complete"));
        aaDebugInfo.WriteFlush();
        aaDebugResult.Write(true);
        aInvocation.EndResponse();
        iDebugManager.Test(aaDebugType, aaDebugInput, writer);
    }

    // include this Debug afterwards so control point will not hang waiting for a response before the device asserts
    if (aaDebugType == Brn("forceassert")) {
        ASSERTS();
    }
}

void ProviderDebug::DebugDump(Net::IDvInvocation& aInvocation, const Brx& aaDebugType, Net::IDvInvocationResponseString& aaDebugInfo)
{
    aInvocation.StartResponse();
    WriterAscii writer(aaDebugInfo);
    iDebugManager.Dump(aaDebugType, writer);
    aaDebugInfo.WriteFlush();
    aInvocation.EndResponse();
}
