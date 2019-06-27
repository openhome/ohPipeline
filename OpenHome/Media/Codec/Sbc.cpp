#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
// #include <OpenHome/Debug.h>
#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/MimeTypeList.h>
#include <thirdparty/sbc/sbc.h>
// #include <thirdparty/sbc/sbc_tables.h>
#include <thirdparty/sbc/a2dp/a2dp-codecs.h>  //Replace this with the one from Buildroot Bluez5(not currently in the staging area)
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
    static const TUint16 kSbcMinFrameLength = 11;
    static const TUint16 kSbcMaxFrameLength = 762;
    static const TByte kSbcSyncword = 0x9C;
    static const TUint8 kCrcTable[256];
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
    TUint BitRate();
private: // from CodecBase
    TBool Recognise(const EncodedStreamInfo& aStreamInfo);
    void StreamInitialise();
    void Process();
    TBool TrySeek(TUint aStreamId, TUint64 aSample);
    void StreamCompleted();
private:
    Bws<kEncAudioBufferSize> iInputBuffer;
    Bwh iOutputBuffer;
    Brn iName;
    SbcStruct iSbcStruct;
    TUint iChannels;
    TUint iSampleRate;
    TUint iBlockLength;
    TUint iSubBands;
    TUint iBitRate;
    TUint iFrameLengthBytes;
    TUint iBitDepth;
    AudioDataEndian iEndianess;
    SbcChannelMode iChannelMode;
    SbcAllocationMethod iAllocationMethod;
    TUint64 iOffset;
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

const TUint8 CodecSbc::kCrcTable[256] = {
    0x00, 0x1D, 0x3A, 0x27, 0x74, 0x69, 0x4E, 0x53,
    0xE8, 0xF5, 0xD2, 0xCF, 0x9C, 0x81, 0xA6, 0xBB,
    0xCD, 0xD0, 0xF7, 0xEA, 0xB9, 0xA4, 0x83, 0x9E,
    0x25, 0x38, 0x1F, 0x02, 0x51, 0x4C, 0x6B, 0x76,
    0x87, 0x9A, 0xBD, 0xA0, 0xF3, 0xEE, 0xC9, 0xD4,
    0x6F, 0x72, 0x55, 0x48, 0x1B, 0x06, 0x21, 0x3C,
    0x4A, 0x57, 0x70, 0x6D, 0x3E, 0x23, 0x04, 0x19,
    0xA2, 0xBF, 0x98, 0x85, 0xD6, 0xCB, 0xEC, 0xF1,
    0x13, 0x0E, 0x29, 0x34, 0x67, 0x7A, 0x5D, 0x40,
    0xFB, 0xE6, 0xC1, 0xDC, 0x8F, 0x92, 0xB5, 0xA8,
    0xDE, 0xC3, 0xE4, 0xF9, 0xAA, 0xB7, 0x90, 0x8D,
    0x36, 0x2B, 0x0C, 0x11, 0x42, 0x5F, 0x78, 0x65,
    0x94, 0x89, 0xAE, 0xB3, 0xE0, 0xFD, 0xDA, 0xC7,
    0x7C, 0x61, 0x46, 0x5B, 0x08, 0x15, 0x32, 0x2F,
    0x59, 0x44, 0x63, 0x7E, 0x2D, 0x30, 0x17, 0x0A,
    0xB1, 0xAC, 0x8B, 0x96, 0xC5, 0xD8, 0xFF, 0xE2,
    0x26, 0x3B, 0x1C, 0x01, 0x52, 0x4F, 0x68, 0x75,
    0xCE, 0xD3, 0xF4, 0xE9, 0xBA, 0xA7, 0x80, 0x9D,
    0xEB, 0xF6, 0xD1, 0xCC, 0x9F, 0x82, 0xA5, 0xB8,
    0x03, 0x1E, 0x39, 0x24, 0x77, 0x6A, 0x4D, 0x50,
    0xA1, 0xBC, 0x9B, 0x86, 0xD5, 0xC8, 0xEF, 0xF2,
    0x49, 0x54, 0x73, 0x6E, 0x3D, 0x20, 0x07, 0x1A,
    0x6C, 0x71, 0x56, 0x4B, 0x18, 0x05, 0x22, 0x3F,
    0x84, 0x99, 0xBE, 0xA3, 0xF0, 0xED, 0xCA, 0xD7,
    0x35, 0x28, 0x0F, 0x12, 0x41, 0x5C, 0x7B, 0x66,
    0xDD, 0xC0, 0xE7, 0xFA, 0xA9, 0xB4, 0x93, 0x8E,
    0xF8, 0xE5, 0xC2, 0xDF, 0x8C, 0x91, 0xB6, 0xAB,
    0x10, 0x0D, 0x2A, 0x37, 0x64, 0x79, 0x5E, 0x43,
    0xB2, 0xAF, 0x88, 0x95, 0xC6, 0xDB, 0xFC, 0xE1,
    0x5A, 0x47, 0x60, 0x7D, 0x2E, 0x33, 0x14, 0x09,
    0x7F, 0x62, 0x45, 0x58, 0x0B, 0x16, 0x31, 0x2C,
    0x97, 0x8A, 0xAD, 0xB0, 0xE3, 0xFE, 0xD9, 0xC4
};

CodecSbc::CodecSbc(IMimeTypeList& aMimeTypeList)
    : CodecBase("SBC")
    , iName("SBC")
    , iChannels(0)
    , iSampleRate(0)
    , iBlockLength(0)
    , iSubBands(0)
    , iBitRate(0)
    , iFrameLengthBytes(0)
    , iBitDepth(16u)
    , iOffset(0)
{
    aMimeTypeList.Add("audio/x-sbc");
}

CodecSbc::~CodecSbc()
{
}

TBool CodecSbc::Recognise(const EncodedStreamInfo& aStreamInfo)
{
    Log::Print(">>>CodecSbc::Recognise()\n\n");
    if (aStreamInfo.StreamFormat() != EncodedStreamInfo::Format::Encoded) {
        return false;
    }

    // Read the max possible frame into local memory
    Bwh sbcMaxFrame(kSbcMaxFrameLength);
    iController->Read(sbcMaxFrame, kSbcMaxFrameLength);

    // Iterate through the buffer to find the Syncword
    TUint frameStart = 0;
    TBool syncwordFound = false;
    for (TUint i = 0; i < kSbcMaxFrameLength; i++) {
        if (sbcMaxFrame[i] == kSbcSyncword) {
            syncwordFound = true;
            frameStart = i;
            break;
        }
    }
    Log::Print(">>>CodecSbc::Recognise(), syncwordFound: %u, position: %u\n\n", syncwordFound, frameStart);

    // ASSERT there is enough space left in buffer for a header
    // FIXME - what will we do if we don't?
    ASSERT((sbcMaxFrame.Bytes() - frameStart) >= 13); // ??

    // FIXME - Can we calculate the frame size i.e. 13 at this point?
    Brn sbcHeader(sbcMaxFrame.Ptr() + frameStart, 13);
    if (syncwordFound) {
        TByte crcHeader[11];
        TUint crcPos = 16;
        TUint consumed = 32;
        TUint /* SBC_ALIGNED */ scaleFactor[2][8];
        TUint subbands = 0;
        crcHeader[0] = sbcHeader[1];
        crcHeader[1] = sbcHeader[2];

        if ((sbcHeader[0] & (BT_A2DP_CHANNEL_MODE_JOINT_STEREO << 2)) == 0x0C) {
            if ((sbcHeader[0] & (BT_A2DP_SUBBANDS_4)) == 0x00) {
                crcHeader[crcPos / 8] = sbcHeader[4] & 0xF0;
                subbands = 4;
            }
            else {
                crcHeader[crcPos / 8] = sbcHeader[4];
                subbands = 8;
            }
            crcPos += subbands;
            consumed += subbands;
        }
        TUint channels = 2;
        if ((sbcHeader[0] & (BT_A2DP_CHANNEL_MODE_MONO << 2)) == 0x00) {
            channels = 1;
        }

        for (TUint ch = 0; ch < channels; ch++) {
            for (TUint sb = 0; sb < subbands; sb++) {
                scaleFactor[ch][sb] = (sbcHeader[consumed >> 3] >> (4 - (consumed & 0x7))) & 0x0F;
                crcHeader[crcPos >> 3] |= scaleFactor[ch][sb] << (4 - (crcPos & 0x7));
                crcPos += 4;
                consumed += 4;
            }
        }

        TUint i;
        TUint8 crc = 0x0F;
        TUint8 octet;

        for (i = 0; i < crcPos / 8; i++) {
            crc = kCrcTable[crc ^ crcHeader[i]];
        }
        octet = crcHeader[i];
        for (i = 0; i < crcPos % 8; i++) {
            char bit = ((octet ^ crc) & 0x80) >> 7;
            crc = ((crc & 0x7f) << 1) ^ (bit ? 0x1d : 0);
            octet = octet << 1;
        }

        // FIXME - We require the first frame we test to be perfect, not ideal...
        Log::Print(">>>CodecSbc::Recognise(), sbcHeader[3]: %02x, crc: %02x\n\n", sbcHeader, crc);
        if (sbcHeader[3] == crc) {
            return true;
        }
    }
    return false;
}

void CodecSbc::StreamInitialise()
{
    iBitRate = 0;
    iFrameLengthBytes = 0;

    sbc_init(&iSbcStruct, 0);


    Bws<4> sbcHeader;
    iController->Read(sbcHeader, 4);
    ASSERT(sbcHeader[0] == kSbcSyncword); // Confirmed in Recognise();

    // Fs
    if ((sbcHeader[1] & (BT_SBC_SAMPLING_FREQ_16000 << 6)) == 0x00) {
        iSbcStruct.frequency = SBC_FREQ_16000;
        iSampleRate = 16000;
    }
    else if ((sbcHeader[1] & (BT_SBC_SAMPLING_FREQ_32000 << 6)) == 0x40) {
        iSbcStruct.frequency = SBC_FREQ_32000;
        iSampleRate = 32000;
    }
    else if ((sbcHeader[1] & (BT_SBC_SAMPLING_FREQ_44100 << 6)) == 0x80) {
        iSbcStruct.frequency = SBC_FREQ_44100;
        iSampleRate = 44100;
    }
    else if ((sbcHeader[1] & (BT_SBC_SAMPLING_FREQ_48000 << 6)) == 0xC0) {
        iSbcStruct.frequency = SBC_FREQ_48000;
        iSampleRate = 48000;
    }
    else {
        ASSERTS();
    }

    // Blocks
    if ((sbcHeader[1] & (BT_A2DP_BLOCK_LENGTH_4 << 4)) == 0x00) {
        iSbcStruct.blocks = SBC_BLK_4;
        iBlockLength = 4;
    }
    else if ((sbcHeader[1] & (BT_A2DP_BLOCK_LENGTH_8 << 4)) == 0x10) {
        iSbcStruct.blocks = SBC_BLK_8;
        iBlockLength = 8;
    }
    else if ((sbcHeader[1] & (BT_A2DP_BLOCK_LENGTH_12 << 4)) == 0x20) {
        iSbcStruct.blocks = SBC_BLK_12;
        iBlockLength = 12;
    }
    else if ((sbcHeader[1] & (BT_A2DP_BLOCK_LENGTH_16 << 4)) == 0x30) {
        iSbcStruct.blocks = SBC_BLK_16;
        iBlockLength = 16;
    }
    else {
        ASSERTS();
    }

    // Channel Mode
    if ((sbcHeader[1] & (BT_A2DP_CHANNEL_MODE_MONO << 2)) == 0x00) {
        iSbcStruct.mode = SBC_MODE_MONO;
        iChannelMode = SbcChannelMode::eMono;
    }
    else if ((sbcHeader[1] & (BT_A2DP_CHANNEL_MODE_DUAL_CHANNEL << 2)) == 0x04) {
        iSbcStruct.mode = SBC_MODE_DUAL_CHANNEL;
        iChannelMode = SbcChannelMode::eDualChannel;
    }
    else if ((sbcHeader[1] & (BT_A2DP_CHANNEL_MODE_STEREO << 2)) == 0x08) {
        iSbcStruct.mode = SBC_MODE_STEREO;
        iChannelMode = SbcChannelMode::eStereo;
    }
    else if ((sbcHeader[1] & (BT_A2DP_CHANNEL_MODE_JOINT_STEREO << 2)) == 0x0C) {
        iSbcStruct.mode = SBC_MODE_JOINT_STEREO;
        iChannelMode = SbcChannelMode::eJointStereo;
    }
    else {
        ASSERTS();
    }

    // Allocation Method
    if ((sbcHeader[1] & (BT_A2DP_ALLOCATION_SNR << 1)) == 0x02) {
        iSbcStruct.allocation = SBC_AM_SNR;
        iAllocationMethod = SbcAllocationMethod::eSnr;
    }
    else if ((sbcHeader[1] & (BT_A2DP_ALLOCATION_LOUDNESS << 1)) == 0x00) {
        iSbcStruct.allocation = SBC_AM_LOUDNESS;
        iAllocationMethod = SbcAllocationMethod::eLoudness;
    }
    else {
        ASSERTS();
    }

    // Sub Bands
    if ((sbcHeader[1] & (BT_A2DP_SUBBANDS_4)) == 0x00) {
        iSbcStruct.subbands = SBC_SB_4;
        iSubBands = 4;
    }
    else if ((sbcHeader[1] & (BT_A2DP_SUBBANDS_8)) == 0x01) {
        iSbcStruct.subbands = SBC_SB_8;
        iSubBands = 8;
    }
    else {
        ASSERTS();
    }

    iSbcStruct.bitpool = sbcHeader[2];

    iEndianess = (iSbcStruct.endian == SBC_LE) ? AudioDataEndian::Little : AudioDataEndian::Big;
    iChannels = (iChannelMode == SbcChannelMode::eMono) ? 1 : 2;

    TUint outpuBufferSize = (kEncAudioBufferSize / kSbcMinFrameLength + 1 ) * sbc_get_codesize(&iSbcStruct); //max frames in a packet times codesize
    iOutputBuffer.Replace(Bwh(outpuBufferSize));

    // PcmStreamInfo pcmStreamInfo;
    // pcmStreamInfo.Set(16u, iSampleRate, iChannels, iEndianess, DeriveProfile(iChannels), 0);

    iController->OutputDecodedStream(BitRate(), 24, iSampleRate, iChannels, iName, 0, 0, kLossless, DeriveProfile(iChannels));
}

void CodecSbc::Process()
{
    iController->Read(iInputBuffer, kEncAudioBufferSize);

    const void *p = iInputBuffer.Ptr() + sizeof(struct rtp_header) + sizeof(struct rtp_payload);
    void *d = (void*)(iOutputBuffer.Ptr());
    size_t to_decode = iInputBuffer.Bytes() - sizeof(struct rtp_header) - sizeof(struct rtp_payload);
    size_t to_write = iOutputBuffer.MaxBytes();

    // Do SBC decoding
    while (to_decode > 0) {
        size_t written;
        ssize_t decoded;

        decoded = sbc_decode(&iSbcStruct,
                             p, to_decode,
                             d, to_write,
                             &written);

        if (decoded <= 0) {
            // Log decode error
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
    if (!iFrameLengthBytes) {
        if (iSbcStruct.mode == SBC_MODE_MONO || iSbcStruct.mode == SBC_MODE_DUAL_CHANNEL) {
            iFrameLengthBytes = (4 + (4 * iSubBands * iChannels) / 8) + ceil(iBlockLength * iChannels * iSbcStruct.bitpool / 8);
        }
        else if (iSbcStruct.mode == SBC_MODE_STEREO) {
            iFrameLengthBytes = (4 + (4 * iSubBands * iChannels) / 8) + ceil(iBlockLength * iSbcStruct.bitpool / 8);
        }
        else if (iSbcStruct.mode == SBC_MODE_JOINT_STEREO) {
            iFrameLengthBytes = (4 + (4 * iSubBands * iChannels) / 8) + ceil(iSubBands + iBlockLength * iSbcStruct.bitpool / 8);
        }
    }
    return iFrameLengthBytes;
}

TUint CodecSbc::BitRate()
{
    if (!iBitRate) {
        iBitRate = 8 * FrameLength() * iSampleRate / iSubBands / iBlockLength;
    }
    return iBitRate;
}
