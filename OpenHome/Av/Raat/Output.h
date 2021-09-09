#pragma once

#include <raat_plugin_output.h>
#include <rc_status.h>
#include <rc_allocator.h>
#include <jansson.h>
#include <raat_stream.h>

namespace OpenHome {
namespace Media {
    class PipelineManager;
}
namespace Av {

class RaatOutput
{
public:
    RaatOutput(Media::PipelineManager& aPipeline);
    ~RaatOutput();
    RAAT__OutputPlugin* OutputPlugin();
    RC__Status GetInfo(json_t** aInfo);
    RC__Status GetSupportedFormats(RC__Allocator* aAlloc, size_t* aNumFormats, RAAT__StreamFormat** aFormats);
    void SetupStream(
        RAAT__StreamFormat* aFormat,
        RAAT__OutputSetupCallback aCbSetup, void* aCbSetupData,
        RAAT__OutputLostCallback aCbLost, void* aCbLostData);
    RC__Status TeardownStream(int aToken);
    RC__Status StartStream(int aToken, int64_t aWallTime, int64_t aStreamTime, RAAT__Stream* aStream);
    RC__Status GetLocalTime(int aToken, int64_t* aTime);
    RC__Status SetRemoteTime(int aToken, int64_t aClockOffset, bool aNewSource);
    RC__Status TryStop(int aToken);
    RC__Status Stop();
private:
    void AddFormatPcm(RAAT__StreamFormat** aFormats, int aSampleRate);
    void AddFormatPcm(RAAT__StreamFormat** aFormats, int aSampleRate, int aBitDepth);
private:
    Media::PipelineManager& iPipeline;
    RAAT__OutputPlugin* iPlugin;
};

}
}