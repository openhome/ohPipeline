#include <OpenHome/Media/Codec/Vorbis.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/Codec/Container.h>
#include <OpenHome/Private/Arch.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Media/MimeTypeList.h>

#include <limits>

extern "C" {
#include <ivorbisfile.h>
}

using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;

CodecBase* CodecFactory::NewVorbis(IMimeTypeList& aMimeTypeList)
{ // static
    return new CodecVorbis(aMimeTypeList);
}


const TUint CodecVorbis::kHeaderBytesReq;
const TUint CodecVorbis::kSearchChunkSize;
const TUint CodecVorbis::kIcyMetadataBytes;
const TUint CodecVorbis::kBitDepth;
const TInt CodecVorbis::kInvalidBitstream = std::numeric_limits<TInt>::max();
const Brn CodecVorbis::kCodecVorbis("VORBIS");

size_t ReadCallback(void *ptr, size_t size, size_t nmemb, void *datasource);
int SeekCallback(void *datasource, ogg_int64_t offset, int whence);
int CloseCallback(void *datasource);
long TellCallback(void *datasource);

void CodecVorbis::ReadCallback(Bwx& aBuf)
{
    try{
        if (!iController->StreamLength() || (iController->StreamPos() < iController->StreamLength())) {
            // Tremor pulls more data after stream exhaustion, as it is looking
            // for 0 bytes to signal EOF. However, controller signals EOF by outputting fewer
            // than requested bytes; any subsequent pulls may pull a quit msg.

            // Account for this by checking if stream has already been exhausted;
            // if not, we'll do another read; otherwise we won't do anything and Tremor
            // will get its EOF identifier.
            iController->Read(aBuf, aBuf.MaxBytes());
        }
    }
    catch(CodecStreamEnded&) {
        aBuf.SetBytes(0);
    }

    //LOG(kCodec,"CodecVorbis::CallbackRead: read %u bytes\n", aBuf.Bytes());
}

TInt CodecVorbis::SeekCallback(TInt64 aOffset, TInt aWhence)
{
    LOG(kCodec, "CodecVorbis::SeekCallback offset %lld, whence %d, iSamplesTotal %llu, iController->StreamLength() %llu\n", aOffset, aWhence, iSamplesTotal, iController->StreamLength());
    return -1; // Non-seekable. Stops the decoder merrily dancing around in the stream during initialisation. Means we have to implement our own approach for user-initiated seeks.
}

TInt CodecVorbis::CloseCallback()
{
    LOG(kCodec,"CodecVorbis::CLOSE\n");
    return 0;
}

TInt64 CodecVorbis::TellCallback()
{
    // If seeking is unsupported, this should always return -1 (or tell callback func in callbacks struct should be set to null).
    return -1;
}

size_t ReadCallback(void *ptr, size_t size, size_t nmemb, void *datasource)
{
    const TUint bytes = size * nmemb;
    Bwn buf((TByte *)ptr, bytes);
    ((CodecVorbis *)datasource)->ReadCallback(buf);
    return buf.Bytes();
}

int SeekCallback(void *datasource, ogg_int64_t offset, int whence)
{
    return ((CodecVorbis *)datasource)->SeekCallback(offset, whence);
}

int CloseCallback(void *datasource)
{
    return ((CodecVorbis *)datasource)->CloseCallback();
}

long TellCallback(void *datasource)
{
    return ((CodecVorbis *)datasource)->TellCallback();
}


// CodecVorbis::Pimpl

// Hide third-party codec structs from header. Useful for classes deriving from CodecVorbis, as they do not need to supply third-party codec headers.
class CodecVorbis::Pimpl
{
public:
    Pimpl();
public:
    ov_callbacks iCallbacks;
    void *iDataSource; // dummy stream identifier
    OggVorbis_File iVf;
};

CodecVorbis::Pimpl::Pimpl()
{
}


// CodecVorbis

CodecVorbis::CodecVorbis(IMimeTypeList& aMimeTypeList)
    : CodecBase("Vorbis", kCostHigh)
    , iPimpl(new Pimpl())
{
    iPimpl->iDataSource = this;
    iPimpl->iCallbacks.read_func = ::ReadCallback;
    iPimpl->iCallbacks.seek_func = ::SeekCallback;
    iPimpl->iCallbacks.close_func = ::CloseCallback;
    iPimpl->iCallbacks.tell_func = ::TellCallback;
    aMimeTypeList.Add("audio/ogg");
    aMimeTypeList.Add("audio/x-ogg");
    aMimeTypeList.Add("application/ogg");
}

CodecVorbis::~CodecVorbis()
{
    LOG(kCodec, "CodecVorbis::~CodecVorbis\n");
}

TBool CodecVorbis::Recognise(const EncodedStreamInfo& aStreamInfo)
{
    if (aStreamInfo.StreamFormat() != EncodedStreamInfo::Format::Encoded) {
        return false;
    }
    const auto testRet = ov_test_callbacks(iPimpl->iDataSource, &iPimpl->iVf, nullptr, 0, iPimpl->iCallbacks);
    const TBool isVorbis = (testRet == 0);
    return isVorbis;
}

void CodecVorbis::ParseOgg()
{
}

TUint64 CodecVorbis::GetSamplesTotal()
{
    if (iController->StreamLength() > 0) {
        // Try do an out-of-band read and parse the final Ogg page ourselves to
        // get the granule pos field. When Vorbis is contained within an Ogg,
        // the granule pos field contains the sample pos, and the final Ogg
        // page tells us the total number of samples in that stream (for
        // non-chained streams).

        // If trying to read and parse the final Ogg page fails, fall back to
        // estimating the track length via a calculation.

        if (FindSync()) {
            try {
                return GetTotalSamples();
            }
            catch (CodecStreamCorrupt&)
            {}
        }

        // Didn't manage to parse last Ogg page; fall back to estimation from average bitrate and file size.
        return iSampleRate * iController->StreamLength() / iBytesPerSec;
    }
    return 0;
}

void CodecVorbis::StreamInitialise()
{
    // LOG(kCodec, "CodecVorbis::StreamInitialise\n");
    iBitstream = kInvalidBitstream;
    iStreamEnded = false;
    iNewStreamStarted = false;
    iTotalSamplesOutput = 0;
    iInBuf.SetBytes(0);
    iOutBuf.SetBytes(0);
    iSamplesTotal = 0;
    iTrackLengthJiffies = 0;
    iTrackOffset = 0;
    iIcyMetadata.Replace(Brx::Empty());

    ParseOgg();

    const TInt opened = ov_test_open(&iPimpl->iVf);
    if (opened < 0) {
        THROW(CodecStreamCorrupt);
    }

    vorbis_info* info = ov_info(&iPimpl->iVf, -1);
    iChannels = info->channels;
    iBitrateAverage = info->bitrate_nominal;
    iSampleRate = info->rate;
    iBytesPerSample = iChannels*kBitDepth/8;
    iBytesPerSec = iBitrateAverage/8; // bitrate of raw data rather than the output bitrate

    iSamplesTotal = GetSamplesTotal();

    if (iSamplesTotal > 0) {
        // Add iSampleRate / 2 before division to round up if above half-values.
        iTrackLengthJiffies = (iSamplesTotal * Jiffies::kPerSecond /*+ iSampleRate / 2*/) / iSampleRate;
    }

    LOG(kCodec, "CodecVorbis::StreamInitialise iBitrateAverage %u, kBitDepth %u, iSampleRate %u, iChannels %u, iTrackLengthJiffies %llu\n", iBitrateAverage, kBitDepth, iSampleRate, iChannels, iTrackLengthJiffies);
    iController->OutputDecodedStream(iBitrateAverage, kBitDepth, iSampleRate, iChannels, kCodecVorbis, iTrackLengthJiffies, 0, false, DeriveProfile(iChannels));
}

void CodecVorbis::StreamCompleted()
{
    LOG(kCodec, "CodecVorbis::StreamCompleted\n");
    ov_clear(&iPimpl->iVf);
}

void CodecVorbis::Write(TByte aValue)
{
    iSeekBuf.Append(aValue);
}

void CodecVorbis::Write(const Brx& aBuffer)
{
    iSeekBuf.Append(aBuffer);
}

void CodecVorbis::WriteFlush()
{
}

TBool CodecVorbis::TrySeekBytes(TUint aStreamId, TUint64 aSample, TUint64 aBytePos)
{
    TBool canSeek = iController->TrySeekTo(aStreamId, aBytePos);
    LOG(kCodec, "CodecVorbis::TrySeekBytes to byte: %llu returned %u\n", aBytePos, canSeek);

    if (canSeek) {
        iTotalSamplesOutput = aSample;
        iTrackOffset = (aSample * Jiffies::kPerSecond) / iSampleRate;
        iInBuf.SetBytes(0);
        iOutBuf.SetBytes(0);
        iController->OutputDecodedStream(0, kBitDepth, iSampleRate, iChannels, kCodecVorbis, iTrackLengthJiffies, aSample, false, DeriveProfile(iChannels));
    }
    return canSeek;
}

TBool CodecVorbis::TrySeek(TUint aStreamId, TUint64 aSample)
{
    LOG(kCodec, "CodecVorbis::TrySeek(%u, %llu)\n", aStreamId, aSample);

    auto bytes = aSample * iController->StreamLength() / iSamplesTotal;
    if (bytes >= iController->StreamLength()) {
        bytes = iController->StreamLength() - 1;
    }

    return TrySeekBytes(aStreamId, aSample, bytes);
}

TBool CodecVorbis::FindSync()
{
    // If this method finds the Ogg sync word ("OggS"), it will return true and
    // iSeekBuf will be the data from the last sync word found onwards.
    // It will return false otherwise and the state of iSeekBuf will be
    // undefined.

    // Vorbis codec reads backwards in 1024-byte chunks, so we do the same.

    TBool keepLooking = true;
    TUint searchSize = kSearchChunkSize;
    TUint64 offset = iController->StreamLength();
    TBool syncFound = false;
    Bws<kSearchChunkSize> stashBuf;

    if (iController->StreamLength() < searchSize) {
        offset = 0;
        searchSize = static_cast<TUint>(iController->StreamLength());
    }
    else {
        offset = iController->StreamLength() - searchSize;
    }

    while (keepLooking) {
        iSeekBuf.SetBytes(0);

        // This will cause callbacks via the IWriter interface.
        // iSeekBuf will only be modified by IWriter callbacks during the
        // Read() below.
        TBool res = iController->Read(*this, offset, searchSize);

        // Try to find the "OggS" sync word.
        TUint idx = 0;
        if (res) {
            iSeekBuf.Append(stashBuf); // sync word may occur across read boundary
            Brn sync("OggS");
            TInt bytes = iSeekBuf.Bytes() - sync.Bytes(); // will go -ve if incompatible sizes
            TInt i = 0;
            for (i=0; i<=bytes; i++) {
                if ((strncmp((char*)&iSeekBuf[i], (char*)&sync[0], sync.Bytes()) == 0)
                        && (iSeekBuf.Bytes()-i >= kHeaderBytesReq)) {
                    // Don't break here - there may still be more Ogg pages
                    // in the buffer. We want the last one, so process
                    // whole buf in case there are more.
                    syncFound = true;
                    idx = i;
                    keepLooking = false;
                }
            }
        }

        if (syncFound) {
            // Shift last Ogg page to front of buffer.
            iSeekBuf.Replace(iSeekBuf.Split(idx));
            break;
        }

        if (!res || offset == 0) {
            // Problem reading stream, or exhausted entire stream without
            // finding required data.
            keepLooking = false;
        }
        else {
            stashBuf.Replace(iSeekBuf.Ptr(), searchSize);   // stash prev read in case Ogg page
                                                            // is split on read boundary.
            TUint64 stepBack = kSearchChunkSize;
            if (offset < kSearchChunkSize) {
                stepBack = offset;
                searchSize = static_cast<TUint>(offset);
            }
            offset -= stepBack;
        }
    }

    return syncFound;
}

TUint64 CodecVorbis::GetTotalSamples()
{
    if (iSeekBuf.Bytes() >= kHeaderBytesReq) {
        TUint64 granulePos1 = Converter::LeUint32At(iSeekBuf, 6);
        TUint64 granulePos2 = Converter::LeUint32At(iSeekBuf, 10);
        TUint64 granulePos = (granulePos1 | (granulePos2 << 32));
        return granulePos;
    }

    // We shouldn't have a truncated Ogg page, as we check there are enough
    // header bytes during the sync word search.
    THROW(CodecStreamCorrupt);
}

// copy audio data to output buffer, converting to big endian if required.
void CodecVorbis::BigEndian(TInt16* aDst, TInt16* aSrc, TUint aSamples)
{
    aSamples *= iChannels;
    while(aSamples--) {
        *aDst++ = Arch::BigEndian2(*aSrc++);
    }
}

void CodecVorbis::Process()
{
    TInt bitstream = 0;
    TUint iPrevBytes = iOutBuf.Bytes();

    if(!iStreamEnded || !iNewStreamStarted) {
        LOG(kCodec, "CodecVorbis::Process bitstream %d\n", bitstream);
        try {
            char *pcm = (char *)iInBuf.Ptr();
            TInt request = (iOutBuf.MaxBytes() - iOutBuf.Bytes());
            ASSERT((TInt)iInBuf.MaxBytes() >= request);

            TInt bytes = 0;
            bytes = ov_read(&iPimpl->iVf, pcm, request, (int*)&bitstream);

            if (bytes == 0) {
                THROW(CodecStreamEnded);
            }

            if (bytes == OV_HOLE) {
                LOG(kCodec, "CodecVorbis::Process ov_read error OV_HOLE, requested %u bytes. Attempting to continue decoding.\n", request);
                return;
            }

            if (bytes < 0) {
                LOG(kCodec, "CodecVorbis::Process ov_read error %d, requested %d bytes\n", bytes, request);
                THROW(CodecStreamCorrupt);
            }

            if(bitstream != iBitstream) {
                LOG(kCodec, "CodecVorbis::Process new bitstream %d, %d\n", iBitstream, bitstream);
                iBitstream = bitstream;

                // Encountered a new logical bitstream. Better push any
                // buffered PCM from previous stream.
                if (iOutBuf.Bytes() > 0) {
                    iTrackOffset += iController->OutputAudioPcm(iOutBuf, iChannels, iSampleRate,
                        kBitDepth, AudioDataEndian::Big, iTrackOffset);
                    iOutBuf.SetBytes(0);
                    LOG(kCodec, "CodecVorbis::Process output (new bitstream detected) - total samples = %llu\n", iTotalSamplesOutput);
                }

                // From ov_read() docs:
                // "However, when reading audio back, the application must be aware that multiple bitstream sections do not necessarily use the same number of channels or sampling rate."

                // Call ov_info() and send a MsgDecodedStream to notify of channel count and/or sample rate changes, send a new MsgMetaText, then continue decoding as normal.
                vorbis_info* info = ov_info(&iPimpl->iVf, -1);
                const TBool infoChanged = StreamInfoChanged(info->channels, info->rate);

                iChannels = info->channels;
                iBitrateAverage = info->bitrate_nominal;
                iSampleRate = info->rate;

                iBytesPerSample = iChannels*kBitDepth/8;
                iBytesPerSec = iBitrateAverage/8; // bitrate of raw data rather than the output bitrate

                // FIXME - reusing iTrackLengthJiffies is incorrect, but it was
                // almost definitely wrong when we started this chained stream anyway.

                LOG(kCodec, "CodecVorbis::Process new bitstream: iBitrateAverage %u, kBitDepth %u, iSampleRate %u, iChannels %u, iTrackLengthJiffies %llu\n", iBitrateAverage, kBitDepth, iSampleRate, iChannels, iTrackLengthJiffies);


                // FIXME - output MsgBitrate here if iBitrateAverage has changed.

                if (infoChanged) {
                    iController->OutputDecodedStream(iBitrateAverage, kBitDepth, iSampleRate, iChannels, kCodecVorbis, iTrackLengthJiffies, 0, false, DeriveProfile(iChannels));
                }

                OutputMetaData();
            }

            TUint samples = bytes/iBytesPerSample;
            TByte* dstByte = const_cast<TByte*>(iOutBuf.Ptr()) + iOutBuf.Bytes();
            TInt16* dst = reinterpret_cast<TInt16*>(dstByte);
            BigEndian(dst, (TInt16 *)pcm, samples);
            iOutBuf.SetBytes(iOutBuf.Bytes()+bytes);
            iTotalSamplesOutput += samples;

            LOG(kCodec, "CodecVorbis::Process read - bytes %d, iPrevBytes %d\n", bytes, iPrevBytes);
            if (iOutBuf.MaxBytes() - iOutBuf.Bytes() < (TUint)((kBitDepth/8) * iChannels)) {
                iTrackOffset += iController->OutputAudioPcm(iOutBuf, iChannels, iSampleRate,
                    kBitDepth, AudioDataEndian::Big, iTrackOffset);
                iOutBuf.SetBytes(0);
                LOG(kCodec, "CodecVorbis::Process output - total samples = %llu\n", iTotalSamplesOutput);
            }
        }
        catch(CodecStreamEnded&) {
            iStreamEnded = true;
        }
        catch (CodecStreamStart&) {
            iNewStreamStarted = true;
        }
    }

    FlushOutput();
}

// flush any remaining samples from the decoded buffer
void CodecVorbis::FlushOutput()
{
    LOG(kCodec, "CodecVorbis::FlushOutput\n");

    if (iStreamEnded || iNewStreamStarted) {
        if (iOutBuf.Bytes() > 0) {
            iTrackOffset += iController->OutputAudioPcm(iOutBuf, iChannels, iSampleRate,
                kBitDepth, AudioDataEndian::Big, iTrackOffset);
            iOutBuf.SetBytes(0);
        }
        if (iNewStreamStarted) {
            THROW(CodecStreamStart);
        }
        THROW(CodecStreamEnded);
    }
}

TBool CodecVorbis::StreamInfoChanged(TUint aChannels, TUint aSampleRate) const
{
    return (aChannels != iChannels || aSampleRate != iSampleRate);
}

void CodecVorbis::OutputMetaData()
{
    vorbis_comment* vc = ov_comment(&iPimpl->iVf, -1);
    Brn artist = Brx::Empty();
    Brn title = Brx::Empty();

    for (TInt i=0; i<vc->comments; i++) {
        Brn comment(reinterpret_cast<const TByte*>(vc->user_comments[i]), vc->comment_lengths[i]);
        LOG(kCodec, "CodecVorbis::OutputMetaData comment: %.*s\n", PBUF(comment));

        Parser parser(comment);
        Brn tag = parser.Next('=');
        if (Ascii::CaseInsensitiveEquals(tag, Brn("artist"))) {
            artist = parser.Remaining();
        }
        else if (Ascii::CaseInsensitiveEquals(tag, Brn("title"))) {
            title = parser.Remaining();
        }

        if (artist != Brx::Empty() && title != Brx::Empty()) {
            break; // terminate loop early if we have both artist and title
        }
    }

    if (artist != Brx::Empty() || title != Brx::Empty()) {
        iNewIcyMetadata.Replace("<DIDL-Lite xmlns:dc='http://purl.org/dc/elements/1.1/' ");
        iNewIcyMetadata.Append("xmlns:upnp='urn:schemas-upnp-org:metadata-1-0/upnp/' ");
        iNewIcyMetadata.Append("xmlns='urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/'>");
        iNewIcyMetadata.Append("<item id='' parentID='' restricted='True'><dc:title>");

        iNewIcyMetadata.Append(artist);
        if (artist != Brx::Empty() && title != Brx::Empty()) {
            iNewIcyMetadata.Append(" - ");
        }
        iNewIcyMetadata.Append(title);

        iNewIcyMetadata.Append("</dc:title><upnp:albumArtURI></upnp:albumArtURI>");
        iNewIcyMetadata.Append("<upnp:class>object.item</upnp:class></item></DIDL-Lite>");
        if (iNewIcyMetadata != iIcyMetadata) {
            iIcyMetadata.Replace(iNewIcyMetadata);
            iController->OutputMetaText(iIcyMetadata);
        }
    }
}
