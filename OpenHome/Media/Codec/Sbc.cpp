#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/MimeTypeList.h>
#include <thirdparty/sbc/sbc.h>
#include <thirdparty/sbc/a2dp/a2dp-codecs.h> //Replace this with the one from Buildroot Bluez5(not currently in the staging area)
#include <thirdparty/sbc/a2dp/ipc.h>
#include <thirdparty/sbc/a2dp/rtp.h>
#include <OpenHome/Media/Debug.h>


#include <bitset>
#include <math.h>

namespace OpenHome {
namespace Media {
namespace Codec {

class CodecSbc : public CodecBase
{
    using SbcStruct = sbc_t;
private:
    static const TUint kEncAudioBufferSize = 2048;
    static const TUint kRtpHeaderBytes = 13;
    static const TUint kSbcHeaderBytes = 4;
    static const TUint kTotalHeaderBytes = kRtpHeaderBytes + kSbcHeaderBytes;

    static const TUint kRtpSyncSize = 2;
    static const TUint16 kMinFrameLength = 11;
    static const TByte kSyncword = 0x9C;
    static const TUint kRtpHeader[kRtpSyncSize];
    static const TBool kLossless = false;
private:
    enum class SbcChannelMode
    {
        eMono,
        eDualChannel,
        eStereo,
        eJointStereo
    };
    enum class SbcAllocationMethod
    {
        eSnr,
        eLoudness
    };
public:
    CodecSbc(IMimeTypeList& aMimeTypeList);
    ~CodecSbc();

    TUint FrameLength();
private: // from CodecBase
    TBool Recognise(const EncodedStreamInfo& aStreamInfo);
    void StreamInitialise();
    void Process();
    TBool TrySeek(TUint aStreamId, TUint64 aSample);
    void StreamCompleted();
private:
    Bws<kEncAudioBufferSize> iInputBuffer;
    Bws<AudioData::kMaxBytes> iOutputBuffer;
    Bwh iHeader;
    Brn iName;
    SbcStruct iSbcStruct;
    TUint iChannels;
    TUint iSampleRate;
    TUint iBlocks;
    TUint iSubBands;
    TUint iBitRate;
    TUint iFrameLengthBytes;
    TUint iBitDepth;
    AudioDataEndian iEndianess;
    SbcChannelMode iChannelMode;
    SbcAllocationMethod iAllocationMethod;
    TUint64 iOffset;
    TBool iStreamStart;
};

} // Codec
} // Media
} // OpenHome


using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;

CodecBase* CodecFactory::NewSbc(IMimeTypeList& aMimeTypeList)
{ // static
    return new CodecSbc(aMimeTypeList);
}

const TUint CodecSbc::kRtpHeader[kRtpSyncSize] = {0x80, 0x60};

CodecSbc::CodecSbc(IMimeTypeList& aMimeTypeList)
    : CodecBase("SBC")
    , iName("SBC")
    , iChannels(0)
    , iSampleRate(0)
    , iBlocks(0)
    , iSubBands(0)
    , iBitRate(0)
    , iFrameLengthBytes(0)
    , iBitDepth(0)
    , iOffset(0)
    , iStreamStart(true)
{
    aMimeTypeList.Add("audio/x-sbc");
}

CodecSbc::~CodecSbc()
{
}

TBool CodecSbc::Recognise(const EncodedStreamInfo& aStreamInfo)
{
    if (aStreamInfo.StreamFormat() != EncodedStreamInfo::Format::Encoded) {
        return false;
    }

    // Read the RTP header and SBC header
    Bwh header(kTotalHeaderBytes);
    iController->Read(header, kTotalHeaderBytes);

    for (TUint i = 0; i < kRtpSyncSize; i++) {
        if (header[i] != kRtpHeader[i]) {
            return false;
        }
    }

    if (header[kRtpHeaderBytes] != kSyncword) {
        return false;
    }

    return true;
}

void CodecSbc::StreamInitialise()
{
    iOffset = 0;

    ASSERT(sbc_init(&iSbcStruct, 0) == 0);

    iHeader.Grow(kTotalHeaderBytes);
    iController->Read(iHeader, kTotalHeaderBytes);

    Brn sbcHeader(iHeader.Ptr() + kRtpHeaderBytes, kSbcHeaderBytes);
    ASSERT_VA(sbcHeader[0] == kSyncword, "sbcHeader[0]: %02x\n", sbcHeader[0]);

    // Fs
    if ((sbcHeader[1] & (SBC_FREQ_48000 << 6)) == 0xC0) {
        iSbcStruct.frequency = SBC_FREQ_48000;
        iSampleRate = 48000;
    }
    else if ((sbcHeader[1] & (SBC_FREQ_44100 << 6)) == 0x80) {
        iSbcStruct.frequency = SBC_FREQ_44100;
        iSampleRate = 44100;
    }
    else if ((sbcHeader[1] & (SBC_FREQ_32000 << 6)) == 0x40) {
        iSbcStruct.frequency = SBC_FREQ_32000;
        iSampleRate = 32000;
    }
    else if ((sbcHeader[1] & (SBC_FREQ_16000 << 6)) == 0x00) {
        iSbcStruct.frequency = SBC_FREQ_16000;
        iSampleRate = 16000;
    }
    else {
        ASSERTS();
    }

    // Blocks
    if ((sbcHeader[1] & (SBC_BLK_16 << 4)) == 0x30) {
        iSbcStruct.blocks = SBC_BLK_16;
        iBlocks = 16;
    }
    else if ((sbcHeader[1] & (SBC_BLK_12 << 4)) == 0x20) {
        iSbcStruct.blocks = SBC_BLK_12;
        iBlocks = 12;
    }
    else if ((sbcHeader[1] & (SBC_BLK_8 << 4)) == 0x10) {
        iSbcStruct.blocks = SBC_BLK_8;
        iBlocks = 8;
    }
    else if ((sbcHeader[1] & (SBC_BLK_4 << 4)) == 0x00) {
        iSbcStruct.blocks = SBC_BLK_4;
        iBlocks = 4;
    }
    else {
        ASSERTS();
    }

    // Channel Mode
    if ((sbcHeader[1] & (SBC_MODE_JOINT_STEREO << 2)) == 0x0C) {
        iSbcStruct.mode = SBC_MODE_JOINT_STEREO;
        iChannelMode = SbcChannelMode::eJointStereo;
    }
    else if ((sbcHeader[1] & (SBC_MODE_STEREO << 2)) == 0x08) {
        iSbcStruct.mode = SBC_MODE_STEREO;
        iChannelMode = SbcChannelMode::eStereo;
    }
    else if ((sbcHeader[1] & (SBC_MODE_DUAL_CHANNEL << 2)) == 0x04) {
        iSbcStruct.mode = SBC_MODE_DUAL_CHANNEL;
        iChannelMode = SbcChannelMode::eDualChannel;
    }
    else if ((sbcHeader[1] & (SBC_MODE_MONO << 2)) == 0x00) {
        iSbcStruct.mode = SBC_MODE_MONO;
        iChannelMode = SbcChannelMode::eMono;
    }
    else {
        ASSERTS();
    }

    // Allocation Method
    if ((sbcHeader[1] & (SBC_AM_SNR << 1)) == 0x02) {
        iSbcStruct.allocation = SBC_AM_SNR;
        iAllocationMethod = SbcAllocationMethod::eSnr;
    }
    else if ((sbcHeader[1] & (SBC_AM_LOUDNESS << 1)) == 0x00) {
        iSbcStruct.allocation = SBC_AM_LOUDNESS;
        iAllocationMethod = SbcAllocationMethod::eLoudness;
    }
    else {
        ASSERTS();
    }

    // Sub Bands
    if ((sbcHeader[1] & (SBC_SB_8)) == 0x01) {
        iSbcStruct.subbands = SBC_SB_8;
        iSubBands = 8;
    }
    else if ((sbcHeader[1] & (SBC_SB_4)) == 0x00) {
        iSbcStruct.subbands = SBC_SB_4;
        iSubBands = 4;
    }
    else {
        ASSERTS();
    }

    iSbcStruct.bitpool = (TUint8)sbcHeader[2];
    iEndianess = (iSbcStruct.endian == SBC_LE) ? AudioDataEndian::Little : AudioDataEndian::Big;
    iChannels = (iChannelMode == SbcChannelMode::eMono) ? 1 : 2;
    iBitRate = 8 * FrameLength() * iSampleRate / iSubBands / iBlocks;
    iBitDepth = 16;

    iStreamStart = true;

    iController->OutputDecodedStream(iBitRate, iBitDepth, iSampleRate, iChannels, iName, 0, 0, kLossless, DeriveProfile(iChannels));
}

void CodecSbc::Process()
{
    iController->ReadNextMsg(iInputBuffer);

    TUint frameOffset = sizeof(struct rtp_header) + sizeof(struct rtp_payload);

    if (iStreamStart) {
        iHeader.Grow(iHeader.Bytes() + iInputBuffer.Bytes());
        iHeader.Append(iInputBuffer);
        iInputBuffer.Replace(iHeader);
        iHeader.SetBytes(0);
        iStreamStart = false;
    }

    const void *p = iInputBuffer.Ptr() + frameOffset;
    void *d = (void*)(iOutputBuffer.Ptr());
    size_t to_decode = iInputBuffer.Bytes() - frameOffset;
    size_t to_write = (iInputBuffer.Bytes() / kMinFrameLength + 1) * sbc_get_codesize(&iSbcStruct);

    // Do SBC decoding
    while (to_decode > 0) {
        size_t written = 0;
        ssize_t decoded = 0;

        decoded = sbc_decode(&iSbcStruct,
                             p, to_decode,
                             d, to_write,
                             &written);

        if (decoded <= 0) {
            Log::Print("CodecSbc::Process::DecodeError: %i\n", decoded);
            break;
        }

        p = (uint8_t*) p + decoded;
        to_decode -= decoded;
        d = (uint8_t*) d + written;
        iOutputBuffer.SetBytes(iOutputBuffer.Bytes() + written);
        to_write -= written;
    }

    iOffset += iController->OutputAudioPcm(iOutputBuffer, iChannels, iSampleRate, iBitDepth, iEndianess, iOffset);
    iInputBuffer.SetBytes(0);
    iOutputBuffer.SetBytes(0);
}

void CodecSbc::StreamCompleted()
{
    // LOG(kCodec, "CodecSbc::StreamCompleted\n");
}

TBool CodecSbc::TrySeek(TUint /*aStreamId*/, TUint64 /*aSample*/)
{
    return false;
}

TUint CodecSbc::FrameLength()
{
    if (iSbcStruct.mode == SBC_MODE_MONO || iSbcStruct.mode == SBC_MODE_DUAL_CHANNEL) {
        iFrameLengthBytes = 4 + (4 * iSubBands * iChannels) / 8 + ceil((iBlocks * iChannels * iSbcStruct.bitpool) / 8);
    }
    else if (iSbcStruct.mode == SBC_MODE_STEREO) {
        iFrameLengthBytes = 4 + (4 * iSubBands * iChannels) / 8 + ceil((iBlocks * iSbcStruct.bitpool) / 8);
    }
    else if (iSbcStruct.mode == SBC_MODE_JOINT_STEREO) {
        iFrameLengthBytes = 4 + (4 * iSubBands * iChannels) / 8 + ceil((iSubBands + iBlocks * iSbcStruct.bitpool) / 8);
    }

    return iFrameLengthBytes;
}