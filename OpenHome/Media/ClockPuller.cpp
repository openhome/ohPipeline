#include <OpenHome/Media/ClockPuller.h>

using namespace OpenHome;
using namespace OpenHome::Media;

// ClockPullerPipeline

ClockPullerPipeline::ClockPullerPipeline(IClockPuller& aClockPullerPipeline)
    : iPipeline(aClockPullerPipeline)
    , iMode(nullptr)
{
}

void ClockPullerPipeline::SetClockPullerMode(Optional<IClockPuller> aClockPuller)
{
    iMode = aClockPuller.Ptr();
}

void ClockPullerPipeline::Update(TInt aDelta)
{
    iPipeline.Update(aDelta);
    if (iMode != nullptr) {
        iMode->Update(aDelta);
    }
}

void ClockPullerPipeline::Start()
{
    iPipeline.Start();
    if (iMode != nullptr) {
        iMode->Start();
    }
}

void ClockPullerPipeline::Stop()
{
    iPipeline.Stop();
    if (iMode != nullptr) {
        iMode->Stop();
    }
}


// ClockPullerMock

void ClockPullerMock::Update(TInt /*aDelta*/) {}
void ClockPullerMock::Start() {}
void ClockPullerMock::Stop() {}
