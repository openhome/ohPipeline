#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Av/Scd/ScdMsg.h>

#include <string>

namespace OpenHome {
namespace Scd {
namespace Sender {

class IScdSupply
{
public:
    enum class Endian
    {
        Little,
        Big
    };
public:
    virtual void OutputMetadataDidl(const std::string& aUri, const std::string& aMetadata) = 0;
    virtual void OutputMetadataOh(const OpenHomeMetadata& aMetadata) = 0;
    virtual void OutputFormat(TUint aBitDepth, TUint aSampleRate, TUint aNumChannels, Endian aEndian,
                              TUint aBitRate, TUint64 aSampleStart, TUint64 aSamplesTotal,
                              TBool aSeekable, TBool aLossless, TBool aLive,
                              TBool aBroadcastAllowed, const std::string& aCodecName) = 0;
    virtual void OutputAudio(const TByte* aData, TUint aBytes) = 0;
    virtual void OutputMetatextDidl(const std::string& aMetatext) = 0;
    virtual void OutputMetatextOh(const OpenHomeMetadata& aMetatext) = 0;
    virtual void OutputHalt() = 0;
    virtual ~IScdSupply() {}
};

class IScdMsgReservoir
{
public:
    virtual ScdMsg* Pull() = 0;
    virtual ~IScdMsgReservoir() {}
};

class ScdSupply : public IScdSupply, public IScdMsgReservoir
{
    static const TUint kMaxAudioDurationMs;
public:
    ScdSupply(ScdMsgFactory& aFactory);
private: // from IScdSupply
    void OutputMetadataDidl(const std::string& aUri, const std::string& aMetadata) override;
    void OutputMetadataOh(const OpenHomeMetadata& aMetadata) override;
    void OutputFormat(TUint aBitDepth, TUint aSampleRate, TUint aNumChannels, Endian aEndian,
                      TUint aBitRate, TUint64 aSampleStart, TUint64 aSamplesTotal,
                      TBool aSeekable, TBool iLossless, TBool aLive,
                      TBool aBroadcastAllowed, const std::string& aCodecName) override;
    void OutputAudio(const TByte* aData, TUint aBytes) override;
    void OutputMetatextDidl(const std::string& aMetatext) override;
    void OutputMetatextOh(const OpenHomeMetadata& aMetatext) override;
    void OutputHalt() override;
private:
    void AppendAudio(const TByte* aData, TUint aBytes);
    void OutputPendingSamples();
    void OutputAudio();
private: // from IScdMsgReservoir
    ScdMsg* Pull() override;
private:
    ScdMsgFactory& iFactory;
    ScdMsgQueue iQueue;
    TUint iBitDepth;
    TUint iSampleRate;
    TUint iNumChannels;
    Endian iEndian;
    TUint iBytesPerSample;
    TUint iBytesPerAudioMsg;
    std::string iAudio;
    TUint iBytesEndianSwapped;
};

} // namespace Sender
} // namespace Scd
} // namespace OpenHome
