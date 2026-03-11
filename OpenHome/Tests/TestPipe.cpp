#include <OpenHome/Tests/TestPipe.h>
#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Fifo.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Debug.h>

using namespace OpenHome;
using namespace OpenHome::Test;


// TestPipeDynamic

TestPipeDynamic::TestPipeDynamic(TUint aSlots)
    : iFifo(aSlots)
    , iLock("TPDL")
{
}

TestPipeDynamic::~TestPipeDynamic()
{
    AutoMutex a(iLock);
    while (iFifo.SlotsUsed() > 0) {
        Bwh* buf = iFifo.Read();
        delete buf;
    }
}

TBool TestPipeDynamic::Expect(const Brx& aMessage)
{
    AutoMutex a(iLock);
    if (iFifo.SlotsUsed() > 0) {
        Bwh* buf = iFifo.Read();
        const TBool match = (*buf == aMessage);
        if (!match) {
            LOG(kEssential, "TestPipeDynamic::Expect ERROR. expected: ");
            LOG(kEssential, aMessage);
            LOG(kEssential, " got: ");
            LOG(kEssential, *buf);
            LOG(kEssential, "\n");
        }
        delete buf;
        return match;
    }
    LOG(kEssential, "TestPipeDynamic::Expect ERROR. msg list empty; expected: ");
    LOG(kEssential, aMessage);
    LOG(kEssential, "\n");
    return false;
}

TBool TestPipeDynamic::ExpectEmpty()
{
    AutoMutex a(iLock);
    const TUint msgs = iFifo.SlotsUsed();
    if (msgs > 0) {
        LOG(kEssential, "TestPipeDynamic::ExpectEmpty ERROR. %u msgs remaining:\n", msgs);
        for (TUint i=0; i<iFifo.SlotsUsed(); i++) {
            Bwh* buf = iFifo.Read();
            LOG(kEssential, "\t");
            LOG(kEssential, *buf);
            LOG(kEssential, "\n");
            iFifo.Write(buf);
        }
        return false;
    }
    return true;
}

void TestPipeDynamic::Print()
{
    AutoMutex a(iLock);
    const TUint slots = iFifo.SlotsUsed();
    LOG(kEssential, "\nTestPipeDynamic::Print\n");
    LOG(kEssential, "[\n");
    for (TUint i=0; i<slots; i++) {
        Bwh* buf = iFifo.Read();
        LOG(kEssential, "\t");
        LOG(kEssential, *buf);
        LOG(kEssential, "\n");
        iFifo.Write(buf);
    }
    LOG(kEssential, "]\n");
}

void TestPipeDynamic::Write(const Brx& aMessage)
{
    AutoMutex a(iLock);
    Bwh* buf = new Bwh(aMessage);
    iFifo.Write(buf);
}
