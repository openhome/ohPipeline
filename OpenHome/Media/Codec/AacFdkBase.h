#pragma once

#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/Codec/Container.h>

extern "C" {
#include <aacdecoder_lib.h>
}


namespace OpenHome {
namespace Media {
    class IMimeTypeList;
namespace Codec {

class CodecAacFdkBase : public CodecBase
{
private:
    static const TUint kInputBufBytes = 4096;   // Input buf size used by third-party decoder examples.
    static const TUint kOutputBufBytes = 8192;  // See #5602 before changing. Was previously set to 7680 for #5602 but needed to be upped to #8192 for certain tracks (see #6137).
public:
    static const Brn kCodecAac;
protected:
    CodecAacFdkBase(const TChar* aId, IMimeTypeList& aMimeTypeList);
    ~CodecAacFdkBase();
protected: // from CodecBase
    TBool SupportsMimeType(const Brx& aMimeType);
    void StreamInitialise();
    void Process() = 0;
    TBool TrySeek(TUint aStreamId, TUint64 aSample);
    void StreamCompleted();
protected:
    void InitialiseDecoderMp4(const Brx& aAudioSpecificConfig);
    void InitialiseDecoderAdts();
    void DecodeFrame();
    void FlushOutput();
private:
protected:
    Bws<kInputBufBytes> iInBuf;
    Bws<kOutputBufBytes> iOutBuf; // see #5602 before changing buffer size

    TUint iSampleRate;
    TUint iOutputSampleRate;
    TUint iBitrateMax;
    TUint iBitrateAverage;
    TUint iChannels;
    TUint iBitDepth;
    TUint64 iSamplesTotal;
    TUint64 iTotalSamplesOutput;
    TUint64 iTrackLengthJiffies;
    TUint64 iTrackOffset;

    TBool iNewStreamStarted;
    TBool iStreamEnded;
private:
    HANDLE_AACDECODER iDecoderHandle;
};

} //namespace Codec
} //namespace Media
} //namespace OpenHome

