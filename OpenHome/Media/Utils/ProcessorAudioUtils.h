#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Pipeline/Msg.h>

namespace OpenHome {
namespace Media {

class ProcessorPcmBufTest : public IPcmProcessor // Reads packed data into dynamically allocated buffer.
                                                 // Suitable for test code only.
{
    static const TUint kBufferGranularity = DecodedAudio::kMaxBytes;
public:
    ProcessorPcmBufTest();
    Brn Buf() const;
    const TByte* Ptr() const;
protected:
    void CheckSize(TUint aAdditionalBytes);
    void ProcessFragment(const Brx& aData);
private: // from IPcmProcessor
    void BeginBlock() override;
    void ProcessFragment(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes) override;
    void ProcessSilence(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes) override;
    void EndBlock() override;
    void Flush() override;
protected:
    Bwh iBuf;
};

class ProcessorDsdBufTest : public IDsdProcessor // Reads packed data into dynamically allocated buffer.
                                                 // Suitable for test code only.
{
    static const TUint kBufferGranularity = DecodedAudio::kMaxBytes;
public:
    ProcessorDsdBufTest();
    Brn Buf() const;
    const TByte* Ptr() const;
protected:
    void CheckSize(TUint aAdditionalBytes);
private: // from IPcmProcessor
    void BeginBlock() override;
    void ProcessFragment(const Brx& aData, TUint aNumChannels, TUint aSampleBlockBits) override;
    void EndBlock() override;
    void Flush() override;
protected:
    Bwh iBuf;
};

} // namespace Media
} // namespace OpenHome

