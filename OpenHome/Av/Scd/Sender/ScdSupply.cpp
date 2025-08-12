#include <OpenHome/Av/Scd/Sender/ScdSupply.h>
#include <OpenHome/Av/Scd/ScdMsg.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>

#include <algorithm>
#include <string>

using namespace OpenHome;
using namespace OpenHome::Scd;
using namespace OpenHome::Scd::Sender;

// ScdSupply

const TUint ScdSupply::kMaxAudioDurationMs = 5;

ScdSupply::ScdSupply(ScdMsgFactory& aFactory)
    : iFactory(aFactory)
{
}

void ScdSupply::OutputMetadataDidl(const std::string& aUri, const std::string& aMetadata)
{
    auto msg = iFactory.CreateMsgMetadataDidl(aUri, aMetadata);
    iQueue.Enqueue(msg);
}

void ScdSupply::OutputMetadataOh(const OpenHomeMetadata& aMetadata)
{
    auto msg = iFactory.CreateMsgMetadataOh(aMetadata);
    iQueue.Enqueue(msg);
}

void ScdSupply::OutputFormat(TUint aBitDepth, TUint aSampleRate, TUint aNumChannels, Endian aEndian,
                             TUint aBitRate, TUint64 aSampleStart, TUint64 aSamplesTotal,
                             TBool aSeekable, TBool aLossless, TBool aLive,
                             TBool aBroadcastAllowed, const std::string& aCodecName)
{
    auto msg = iFactory.CreateMsgFormat(aBitDepth, aSampleRate, aNumChannels,
                                        aBitRate, aSampleStart, aSamplesTotal,
                                        aSeekable, aLossless, aLive,
                                        aBroadcastAllowed, aCodecName);
    iQueue.Enqueue(msg);
    
    iBitDepth = aBitDepth;
    iSampleRate = aSampleRate;
    iNumChannels = aNumChannels;
    iBytesPerSample = (aBitDepth/8) * aNumChannels;
    iBytesPerAudioMsg = iBytesPerSample * (iSampleRate * kMaxAudioDurationMs / 1000);
    iEndian = aEndian;
    iAudio.reserve(iBytesPerAudioMsg);
    iBytesEndianSwapped = 0;
}

void ScdSupply::OutputAudio(const TByte* aData, TUint aBytes)
{
    TUint space = iBytesPerAudioMsg - iAudio.size();
    if (aBytes <= space) {
        AppendAudio(aData, aBytes);
        if (iBytesPerAudioMsg == iAudio.size()) {
            OutputAudio();
        }
    }
    else {
        TUint max = space;
        do {
            const TUint bytes = std::min(max, aBytes);
            AppendAudio(aData, bytes);
            aData += bytes;
            aBytes -= bytes;
            max = iBytesPerAudioMsg;
            if (iAudio.size() == iBytesPerAudioMsg) {
                OutputAudio();
            }
        } while (aBytes > 0);
    }
}

void ScdSupply::OutputMetatextDidl(const std::string& aMetatext)
{
    auto msg = iFactory.CreateMsgMetatextDidl(aMetatext);
    iQueue.Enqueue(msg);
}

void ScdSupply::OutputMetatextOh(const OpenHomeMetadata& aMetatext)
{
    auto msg = iFactory.CreateMsgMetatextOh(aMetatext);
    iQueue.Enqueue(msg);
}

void ScdSupply::OutputHalt()
{
    auto msg = iFactory.CreateMsgHalt();
    iQueue.Enqueue(msg);
}

void ScdSupply::AppendAudio(const TByte* aData, TUint aBytes)
{
    iAudio.append(reinterpret_cast<const char*>(aData), aBytes);
    if (iEndian == Endian::Little && iBitDepth > 8) {
        // scd audio must be big endian. swap bytes order
        const TUint samples = (iAudio.size() - iBytesEndianSwapped) / iBytesPerSample;
        const TUint subsamples = samples * iNumChannels;
        char* p = (char*)iAudio.data() + iBytesEndianSwapped;
        if (iBitDepth == 16) {
            for (TUint i = 0; i < subsamples; i++) {
                char b = p[0];
                p[0] = p[1];
                p[1] = b;
                p += 2;
            }
        }
        else if (iBitDepth == 24) {
            for (TUint i = 0; i < subsamples; i++) {
                char b = p[0];
                p[0] = p[2];
                p[2] = b;
                p += 3;
            }
        }
        iBytesEndianSwapped += samples * iBytesPerSample;
    }
}

void ScdSupply::OutputPendingSamples()
{
    const TUint excess = iAudio.size() % iBytesPerSample;
    if (excess > 0) {
        iAudio.erase(iAudio.size() - excess + 1, excess);
    }
    if (iAudio.size() > 0) {
        OutputAudio();
    }
}

void ScdSupply::OutputAudio()
{
    ASSERT(iAudio.size() > 0);
    ASSERT(iAudio.size() % iBytesPerSample == 0);
    const TUint numSamples = iAudio.size() / iBytesPerSample;
    auto msg = iFactory.CreateMsgAudioOut(iAudio, numSamples);
    iQueue.Enqueue(msg);
    iAudio.clear();
    iBytesEndianSwapped = 0;
}

ScdMsg* ScdSupply::Pull()
{
    return iQueue.Dequeue();
}
