#pragma once
#include <OpenHome/OhNetTypes.h> 

namespace OpenHome {
namespace Av {
    class OhmMsgFactory;
}
namespace Media {
    class IMimeTypeList;
namespace Codec {

class CodecBase;

class CodecFactory
{
public:
    static CodecBase* NewAacFdkAdts(IMimeTypeList& aMimeTypeList);
    static CodecBase* NewAacFdkMp4(IMimeTypeList& aMimeTypeList);
    static CodecBase* NewAifc(IMimeTypeList& aMimeTypeList);
    static CodecBase* NewAiff(IMimeTypeList& aMimeTypeList);
    static CodecBase* NewAlacApple(IMimeTypeList& aMimeTypeList);
    static CodecBase* NewFlac(IMimeTypeList& aMimeTypeList);
    static CodecBase* NewOpus(IMimeTypeList& aMimeTypeList);
    static CodecBase* NewMp3(IMimeTypeList& aMimeTypeList);
    static CodecBase* NewDsdDsf(IMimeTypeList& aMimeTypeList, TUint aSampleBlockWords, TUint aPaddingBytes);
    static CodecBase* NewDsdDff(IMimeTypeList& aMimeTypeList, TUint aSampleBlockWords, TUint aPaddingBytes);
    static CodecBase* NewPcm();
    static CodecBase* NewDsdRaw(TUint aSampleBlockWords, TUint aPaddingBytes);
    static CodecBase* NewRaop();
    static CodecBase* NewVorbis(IMimeTypeList& aMimeTypeList);
    static CodecBase* NewWav(IMimeTypeList& aMimeTypeList);
};

} // namespace Codec
} // namespace Media
} // namespace OpenHome

