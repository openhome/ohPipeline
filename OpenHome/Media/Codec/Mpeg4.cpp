#include <OpenHome/Media/Codec/Mpeg4.h>
#include <OpenHome/Media/Codec/ContainerFactory.h>
#include <OpenHome/Private/Arch.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Media/MimeTypeList.h>

#include <limits>
#include <vector>

using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;

ContainerBase* ContainerFactory::NewMpeg4(IMimeTypeList& aMimeTypeList, Optional<IMpegDRMProvider> aDRMProvider)
{ // static
    return new Mpeg4Container(aMimeTypeList, aDRMProvider);
}



// Mpeg4Info

Mpeg4Info::Mpeg4Info() :
        iSampleRate(0), iTimescale(0), iChannels(0), iBitDepth(0), iDuration(0)
{
}

Mpeg4Info::Mpeg4Info(const Brx& aCodec, TUint aSampleRate, TUint aTimescale,
        TUint aChannels, TUint aBitDepth, TUint64 aDuration,
        TUint aStreamDescriptorBytes) :
        iCodec(aCodec), iSampleRate(aSampleRate), iTimescale(aTimescale), iChannels(
                aChannels), iBitDepth(aBitDepth), iDuration(aDuration), iStreamDescBytes(
                        aStreamDescriptorBytes)
{
}

TBool Mpeg4Info::Initialised() const
{
    const TBool initialised = iCodec.Bytes() > 0 && iSampleRate != 0
            && iTimescale != 0 && iChannels != 0 && iBitDepth != 0
            && iDuration != 0 && iStreamDescBytes > 0;
    return initialised;
}

const Brx& Mpeg4Info::Codec() const
{
    return iCodec;
}

TUint Mpeg4Info::SampleRate() const
{
    // FIXME - ASSERT if not set?
    return iSampleRate;
}

TUint Mpeg4Info::Timescale() const
{
    return iTimescale;
}

TUint Mpeg4Info::Channels() const
{
    return iChannels;
}

TUint Mpeg4Info::BitDepth() const
{
    return iBitDepth;
}

TUint64 Mpeg4Info::Duration() const
{
    return iDuration;
}

TUint Mpeg4Info::StreamDescriptorBytes() const
{
    return iStreamDescBytes;
}

void Mpeg4Info::SetCodec(const Brx& aCodec)
{
    iCodec.Replace(aCodec);
}

void Mpeg4Info::SetSampleRate(TUint aSampleRate)
{
    // FIXME - ASSERT if already set?
    iSampleRate = aSampleRate;
}

void Mpeg4Info::SetTimescale(TUint aTimescale)
{
    iTimescale = aTimescale;
}

void Mpeg4Info::SetChannels(TUint aChannels)
{
    iChannels = aChannels;
}

void Mpeg4Info::SetBitDepth(TUint aBitDepth)
{
    iBitDepth = aBitDepth;
}

void Mpeg4Info::SetDuration(TUint64 aDuration)
{
    iDuration = aDuration;
}

void Mpeg4Info::SetStreamDescriptorBytes(TUint aBytes)
{
    iStreamDescBytes = aBytes;
}

// Mpeg4InfoReader

Mpeg4InfoReader::Mpeg4InfoReader(IReader& aReader) :
        iReader(aReader)
{
}

void Mpeg4InfoReader::Read(IMpeg4InfoWritable& aInfo)
{
    // These may throw up ReaderError, catch and rethrow?
    ReaderBinary readerBin(iReader);
    try {
        Bws<IMpeg4InfoWritable::kCodecBytes> codec;

        readerBin.ReadReplace(codec.MaxBytes(), codec);
        aInfo.SetCodec(codec);

        const TUint sampleRate = readerBin.ReadUintBe(4);
        aInfo.SetSampleRate(sampleRate);

        const TUint timescale = readerBin.ReadUintBe(4);
        aInfo.SetTimescale(timescale);

        const TUint channels = readerBin.ReadUintBe(4);
        aInfo.SetChannels(channels);

        const TUint bitDepth = readerBin.ReadUintBe(4);
        aInfo.SetBitDepth(bitDepth);

        const TUint64 duration = readerBin.ReadUint64Be(8);
        aInfo.SetDuration(duration);

        const TUint streamDescriptorBytes = readerBin.ReadUintBe(4);
        aInfo.SetStreamDescriptorBytes(streamDescriptorBytes);

        //ASSERT(aInfo.Initialised());
    } catch (ReaderError&) {
        THROW(MediaMpeg4FileInvalid);
    }
}

// Mpeg4InfoWriter

Mpeg4InfoWriter::Mpeg4InfoWriter(const IMpeg4InfoReadable& aInfo) :
        iInfo(aInfo)
{
}

void Mpeg4InfoWriter::Write(IWriter& aWriter) const
{
    //ASSERT(iInfo.Initialised());
    WriterBinary writer(aWriter);
    writer.Write(iInfo.Codec());
    writer.WriteUint32Be(iInfo.SampleRate());
    writer.WriteUint32Be(iInfo.Timescale());
    writer.WriteUint32Be(iInfo.Channels());
    writer.WriteUint32Be(iInfo.BitDepth());
    writer.WriteUint64Be(iInfo.Duration());
    writer.WriteUint32Be(iInfo.StreamDescriptorBytes());
    aWriter.WriteFlush();   // FIXME - required?
}

// Mpeg4BoxHeaderReader

void Mpeg4BoxHeaderReader::Reset(IMsgAudioEncodedCache& aCache)
{
    iCache = &aCache;
    iHeader.SetBytes(0);
    iId.Set(Brx::Empty());
    iBytes = 0;
    iHeaderReadPending = false;
}

Msg* Mpeg4BoxHeaderReader::ReadHeader()
{
    ASSERT(iCache != nullptr);
    while (iId.Bytes() == 0) {
        if (!iHeaderReadPending) {
            iCache->Inspect(iHeader, iHeader.MaxBytes());
            iHeaderReadPending = true;
        }
        Msg* msg = iCache->Pull();
        if (msg != nullptr) {
            return msg;
        }

        if (msg == nullptr && iHeader.Bytes() == 0) {
            // Was unable to read from cache.
            return nullptr;
        }

        iBytes = Converter::BeUint32At(iHeader, 0);
        iId.Set(iHeader.Ptr() + kSizeBytes, kNameBytes);
    }

    return nullptr;
}

TUint Mpeg4BoxHeaderReader::Bytes() const
{
    return iBytes;
}

TUint Mpeg4BoxHeaderReader::PayloadBytes() const
{
    if (iBytes < kHeaderBytes) {
        THROW(MediaMpeg4FileInvalid);
    }
    return iBytes - kHeaderBytes;
}

const Brx& Mpeg4BoxHeaderReader::Id() const
{
    return iId;
}

// Mpeg4BoxSwitcherRoot

const TChar* Mpeg4BoxSwitcherRoot::kNoTargetId = "";

Mpeg4BoxSwitcherRoot::Mpeg4BoxSwitcherRoot(IMpeg4BoxProcessorFactory& aProcessorFactory)
    : iProcessorFactory(aProcessorFactory)
    , iCache(nullptr)
    , iTargetId(kNoTargetId)
{
    Reset();
}

void Mpeg4BoxSwitcherRoot::Reset()
{
    iProcessor = nullptr;
    iState = eNone;
    iOffset = 0;
}

void Mpeg4BoxSwitcherRoot::Set(IMsgAudioEncodedCache& aCache, const TChar* aTargetId)
{
    iCache = &aCache;
    iTargetId.Set(aTargetId);
}

Msg* Mpeg4BoxSwitcherRoot::Process()
{
    while (iState != eComplete) {
        Msg* msg = nullptr;

        // All pulling calls below returns nullptr when there is something of interest for this class.
        if (iState == eHeader) {
            msg = iHeaderReader.ReadHeader();
        }
        else if (iState == eBox) {
            msg = iProcessor->Process();
        }

        if (msg != nullptr) {
            LOG(kCodec, "<Mpeg4BoxSwitcherRoot::Process pulled non-audio msg: %p\n", msg);
            return msg;
        }

        if (iState == eNone) {
            iHeaderReader.Reset(*iCache);
            iState = eHeader;
        }
        else if (iState == eHeader) {
            if (iHeaderReader.Bytes() == 0) {
                // Didn't manage to read header.
                return nullptr;
            }

            const Brx& id = iHeaderReader.Id();
            LOG(kCodec, "Mpeg4BoxSwitcherRoot::Process found box %.*s, %u bytes\n",
                        PBUF(id), iHeaderReader.Bytes());

            // Got header, now find a processor.
            try {
                iProcessor = &iProcessorFactory.GetMpeg4BoxProcessor(
                        iHeaderReader.Id(), iHeaderReader.PayloadBytes(),
                        *iCache);
                iState = eBox;
            }
            catch (Mpeg4BoxUnrecognised&) {
                LOG(kCodec, "Mpeg4BoxSwitcherRoot::Process couldn't find processor for %.*s, %u bytes\n",
                            PBUF(id), iHeaderReader.Bytes());

                iCache->Discard(iHeaderReader.PayloadBytes());
                iOffset += iHeaderReader.Bytes();
                iProcessor = nullptr;
                iHeaderReader.Reset(*iCache);
                iState = eHeader;
            }
        }
        else if (iState == eBox) {
            // If found target box ID, mark as complete. Otherwise, read next box.
            iOffset += iHeaderReader.Bytes();

            if (iHeaderReader.Id() == iTargetId) {
                iProcessor = nullptr;
                iHeaderReader.Reset(*iCache);
                iState = eComplete;
            }
            else {
                iProcessor = nullptr;
                iHeaderReader.Reset(*iCache);
                iState = eHeader;
            }
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TUint64 Mpeg4BoxSwitcherRoot::BoxOffset() const
{
    return iOffset;
}

// Mpeg4BoxSwitcher

Mpeg4BoxSwitcher::Mpeg4BoxSwitcher(IMpeg4BoxProcessorFactory& aProcessorFactory,
        const Brx& aBoxId) :
        iProcessorFactory(aProcessorFactory), iId(aBoxId)
{
    Reset();
}

Msg* Mpeg4BoxSwitcher::Process()
{
    while (!Complete()) {
        Msg* msg = nullptr;

        // All pulling calls below returns nullptr when there is something of interest for this class.
        if (iState == eHeader) {
            msg = iHeaderReader.ReadHeader();
        }
        else if (iState == eBox) {
            msg = iProcessor->Process();
        }

        if (msg != nullptr) {
            return msg;
        }

        if (iState == eNone) {
            iHeaderReader.Reset(*iCache);
            iState = eHeader;
        }
        else if (iState == eHeader) {
            const Brx& id = iHeaderReader.Id();
            LOG(kCodec, "Mpeg4BoxSwitcher::Process found box %.*s, %u bytes\n",
                        PBUF(id), iHeaderReader.Bytes());

            // Got header, now find a processor.
            try {
                iProcessor = &iProcessorFactory.GetMpeg4BoxProcessor(
                        iHeaderReader.Id(), iHeaderReader.PayloadBytes(),
                        *iCache);
                iState = eBox;
            } catch (Mpeg4BoxUnrecognised&) {
                LOG(kCodec, "Mpeg4BoxSwitcher::Process couldn't find processor for %.*s, %u bytes\n",
                            PBUF(id), iHeaderReader.Bytes());

                iCache->Discard(iHeaderReader.PayloadBytes());
                iOffset += iHeaderReader.Bytes();
                iProcessor = nullptr;
                iHeaderReader.Reset(*iCache);
                iState = eHeader;
            }
        }
        else if (iState == eBox) {
            // Box processing is complete.
            iOffset += iHeaderReader.Bytes();

            ASSERT(iOffset <= iBytes);
            if (iOffset == iBytes) {
                iState = eComplete;
            }
            else {
                // Read next box.
                iProcessor = nullptr;
                iHeaderReader.Reset(*iCache);
                iState = eHeader;
            }
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxSwitcher::Complete() const
{
    return iOffset == iBytes;
}

void Mpeg4BoxSwitcher::Reset()
{
    //iCache = nullptr;
    iProcessor = nullptr;
    iState = eNone;
    iBytes = 0;
    iOffset = 0;
}

TBool Mpeg4BoxSwitcher::Recognise(const Brx& aBoxId) const
{
    return aBoxId == iId;
}

void Mpeg4BoxSwitcher::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    iCache = &aCache;
    iBytes = aBoxBytes;
}

// Mpeg4BoxProcessorFactory

void Mpeg4BoxProcessorFactory::Add(IMpeg4BoxRecognisable* aProcessor)
{
    iProcessors.push_back(
            std::unique_ptr < IMpeg4BoxRecognisable > (aProcessor));
}

IMpeg4BoxProcessor& Mpeg4BoxProcessorFactory::GetMpeg4BoxProcessor(
        const Brx& aBoxId, TUint aBytes, IMsgAudioEncodedCache& aCache)
{
    for (auto& processor : iProcessors) {
        if (processor->Recognise(aBoxId)) {
            processor->Reset();
            processor->Set(aCache, aBytes);
            return *processor;
        }
    }
    THROW(Mpeg4BoxUnrecognised);
}

void Mpeg4BoxProcessorFactory::Reset()
{
    for (auto& processor : iProcessors) {
        processor->Reset();
    }
}


// Mpeg4BoxMoov

Mpeg4BoxMoov::Mpeg4BoxMoov(IMpeg4BoxProcessorFactory& aProcessorFactory, IMpeg4MetadataNotifiable& aMetadataNotifiable)
    : Mpeg4BoxSwitcher(aProcessorFactory, Brn("moov"))
    , iMetadataNotifiable(aMetadataNotifiable)
{
    Reset();
}

Msg* Mpeg4BoxMoov::Process()
{
    Msg* msg = Mpeg4BoxSwitcher::Process();
    if (msg == nullptr) {
        iMetadataRetrieved = true;
        iMetadataNotifiable.MetadataRetrieved();
    }
    return msg;
}

void Mpeg4BoxMoov::Reset()
{
    Mpeg4BoxSwitcher::Reset();
    iMetadataRetrieved = false;
}

TBool Mpeg4BoxMoov::Recognise(const Brx& aBoxId) const
{
    // Only recognise if metadata has not already been retrieved.
    // i.e., ignore metadata if it was already read out-of-band.
    if (Mpeg4BoxSwitcher::Recognise(aBoxId) && !iMetadataRetrieved) {
        return true;
    }
    return false;
}


// Mpeg4BoxMoof

Mpeg4BoxMoof::Mpeg4BoxMoof(IMpeg4BoxProcessorFactory& aProcessorFactory, Mpeg4ContainerInfo& aContainerInfo, IBoxOffsetProvider& aBoxOffsetProvider, SeekTable& aSeekTable)
    : Mpeg4BoxSwitcher(aProcessorFactory, Brn("moof"))
    , iContainerInfo(aContainerInfo)
    , iBoxOffsetProvider(aBoxOffsetProvider)
    , iSeekTable(aSeekTable)
{ }

void Mpeg4BoxMoof::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    Mpeg4BoxSwitcher::Set(aCache, aBoxBytes);
    iContainerInfo.SetFragmented(aBoxBytes + 8); // Include size + 'moof' bytes here
    iContainerInfo.SetFirstMoofStart(iBoxOffsetProvider.BoxOffset());
    iSeekTable.SetIsFragmentedStream(true);
}

void Mpeg4BoxMoof::Reset()
{
    Mpeg4BoxSwitcher::Reset();
    iContainerInfo.Reset();
}

// Mpeg4BoxSidx

Mpeg4BoxSidx::Mpeg4BoxSidx(SeekTable& aSeekTable)
    : iSeekTable(aSeekTable)
    , iCache(nullptr)
    , iBytes(0)
    , iOffset(0)
{
}

Msg* Mpeg4BoxSidx::Process()
{
    // Table of audio samples per sample - used to convert audio samples to codec samples.

    while (!Complete()) {
        if (iState != eNone) {
            Msg* msg = iCache->Pull();
            if (msg != nullptr) {
                return msg;
            }
        }

        if (iState == eNone) {
            iCache->Inspect(iBuf, 4);
            iState = eVersion;
        }
        else if (iState == eVersion) {
            iOffset += iBuf.Bytes();
            iVersion = Converter::BeUint32At(iBuf, 0);

            if (iVersion < 0 || iVersion > 1) {
                Log::Print("Mpeg4BoxSidx::Process - Unsupported version (%u) found.\n", iVersion);
                THROW(MediaMpeg4FileInvalid);
            }

            // Skip reference ID
            iCache->Discard(4);
            iOffset += 4;

            iCache->Inspect(iBuf, 4);
            iState = eTimescale;
        }
        else if (iState == eTimescale) {
            iOffset += iBuf.Bytes();
            iTimescale = Converter::BeUint32At(iBuf, 0);

            if (iVersion == 0) {
                iCache->Discard(4); // Skip earliest_presentation_time;
                iOffset += 4;

                iCache->Inspect(iBuf, 4);
            }
            else {
                iCache->Discard(8);
                iOffset += 8;

                iCache->Inspect(iBuf, 8);
            }

            iState = eFirstOffset;
        }
        else if (iState == eFirstOffset) {
            iOffset += iBuf.Bytes();

            if (iVersion == 0) {
                iFirstOffset = (TUint64)Converter::BeUint32At(iBuf, 0);
            }
            else {
                iFirstOffset = Converter::BeUint64At(iBuf, 0);
            }

            iCache->Discard(2); // Skip reserved
            iOffset += 2;

            iCache->Inspect(iBuf, 2);
            iState = eSegmentCount;
        }
        else if (iState == eSegmentCount) {
            iOffset += iBuf.Bytes();

            iSegmentsTotal       = Converter::BeUint16At(iBuf, 0);
            iSegmentsLeftToParse = iSegmentsTotal;

            if (iSegmentsLeftToParse > 0) {
                iCache->Inspect(iBuf, 12);
                iState = eSegment;
            }
            else {
                iState = eComplete;
            }
        }
        else if (iState == eSegment) {
            iOffset += iBuf.Bytes();

            const TUint part1 = Converter::BeUint32At(iBuf, 0);
            const TUint part2 = Converter::BeUint32At(iBuf, 4);
            //const TUint part3 = Converter::BeUint32At(iBuf, 8);

            // Part1:
            // - ReferenceType  = Bit (1)
            // - ReferencedSize = unsigned int (31)
            //const TUint referenceType  = (part1 & 0x80000000) >> 31;
            const TUint referencedSize = (part1 & 0x7FFFFFFF);

            // Part2:
            // subSegmentDuration = unsigned int (32)
            const TUint subSegmentDuration = part2;

            // Part3:
            // - StartsWithSAP = Bit (1)
            // - SAPType       = unsigned int (3)
            // - SAPDeltaTime  = unsigned int (28)
            //const TUint startsWithSAP = (part3 & 0x80000000) >> 31;
            //const TUint SAPType       = (part3 & 0x70000000) >> 28;
            //const TUint SAPDeltaTime  = (part3 & 0x0FFFFFFF);

            // NOTE: Here we set:
            // - FirstChunk             = Segment index
            // - SamplesPerChunk        = SegmentDuration
            // - SampleDescriptionIndex = 0 (Ignored)
            const TUint segmentDuration = subSegmentDuration / iTimescale;
            iSeekTable.SetSamplesPerChunk(iSegmentsTotal - iSegmentsLeftToParse,
                                          segmentDuration,
                                          0);

            iSeekTable.SetOffset(referencedSize);

            iSegmentsLeftToParse -= 1;

            if (iSegmentsLeftToParse > 0) {
                iCache->Inspect(iBuf, 12);
            }
            else {
                iState = eComplete;
            }
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxSidx::Complete() const
{
    return iOffset == iBytes;
}

void Mpeg4BoxSidx::Reset()
{
    iCache = nullptr;
    iState = eNone;
    iBytes = 0;
    iOffset = 0;
    iVersion = 0;
    iTimescale = 0;
    iFirstOffset = 0;
    iSegmentsTotal = 0;
    iSegmentsLeftToParse = 0;
}

TBool Mpeg4BoxSidx::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("sidx");
}

void Mpeg4BoxSidx::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
}


// Mpeg4BoxStts

Mpeg4BoxStts::Mpeg4BoxStts(SeekTable& aSeekTable)
    : iSeekTable(aSeekTable)
    , iCache(nullptr)
    , iBytes(0)
    , iOffset(0)
    , iSampleCount(0)
{
}

Msg* Mpeg4BoxStts::Process()
{
    // Table of audio samples per sample - used to convert audio samples to codec samples.

    while (!Complete()) {
        if (iState != eNone) {
            Msg* msg = iCache->Pull();
            if (msg != nullptr) {
                return msg;
            }
        }

        if (iState == eNone) {
            iCache->Inspect(iBuf, iBuf.MaxBytes());
            iState = eVersion;
        }
        else if (iState == eVersion) {
            iOffset += iBuf.Bytes();
            const TUint version = Converter::BeUint32At(iBuf, 0);
            if (version != kVersion) {
                iCache->Discard(iBytes - iOffset);
                iOffset = iBytes;
                THROW(MediaMpeg4FileInvalid);
            }
            iCache->Inspect(iBuf, iBuf.MaxBytes());
            iState = eEntries;
        }
        else if (iState == eEntries) {
            iOffset += iBuf.Bytes();
            iEntries = Converter::BeUint32At(iBuf, 0);
            iEntryCount = 0;
            iSeekTable.InitialiseAudioSamplesPerSample(iEntries);

            if (iEntries > 0) {
                iCache->Inspect(iBuf, iBuf.MaxBytes());
                iState = eSampleCount;
            }
            else {
                iState = eComplete;
            }
        }
        else if (iState == eSampleCount) {
            iOffset += iBuf.Bytes();
            iSampleCount = Converter::BeUint32At(iBuf, 0);
            iCache->Inspect(iBuf, iBuf.MaxBytes());
            iState = eSampleDelta;
        }
        else if (iState == eSampleDelta) {
            iOffset += iBuf.Bytes();
            const TUint sampleDelta = Converter::BeUint32At(iBuf, 0);
            iSeekTable.SetAudioSamplesPerSample(iSampleCount,
                sampleDelta);
            iSampleCount = 0;

            iEntryCount++;
            if (iEntryCount < iEntries) {
                iCache->Inspect(iBuf, iBuf.MaxBytes());
                iState = eSampleCount;
            }
            else {
                if (!Complete()) {
                    iCache->Discard(iBytes - iOffset);
                    iOffset = iBytes;
                    THROW(MediaMpeg4FileInvalid);
                }
                iState = eComplete;
            }
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxStts::Complete() const
{
    return iOffset == iBytes;
}

void Mpeg4BoxStts::Reset()
{
    //iSeekTable.Deinitialise();
    iCache = nullptr;
    iState = eNone;
    iBytes = 0;
    iOffset = 0;
    iEntries = 0;
    iEntryCount = 0;
    iSampleCount = 0;
}

TBool Mpeg4BoxStts::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("stts");
}

void Mpeg4BoxStts::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
}

// Mpeg4BoxStsc

Mpeg4BoxStsc::Mpeg4BoxStsc(SeekTable& aSeekTable)
    : iSeekTable(aSeekTable)
{
    Reset();
}

Msg* Mpeg4BoxStsc::Process()
{
    // Table of samples per chunk - used to seek to specific sample.

    while (!Complete()) {
        if (iState != eNone) {
            Msg* msg = iCache->Pull();
            if (msg != nullptr) {
                return msg;
            }
        }

        if (iState == eNone) {
            iCache->Inspect(iBuf, iBuf.MaxBytes());
            iState = eVersion;
        }
        else if (iState == eVersion) {
            iOffset += iBuf.Bytes();
            const TUint version = Converter::BeUint32At(iBuf, 0);
            if (version != kVersion) {
                iCache->Discard(iBytes - iOffset);
                iOffset = iBytes;
                THROW(MediaMpeg4FileInvalid);
            }
            iCache->Inspect(iBuf, iBuf.MaxBytes());
            iState = eEntries;
        }
        else if (iState == eEntries) {
            iOffset += iBuf.Bytes();
            iEntries = Converter::BeUint32At(iBuf, 0);
            iEntryCount = 0;
            iSeekTable.InitialiseSamplesPerChunk(iEntries);

            if (iEntries > 0) {
                iCache->Inspect(iBuf, iBuf.MaxBytes());
                iState = eFirstChunk;
            }
            else {
                iState = eComplete;
            }
        }
        else if (iState == eFirstChunk) {
            iOffset += iBuf.Bytes();
            iFirstChunk = Converter::BeUint32At(iBuf, 0);
            iCache->Inspect(iBuf, iBuf.MaxBytes());
            iState = eSamplesPerChunk;
        }
        else if (iState == eSamplesPerChunk) {
            iOffset += iBuf.Bytes();
            iSamplesPerChunk = Converter::BeUint32At(iBuf, 0);
            iCache->Inspect(iBuf, iBuf.MaxBytes());
            iState = eSampleDescriptionIndex;
        }
        else if (iState == eSampleDescriptionIndex) {
            iOffset += iBuf.Bytes();
            iSampleDescriptionIndex = Converter::BeUint32At(iBuf, 0);
            iSeekTable.SetSamplesPerChunk(iFirstChunk, iSamplesPerChunk,
                    iSampleDescriptionIndex);

            iEntryCount++;
            if (iEntryCount < iEntries) {
                iCache->Inspect(iBuf, iBuf.MaxBytes());
                iState = eFirstChunk;
            }
            else {
                if (!Complete()) {
                    iCache->Discard(iBytes - iOffset);
                    iOffset = iBytes;
                    THROW(MediaMpeg4FileInvalid);
                }
                iState = eComplete;
            }
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxStsc::Complete() const
{
    ASSERT(iOffset <= iBytes);
    return iOffset == iBytes;
}

void Mpeg4BoxStsc::Reset()
{
    iCache = nullptr;
    iState = eNone;
    iBytes = 0;
    iOffset = 0;
    iBuf.SetBytes(0);
    iEntries = 0;
    iEntryCount = 0;
    iFirstChunk = 0;
    iSamplesPerChunk = 0;
    iSampleDescriptionIndex = 0;
}

TBool Mpeg4BoxStsc::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("stsc");
}

void Mpeg4BoxStsc::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
}

// Mpeg4BoxStco

Mpeg4BoxStco::Mpeg4BoxStco(SeekTable& aSeekTable)
    : iSeekTable(aSeekTable)
{
    Reset();
}

Msg* Mpeg4BoxStco::Process()
{
    // Table of file offsets for each chunk (32-bit offsets).

    while (!Complete()) {
        if (iState != eNone) {
            Msg* msg = iCache->Pull();
            if (msg != nullptr) {
                return msg;
            }
        }

        if (iState == eNone) {
            iCache->Inspect(iBuf, iBuf.MaxBytes());
            iState = eVersion;
        }
        else if (iState == eVersion) {
            iOffset += iBuf.Bytes();
            const TUint version = Converter::BeUint32At(iBuf, 0);
            if (version != kVersion) {
                iCache->Discard(iBytes - iOffset);
                iOffset = iBytes;
                THROW(MediaMpeg4FileInvalid);
            }
            iCache->Inspect(iBuf, iBuf.MaxBytes());
            iState = eEntries;
        }
        else if (iState == eEntries) {
            iOffset += iBuf.Bytes();
            iEntries = Converter::BeUint32At(iBuf, 0);
            iEntryCount = 0;
            iSeekTable.InitialiseOffsets(iEntries);

            if (iEntries > 0) {
                iCache->Inspect(iBuf, iBuf.MaxBytes());
                iState = eChunkOffset;
            }
            else {
                iState = eComplete;
            }
        }
        else if (iState == eChunkOffset) {
            iOffset += iBuf.Bytes();
            const TUint offset = Converter::BeUint32At(iBuf, 0);
            iSeekTable.SetOffset(offset);

            iEntryCount++;
            if (iEntryCount < iEntries) {
                iCache->Inspect(iBuf, iBuf.MaxBytes());
                iState = eChunkOffset;
            }
            else {
                if (!Complete()) {
                    iCache->Discard(iBytes - iOffset);
                    iOffset = iBytes;
                    THROW(MediaMpeg4FileInvalid);
                }
                iState = eComplete;
            }
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxStco::Complete() const
{
    ASSERT(iOffset <= iBytes);
    return iOffset == iBytes;
}

void Mpeg4BoxStco::Reset()
{
    iCache = nullptr;
    iState = eNone;
    iBytes = 0;
    iOffset = 0;
    iBuf.SetBytes(0);
}

TBool Mpeg4BoxStco::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("stco");
}

void Mpeg4BoxStco::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
}

// Mpeg4BoxCo64

Mpeg4BoxCo64::Mpeg4BoxCo64(SeekTable& aSeekTable)
    : iSeekTable(aSeekTable)
{
    Reset();
}

Msg* Mpeg4BoxCo64::Process()
{
    // Table of file offsets for each chunk (64-bit offsets).

    while (!Complete()) {
        if (iState != eNone) {
            Msg* msg = iCache->Pull();
            if (msg != nullptr) {
                return msg;
            }
        }

        if (iState == eNone) {
            iCache->Inspect(iBuf32, iBuf32.MaxBytes());
            iState = eVersion;
        }
        else if (iState == eVersion) {
            iOffset += iBuf32.Bytes();
            const TUint version = Converter::BeUint32At(iBuf32, 0);
            if (version != kVersion) {
                iCache->Discard(iBytes - iOffset);
                iOffset = iBytes;
                THROW(MediaMpeg4FileInvalid);
            }
            iCache->Inspect(iBuf32, iBuf32.MaxBytes());
            iState = eEntries;
        }
        else if (iState == eEntries) {
            iOffset += iBuf32.Bytes();
            iEntries = Converter::BeUint32At(iBuf32, 0);
            iEntryCount = 0;
            iSeekTable.InitialiseOffsets(iEntries);

            if (iEntries > 0) {
                iCache->Inspect(iBuf64, iBuf64.MaxBytes());
                iState = eChunkOffset;
            }
            else {
                iState = eComplete;
            }
        }
        else if (iState == eChunkOffset) {
            iOffset += iBuf64.Bytes();
            const TUint64 offset = Converter::BeUint64At(iBuf64, 0);
            iSeekTable.SetOffset(offset);

            iEntryCount++;
            if (iEntryCount < iEntries) {
                iCache->Inspect(iBuf64, iBuf64.MaxBytes());
                iState = eChunkOffset;
            }
            else {
                if (!Complete()) {
                    iCache->Discard(iBytes - iOffset);
                    iOffset = iBytes;
                    THROW(MediaMpeg4FileInvalid);
                }
                iState = eComplete;
            }
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxCo64::Complete() const
{
    ASSERT(iOffset <= iBytes);
    return iOffset == iBytes;
}

void Mpeg4BoxCo64::Reset()
{
    iCache = nullptr;
    iState = eNone;
    iBytes = 0;
    iOffset = 0;
    iBuf32.SetBytes(0);
    iBuf64.SetBytes(0);
    iEntries = 0;
    iEntryCount = 0;
}

TBool Mpeg4BoxCo64::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("co64");
}

void Mpeg4BoxCo64::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
}

// Mpeg4BoxStsz

Mpeg4BoxStsz::Mpeg4BoxStsz(SampleSizeTable& aSampleSizeTable)
    : iSampleSizeTable(aSampleSizeTable)
{
    Reset();
}

Msg* Mpeg4BoxStsz::Process()
{
    while (!Complete()) {
        if (iState != eNone) {
            Msg* msg = iCache->Pull();
            if (msg != nullptr) {
                return msg;
            }
        }

        if (iState == eNone) {
            iCache->Inspect(iBuf, iBuf.MaxBytes());
            iState = eVersion;
        }
        else if (iState == eVersion) {
            iOffset += iBuf.Bytes();
            const TUint version = Converter::BeUint32At(iBuf, 0);
            if (version != kVersion) {
                iCache->Discard(iBytes - iOffset);
                iOffset = iBytes;
                THROW(MediaMpeg4FileInvalid);
            }
            iCache->Inspect(iBuf, iBuf.MaxBytes());
            iState = eSampleSize;
        }
        else if (iState == eSampleSize) {
            iOffset += iBuf.Bytes();
            iSampleSize = Converter::BeUint32At(iBuf, 0);
            iCache->Inspect(iBuf, iBuf.MaxBytes());
            iState = eEntryCount;
        }
        else if (iState == eEntryCount) {
            iOffset += iBuf.Bytes();
            const TUint entries = Converter::BeUint32At(iBuf, 0);

            // NOTE: Previously we'd only continue here if entries > 0.
            //       However, in the case of 'moof' based streams, each 'moof' box containers
            //       the details for that particular fragment. Checking if entries > 0 is no
            //       longer valud at this point.

            if (iSampleSizeTable.Count() > 0) {
                // Table already initialised.
                // Can't currently play all files with >1 "trak" atoms, so
                // give up on this file.

                // FIXME - See #4779.
                iCache->Discard(iBytes - iOffset);
                iOffset = iBytes;
                THROW(MediaMpeg4FileInvalid);
            }

            iSampleSizeTable.Init(entries);

            // If iSampleSize == 0, there follows an array of sample size entries.
            // If iSampleSize > 0, there are <entries> entries each of size <iSampleSize> (and no array follows).
            if (iSampleSize > 0) {
                // Sample size table currently doesn't support a cheap way of having
                // a fixed iSampleSize, so just perform a pseudo-population of it here.
                for (TUint i=0; i<entries; i++) {
                    iSampleSizeTable.AddSampleSize(iSampleSize);
                }
                iState = eComplete;
            }
            else if (entries == 0) { // Spec Link (8.7.3.2.2)
                iState = eComplete;
            }
            else {
                // Array of sample size entries follows; prepare to read it.
                iCache->Inspect(iBuf, iBuf.MaxBytes());
                iState = eEntry;
            }
        }
        else if (iState == eEntry) {
            iOffset += iBuf.Bytes();
            const TUint entrySize = Converter::BeUint32At(iBuf, 0);
            iSampleSizeTable.AddSampleSize(entrySize);

            ASSERT(iOffset <= iBytes);
            if (iOffset == iBytes) {
                iState = eComplete;
            }
            else {
                iCache->Inspect(iBuf, iBuf.MaxBytes());
            }
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxStsz::Complete() const
{
    ASSERT(iOffset <= iBytes);
    return iOffset == iBytes;
}

void Mpeg4BoxStsz::Reset()
{
    iCache = nullptr;
    iState = eNone;
    iBytes = 0;
    iOffset = 0;
    iBuf.SetBytes(0);
    iSampleSize = 0;
}

TBool Mpeg4BoxStsz::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("stsz");
}

void Mpeg4BoxStsz::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
}


// Mpeg4BoxTfhd

const TUint Mpeg4BoxTfhd::kFlagBaseDataOffsetPresent           = 1 << 0;
const TUint Mpeg4BoxTfhd::kFlagSampleDescriptionIndexPresent   = 1 << 1;
const TUint Mpeg4BoxTfhd::kFlagDefaultSampleDurationPresent    = 1 << 2;
const TUint Mpeg4BoxTfhd::kFlagDefaultSampleSizePresent        = 1 << 3;
const TUint Mpeg4BoxTfhd::kFlagDefaultSampleFlagsPresent       = 1 << 4;

Mpeg4BoxTfhd::Mpeg4BoxTfhd(SampleSizeTable& aSampleSizeTable, Mpeg4ContainerInfo& aContainerInfo)
    : iSampleSizeTable(aSampleSizeTable)
    , iContainerInfo(aContainerInfo)
{
    Reset();
}

Msg* Mpeg4BoxTfhd::Process()
{
    while (!Complete()) {
        if (iState != eNone) {
            Msg* msg = iCache->Pull();
            if (msg != nullptr) {
                return msg;
            }
        }

        if (iState == eNone) {
            iCache->Inspect(iBuf, iBuf.MaxBytes());
            iState = eFlags;
        }
        else if (iState == eFlags) {
            iOffset += iBuf.Bytes();

            const TUint flags = Converter::BeUint32At(iBuf, 0);

            iFlags |= (flags & 0x000001) ? kFlagBaseDataOffsetPresent         : 0;
            iFlags |= (flags & 0x000002) ? kFlagSampleDescriptionIndexPresent : 0;
            iFlags |= (flags & 0x000008) ? kFlagDefaultSampleDurationPresent  : 0;
            iFlags |= (flags & 0x000010) ? kFlagDefaultSampleSizePresent      : 0;
            iFlags |= (flags & 0x000020) ? kFlagDefaultSampleFlagsPresent     : 0;

            // NOTE: duration_is_empty flag is currently ignored and unused

            const TBool defaultBaseIsMoof     = flags & 0x020000;
            const TBool baseDataOffsetPresent = iFlags & kFlagBaseDataOffsetPresent;

            if (defaultBaseIsMoof && baseDataOffsetPresent == 0) {
                iContainerInfo.SetDefaultBaseIsMoof();
            }

            TUint bytesToDiscard = 4; // Ignore 'TrackId;
            iOffset += 4;

            if (baseDataOffsetPresent) {
                iCache->Discard(bytesToDiscard);
                iCache->Inspect(iBuf64, iBuf64.MaxBytes());
                iState = eBaseDataOffset;
                continue;
            }

            if (iFlags & kFlagSampleDescriptionIndexPresent) {
                // SampleDescriptionIndex is currently ignored.
                iOffset        += 4;
                bytesToDiscard += 4;
            }

            if (iFlags & kFlagDefaultSampleDurationPresent) {
                // DefaultSampleDuration is currently ignored
                iOffset        += 4;
                bytesToDiscard += 4;
            }

            if (iFlags & kFlagDefaultSampleSizePresent) {
                iCache->Discard(bytesToDiscard);
                iCache->Inspect(iBuf, iBuf.MaxBytes());
                iState = eDefaultSampleSize;
                continue;
            }

            if (iFlags & kFlagDefaultSampleFlagsPresent) {
                // DefaultSampleFlags is currently ignored
                iOffset        += 4;
                bytesToDiscard += 4;
            }

            iCache->Discard(bytesToDiscard);
            iState = eComplete;
        }

        else if (iState == eBaseDataOffset) {
            ASSERT(iFlags & kFlagBaseDataOffsetPresent);

            iOffset += iBuf64.Bytes();

            iContainerInfo.SetBaseDataOffset(Converter::BeUint64At(iBuf64, 0));

            TUint bytesToDiscard = 0;
            if (iFlags & kFlagSampleDescriptionIndexPresent) {
                // SampleDescriptionIndex is currently ignored.
                iOffset        += 4;
                bytesToDiscard += 4;
            }

            if (iFlags & kFlagDefaultSampleDurationPresent) {
                // DefaultSampleDuration is currently ignored
                iOffset        += 4;
                bytesToDiscard += 4;
            }

            if (iFlags & kFlagDefaultSampleSizePresent) {
                iCache->Discard(bytesToDiscard);
                iCache->Inspect(iBuf, iBuf.MaxBytes());
                iState = eDefaultSampleSize;
                continue;
            }

            if (iFlags & kFlagDefaultSampleFlagsPresent) {
                // DefaultSampleFlags is currently ignored
                iOffset        += 4;
                bytesToDiscard += 4;
            }

            iCache->Discard(bytesToDiscard);
            iState = eComplete;
        }
        else if (iState == eDefaultSampleSize) {
            ASSERT(iFlags & kFlagDefaultSampleSizePresent);

            iOffset += iBuf.MaxBytes();

            iSampleSizeTable.SetDefaultSampleSize(Converter::BeUint32At(iBuf, 0));

            if (iFlags & kFlagDefaultSampleFlagsPresent) {
                // DefaultSampleFlags is currently ignored
                iOffset += 4;
                iCache->Discard(4);
            }

            iState = eComplete;
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxTfhd::Complete() const
{
    ASSERT(iOffset <= iBytes);
    return iOffset == iBytes;
}

void Mpeg4BoxTfhd::Reset()
{
    iCache  = nullptr;
    iState  = eNone;
    iBytes  = 0;
    iOffset = 0;
    iFlags  = 0;

    iSampleSizeTable.SetDefaultSampleSize(0);
    iBuf.SetBytes(0);
    iBuf64.SetBytes(0);
}

TBool Mpeg4BoxTfhd::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("tfhd");
}

void Mpeg4BoxTfhd::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
}


// Mpeg4BoxTrun

const TUint Mpeg4BoxTrun::kFlagDataOffsetPresent       = 1 << 0;
const TUint Mpeg4BoxTrun::kFlagFirstSampleFlagsPresent = 1 << 1;
const TUint Mpeg4BoxTrun::kFlagSampleDurationPresent   = 1 << 2;
const TUint Mpeg4BoxTrun::kFlagSampleSizePresent       = 1 << 3;

Mpeg4BoxTrun::Mpeg4BoxTrun(SampleSizeTable& aSampleSizeTable, Mpeg4ContainerInfo& aContainerInfo)
    : iSampleSizeTable(aSampleSizeTable)
    , iContainerInfo(aContainerInfo)
{
    Reset();
}

Msg* Mpeg4BoxTrun::Process()
{
    while (!Complete()) {
        if (iState != eNone) {
            Msg* msg = iCache->Pull();
            if (msg != nullptr) {
                return msg;
            }
        }

        if (iState == eNone) {
            iCache->Inspect(iBuf, iBuf.MaxBytes());
            iState = eVersionAndFlags;
        }
        else if (iState == eVersionAndFlags) {
            iOffset += iBuf.Bytes();

            const TUint versionAndFlags = Converter::BeUint32At(iBuf, 0);
            const TUint flags           =  versionAndFlags & 0x0FFF;
            // const TUint version         = (versionAndFlags & 0xF000) >> 24; // Unused, only need to read the sample_composition_time entry which we ignore

            iFlags |= (flags & 0x000001) ? kFlagDataOffsetPresent       : 0;
            iFlags |= (flags & 0x000004) ? kFlagFirstSampleFlagsPresent : 0;
            iFlags |= (flags & 0x000100) ? kFlagSampleDurationPresent   : 0;
            iFlags |= (flags & 0x000200) ? kFlagSampleSizePresent       : 0;

            const TBool sampleFlagsPresent           = flags & 0x000400;
            const TBool sampleCompositionTimePresent = flags & 0x000800;

            // Work out the size of each sample entry, as each of the 4 fields are optional...
            if (iFlags & kFlagSampleDurationPresent) {
                iEntryBytes += 4;
            }

            if (iFlags & kFlagSampleSizePresent) {
                iEntryBytes += 4;
            }

            if (sampleFlagsPresent) {
                iEntryBytes += 4;
            }

            if (sampleCompositionTimePresent) {
                iEntryBytes += 4;
            }

            const TBool entriesAreEmpty      = iEntryBytes == 0;
            const TBool hasDefaultSampleSize = iSampleSizeTable.DefaultSampleSize() > 0;

            if (entriesAreEmpty) {
                if (hasDefaultSampleSize) {
                    LOG_TRACE(kCodec, "Mpeg4BoxTrun::Process - Sample table is empty, each sample will be use default sample size of: %u\n", iSampleSizeTable.DefaultSampleSize());
                }
                else {
                    LOG_ERROR(kCodec, "Mpeg4BoxTrun::Process - Sample entires have no fields and no default sample size has been set!\n");
                    THROW(MediaMpeg4FileInvalid);
                }
            }

            iCache->Inspect(iBuf, iBuf.MaxBytes()); // Set to read sample_count
            iState = eSampleCount;
        }
        else if (iState == eSampleCount) {
            iOffset += iBuf.Bytes();

            iSampleCount = Converter::BeUint32At(iBuf, 0);
            if (iSampleCount == 0) {
                LOG_ERROR(kCodec, "Mpeg4BoxTrun::Process - Run reports 0 samples. We don't support this type of MPEG stream.\n");
                THROW(MediaMpeg4FileInvalid);
            }

            iSampleSizeTable.Clear();
            iSampleSizeTable.Init(iSampleCount);

            const TBool entriesUseDefaultSize = iEntryBytes == 0;
            if (entriesUseDefaultSize) {
                ASSERT(iSampleSizeTable.DefaultSampleSize() > 0);
                const TUint defaultSampleSize = iSampleSizeTable.DefaultSampleSize();
                for(TUint i = 0; i < iSampleCount; i += 1) {
                    iSampleSizeTable.AddSampleSize(defaultSampleSize);
                }
            }

            if (iFlags & kFlagDataOffsetPresent) {
                iCache->Inspect(iBuf, iBuf.MaxBytes());
                iState = eDataOffset;
                continue;
            }

            if (iFlags & kFlagFirstSampleFlagsPresent) {
                // FirstSampleFlags are currently ignored
                iCache->Discard(4);
                iOffset += 4;
            }

            if (entriesUseDefaultSize) {
                ASSERT(Complete());
                iState = eComplete;
            }
            else {
                iCache->Inspect(iEntryBuf, iEntryBytes);
                iState = eEntries;
            }

        }
        else if (iState == eDataOffset) {
            ASSERT(iFlags & kFlagDataOffsetPresent);

            iOffset += iBuf.Bytes();

            iContainerInfo.SetDataOffset((TUint64)Converter::BeUint32At(iBuf, 0));

            if (iFlags & kFlagFirstSampleFlagsPresent) {
                // FirstSampleFlags are currently ignored
                iCache->Discard(4);
                iOffset += 4;
            }

            const TBool entriesUseDefaultSize = iEntryBytes == 0;
            if (entriesUseDefaultSize) {
                ASSERT(Complete());
                iState = eComplete;
            }
            else {
                iCache->Inspect(iEntryBuf, iEntryBytes);
                iState = eEntries;
            }
        }
        else if (iState == eEntries) {
            TUint offset = 0;
            TUint sampleSize = iSampleSizeTable.DefaultSampleSize();

            if (iFlags & kFlagSampleDurationPresent) {
                // Unused
                offset += 4;
            }

            if (iFlags & kFlagSampleSizePresent) {
                sampleSize = Converter::BeUint32At(iEntryBuf, offset);
                offset += 4;
            }

            // Sample flags & composition time are unused, so we don't bother trying to read them.

            iSampleSizeTable.AddSampleSize(sampleSize);

            iSamplesRead += 1;
            iOffset      += iEntryBytes;

            if (iSamplesRead == iSampleCount) {
                iState = eComplete;
            }
            else {
                iCache->Inspect(iEntryBuf, iEntryBytes);
            }
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxTrun::Complete() const
{
    ASSERT(iOffset <= iBytes);
    return iOffset == iBytes;
}

void Mpeg4BoxTrun::Reset()
{
    iCache       = nullptr;
    iState       = eNone;
    iBytes       = 0;
    iOffset      = 0;
    iFlags       = 0;
    iEntryBytes  = 0;
    iSampleCount = 0;
    iSamplesRead = 0;

    iBuf.SetBytes(0);
}

TBool Mpeg4BoxTrun::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("trun");
}

void Mpeg4BoxTrun::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
}


// Mpeg4BoxMdhd

Mpeg4BoxMdhd::Mpeg4BoxMdhd(IMpeg4DurationSettable& aDurationSettable) :
        iDurationSettable(aDurationSettable)
{
    Reset();
}

Msg* Mpeg4BoxMdhd::Process()
{
    while (!Complete()) {
        if (iState != eNone) {
            Msg* msg = iCache->Pull();
            if (msg != nullptr) {
                return msg;
            }
        }

        if (iState == eNone) {
            iCache->Inspect(iBuf32, iBuf32.MaxBytes());  // Set to read version.
            iState = eVersion;
        }
        else if (iState == eVersion) {
            iOffset += iBuf32.Bytes();
            iVersion = Converter::BeUint32At(iBuf32, 0);
            if (iVersion != kVersion32 && iVersion != kVersion64) {
                iCache->Discard(iBytes - iOffset);
                iOffset = iBytes;
                THROW(MediaMpeg4FileInvalid);
            }

            TUint discard = 0;
            if (iVersion == kVersion32) {
                discard = 8;     // Discard creation time and modification time.
            }
            else {
                discard = 16;    // Discard creation time and modification time.
            }
            iCache->Discard(discard);
            iOffset += discard;
            iCache->Inspect(iBuf32, iBuf32.MaxBytes()); // Set to read timescale.
            iState = eTimescale;
        }
        else if (iState == eTimescale) {
            iOffset += iBuf32.Bytes();
            const TUint timescale = Converter::BeUint32At(iBuf32, 0);
            iDurationSettable.SetTimescale(timescale);
            if (iVersion == kVersion32) {
                iCache->Inspect(iBuf32, iBuf32.MaxBytes());
            }
            else {
                iCache->Inspect(iBuf64, iBuf64.MaxBytes());
            }
            iState = eDuration;
        }
        else if (iState == eDuration) {
            TUint64 duration = 0;
            if (iVersion == kVersion32) {
                iOffset += iBuf32.Bytes();
                duration = Converter::BeUint32At(iBuf32, 0);
            }
            else {
                iOffset += iBuf64.Bytes();
                duration = Converter::BeUint64At(iBuf64, 0);
            }

            // NOTE: For 'moof' based streams, the duration might be present in 'tkhd' or 'mehd' boxes
            //       and so what we read here is 0. We don't want to set it unless there's something reasonable.
            if (duration > 0) {
                iDurationSettable.SetDuration(duration);
            }

            if (iOffset < iBytes) {
                const TUint discard = iBytes - iOffset;
                iCache->Discard(discard);
                iOffset += discard;

            }
            iState = eComplete;
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxMdhd::Complete() const
{
    ASSERT(iOffset <= iBytes);
    return iOffset == iBytes;
}

void Mpeg4BoxMdhd::Reset()
{
    iCache = nullptr;
    iState = eNone;
    iBytes = 0;
    iOffset = 0;
    iBuf32.SetBytes(0);
    iBuf64.SetBytes(0);
    iVersion = 0;
}

TBool Mpeg4BoxMdhd::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("mdhd");
}

void Mpeg4BoxMdhd::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
}


// Mpeg4BoxTkhd

Mpeg4BoxTkhd::Mpeg4BoxTkhd(IMpeg4DurationSettable& aDurationSettable) :
    iDurationSettable(aDurationSettable)
{
    Reset();
}

Msg* Mpeg4BoxTkhd::Process()
{
    while (!Complete()) {
        if (iState != eNone) {
            Msg* msg = iCache->Pull();
            if (msg != nullptr) {
                return msg;
            }
        }

        if (iState == eNone) {
            iCache->Inspect(iBuf32, iBuf32.MaxBytes());  // Set to read version.
            iState = eFlagsAndVersion;
        }
        else if (iState == eFlagsAndVersion) {
            iOffset += iBuf32.Bytes();

            TUint discard               = 0;
            const TUint versionAndFlags = Converter::BeUint32At(iBuf32, 0);
            const TUint flags           = (versionAndFlags & 0x0FFF);
            iVersion                    = (versionAndFlags & 0xF000) >> 24; // NOTE: We ignore the flags here.

            if ((flags & 0x000001) == 0) {
                // Track is disabled. Ignore!
                iCache->Discard(iBytes - iOffset);
                iOffset = iBytes;
                iState = eComplete;
            }
            else {
                discard = iVersion == kVersion32 ? 8
                                                 : 16; // Discard creation time and modification time.
                discard += 4;                          // Discard track_id
                discard += 4;                          // Discard __reserved__
            }

            iCache->Discard(discard);
            iOffset += discard;

            // Set to read duration...
            if (iVersion == kVersion32) {
                iCache->Inspect(iBuf32, iBuf32.MaxBytes());
            }
            else {
                iCache->Inspect(iBuf64, iBuf64.Bytes());
            }

            iState = eDuration;
        }
        else if (iState == eDuration) {
            TUint64 duration = 0;
            if (iVersion == kVersion32) {
                iOffset += iBuf32.Bytes();
                duration = Converter::BeUint32At(iBuf32, 0);
            }
            else {
                iOffset += iBuf64.Bytes();
                duration = Converter::BeUint64At(iBuf64, 0);
            }

            // NOTE: This box + field are optional, so we only want to set it if
            //       present to a reasonable value...
            if (duration > 0) {
                iDurationSettable.SetDuration(duration);
            }

            // Discard the rest of the box
            if (iOffset < iBytes) {
                const TUint discard = iBytes - iOffset;
                iCache->Discard(discard);
                iOffset += discard;

            }
            iState = eComplete;
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxTkhd::Complete() const
{
    ASSERT(iOffset <= iBytes);
    return iOffset == iBytes;
}

void Mpeg4BoxTkhd::Reset()
{
    iCache = nullptr;
    iState = eNone;
    iBytes = 0;
    iOffset = 0;
    iBuf32.SetBytes(0);
    iBuf64.SetBytes(0);
    iVersion = 0;
}

TBool Mpeg4BoxTkhd::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("tkhd");
}

void Mpeg4BoxTkhd::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
}


// Mpeg4BoxMehd

Mpeg4BoxMehd::Mpeg4BoxMehd(IMpeg4DurationSettable& aDurationSettable) :
    iDurationSettable(aDurationSettable)
{
    Reset();
}

Msg* Mpeg4BoxMehd::Process()
{
    while (!Complete()) {
        if (iState != eNone) {
            Msg* msg = iCache->Pull();
            if (msg != nullptr) {
                return msg;
            }
        }

        if (iState == eNone) {
            iCache->Inspect(iBuf, 4);  // Set to read version - 4 bytes (32bit uint)
            iState = eVersion;
        }
        else if (iState == eVersion) {
            iOffset += iBuf.Bytes();

            iVersion = Converter::BeUint32At(iBuf, 0);

            const TUint bytesToRead = iVersion == kVersion32 ? 4 : 8;

            iCache->Inspect(iBuf, bytesToRead);
            iState = eFragmentDuration;
        }
        else if (iState == eFragmentDuration) {
            iOffset += iBuf.Bytes();

            TUint64 duration = 0;
            if (iVersion == kVersion32) {
                duration = Converter::BeUint32At(iBuf, 0);
            }
            else {
                duration = Converter::BeUint64At(iBuf, 0);
            }

            // NOTE: This entire box is optional, so if box is present then the value
            //       should be set to a non-zero value
            if (duration == 0) {
                THROW(MediaMpeg4FileInvalid);
            }

            iDurationSettable.SetDuration(duration);

            iState = eComplete;
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxMehd::Complete() const
{
    ASSERT(iOffset <= iBytes);
    return iOffset == iBytes;
}

void Mpeg4BoxMehd::Reset()
{
    iCache = nullptr;
    iState = eNone;
    iBytes = 0;
    iOffset = 0;
    iBuf.SetBytes(0);
    iVersion = 0;
}

TBool Mpeg4BoxMehd::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("mehd");
}

void Mpeg4BoxMehd::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
}


// Mpeg4BoxCodecBase

Mpeg4BoxCodecBase::Mpeg4BoxCodecBase(const Brx& aCodecId, IStreamInfoSettable& aStreamInfoSettable)
    : iId(aCodecId)
    , iBoxId(aCodecId)
    , iStreamInfoSettable(aStreamInfoSettable)
{
    Reset();
}

Mpeg4BoxCodecBase::Mpeg4BoxCodecBase(const Brx& aCodecId, const Brx& aCodecBoxId, IStreamInfoSettable& aStreamInfoSettable)
    : iId(aCodecId)
    , iBoxId(aCodecBoxId)
    , iStreamInfoSettable(aStreamInfoSettable)
{
    Reset();
}

Msg* Mpeg4BoxCodecBase::Process()
{
    while (!Complete()) {
        Msg* msg = nullptr;

        // All pulling calls below returns nullptr when there is something of interest for this class.
        if (iState == eHeader) {
            msg = iHeaderReader.ReadHeader();
        }
        else if (iState == eBox) {
            msg = iProcessor->Process();
        }
        else if (iState != eNone) {
            msg = iCache->Pull();
        }

        if (msg != nullptr) {
            return msg;
        }

        if (iState == eNone) {
            // Skip 6 byte reserved block.
            // Skip 2 byte data ref index.
            // Skip 4 byte*2 reserved block.
            static const TUint discard = 16;
            iCache->Discard(discard);
            iOffset += discard;
            iCache->Inspect(iBuf, iBuf.MaxBytes());  // Set to read channels.
            iState = eChannels;
        }
        else if (iState == eChannels) {
            iOffset += iBuf.Bytes();
            const TUint channels = Converter::BeUint16At(iBuf, 0);
            iStreamInfoSettable.SetChannels(channels);
            iCache->Inspect(iBuf, iBuf.MaxBytes());  // Set to read bit depth.
            iState = eBitDepth;
        }
        else if (iState == eBitDepth) {
            iOffset += iBuf.Bytes();
            const TUint bitDepth = Converter::BeUint16At(iBuf, 0);
            iStreamInfoSettable.SetBitDepth(bitDepth);

            // Skip 2 byte pre-defined block.
            // Skip 2 byte reserved block.
            static const TUint discard = 4;
            iCache->Discard(discard);
            iOffset += discard;

            iCache->Inspect(iBuf, iBuf.MaxBytes()); // Set to read sample rate (only care about 2 MSBs).
            iState = eSampleRate;
        }
        else if (iState == eSampleRate) {
            iOffset += iBuf.Bytes();
            const TUint sampleRate = Converter::BeUint16At(iBuf, 0);
            iStreamInfoSettable.SetSampleRate(sampleRate);

            // Skip 2 LSBs of sample rate.
            static const TUint discard = 2;
            iCache->Discard(discard);
            iOffset += discard;

            iHeaderReader.Reset(*iCache);
            iState = eHeader;
        }
        else if (iState == eHeader) {
            // Got header, now find a processor.
            try {
                iProcessor = &iProcessorFactory.GetMpeg4BoxProcessor(
                        iHeaderReader.Id(), iHeaderReader.PayloadBytes(),
                        *iCache);
                iState = eBox;
            } catch (Mpeg4BoxUnrecognised&) {
                LOG(kCodec, "Mpeg4CodecBase::Process couldn't find processor for "); LOG(kCodec, iHeaderReader.Id()); LOG(kCodec, "\n");

                iCache->Discard(iHeaderReader.PayloadBytes());
                iOffset += iHeaderReader.Bytes();
                iProcessor = nullptr;
                iHeaderReader.Reset(*iCache);
                iState = eHeader;
            }
        }
        else if (iState == eBox) {
            // Box processing is complete.
            iOffset += iHeaderReader.Bytes();

            if (iOffset > iBytes) {
                THROW(MediaMpeg4FileInvalid);
            }
            if (iOffset == iBytes) {
                iState = eComplete;
            }
            else {
                // Read next box.
                iProcessor = nullptr;
                iHeaderReader.Reset(*iCache);
                iState = eHeader;
            }
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxCodecBase::Complete() const
{
    if (iOffset > iBytes) {
        THROW(MediaMpeg4FileInvalid);
    }
    return iOffset == iBytes;
}

void Mpeg4BoxCodecBase::Reset()
{
    iCache = nullptr;
    iProcessor = nullptr;
    iState = eNone;
    iBytes = 0;
    iOffset = 0;
    iBuf.SetBytes(0);
}

TBool Mpeg4BoxCodecBase::Recognise(const Brx& aBoxId) const
{
    return aBoxId == iBoxId;
}

void Mpeg4BoxCodecBase::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
    iStreamInfoSettable.SetCodec(iId);
}

// Mpeg4BoxCodecMp4a

Mpeg4BoxCodecMp4a::Mpeg4BoxCodecMp4a(IStreamInfoSettable& aStreamInfoSettable,
        ICodecInfoSettable& aCodecInfoSettable) :
        Mpeg4BoxCodecBase(Brn("mp4a"), aStreamInfoSettable)
{
    iProcessorFactory.Add(new Mpeg4BoxEsds(aCodecInfoSettable));
}

// Mpeg4BoxCodecAlac

Mpeg4BoxCodecAlac::Mpeg4BoxCodecAlac(IStreamInfoSettable& aStreamInfoSettable,
        ICodecInfoSettable& aCodecInfoSettable) :
        Mpeg4BoxCodecBase(Brn("alac"), aStreamInfoSettable)
{
    iProcessorFactory.Add(new Mpeg4BoxAlac(aCodecInfoSettable));
}

// Mpeg4BoxCodecFlac

Mpeg4BoxCodecFlac::Mpeg4BoxCodecFlac(IStreamInfoSettable& aStreamInfoSettable, ICodecInfoSettable& aCodecInfoSettable)
    : Mpeg4BoxCodecBase(Brn("fLaC"), aStreamInfoSettable)
{
    iProcessorFactory.Add(new Mpeg4BoxDfla(aCodecInfoSettable));
}

// Mpeg4BoxCodecOpus

// NOTE: Opus under MPEG is a totally different format from bog-standard Opus files.
//       Therefore, while we recognise this as "Opus" content, we need to provide a different identifier
//       to processing so we know how to decode this.
//       CodecID: dOps (Opus under MPEG)
//       BoxId:   Opus

Mpeg4BoxCodecOpus::Mpeg4BoxCodecOpus(IStreamInfoSettable& aStreamInfoSettable, ICodecInfoSettable& aCodecInfoSettable)
    : Mpeg4BoxCodecBase(Brn("dOps"), Brn("Opus"), aStreamInfoSettable)
{
    iProcessorFactory.Add(new Mpeg4BoxDops(aCodecInfoSettable));
}

// Mpeg4BoxCodecMp4aProtected

Mpeg4BoxCodecMp4aProtected::Mpeg4BoxCodecMp4aProtected(IStreamInfoSettable& aStreamInfoSettable,
                                                       Mpeg4ProtectionDetails& aProtectionDetails)
    : Mpeg4BoxCodecBase(Brn("enca"), aStreamInfoSettable)
{
    iProcessorFactory.Add(new Mpeg4BoxSwitcher(iProcessorFactory, Brn("sinf")));
    iProcessorFactory.Add(new Mpeg4BoxSchm());
    iProcessorFactory.Add(new Mpeg4BoxSwitcher(iProcessorFactory, Brn("schi")));
    iProcessorFactory.Add(new Mpeg4BoxTenc(aProtectionDetails));
}

// Mpeg4BoxSchm

Mpeg4BoxSchm::Mpeg4BoxSchm()
{
    Reset();
}

Msg* Mpeg4BoxSchm::Process()
{
    while (!Complete()) {
        if (iState != eNone) {
            Msg* msg = iCache->Pull();
            if (msg != nullptr) {
                return msg;
            }
        }

        if (iState == eNone) {
            iCache->Inspect(iBuf, iBuf.MaxBytes());
            iState = eFlags;
        }
        else if (iState == eFlags) {
            iOffset += iBuf.Bytes();

            iCache->Inspect(iBuf, iBuf.MaxBytes()); // Set to read Scheme Type
            iState = eSchemeType;
        }
        else if (iState == eSchemeType) {
            iOffset += iBuf.Bytes();

            // NOTE: We currently only support cenc encryption
            const Brn kSupported("cenc");
            if (iBuf.Equals(kSupported) == false) {
                Log::Print("Mpeg4::Mpeg4BoxSchim - Encountered a protected container with encryption format: %.*s which is unsupported.\n", PBUF(iBuf));
                THROW(MediaMpeg4FileInvalid);
            }

            TUint bytesToDiscard = 4; // Skip over scheme version
            iOffset += 4;

            bytesToDiscard += (iBytes - iOffset); // Skip over the schemeURL (if present)
            iOffset = iBytes;

            iCache->Discard(bytesToDiscard);

            iState = eComplete;
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxSchm::Complete() const
{
    ASSERT(iOffset <= iBytes);
    return iOffset == iBytes;
}

void Mpeg4BoxSchm::Reset()
{
    iCache  = nullptr;
    iState  = eNone;
    iBytes  = 0;
    iOffset = 0;
    iBuf.SetBytes(0);
}

TBool Mpeg4BoxSchm::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("schm");
}

void Mpeg4BoxSchm::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
}


// Mpeg4BoxTenc

Mpeg4BoxTenc::Mpeg4BoxTenc(Mpeg4ProtectionDetails& aProtectionDetails)
    : iProtectionDetails(aProtectionDetails)
{
    Reset();
}

Msg* Mpeg4BoxTenc::Process()
{
    while (!Complete()) {
        if (iState != eNone) {
            Msg* msg = iCache->Pull();
            if (msg != nullptr) {
                return msg;
            }
        }

        if (iState == eNone) {
            iCache->Inspect(iBuf, iBuf.MaxBytes());
            iState = eFlagsAndVersion;
        }
        else if (iState == eFlagsAndVersion) {
            iOffset += iBuf.Bytes();

            const TUint versionAndFlags = Converter::BeUint32At(iBuf, 0);
            const TUint version         = (versionAndFlags & 0xF000) >> 24; // Unused, only need to read the sample_composition_time entry which we ignore

            if (version != 0) {
                // NOTE: We don't currently support tracks that are non-version 0 here so just throw,.
                THROW(MediaMpeg4FileInvalid);
            }

            // Discard the reserved byte + the optional version > 1 byte
            iOffset += 2;
            iCache->Discard(2);

            iCache->Inspect(iBuf8, iBuf8.MaxBytes());
            iState = eIsProtected;
        }
        else if (iState == eIsProtected) {
            iOffset += iBuf8.Bytes();

            const TUint isProtectedValue = iBuf8.At(0);

            if (isProtectedValue == 0x0) {
                // Do nothing here...
            }
            else if (isProtectedValue == 0x1) {
                iProtectionDetails.SetProtected();
            }
            else {
                THROW(MediaMpeg4FileInvalid);
            }

            iCache->Inspect(iBuf8, iBuf8.MaxBytes());
            iState = eDefaultPerSampleIVSize;
        }
        else if (iState == eDefaultPerSampleIVSize) {
            iOffset += iBuf8.Bytes();

            iProtectionDetails.SetPerSampleIVSize((TUint)iBuf8.At(0));

            if (iBytes - iOffset > iKIDBuf.MaxBytes()) {
                LOG_ERROR(kCodec, "Mpeg4BoxTenc::Process - Provided KID is larger than space we have allocated. KID should be %u bytes, we were given %u\n", iKIDBuf.MaxBytes(), (iBytes - iOffset));
                THROW(MediaMpeg4FileInvalid);
            }

            iCache->Inspect(iKIDBuf, iKIDBuf.MaxBytes());
            iState = eDefaultKID;
        }
        else if (iState == eDefaultKID) {
            iOffset += iKIDBuf.Bytes();

            iProtectionDetails.SetKID(iKIDBuf);

            if (iProtectionDetails.IsProtected() && iProtectionDetails.PerSampleIVSizeBytes() == 0) {
                Log::Print("Mpeg4BoxTenc::Process - Content is encrypted with scheme requiring a ConstantIV. This is not something we support.\n");
                THROW(MediaMpeg4FileInvalid);
            }

            iState = eComplete;
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxTenc::Complete() const
{
    ASSERT(iOffset <= iBytes);
    return iOffset == iBytes;
}

void Mpeg4BoxTenc::Reset()
{
    iCache  = nullptr;
    iState  = eNone;
    iBytes  = 0;
    iOffset = 0;

    iBuf.SetBytes(0);
    iKIDBuf.SetBytes(0);
    iProtectionDetails.Reset();
}

TBool Mpeg4BoxTenc::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("tenc");
}

void Mpeg4BoxTenc::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
}


// Mpeg4BoxSenc

Mpeg4BoxSenc::Mpeg4BoxSenc(Mpeg4ProtectionDetails& aProtectionDetails)
    : iProtectionDetails(aProtectionDetails)
{
    Reset();
}

Msg* Mpeg4BoxSenc::Process()
{
    while (!Complete()) {
        if (iState != eNone) {
            Msg* msg = iCache->Pull();
            if (msg != nullptr) {
                return msg;
            }
        }

        if (iState == eNone) {
            iCache->Inspect(iBuf, iBuf.MaxBytes());
            iState = eFlagsAndVersion;
        }
        else if (iState == eFlagsAndVersion) {
            iOffset += iBuf.Bytes();

            const TUint versionAndFlags = Converter::BeUint32At(iBuf, 0);
            const TUint version         = (versionAndFlags & 0xF000) >> 24; // Unused, only need to read the sample_composition_time entry which we ignore
            const TUint flags           = (versionAndFlags & 0x0FFF);

            // NOTE: We only support version 0 here.
            if (version != 0) {
                LOG_ERROR(kCodec, "Mpeg4BoxSenc::Process - Found box with version: %u when we only support verison 0\n", version);
                THROW(MediaMpeg4FileInvalid);
            }

            const TBool senc_use_subsamples = flags & 0x000002;
            if (senc_use_subsamples) {
                LOG_ERROR(kCodec, "Mpeg4BoxSenc::Process - Found box that requires subsample encryption that we don't support.\n");
                THROW(MediaMpeg4FileInvalid);
            }

            iCache->Inspect(iBuf, iBuf.MaxBytes());
            iState = eSampleCount;
        }
        else if (iState == eSampleCount) {
            iOffset += iBuf.Bytes();

            iSampleCount = Converter::BeUint32At(iBuf, 0);

            if (iSampleCount == 0) {
                iState = eComplete;
            }
            else {
                if (iProtectionDetails.PerSampleIVSizeBytes() > iBuf64.MaxBytes())  {
                    LOG_ERROR(kCodec, "Mpeg4BoxSenc::Process - Need %u byte(s) to read IV, we only have capacity of: %u\n", iProtectionDetails.PerSampleIVSizeBytes(), iBuf64.MaxBytes());
                    THROW(MediaMpeg4FileInvalid);
                }

                iCache->Inspect(iBuf64, iProtectionDetails.PerSampleIVSizeBytes());
                iState = eSampleIV;
            }
        }
        else if (iState == eSampleIV) {
            iOffset += iBuf64.Bytes();

            iSampleCount -= 1;

            iProtectionDetails.AddSampleIV(iBuf64);

            if (iSampleCount == 0) {
                iState = eComplete;
            }
            else {
                iCache->Inspect(iBuf64, iProtectionDetails.PerSampleIVSizeBytes());
            }
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxSenc::Complete() const
{
    ASSERT(iOffset <= iBytes);
    return iOffset == iBytes;
}

void Mpeg4BoxSenc::Reset()
{
    iCache       = nullptr;
    iState       = eNone;
    iBytes       = 0;
    iOffset      = 0;
    iSampleCount = 0;

    iBuf.SetBytes(0);
    iBuf64.SetBytes(0);
    iProtectionDetails.ClearSampleIVs();
}

TBool Mpeg4BoxSenc::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("senc");
}

void Mpeg4BoxSenc::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
}

// Mpeg4BoxEsds

Mpeg4BoxEsds::Mpeg4BoxEsds(ICodecInfoSettable& aCodecInfoSettable) :
        iCodecInfoSettable(aCodecInfoSettable)
{
    Reset();
}

Msg* Mpeg4BoxEsds::Process()
{
    while (!Complete()) {
        if (iState != eNone) {
            Msg* msg = iCache->Pull();
            if (msg != nullptr) {
                msg = msg->Process(iAudioEncodedRecogniser);
                if (msg != nullptr) {
                    return msg;
                }
            }
        }

        if (iState == eNone) {
            iCache->Inspect(iBuf, iBuf.MaxBytes());  // Set to read version.
            iState = eVersion;
        }
        else if (iState == eVersion) {
            iOffset += iBuf.Bytes();
            const TUint version = iBuf[0];
            if (version != kVersion) {
                iCache->Discard(iBytes - iOffset);
                iOffset = iBytes;
                THROW(MediaMpeg4FileInvalid);
            }

            // Skip 24-bit field reserved for flags.
            static const TUint discard = 3;
            iCache->Discard(discard);
            iOffset += discard;

            const TUint remaining = iBytes-iOffset;
            iCache->Accumulate(remaining);  // Set to read codec info.
            iState = eCodecInfo;
        }
        else if (iState == eCodecInfo) {
            MsgAudioEncoded* msg = iAudioEncodedRecogniser.AudioEncoded();
            ASSERT(msg != nullptr);
            iOffset += msg->Bytes();
            iCodecInfoSettable.SetCodecInfo(msg);
            iState = eComplete;
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxEsds::Complete() const
{
    ASSERT(iOffset <= iBytes);
    return iOffset == iBytes;
}

void Mpeg4BoxEsds::Reset()
{
    iCache = nullptr;
    iAudioEncodedRecogniser.Reset();
    iState = eNone;
    iBytes = 0;
    iOffset = 0;
    iBuf.SetBytes(0);
}

TBool Mpeg4BoxEsds::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("esds");
}

void Mpeg4BoxEsds::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
}

// Mpeg4BoxAlac

Mpeg4BoxAlac::Mpeg4BoxAlac(ICodecInfoSettable& aCodecInfoSettable) :
        iCodecInfoSettable(aCodecInfoSettable)
{
    Reset();
}

Msg* Mpeg4BoxAlac::Process()
{
    while (!Complete()) {
        if (iState != eNone) {
            Msg* msg = iCache->Pull();
            if (msg != nullptr) {
                msg = msg->Process(iAudioEncodedRecogniser);
                if (msg != nullptr) {
                    return msg;
                }
            }
        }

        if (iState == eNone) {
            const TUint remainder = iBytes - iOffset;
            iCache->Accumulate(remainder);  // Set to read codec info.
            iState = eCodecInfo;
        }
        else if (iState == eCodecInfo) {
            MsgAudioEncoded* msg = iAudioEncodedRecogniser.AudioEncoded();
            ASSERT(msg != nullptr);
            iOffset += msg->Bytes();
            iCodecInfoSettable.SetCodecInfo(msg);
            iState = eComplete;
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxAlac::Complete() const
{
    ASSERT(iOffset <= iBytes);
    return iOffset == iBytes;
}

void Mpeg4BoxAlac::Reset()
{
    iCache = nullptr;
    iAudioEncodedRecogniser.Reset();
    iState = eNone;
    iBytes = 0;
    iOffset = 0;
    iBuf.SetBytes(0);
}

TBool Mpeg4BoxAlac::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("alac");
}

void Mpeg4BoxAlac::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
}


// Mpeg4BoxDfla
Mpeg4BoxDfla::Mpeg4BoxDfla(ICodecInfoSettable& aCodecInfoSettable)
    : iCodecInfoSettable(aCodecInfoSettable)

{
    Reset();
}

Msg* Mpeg4BoxDfla::Process()
{
    while (!Complete()) {
        if (iState != eNone) {
            Msg* msg = iCache->Pull();
            if (msg != nullptr) {
                msg = msg->Process(iAudioEncodedRecogniser);
                if (msg != nullptr) {
                    return msg;
                }
            }
        }

        if (iState == eNone) {
            iCache->Inspect(iBuf, iBuf.MaxBytes());  // Set to read version.
            iState = eVersion;
        }
        else if (iState == eVersion) {
            iOffset += iBuf.Bytes();
            const TUint version = Converter::BeUint32At(iBuf, 0);
            if (version != kVersion) {
                LOG_ERROR(kCodec, "MpegBoxDfla::Process() - Encountered version '%u'. We only support version(s): 0\n", version);
                THROW(MediaMpeg4FileInvalid);
            }

            const TUint remaining = iBytes - iOffset;
            iCache->Accumulate(remaining);

            iState = eCodecInfo;
        }
        else if (iState == eCodecInfo) {
            MsgAudioEncoded* msg = iAudioEncodedRecogniser.AudioEncoded();
            ASSERT(msg != nullptr);
            iOffset += msg->Bytes();
            iCodecInfoSettable.SetCodecInfo(msg);

            iState = eComplete;
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxDfla::Complete() const
{
    ASSERT(iOffset <= iBytes);
    return iOffset == iBytes;
}

void Mpeg4BoxDfla::Reset()
{
    iCache  = nullptr;
    iState  = eNone;
    iBytes  = 0;
    iOffset = 0;

    iBuf.SetBytes(0);
    iAudioEncodedRecogniser.Reset();
}

TBool Mpeg4BoxDfla::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("dfLa");
}

void Mpeg4BoxDfla::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
}

// Mpeg4BoxDops
Mpeg4BoxDops::Mpeg4BoxDops(ICodecInfoSettable& aCodecInfoSettable)
    : iCodecInfoSettable(aCodecInfoSettable)

{
    Reset();
}

Msg* Mpeg4BoxDops::Process()
{
    while (!Complete()) {
        if (iState != eNone) {
            Msg* msg = iCache->Pull();
            if (msg != nullptr) {
                msg = msg->Process(iAudioEncodedRecogniser);
                if (msg != nullptr) {
                    return msg;
                }
            }
        }

        if (iState == eNone) {
            iCache->Accumulate(iBytes - iOffset);
            iState = eCodecInfo;
        }
        else if (iState == eCodecInfo) {
            MsgAudioEncoded* msg = iAudioEncodedRecogniser.AudioEncoded();
            ASSERT(msg != nullptr);
            iOffset += msg->Bytes();
            iCodecInfoSettable.SetCodecInfo(msg);

            iState = eComplete;
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxDops::Complete() const
{
    ASSERT(iOffset <= iBytes);
    return iOffset == iBytes;
}

void Mpeg4BoxDops::Reset()
{
    iCache  = nullptr;
    iState  = eNone;
    iBytes  = 0;
    iOffset = 0;

    iBuf.SetBytes(0);
    iAudioEncodedRecogniser.Reset();
}

TBool Mpeg4BoxDops::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("dOps");
}

void Mpeg4BoxDops::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
}



// Mpeg4BoxStsd

Mpeg4BoxStsd::Mpeg4BoxStsd(IStreamInfoSettable& aStreamInfoSettable,
                           ICodecInfoSettable& aCodecInfoSettable,
                           Mpeg4ProtectionDetails& aProtectionDetails)
    : iCache(nullptr)
    , iProcessor(nullptr)
    , iState(eNone)
    , iBytes(0)
    , iOffset(0)
{
    iProcessorFactory.Add(
            new Mpeg4BoxCodecMp4a(aStreamInfoSettable, aCodecInfoSettable));
    iProcessorFactory.Add(
            new Mpeg4BoxCodecAlac(aStreamInfoSettable, aCodecInfoSettable));
    iProcessorFactory.Add(
            new Mpeg4BoxCodecFlac(aStreamInfoSettable, aCodecInfoSettable));
    iProcessorFactory.Add(
            new Mpeg4BoxCodecOpus(aStreamInfoSettable, aCodecInfoSettable));
    iProcessorFactory.Add(
            new Mpeg4BoxCodecMp4aProtected(aStreamInfoSettable, aProtectionDetails));
}

Msg* Mpeg4BoxStsd::Process()
{
    while (!Complete()) {
        Msg* msg = nullptr;

        // All pulling calls below return nullptr when there is something of interest for this class.
        if (iState == eHeader) {
            msg = iHeaderReader.ReadHeader();
        }
        else if (iState == eBox) {
            msg = iProcessor->Process();
        }
        else if (iState != eNone) {
            msg = iCache->Pull();
        }

        if (msg != nullptr) {
            return msg;
        }

        if (iState == eNone) {
            iCache->Inspect(iBuf, iBuf.MaxBytes());  // Set to read version.
            iState = eVersion;
        }
        else if (iState == eVersion) {
            iOffset += iBuf.Bytes();
            const TUint version = Converter::BeUint32At(iBuf, 0);
            if (version != kVersion) {
                iCache->Discard(iBytes - iOffset);
                iOffset = iBytes;
                THROW(MediaMpeg4FileInvalid);
            }
            iCache->Inspect(iBuf, iBuf.MaxBytes());  // Set to read entry count.
            iState = eEntries;
        }
        else if (iState == eEntries) {
            iOffset += iBuf.Bytes();
            const TUint entries = Converter::BeUint32At(iBuf, 0);
            LOG(kCodec, "Mpeg4BoxStsd::Process entries: %u\n", entries);
            iHeaderReader.Reset(*iCache);
            iState = eHeader;
        }
        else if (iState == eHeader) {
            // Got header, now find a processor.
            try {
                iProcessor = &iProcessorFactory.GetMpeg4BoxProcessor(
                        iHeaderReader.Id(), iHeaderReader.PayloadBytes(),
                        *iCache);
                iState = eBox;
            } catch (Mpeg4BoxUnrecognised&) {
                LOG(kCodec, "Mpeg4BoxStsd::Process couldn't find processor for "); LOG(kCodec, iHeaderReader.Id()); LOG(kCodec, "\n");

                iCache->Discard(iHeaderReader.PayloadBytes());
                iOffset += iHeaderReader.Bytes();
                iProcessor = nullptr;
                iHeaderReader.Reset(*iCache);
                iState = eHeader;
            }
        }
        else if (iState == eBox) {
            // Box processing is complete.
            iOffset += iHeaderReader.Bytes();

            ASSERT(iOffset <= iBytes);
            if (iOffset == iBytes) {
                iState = eComplete;
            }
            else {
                // Read next box.
                iProcessor = nullptr;
                iHeaderReader.Reset(*iCache);
                iState = eHeader;
            }
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxStsd::Complete() const
{
    return iOffset == iBytes;
}

void Mpeg4BoxStsd::Reset()
{
    iProcessor = nullptr;
    iState = eNone;
    iBytes = 0;
    iOffset = 0;
    iBuf.SetBytes(0);
}

TBool Mpeg4BoxStsd::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("stsd");
}

void Mpeg4BoxStsd::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    iCache = &aCache;
    iBytes = aBoxBytes;
}

// Mpeg4Duration

Mpeg4Duration::Mpeg4Duration() :
        iTimescale(0), iDuration(0)
{
}

void Mpeg4Duration::Reset()
{
    iTimescale = 0;
    iDuration = 0;
}

TUint Mpeg4Duration::Timescale() const
{
    return iTimescale;
}

TUint64 Mpeg4Duration::Duration() const
{
    return iDuration;
}

void Mpeg4Duration::SetTimescale(TUint aTimescale)
{
    iTimescale = aTimescale;
}

void Mpeg4Duration::SetDuration(TUint64 aDuration)
{
    iDuration = aDuration;
}

// Mpeg4StreamInfo

Mpeg4StreamInfo::Mpeg4StreamInfo() :
        iChannels(0), iBitDepth(0), iSampleRate(0)
{
}

void Mpeg4StreamInfo::Reset()
{
    iChannels = 0;
    iBitDepth = 0;
    iSampleRate = 0;
    iCodec.SetBytes(0);
}

TUint Mpeg4StreamInfo::Channels() const
{
    return iChannels;
}

TUint Mpeg4StreamInfo::BitDepth() const
{
    return iBitDepth;
}

TUint Mpeg4StreamInfo::SampleRate() const
{
    return iSampleRate;
}

const Brx& Mpeg4StreamInfo::Codec() const
{
    return iCodec;
}

void Mpeg4StreamInfo::SetChannels(TUint aChannels)
{
    iChannels = aChannels;
}

void Mpeg4StreamInfo::SetBitDepth(TUint aBitDepth)
{
    iBitDepth = aBitDepth;
}

void Mpeg4StreamInfo::SetSampleRate(TUint aSampleRate)
{
    iSampleRate = aSampleRate;
}

void Mpeg4StreamInfo::SetCodec(const Brx& aCodec)
{
    iCodec.Replace(aCodec);
}

// Mpeg4CodecInfo

Mpeg4CodecInfo::Mpeg4CodecInfo() :
        iAudioEncoded(nullptr)
{
}

Mpeg4CodecInfo::~Mpeg4CodecInfo()
{
    Reset();
}

void Mpeg4CodecInfo::Reset()
{
    if (iAudioEncoded != nullptr) {
        iAudioEncoded->RemoveRef();
        iAudioEncoded = nullptr;
    }
}

MsgAudioEncoded* Mpeg4CodecInfo::CodecInfo()
{
    MsgAudioEncoded* msg = iAudioEncoded;
    iAudioEncoded = nullptr;
    return msg;
}

void Mpeg4CodecInfo::SetCodecInfo(MsgAudioEncoded* aMsg)
{
    ASSERT(iAudioEncoded == nullptr);
    iAudioEncoded = aMsg;
}

// Mpeg4BoxMdat

Mpeg4BoxMdat::Mpeg4BoxMdat(Optional<IMpegDRMProvider> aDRMProvider,
                           MsgFactory& aMsgFactory,
                           Mpeg4BoxSwitcherRoot& aBoxSwitcher,
                           IMpeg4MetadataChecker& aMetadataChecker,
                           IMpeg4MetadataProvider& aMetadataProvider,
                           IMpeg4ChunkSeekObservable& aChunkSeeker,
                           IBoxOffsetProvider& aOffsetProvider,
                           SeekTable& aSeekTable,
                           SampleSizeTable& aSampleSizeTable,
                           Mpeg4ProtectionDetails& aProtectionDetails,
                           Mpeg4ContainerInfo& aContainerInfo,
                           Mpeg4OutOfBandReader& aOutOfBandReader)
    : iDRMProvider(aDRMProvider)
    , iMsgFactory(aMsgFactory)
    , iBoxSwitcher(aBoxSwitcher)
    , iMetadataChecker(aMetadataChecker)
    , iMetadataProvider(aMetadataProvider)
    , iOffsetProvider(aOffsetProvider)
    , iSeekTable(aSeekTable)
    , iSampleSizeTable(aSampleSizeTable)
    , iProtectionDetails(aProtectionDetails)
    , iContainerInfo(aContainerInfo)
    , iOutOfBandReader(aOutOfBandReader)
    , iLock("MP4D")
    , iLoggedMissingEncryptionError(false)
    , iChunkMsg(nullptr)
{
    aChunkSeeker.RegisterChunkSeekObserver(*this);

    if (iDRMProvider.Ok()) {
        iSampleBuf     = new Bwh(1024 * 12);
        iDecryptionBuf = new Bwh(1024 * 12);
    }
    else {
        iSampleBuf     = nullptr;
        iDecryptionBuf = nullptr;
    }

    Reset();
}

Mpeg4BoxMdat::~Mpeg4BoxMdat()
{
    if (iSampleBuf) {
        delete iSampleBuf;
    }

    if (iDecryptionBuf) {
        delete iDecryptionBuf;
    }

    if (iChunkMsg) {
        iChunkMsg->RemoveRef();
        iChunkMsg = nullptr;
    }
}

Msg* Mpeg4BoxMdat::Process()
{
    while (!Complete()) {
        if (iState == eChunk) {
            Msg* msg = iCache->Pull();
            if (msg != nullptr) {
                msg = msg->Process(iAudioEncodedRecogniser);
                if (msg != nullptr) {
                    return msg;
                }
            }
        }

        if (iState == eNone) {
            if (!iMetadataChecker.MetadataAvailable()) {
                iState = eRetrieveMetadata;
            }
            else {
                iMetadataProvider.ResetProvider();
                iState = eTransmitMetadata;
            }
        }
        else if (iState == eRetrieveMetadata) {
            iOutOfBandReader.SetReadOffset(iFileReadOffset+iBytes);
            iBoxSwitcher.Reset();
            iBoxSwitcher.Set(iOutOfBandReader, "moov");
            Msg* msg = iBoxSwitcher.Process();
            ASSERT(msg == nullptr); // Shouldn't get any msgs from out-of-band reader.

            if (!iMetadataChecker.MetadataAvailable()) {
                // Still failed to retrieve metadata.
                return nullptr;
            }

            iMetadataProvider.ResetProvider();
            iState = eTransmitMetadata;
        }
        else if (iState == eTransmitMetadata) {
            MsgAudioEncoded* msg = iMetadataProvider.GetMetadata();
            if (iMetadataProvider.Complete()) {
                iChunk = 0;

                if (!ChunkBytes(&iChunkBytesRemaining)) {
                    msg->RemoveRef();
                    THROW(MediaMpeg4FileInvalid);
                }

                iState = eChunkReadSetup;
            }

            // Need to check for nullptr here as if there is no codec info, we've nothing to output.
            // This often happens with fragmented streams when we transition to the second 'moof' fragment.
            if (msg != nullptr) {
                return msg;
            }
        }
        else if (iState == eChunkReadSetup) {

            {
                AutoMutex a(iLock);
                if (iSeek) {
                    LOG(kCodec, "Mpeg4BoxMdat::Process seek occured iSeekChunk: %u\n", iSeekChunk);
                    // Chunk has changed due to seek.
                    iChunk = iSeekChunk;

                    if (!ChunkBytes(&iChunkBytesRemaining)) {
                        THROW(MediaMpeg4FileInvalid);
                    }

                    iFileReadOffset = iBoxStartOffset+Mpeg4BoxHeaderReader::kHeaderBytes;
                    iOffset = iFileReadOffset - Mpeg4BoxHeaderReader::kHeaderBytes - iBoxStartOffset;

                    const TUint chunkOffset = BytesUntilChunk();
                    iFileReadOffset += chunkOffset;
                    iOffset = chunkOffset;

                    iSeek = false;
                    iSeekChunk = 0;
                }
            }

            const TUint discard = BytesUntilChunk();
            iCache->Discard(discard);
            iOffset += discard;
            iFileReadOffset += discard;

            const TUint readBytes = BytesToRead();
            iCache->Accumulate(readBytes);
            iState = eChunk;
        }
        else if (iState == eChunk) {
            MsgAudioEncoded* msg = iAudioEncodedRecogniser.AudioEncoded();
            ASSERT(msg != nullptr);
            ASSERT(msg->Bytes() <= iChunkBytesRemaining);

            TBool seek = false;
            {
                AutoMutex a(iLock);
                seek = iSeek;
            }

            if (seek) {
                msg->RemoveRef();   // Discard msg; now invalid.
                msg = nullptr;
                iState = eChunkReadSetup;
            }
            else {
                // Chunk still valid.
                iOffset += msg->Bytes();
                iFileReadOffset += msg->Bytes();
                iChunkBytesRemaining -= msg->Bytes();

                if (iChunkBytesRemaining == 0) {
                    MoveToNextChunkIfPossible();
                }
                else {
                    // Bytes remaining from this chunk; set to read next block but remain in this state.
                    const TUint readBytes = BytesToRead();
                    iCache->Accumulate(readBytes);
                    iState = eChunk;
                }


                // If the content is encrypted, we need to decrypt here before passing on...
                if (iProtectionDetails.IsProtected() && iProtectionDetails.HasPerSampleIVs()) {
                    if (!iDRMProvider.Ok()) {
                        if (!iLoggedMissingEncryptionError) {
                            iLoggedMissingEncryptionError = true;
                            LOG_ERROR(kCodec, "Mpeg4BoxMdat::Process - Encountered an encrypted stream but have no means to decrypt content.\n");
                        }

                        msg->RemoveRef(); // Discard msg, no longer needed.
                        THROW(CodecStreamCorrupt);
                    }


                    ASSERT(iChunkMsg == nullptr);
                    iChunkMsg = msg;
                    iState    = eProtectedChunk;
                }
                else {
                    return msg;
                }
            }
        }
        else if (iState == eProtectedChunk) {

            // Reset the state of our decryption buffer. If we have more data present than an emitted MsgAudioEncoded
            // move the contents to the start of the buffer, otherwise we clear.
            if (iDecryptionBuf->Bytes() > AudioData::kMaxBytes) {
                const Brn remaining(iDecryptionBuf->Ptr() + AudioData::kMaxBytes, iDecryptionBuf->Bytes() - AudioData::kMaxBytes);
                iDecryptionBuf->Replace(remaining);
            }
            else {
                iDecryptionBuf->SetBytes(0);
            }

            // Next - consume as much audio as possible from the audio stream.
            if (iChunkMsg) {
                MsgAudioEncoded* remaining = nullptr;

                const TUint chunkBytesLeftToRead = iChunkMsg->Bytes();
                const TUint maxBytesToRead       = iSampleBuf->BytesRemaining();
                const TUint bytesToRead          = std::min(chunkBytesLeftToRead, maxBytesToRead);

                if (bytesToRead < iChunkMsg->Bytes()) {
                    remaining = iChunkMsg->Split(bytesToRead);
                }

                ASSERT(iSampleBuf->BytesRemaining() >= bytesToRead);

                TByte* ptr = const_cast<TByte*>(iSampleBuf->Ptr() + iSampleBuf->Bytes());
                iChunkMsg->CopyTo(ptr);

                iSampleBuf->SetBytes(iSampleBuf->Bytes() + bytesToRead);

                iChunkMsg->RemoveRef();
                iChunkMsg = remaining;
            }

            // This assumes WideVine DRM has been applied. For this, we must decrypt each MPEG sample in turn.
            // Sadly, we must buffer the FULL sample as WideVine does not support partial sample decryptions.
            ReaderBuffer sampleReader(*iSampleBuf);

            while (true) {
                const TBool hasReadAllSamples = iSampleIndex >= iSampleSizeTable.Count();
                if (hasReadAllSamples) {
                    MoveToNextChunkIfPossible();
                    break;
                }

                // Otherwise, we attempt to decrypt the current sample.
                const TUint sampleBytes = iSampleSizeTable.SampleSize(iSampleIndex);
                const Brx&  sampleData  = sampleReader.Read(sampleBytes);

                const TBool hasReadFullSample = sampleData.Bytes() == sampleBytes;
                const TBool hasSpaceToDecrypt = iDecryptionBuf->BytesRemaining() >= sampleData.Bytes();

                if (!hasReadFullSample || !hasSpaceToDecrypt) {
                    iSampleBuf->Replace(sampleData);

                    if (iChunkMsg == nullptr) {
                        if (iChunkBytesRemaining > 0) {
                            iState = eChunk;
                        }
                        else {
                            MoveToNextChunkIfPossible();
                        }
                    }

                    break;
                }

                const Brx& KID = iProtectionDetails.KID();
                const Brx&  IV = iProtectionDetails.GetSampleIV(iSampleIndex);

                ASSERT(iDRMProvider.Ok());

                if (!iDRMProvider.Unwrap().Decrypt(KID, sampleData, IV, *iDecryptionBuf)) {
                    LOG_ERROR(kCodec, "Mpeg4BoxMdat::Process() - Failed to decrypt content\n");

                    // Need to drain whatever is left so we don't continue to read and process...
                    const TUint bytesRemaining = iBytes - (TUint)iOffset;
                    iOffset = iBytes;
                    iState = eComplete;

                    iCache->Discard(bytesRemaining);

                    if (iChunkMsg) {
                        iChunkMsg->RemoveRef();
                        iChunkMsg = nullptr;
                    }

                    // Finally indicate the stream is corrupt to cause the pipeline to stop this track.
                    THROW(CodecStreamCorrupt);
                }

                iSampleIndex += 1;

            }

            if (iDecryptionBuf->Bytes() > 0) {
                const Brn outputData(iDecryptionBuf.Ptr(), std::min(iDecryptionBuf->Bytes(), AudioData::kMaxBytes));
                return iMsgFactory.CreateMsgAudioEncoded(outputData);
            }
        }
        else {
            // Unhandled state.
            ASSERTS();
        }
    }

    return nullptr;
}

TBool Mpeg4BoxMdat::Complete() const
{
    ASSERT(iOffset <= iBytes);

    const TBool finishedReading    = iOffset == iBytes;
    const TBool finishedDecrypting = iChunkMsg == nullptr && iDecryptionBuf && (iDecryptionBuf->Bytes() < AudioData::kMaxBytes);

    return finishedReading && finishedDecrypting;
}

void Mpeg4BoxMdat::Reset()
{
    iCache = nullptr;
    iAudioEncodedRecogniser.Reset();
    iState = eNone;
    iChunk = 0;
    iSeekChunk = 0;
    iSeek = false;
    iChunkBytesRemaining = 0;
    iBytes = 0;
    iOffset = 0;
    iBoxStartOffset = 0;
    iFileReadOffset = 0;
    iSampleIndex = 0;
    iLoggedMissingEncryptionError = false;

    if (iChunkMsg) {
        iChunkMsg->RemoveRef();
        iChunkMsg = nullptr;
    }

    if (iSampleBuf) {
        iSampleBuf->SetBytes(0);
    }

    if (iDecryptionBuf) {
        iDecryptionBuf->SetBytes(0);
    }
}

TBool Mpeg4BoxMdat::Recognise(const Brx& aBoxId) const
{
    return aBoxId == Brn("mdat");
}

void Mpeg4BoxMdat::Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes)
{
    ASSERT(iCache == nullptr);
    iCache = &aCache;
    iBytes = aBoxBytes;
    iBoxStartOffset = iOffsetProvider.BoxOffset();
    iFileReadOffset = iBoxStartOffset + Mpeg4BoxHeaderReader::kHeaderBytes;
}

void Mpeg4BoxMdat::ChunkSeek(TUint aChunk)
{
    AutoMutex a(iLock);
    iSeek = true;
    iSeekChunk = aChunk;

    if (iContainerInfo.ProcessingMode() == Mpeg4ContainerInfo::EProcessingMode::Fragmented) {
        // For fragmented files, we are likely moving to a completely different fragment in the file,
        // not the same one we are currently in. Therefore, we need to ensure that we mark ourselves here
        // as "Complete" so that we'll pull the next box through at the seeked position.

        iOffset = iBytes;
        if (iChunkMsg != nullptr) {
            iChunkMsg->RemoveRef();
            iChunkMsg = nullptr;
        }

        iState = eComplete;
    }
    else {
        iState = eChunkReadSetup;
    }
}

TUint Mpeg4BoxMdat::BytesUntilChunk() const
{
    const TBool isFragmentedStream = iContainerInfo.ProcessingMode() == Mpeg4ContainerInfo::EProcessingMode::Fragmented;
    if (isFragmentedStream && !iContainerInfo.CanProcess(iFileReadOffset)) {
        LOG_ERROR(kCodec, "Mpeg4BoxMdat::BytesUntilChunk - Attempting to stream a 'moof' based stream that relies on data offsets which is unsupported\n");
        THROW(MediaMpeg4FileInvalid);
    }

    const TUint64 chunkOffset = isFragmentedStream ? iFileReadOffset
                                                   : iSeekTable.GetOffset(iChunk);

    if (chunkOffset < iFileReadOffset) {
        THROW(MediaMpeg4FileInvalid);
    }
    const TUint64 toDiscard = chunkOffset - iFileReadOffset;
    ASSERT(toDiscard <= std::numeric_limits < TUint > ::max());
    const TUint bytes = static_cast<TUint>(toDiscard);
    return bytes;
}

TBool Mpeg4BoxMdat::ChunkBytes(TUint *aChunkBytes) const
{
    TUint chunkBytes               = 0;
    const TBool isFragmentedStream = iContainerInfo.ProcessingMode() == Mpeg4ContainerInfo::EProcessingMode::Fragmented;

    if (isFragmentedStream) {
        for (TUint i = 0; i < iSampleSizeTable.Count(); i += 1) {
            const TUint sampleBytes = iSampleSizeTable.SampleSize(i);

            if ((std::numeric_limits<TUint>::max() - chunkBytes) < sampleBytes) {
                // Wrapping will occur.
                *aChunkBytes = 0;
                return false;
            }
            chunkBytes += sampleBytes;
        }
    }
    else {
        if (iChunk >= iSeekTable.ChunkCount()) {
            *aChunkBytes = 0;
            return false;
        }
        const TUint chunkSamples = iSeekTable.SamplesPerChunk(iChunk);
        const TUint startSample = iSeekTable.StartSample(iChunk); // NOTE: this assumes first sample == 0 (which is valid with how our tables are setup), but in MPEG4 spec, first sample == 1.
        // Samples start from 1. However, tables here are indexed from 0.
        for (TUint i = startSample; i < startSample + chunkSamples; i++) {
            const TUint sampleBytes = iSampleSizeTable.SampleSize(i);

            if ((std::numeric_limits<TUint>::max() - chunkBytes) < sampleBytes) {
                // Wrapping will occur.
                *aChunkBytes = 0;
                return false;
            }
            chunkBytes += sampleBytes;
        }
    }

    *aChunkBytes = chunkBytes;
    return true;
}

TUint Mpeg4BoxMdat::BytesToRead() const
{
    // Read data in sensible-sized blocks.
    // A single file could be composed of a single chunk.
    // Would exhaust allocators if try to buffer entire large file/chunk.
    TUint bytes = iChunkBytesRemaining;
    if (iChunkBytesRemaining > EncodedAudio::kMaxBytes) {
        bytes = EncodedAudio::kMaxBytes;
    }
    return bytes;
}

void Mpeg4BoxMdat::MoveToNextChunkIfPossible()
{
    ASSERT(iOffset <= iBytes); // We should not have read more than the box contents

    const TBool boxFinished = iOffset == iBytes;
    if (boxFinished) {
        iState = eComplete;
    }
    else {
        iChunk += 1;

        if (!ChunkBytes(&iChunkBytesRemaining)) {
            THROW(MediaMpeg4FileInvalid);
        }

        iState = eChunkReadSetup;
    }
}


// SampleSizeTable

SampleSizeTable::SampleSizeTable()
{
    WriteInit();
}

SampleSizeTable::~SampleSizeTable()
{
    Clear();
}

void SampleSizeTable::Init(TUint aMaxEntries)
{
    ASSERT(iTable.size() == 0);
    iTable.reserve(aMaxEntries);
}

void SampleSizeTable::Clear()
{
    iTable.clear();
}

void SampleSizeTable::Reset()
{
    Clear();
    iDefaultSampleSize = 0;
}

void SampleSizeTable::AddSampleSize(TUint aSize)
{
    if (iTable.size() == iTable.capacity()) {
        // File contains more sample sizes than it reported (and than we reserved capacity for).
        THROW(MediaMpeg4FileInvalid);
    }
    iTable.push_back(aSize);
}

TUint32 SampleSizeTable::SampleSize(TUint aIndex) const
{
    if (aIndex > iTable.size() - 1) {
        THROW(MediaMpeg4FileInvalid);
    }
    return iTable[aIndex];
}

TUint32 SampleSizeTable::DefaultSampleSize() const
{
    return iDefaultSampleSize;
}

void SampleSizeTable::SetDefaultSampleSize(TUint aDefaultSampleSize)
{
    iDefaultSampleSize = aDefaultSampleSize;
}

TUint32 SampleSizeTable::Count() const
{
    return iTable.size();
}

void SampleSizeTable::WriteInit()
{
    iWriteIndex = 0;
}

void SampleSizeTable::Write(IWriter& aWriter, TUint aMaxBytes)
{
    TUint bytesLeftToWrite = aMaxBytes;
    WriterBinary writerBin(aWriter);

    if (iWriteIndex == 0) {
        if (bytesLeftToWrite < sizeof(TUint32)) {
           return;
       }
       writerBin.WriteUint32Be(Count());
       bytesLeftToWrite -= sizeof(TUint32);
    }

    while ((iWriteIndex < Count()) && (bytesLeftToWrite >= sizeof(TUint32))) {
       writerBin.WriteUint32Be(SampleSize(iWriteIndex));
       bytesLeftToWrite -= sizeof(TUint32);
       iWriteIndex++;
    }
}

TBool SampleSizeTable::WriteComplete() const
{
    return (iWriteIndex == Count());
}

// SeekTable
// Table of samples->chunk->offset required for seeking

SeekTable::SeekTable()
{
    WriteInit();
}

SeekTable::~SeekTable()
{
    Deinitialise();
}

void SeekTable::InitialiseSamplesPerChunk(TUint aEntries)
{
    iSamplesPerChunk.reserve(aEntries);
}

void SeekTable::InitialiseAudioSamplesPerSample(TUint aEntries)
{
    iAudioSamplesPerSample.reserve(aEntries);
}

void SeekTable::InitialiseOffsets(TUint aEntries)
{
    iOffsets.reserve(aEntries);
}

TBool SeekTable::Initialised() const
{
    const TBool initialised = iSamplesPerChunk.size() > 0
            && iAudioSamplesPerSample.size() > 0 && iOffsets.size() > 0;
    return initialised;
}

void SeekTable::Deinitialise()
{
    iSamplesPerChunk.clear();
    iAudioSamplesPerSample.clear();
    iOffsets.clear();
    iIsFragmentedStream = false;
}

void SeekTable::SetSamplesPerChunk(TUint aFirstChunk, TUint aSamplesPerChunk,
        TUint aSampleDescriptionIndex)
{
    TSamplesPerChunkEntry entry = { aFirstChunk, aSamplesPerChunk,
            aSampleDescriptionIndex };
    iSamplesPerChunk.push_back(entry);
}

void SeekTable::SetAudioSamplesPerSample(TUint32 aSampleCount,
        TUint32 aAudioSamples)
{
    TAudioSamplesPerSampleEntry entry = { aSampleCount, aAudioSamples };
    iAudioSamplesPerSample.push_back(entry);
}

void SeekTable::SetOffset(TUint64 aOffset)
{
    iOffsets.push_back(aOffset);
}

void SeekTable::SetIsFragmentedStream(TBool aIsFragmented)
{
    iIsFragmentedStream = aIsFragmented;
}

TUint SeekTable::ChunkCount() const
{
    return iOffsets.size();
}

TUint SeekTable::AudioSamplesPerSample() const
{
    return iAudioSamplesPerSample.size();
}

TUint SeekTable::SamplesPerChunk(TUint aChunkIndex) const
{
    //ASSERT(aChunkIndex < iSamplesPerChunk.size());
    //return iSamplesPerChunk[aChunkIndex];

    // FIXME - don't move through loop backwards.
    TUint current = iSamplesPerChunk.size() - 1;
    for (;;) {
        // Note: aChunkIndex = 0 => iFirstChunk = 1
        if (iSamplesPerChunk[current].iFirstChunk <= aChunkIndex + 1) {
            return iSamplesPerChunk[current].iSamples;
        }
        ASSERT(current != 0);
        current--;
    }
}

TUint SeekTable::StartSample(TUint aChunkIndex) const
{
    // NOTE: chunk indexes passed in start from 0, but chunks referenced within seek table start from 1.
    TUint startSample = 0;
    const TUint desiredChunk = aChunkIndex + 1;
    TUint prevFirstChunk = 1;
    TUint prevSamples = 0;
    for (TUint i = 0; i < iSamplesPerChunk.size(); i++) {
        const TUint nextFirstChunk = iSamplesPerChunk[i].iFirstChunk;
        const TUint nextSamples = iSamplesPerChunk[i].iSamples;

        // Desired chunk was within last chunk range.
        if (nextFirstChunk >= desiredChunk) {
            const TUint chunkDiff = desiredChunk - prevFirstChunk;
            startSample += chunkDiff * prevSamples;
            prevFirstChunk = nextFirstChunk;
            prevSamples = nextSamples;
            break;
        }

        const TUint chunkDiff = nextFirstChunk - prevFirstChunk;
        startSample += chunkDiff * prevSamples;
        prevFirstChunk = nextFirstChunk;
        prevSamples = nextSamples;
    }

    // See if exhausted samples per chunk table without encountering desired chunk.
    if (prevFirstChunk < desiredChunk) {
        const TUint chunkDiff = desiredChunk - prevFirstChunk;
        startSample += chunkDiff * prevSamples;
    }

    return startSample;
}

TUint64 SeekTable::Offset(TUint64& aAudioSample, TUint64& aSample)
{
    if (iSamplesPerChunk.size() == 0 || iAudioSamplesPerSample.size() == 0
            || iOffsets.size() == 0) {
        THROW(CodecStreamCorrupt); // seek table empty - cannot do seek // FIXME - throw a MpegMediaFileInvalid exception, which is actually expected/caught?
    }

    const TUint64 codecSampleFromAudioSample = CodecSample(aAudioSample);

    // FIXME - if stss box was present, must use it here to find appropriate sync sample.
    // If stss box not present all codec samples are sync samples.

    const TUint chunk = Chunk(codecSampleFromAudioSample);
    // FIXME - could go one step further and use chunk-to-sample table to find offset of desired sample within desired chunk.
    const TUint codecSampleFromChunk = CodecSampleFromChunk(chunk);
    const TUint audioSampleFromCodecSample = AudioSampleFromCodecSample(
            codecSampleFromChunk);

    aAudioSample = audioSampleFromCodecSample;
    aSample = codecSampleFromChunk;

    //stco:
    if (chunk >= iOffsets.size()+1) { // error - required chunk doesn't exist
        THROW(MediaMpeg4OutOfRange);
    }
    return iOffsets[chunk - 1]; // entry found - return offset to required chunk
}

TUint64 SeekTable::GetOffset(TUint aChunkIndex) const
{
    if (aChunkIndex >= iOffsets.size()) {
        Log::Print("SOMETHING EWAN HAS DONE HAS GONE WRONG\n");
    }

    ASSERT(aChunkIndex < iOffsets.size());
    return iOffsets[aChunkIndex];
}

TBool SeekTable::IsFragmentedStream() const
{
    return iIsFragmentedStream;
}


void SeekTable::WriteInit()
{
    iSpcWriteIndex = 0;
    iAspsWriteIndex = 0;
    iOffsetsWriteIndex = 0;
}

void SeekTable::Write(IWriter& aWriter, TUint aMaxBytes)
{
    TUint bytesLeftToWrite = aMaxBytes;
    WriterBinary writerBin(aWriter);

    writerBin.WriteUint8(iIsFragmentedStream ? 1 : 0);

    const TUint samplesPerChunkCount = iSamplesPerChunk.size();
    if (iSpcWriteIndex == 0) {
        if (bytesLeftToWrite < sizeof(TUint32)) {
            return;
        }
        writerBin.WriteUint32Be(samplesPerChunkCount);
        bytesLeftToWrite -= sizeof(TUint32);
    }

    while (iSpcWriteIndex < samplesPerChunkCount) {
        if (bytesLeftToWrite < 3*sizeof(TUint32)) {
            return;
        }
        writerBin.WriteUint32Be(iSamplesPerChunk[iSpcWriteIndex].iFirstChunk);
        writerBin.WriteUint32Be(iSamplesPerChunk[iSpcWriteIndex].iSamples);
        writerBin.WriteUint32Be(iSamplesPerChunk[iSpcWriteIndex].iSampleDescriptionIndex);
        bytesLeftToWrite -= 3*sizeof(TUint32);
        iSpcWriteIndex++;
    }

    const TUint audioSamplesPerSampleCount = iAudioSamplesPerSample.size();
    if (iAspsWriteIndex == 0) {
        if (bytesLeftToWrite < sizeof(TUint32)) {
            return;
        }
        writerBin.WriteUint32Be(audioSamplesPerSampleCount);
        bytesLeftToWrite -= sizeof(TUint32);
    }

    while (iAspsWriteIndex < audioSamplesPerSampleCount) {
        if (bytesLeftToWrite < 2*sizeof(TUint32)) {
            return;
        }
        writerBin.WriteUint32Be(iAudioSamplesPerSample[iAspsWriteIndex].iSampleCount);
        writerBin.WriteUint32Be(iAudioSamplesPerSample[iAspsWriteIndex].iAudioSamples);
        bytesLeftToWrite -= 2*sizeof(TUint32);
        iAspsWriteIndex++;
    }

    const TUint chunkCount = iOffsets.size();
    if (iOffsetsWriteIndex == 0) {
        if (bytesLeftToWrite < sizeof(TUint32)) {
            return;
        }
        writerBin.WriteUint32Be(chunkCount);
        bytesLeftToWrite -= sizeof(TUint32);
    }

    while (iOffsetsWriteIndex < chunkCount) {
        if (bytesLeftToWrite < sizeof(TUint64)) {
            return;
        }
        writerBin.WriteUint64Be(iOffsets[iOffsetsWriteIndex]);
        bytesLeftToWrite -= sizeof(TUint64);
        iOffsetsWriteIndex++;
    }
}

TBool SeekTable::WriteComplete() const
{
    return (iSpcWriteIndex == iSamplesPerChunk.size()) &&
           (iAspsWriteIndex == iAudioSamplesPerSample.size()) &&
           (iOffsetsWriteIndex == iOffsets.size());
}

TUint64 SeekTable::CodecSample(TUint64 aAudioSample) const
{
    // Use entries from stts box to find codec sample that contains the desired
    // audio sample.
    TUint64 totalCodecSamples = 0;
    TUint64 totalAudioSamples = 0;
    for (TUint entry = 0; entry < iAudioSamplesPerSample.size(); entry++) {
        const TUint sampleCount = iAudioSamplesPerSample[entry].iSampleCount;
        const TUint audioSamples = iAudioSamplesPerSample[entry].iAudioSamples;
        const TUint audioSamplesInrange = sampleCount * audioSamples;
        if (aAudioSample <= totalCodecSamples + audioSamplesInrange) {
            // Audio samples are within this range.

            // Find codec sample in this range that contains given audio sample.
            ASSERT(aAudioSample >= totalAudioSamples);
            const TUint64 audioSampleOffset = aAudioSample - totalAudioSamples;
            const TUint64 codecSampleOffset = audioSampleOffset / audioSamples;
            ASSERT(codecSampleOffset <= sampleCount);

            totalCodecSamples += codecSampleOffset;
            return totalCodecSamples;
        }
        totalCodecSamples += sampleCount;
        totalAudioSamples += audioSamplesInrange;
    }

    if (aAudioSample > totalAudioSamples) {
        THROW(MediaMpeg4OutOfRange);
    }

    // Something went wrong. Could be corrupt table or programmer error!
    LOG(kCodec, "SeekTable::CodecSample could not find aAudioSample: %u\n", aAudioSample);
    THROW(MediaMpeg4FileInvalid);
}

TUint SeekTable::SamplesPerChunkTotal(TUint aIndex) const
{
    // Calculates chunks*samples_per_chunk at given index in samples-per-chunk
    // table.
    ASSERT(aIndex < iSamplesPerChunk.size());
    const TUint startChunk = iSamplesPerChunk[aIndex].iFirstChunk;
    const TUint spc = iSamplesPerChunk[aIndex].iSamples;
    TUint endChunk = 0;

    // Find last chunk in current run.
    if (aIndex + 1 < iSamplesPerChunk.size()) {
        endChunk = iSamplesPerChunk[aIndex + 1].iFirstChunk;
    }
    else {
        // No next entry, so end chunk must be last chunk in file.
        // Since chunk numbers start at one, must be chunk_count+1.
        endChunk = iOffsets.size()+1;
    }

    const TUint chunkDiff = endChunk - startChunk;
    const TUint samplesInRange = chunkDiff * spc;

    return samplesInRange;
}

TUint SeekTable::ChunkWithinSamplesPerChunk(TUint aIndex, TUint aSampleOffset) const
{
    ASSERT(aIndex < iSamplesPerChunk.size());
    const TUint chunk = iSamplesPerChunk[aIndex].iFirstChunk;
    const TUint spc = iSamplesPerChunk[aIndex].iSamples;
    const TUint chunkOffset = static_cast<TUint>(aSampleOffset / spc);
    return chunk + chunkOffset;
}

TUint SeekTable::Chunk(TUint64 aCodecSample) const
{
    // Use data from stsc box to find chunk containing the desired codec sample.
    TUint64 totalSamples = 0;
    for (TUint entry = 0; entry < iSamplesPerChunk.size(); entry++) {
        const TUint samplesInRange = SamplesPerChunkTotal(entry);
        if (aCodecSample < totalSamples + samplesInRange) {
            // Desired sample is in this range.

            // Find chunk in this range that contains the desired sample.
            ASSERT(aCodecSample >= totalSamples);
            const TUint64 sampleOffset64 = aCodecSample - totalSamples;
            ASSERT(sampleOffset64 <= std::numeric_limits<TUint>::max());  // Ensure no issues with casting to smaller type.
            const TUint sampleOffset = static_cast<TUint>(sampleOffset64);
            const TUint chunk = ChunkWithinSamplesPerChunk(entry, sampleOffset);

            return chunk;
        }

        totalSamples += samplesInRange;
    }

    if (aCodecSample > totalSamples) {
        THROW(MediaMpeg4OutOfRange);
    }

    LOG(kCodec, "SeekTable::Chunk could not find aCodecSample: %u\n", aCodecSample);
    THROW(MediaMpeg4FileInvalid);
}

TUint SeekTable::CodecSampleFromChunk(TUint aChunk) const
{
    // Use data from stsc box to find chunk containing the desired codec sample.
    TUint totalSamples = 0;
    TUint chunk = 1;
    for (TUint entry = 0; entry < iSamplesPerChunk.size(); entry++) {
        const TUint startChunk = iSamplesPerChunk[entry].iFirstChunk;
        const TUint spc = iSamplesPerChunk[entry].iSamples;
        TUint endChunk = 0;

        // Find last chunk in current run.
        if (entry + 1 < iSamplesPerChunk.size()) {
            endChunk = iSamplesPerChunk[entry + 1].iFirstChunk;
        }
        else {
            // No next entry, so end chunk must be last chunk in file.
            endChunk = iOffsets.size();
        }

        const TUint chunkDiff = endChunk - startChunk;
        const TUint samplesInRange = chunkDiff * spc;

        if (aChunk <= endChunk) {
            // Desired chunk is in this range.

            const TUint chunkOffset = aChunk - startChunk;
            const TUint sampleOffset = chunkOffset * spc;
            totalSamples += sampleOffset;
            return totalSamples;
        }

        totalSamples += samplesInRange;
        chunk = startChunk;
    }

    if (aChunk > chunk) {
        THROW(MediaMpeg4OutOfRange);
    }

    LOG(kCodec, "SeekTable::CodecSampleFromChunk could not find aCodecSample: %u\n", aChunk);
    THROW(MediaMpeg4FileInvalid);
}

TUint SeekTable::AudioSampleFromCodecSample(TUint aCodecSample) const
{
    // Use entries from stts box to find audio sample that start at given codec sample;
    TUint totalCodecSamples = 0;
    for (TUint entry = 0; entry < iAudioSamplesPerSample.size(); entry++) {
        const TUint sampleCount = iAudioSamplesPerSample[entry].iSampleCount;
        const TUint audioSamples = iAudioSamplesPerSample[entry].iAudioSamples;
        if (aCodecSample <= totalCodecSamples + sampleCount) {
            // Codec sample is within this range.

            // Find the number of audio samples at the start of the given codec sample.
            ASSERT(totalCodecSamples <= aCodecSample);
            const TUint codecSampleOffset = aCodecSample - totalCodecSamples;
            const TUint audioSampleOffset = codecSampleOffset * audioSamples;

            return audioSampleOffset;
        }
        totalCodecSamples += sampleCount;
    }

    if (aCodecSample > totalCodecSamples) {
        THROW(MediaMpeg4OutOfRange);
    }

    // Something went wrong. Could be corrupt table or programmer error!
    LOG(kCodec, "SeekTable::AudioSampleFromCodecSample could not find aCodecSample: %u\n", aCodecSample);
    THROW(MediaMpeg4FileInvalid);
}


// SeekTableInitialiser

SeekTableInitialiser::SeekTableInitialiser(SeekTable& aSeekTable, IReader& aReader)
    : iSeekTable(aSeekTable)
    , iReader(aReader)
    , iInitialised(false)
{
}

void SeekTableInitialiser::Init()
{
    ASSERT(!iInitialised);
    ReaderBinary readerBin(iReader);

    const TBool isFragmentedStream = readerBin.ReadUintBe(1) == 1 ? true : false;
    iSeekTable.SetIsFragmentedStream(isFragmentedStream);

    const TUint samplesPerChunkCount = readerBin.ReadUintBe(4);
    iSeekTable.InitialiseSamplesPerChunk(samplesPerChunkCount);
    for (TUint i = 0; i < samplesPerChunkCount; i++) {
        const TUint firstChunk = readerBin.ReadUintBe(4);
        const TUint samples = readerBin.ReadUintBe(4);
        const TUint sampleDescriptionIndex = readerBin.ReadUintBe(4);
        iSeekTable.SetSamplesPerChunk(firstChunk, samples,
                sampleDescriptionIndex);
    }

    const TUint audioSamplesPerSampleCount = readerBin.ReadUintBe(4);
    iSeekTable.InitialiseAudioSamplesPerSample(audioSamplesPerSampleCount);
    for (TUint i = 0; i < audioSamplesPerSampleCount; i++) {
        const TUint sampleCount = readerBin.ReadUintBe(4);
        const TUint audioSamples = readerBin.ReadUintBe(4);
        iSeekTable.SetAudioSamplesPerSample(sampleCount, audioSamples);
    }

    const TUint chunkCount = readerBin.ReadUintBe(4);
    iSeekTable.InitialiseOffsets(chunkCount);
    for (TUint i = 0; i < chunkCount; i++) {
        const TUint64 offset = readerBin.ReadUint64Be(8);
        iSeekTable.SetOffset(offset);
    }
    iInitialised = true;
}


// MsgAudioEncodedWriter

MsgAudioEncodedWriter::MsgAudioEncodedWriter(MsgFactory& aMsgFactory) :
        iMsgFactory(aMsgFactory), iMsg(nullptr)
{
}

MsgAudioEncodedWriter::~MsgAudioEncodedWriter()
{
    ASSERT(iMsg == nullptr);
    ASSERT(iBuf.Bytes() == 0);
}

MsgAudioEncoded* MsgAudioEncodedWriter::Msg()
{
    ASSERT(iBuf.Bytes() == 0);  // Ensure no audio still buffered.
    MsgAudioEncoded* msg = iMsg;
    iMsg = nullptr;
    return msg;
}

void MsgAudioEncodedWriter::Write(TByte aValue)
{
    const TUint bufCapacity = iBuf.BytesRemaining();
    if (bufCapacity >= sizeof(TByte)) {
        iBuf.Append(aValue);
    }
    else {
        AllocateMsg();
        iBuf.Append(aValue);
    }
}

void MsgAudioEncodedWriter::Write(const Brx& aBuffer)
{
    TUint remaining = aBuffer.Bytes();
    TUint offset = 0;

    while (remaining > 0) {
        const TUint bufCapacity = iBuf.BytesRemaining();

        // Do a partial append of aBuffer if space in iBuf.
        if (bufCapacity > 0) {
            TUint bytes = bufCapacity;
            if (remaining < bufCapacity) {
                bytes = remaining;
            }
            iBuf.Append(aBuffer.Ptr() + offset, bytes);
            offset += bytes;
            remaining -= bytes;
        }
        else {
            AllocateMsg();
        }
    }
}

void MsgAudioEncodedWriter::WriteFlush()
{
    if (iBuf.Bytes() > 0) {
        AllocateMsg();
    }
}

void MsgAudioEncodedWriter::AllocateMsg()
{
    ASSERT(iBuf.Bytes() > 0);
    MsgAudioEncoded* msg = iMsgFactory.CreateMsgAudioEncoded(
            Brn(iBuf.Ptr(), iBuf.Bytes()));
    if (iMsg == nullptr) {
        iMsg = msg;
    }
    else {
        iMsg->Add(msg);
    }
    iBuf.SetBytes(0);
}

// Mpeg4OutOfBandReader

Mpeg4OutOfBandReader::Mpeg4OutOfBandReader(MsgFactory& aMsgFactory, IContainerUrlBlockWriter& aBlockWriter)
    : iMsgFactory(aMsgFactory)
    , iBlockWriter(aBlockWriter)
    , iOffset(0)
    , iStreamBytes(0)
    , iDiscardBytes(0)
    , iInspectBytes(0)
    , iAccumulateBytes(0)
    , iInspectBuffer(nullptr)
{
}

void Mpeg4OutOfBandReader::Reset(TUint64 aStreamBytes)
{
    iStreamBytes = aStreamBytes;
    iDiscardBytes = 0;
    iInspectBytes = 0;
    iAccumulateBytes = 0;
    iInspectBuffer = nullptr;
    iReadBuffer.SetBytes(0);
    iAccumulateBuffer.SetBytes(0);
}

void Mpeg4OutOfBandReader::SetReadOffset(TUint64 aStartOffset)
{
    iOffset = aStartOffset;
}

void Mpeg4OutOfBandReader::Discard(TUint aBytes)
{
    ASSERT(iDiscardBytes == 0);
    iDiscardBytes = aBytes;
}

void Mpeg4OutOfBandReader::Inspect(Bwx& aBuf, TUint aBytes)
{
    ASSERT(iInspectBuffer == nullptr);
    ASSERT(aBuf.MaxBytes() >= aBytes);
    aBuf.SetBytes(0);
    iInspectBuffer = &aBuf;
    iInspectBytes = aBytes;
}

void Mpeg4OutOfBandReader::Accumulate(TUint aBytes)
{
    ASSERT(iAccumulateBytes == 0);
    // FIXME - alter MPEG4 parsing so that no metadata processor directly pulls a MsgAudioEncoded?
    ASSERT(iAccumulateBytes <= kMaxAccumulateBytes); // Can't support accumulating more than this.
    iAccumulateBytes = aBytes;
    iAccumulateBuffer.SetBytes(0);
}

Msg* Mpeg4OutOfBandReader::Pull()
{
    // Don't support just pulling msgs.
    ASSERT(iDiscardBytes > 0 || iInspectBytes > 0 || iAccumulateBytes > 0);

    if (iDiscardBytes > 0) {
        if (iDiscardBytes == iReadBuffer.Bytes()) {
            iDiscardBytes = 0;
            iReadBuffer.SetBytes(0);
        }
        else if (iDiscardBytes > iReadBuffer.Bytes()) {
            iDiscardBytes -= iReadBuffer.Bytes();
            iReadBuffer.SetBytes(0);
            iOffset += iDiscardBytes;
            iDiscardBytes = 0;
        }
        else {
            Brn remaining(iReadBuffer.Ptr() + iDiscardBytes, iReadBuffer.Bytes() - iDiscardBytes);
            iReadBuffer.Replace(remaining);
            iDiscardBytes = 0;
        }
    }

    if (iInspectBytes > 0) {
        const TBool success = PopulateBuffer(*iInspectBuffer, iInspectBytes);
        iInspectBytes = 0;
        iInspectBuffer = nullptr;   // No need to reference anymore, as won't append more data.
        if (success) {
            return nullptr;
        }
        else {
            THROW(AudioCacheException);
        }
    }

    if (iAccumulateBytes > 0) {
        const TBool success = PopulateBuffer(iAccumulateBuffer, iAccumulateBytes);
        iAccumulateBytes = 0;
        if (success) {
            MsgAudioEncoded* msg = iMsgFactory.CreateMsgAudioEncoded(iAccumulateBuffer);
            iAccumulateBuffer.SetBytes(0);
            return msg;
        }
        else {
            THROW(AudioCacheException);
        }
    }
    ASSERTS();
    return nullptr;
}

TBool Mpeg4OutOfBandReader::PopulateBuffer(Bwx& aBuf, TUint aBytes)
{
    while (aBytes > 0) {
        TBool success = true;
        if (iReadBuffer.Bytes() == 0) {
            WriterBuffer writerBuffer(iReadBuffer);

            // For efficiency, try fill entire read buffer in case more reads come in.
            TUint bytes = iReadBuffer.MaxBytes();
            TUint64 fileBytesRemaining = 0;
            if (iStreamBytes > iOffset) {
                fileBytesRemaining = iStreamBytes - iOffset;
            }
            // Don't want to read beyond end of stream, as TryGetUrl() will return false.
            if (fileBytesRemaining < bytes) {
                // If we get here, fileBytesRemaining MUST fit within TUint.
                bytes = static_cast<TUint>(fileBytesRemaining);
            }
            success = iBlockWriter.TryGetUrl(writerBuffer, iOffset, bytes);
            //success = iBlockWriter.TryGetUrl(writerBuffer, iOffset, iReadBuffer.MaxBytes());
            iOffset += iReadBuffer.Bytes();
        }

        if (iReadBuffer.Bytes() <= aBytes) {
            aBuf.Append(iReadBuffer);
            aBytes -= iReadBuffer.Bytes();
            iReadBuffer.SetBytes(0);
        }
        else {
            aBuf.Append(Brn(iReadBuffer.Ptr(), aBytes));
            iReadBuffer.Replace(Brn(iReadBuffer.Ptr()+aBytes, iReadBuffer.Bytes()-aBytes));
            aBytes = 0;
        }

        if (!success) {
            return false;
        }
    }
    ASSERT(aBytes == 0);
    return true;
}


// Mpeg4MetadataChecker

Mpeg4MetadataChecker::Mpeg4MetadataChecker()
    : iMetadataAvailable(false)
{
}

void Mpeg4MetadataChecker::Reset()
{
    iMetadataAvailable = false;
}

TBool Mpeg4MetadataChecker::MetadataAvailable() const
{
    return iMetadataAvailable;
}

void Mpeg4MetadataChecker::MetadataRetrieved()
{
    iMetadataAvailable = true;
}


// Mpeg4ProtectionDetails

const TUint Mpeg4ProtectionDetails::kInitialSampleIVBufferSize = 1024 * 2; // 2KB
const TUint Mpeg4ProtectionDetails::kSampleIVBufferGrowthSize  = 1024 * 1; // 1KB

Mpeg4ProtectionDetails::Mpeg4ProtectionDetails()
    : iIsProtected(false)
    , iPerSampleIVSize(0)
    , iSampleIVs(kInitialSampleIVBufferSize, kSampleIVBufferGrowthSize)
{ }

TBool Mpeg4ProtectionDetails::IsProtected() const
{
    return iIsProtected;
}

const Brx& Mpeg4ProtectionDetails::KID() const
{
    return iKID;
}

TUint Mpeg4ProtectionDetails::PerSampleIVSizeBytes() const
{
    return iPerSampleIVSize;
}

TBool Mpeg4ProtectionDetails::HasPerSampleIVs() const
{
    return iSampleIVs.Buffer().Bytes() > 0;
}

void Mpeg4ProtectionDetails::SetProtected()
{
    iIsProtected = true;
}

void Mpeg4ProtectionDetails::SetPerSampleIVSize(TUint aPerSampleIVSize)
{
    iPerSampleIVSize = aPerSampleIVSize;
}

void Mpeg4ProtectionDetails::SetKID(const Brx& aKID)
{
    iKID.ReplaceThrow(aKID);
}

void Mpeg4ProtectionDetails::AddSampleIV(const Brx& aIV)
{
    iSampleIVs.Write(aIV);
}

const Brx& Mpeg4ProtectionDetails::GetSampleIV(TUint aSampleIndex)
{
    const TUint offset = aSampleIndex * iPerSampleIVSize;
    if (offset >= iSampleIVs.Buffer().Bytes()) {
        // TODO: Do we assert here or return an empty IV??
        return Brx::Empty();
    }

    Brn iv(iSampleIVs.Buffer().Ptr() + offset, iPerSampleIVSize);
    AlignIV16(iIVBuffer, iv);

    return iIVBuffer;
}

void Mpeg4ProtectionDetails::Reset()
{
    iIsProtected = false;

    iPerSampleIVSize = 0;

    iKID.SetBytes(0);
    iIVBuffer.SetBytes(0);

    ClearSampleIVs();
}

void Mpeg4ProtectionDetails::ClearSampleIVs()
{
    iSampleIVs.Reset();
}

void Mpeg4ProtectionDetails::AlignIV16(Bwx& aBuffer, const Brx& aIV)
{
    ASSERT_VA(aBuffer.MaxBytes() >= 16, "%s\n", "A minimum of 16byte buffer is required for this.");
    ASSERT_VA(aIV.Bytes() == 8 || aIV.Bytes() == 16, "%s\n", "An 8 or 16byte IV is required for this.");

    // Spec Link: 23001-7 (9.1)
    // If IV_SIZE is 16, then IV specifies the entire 128-bit IV value
    // If IV_SIZE is 8, then the 128-bit IV value is made of the IV value copied to bytes 0 to 7 and then 8 to 15 are set to zero.
    aBuffer.Replace(aIV);

    while(aBuffer.Bytes() < 16) {
        aBuffer.Append((TByte)0);
    }
}

// Mpeg4ContainerInfo

Mpeg4ContainerInfo::Mpeg4ContainerInfo()
{
    Reset();
}

Mpeg4ContainerInfo::EProcessingMode Mpeg4ContainerInfo::ProcessingMode() const
{
    return iProcessingMode;
}

TBool Mpeg4ContainerInfo::CanProcess(TUint64 aFileOffset) const
{
    if (iProcessingMode == EProcessingMode::Complete) {
        return true;
    }

    // NOTE: For fragmented streams we only support containers who only provide complete stream data
    //       as part of the 'mdat' box. This means that the dataOffset must point to the first byte
    //       of stream data inside of the 'mdat' box.
    //       This is signalled by:
    //       A) Have 'DefaultBaseIsMoof' set + baseDataOffset == 0
    //       B) Have a total data offset == first byte of data inside 'mdat' box
    //       C) baseDataOffset + dataOffset == current FileReadOffset (When DefaultBaseIsMoof is not set, but implied by the values of baseDataOffset + dataOffset)
    const TUint conditionA = iDefaultBaseIsMoof && iBaseDataOffset == 0 && (iDataOffset == 0 || iDataOffset == iMoofBoxSize + 8);
    const TUint conditionB = (iBaseDataOffset + iDataOffset) == (iMoofBoxSize + 8);
    const TUint conditionC = (iBaseDataOffset + iDataOffset) == aFileOffset;

    return conditionA || conditionB || conditionC;
}

TUint64 Mpeg4ContainerInfo::FirstMoofStart() const
{
    return iFirstMoofOffset;
}

void Mpeg4ContainerInfo::SetFragmented(TUint aMoofBoxSize)
{
    iProcessingMode = EProcessingMode::Fragmented;
    iMoofBoxSize    = aMoofBoxSize;
}

void Mpeg4ContainerInfo::SetBaseDataOffset(TUint64 aBaseDataOffset)
{
    iBaseDataOffset = aBaseDataOffset;
}

void Mpeg4ContainerInfo::SetDefaultBaseIsMoof()
{
    iDefaultBaseIsMoof = true;
}

void Mpeg4ContainerInfo::SetDataOffset(TUint64 aDataOffset)
{
    iDataOffset = aDataOffset;
}

void Mpeg4ContainerInfo::SetFirstMoofStart(TUint64 aOffset)
{
    if (iFirstMoofOffset == 0) {
        iFirstMoofOffset = aOffset;
    }
}

void Mpeg4ContainerInfo::Reset()
{
    iProcessingMode    = EProcessingMode::Complete;
    iMoofBoxSize       = 0;
    iBaseDataOffset    = 0;
    iDataOffset        = 0;
    iFirstMoofOffset   = 0;
    iDefaultBaseIsMoof = false;
}


// Mpeg4Container

Mpeg4Container::Mpeg4Container(IMimeTypeList& aMimeTypeList, Optional<IMpegDRMProvider> aDRMProvider)
    : ContainerBase(Brn("MP4"))
    , iDRMProvider(aDRMProvider)
    , iBoxRoot(iProcessorFactory)
    , iBoxRootOutOfBand(iProcessorFactory) // Share factory; okay here as neither should access the same box simultaneously.
    , iOutOfBandReader(nullptr)
    , iSeekObserver(nullptr)
    , iLock("MP4L")
{
    aMimeTypeList.Add("audio/mp4");
}

Mpeg4Container::~Mpeg4Container()
{
    delete iOutOfBandReader;
}

void Mpeg4Container::Construct(IMsgAudioEncodedCache& aCache, MsgFactory& aMsgFactory, IContainerSeekHandler& aSeekHandler, IContainerUrlBlockWriter& aUrlBlockWriter, IContainerStopper& aContainerStopper)
{
    ContainerBase::Construct(aCache, aMsgFactory, aSeekHandler, aUrlBlockWriter, aContainerStopper);

    iOutOfBandReader = new Mpeg4OutOfBandReader(aMsgFactory, aUrlBlockWriter);

    iProcessorFactory.Add(new Mpeg4BoxSwitcher(iProcessorFactory, Brn("trak")));
    iProcessorFactory.Add(new Mpeg4BoxSwitcher(iProcessorFactory, Brn("mdia")));
    iProcessorFactory.Add(new Mpeg4BoxSwitcher(iProcessorFactory, Brn("minf")));
    iProcessorFactory.Add(new Mpeg4BoxSwitcher(iProcessorFactory, Brn("stbl")));
    iProcessorFactory.Add(new Mpeg4BoxMoov(iProcessorFactory, iMetadataChecker));
    iProcessorFactory.Add(new Mpeg4BoxStsd(iStreamInfo, iCodecInfo, iProtectionDetails));
    iProcessorFactory.Add(new Mpeg4BoxStts(iSeekTable));
    iProcessorFactory.Add(new Mpeg4BoxStsc(iSeekTable));
    iProcessorFactory.Add(new Mpeg4BoxStco(iSeekTable));
    iProcessorFactory.Add(new Mpeg4BoxCo64(iSeekTable));
    iProcessorFactory.Add(new Mpeg4BoxStsz(iSampleSizeTable));
    iProcessorFactory.Add(new Mpeg4BoxMdhd(iDurationInfo));
    iProcessorFactory.Add(
        new Mpeg4BoxMdat(iDRMProvider, aMsgFactory, iBoxRootOutOfBand, iMetadataChecker, *this, *this, iBoxRoot, iSeekTable, iSampleSizeTable, iProtectionDetails, iContainerInfo, *iOutOfBandReader));

    // 'Moof' specific boxes
    iProcessorFactory.Add(new Mpeg4BoxSidx(iSeekTable));

    iProcessorFactory.Add(new Mpeg4BoxTkhd(iDurationInfo));
    iProcessorFactory.Add(new Mpeg4BoxSwitcher(iProcessorFactory, Brn("mvex")));
    iProcessorFactory.Add(new Mpeg4BoxMehd(iDurationInfo));

    iProcessorFactory.Add(new Mpeg4BoxMoof(iProcessorFactory, iContainerInfo, static_cast<IBoxOffsetProvider&>(iBoxRoot), iSeekTable));
    iProcessorFactory.Add(new Mpeg4BoxSwitcher(iProcessorFactory, Brn("traf")));
    iProcessorFactory.Add(new Mpeg4BoxTfhd(iSampleSizeTable, iContainerInfo));
    iProcessorFactory.Add(new Mpeg4BoxTrun(iSampleSizeTable, iContainerInfo));
    iProcessorFactory.Add(new Mpeg4BoxSenc(iProtectionDetails));

    ASSERT(iSeekObserver != nullptr);

    Reset();
}

Msg* Mpeg4Container::Recognise()
{
    LOG(kMedia, "Mpeg4Container::Recognise\n");

    if (!iRecognitionStarted) {
        static const TUint kSizeBytes = 4;
        iCache->Discard(kSizeBytes);
        iCache->Inspect(iRecogBuf, iRecogBuf.MaxBytes());
        iRecognitionStarted = true;
    }

    // Avoid pulling through new MsgEncodedStream during recognition (which would then be discarded!)
    Msg* msg = iCache->Pull();
    if (msg != nullptr) {
        return msg;
    }

    if (iRecogBuf == Brn("ftyp")) {
        iRecognitionSuccess = true;
        return nullptr;
    }

    return nullptr;
}

TBool Mpeg4Container::Recognised() const
{
    return iRecognitionSuccess;
}

void Mpeg4Container::Reset()
{
    iProcessorFactory.Reset();
    iBoxRoot.Reset();
    iBoxRoot.Set(*iCache, Mpeg4BoxSwitcherRoot::kNoTargetId);
    iBoxRootOutOfBand.Reset();
    iMetadataChecker.Reset();
    iDurationInfo.Reset();
    iStreamInfo.Reset();
    iCodecInfo.Reset();
    iSampleSizeTable.Reset();
    iSeekTable.Deinitialise();
    iContainerInfo.Reset();
    iProtectionDetails.Reset();
    iRecognitionStarted = false;
    iRecognitionSuccess = false;
}

void Mpeg4Container::Init(TUint64 aStreamBytes)
{
    iOutOfBandReader->Reset(aStreamBytes);
}

TBool Mpeg4Container::TrySeek(TUint aStreamId, TUint64 aOffset)
{
    if (iContainerInfo.ProcessingMode() == Mpeg4ContainerInfo::EProcessingMode::Fragmented) {
        // Fragmented streams are based on the SIDX.
        // This defines how large each fragment/segment is starting from the position of the first MOOF box encountered in the stream
        const TUint fragmentIndex = (TUint)aOffset;
        if (fragmentIndex >= iSeekTable.ChunkCount()) {
            LOG_ERROR(kCodec, "Mpeg4Container::TrySeek - Index of: %u doesn't exist. We have %u available.\n", fragmentIndex, iSeekTable.ChunkCount());
        }

        TUint64 offset = iContainerInfo.FirstMoofStart();
        for(TUint i = 0; i < fragmentIndex; i += 1) {
            offset += iSeekTable.GetOffset(i);
        }

        const TBool seek = iSeekHandler->TrySeekTo(aStreamId, offset);
        if (seek) {
            iSeekObserver->ChunkSeek(0); // The value here doesn't really matter for fragmented files, but we still need to call the function
            iBoxRoot.Reset();
        }
        return seek;
    }
    else {
        // As TrySeek requires a byte offset, any codec that uses an Mpeg4 stream MUST find the appropriate seek offset (in bytes) and pass that via TrySeek().
        // i.e., aOffset MUST match a chunk offset.

        const TUint chunkCount = iSeekTable.ChunkCount();
        for (TUint i = 0; i < chunkCount; i++) {
            if (iSeekTable.GetOffset(i) == aOffset) {
                const TBool seek = iSeekHandler->TrySeekTo(aStreamId, aOffset);
                if (seek) {
                    iSeekObserver->ChunkSeek(i);
                }
                return seek;
            }
        }
    }

    ASSERTS();
    return false;
}

Msg* Mpeg4Container::Pull()
{
    try {
        Msg* msg = nullptr;
        while (msg == nullptr) {
            msg = iBoxRoot.Process();
        }
        return msg;
    }
    catch (const MediaMpeg4FileInvalid&) {
        THROW(ContainerStreamCorrupt);
    }
}

void Mpeg4Container::ResetProvider()
{
    iMdataState = eMdataNone;
}

MsgAudioEncoded* Mpeg4Container::GetMetadata()
{
    MsgAudioEncoded* msg = nullptr;

    switch (iMdataState) {
    case eMdataNone:
        {
            // FIXME - should be able to pass codec info msg on directly without copying into buffer here.
            // However, need to know size of it for codecs to unpack it into a buffer, and it's generally small (< 50 bytes) and a one-off per stream so it isn't a huge performance hit.
            MsgAudioEncoded* codecInfo = iCodecInfo.CodecInfo();

            // Metadata requirements depend very much on the type of MPEG stream (Complete streams / Fragmented Stream) and on the underlying
            // audio codec.
            // For Complete Streams:
            //  - We'll enter this function once.
            //  - 'CodecInfo' will always be present as this is required to be provided by the MPEG container prior to accessing
            //    the audio data.
            //  - If the codec requires it, we'll provide some information about the codec prior to the 'CodecInfo' and then follow
            //    up with a populated sample and seek table for use.
            //
            // For fragmented streams:
            //  - We'll enter this function for each 'mdat' box encountered (usually 1-per-fragment).
            //  - The first time we visit, 'CodecInfo' will be present. Subsequent visits, this will be null.
            //  - The first time we visit, if required by the Codec, we'll provide the information about the codec prior to
            //    the 'CodecInfo' and then send over a populated sample and seek table.
            //  - Future visits, dpending on the codec, will also send over an updated popualted sample & seek table.
            const TBool isFragmentedStream = iContainerInfo.ProcessingMode() == Mpeg4ContainerInfo::EProcessingMode::Fragmented;
            const TBool hasCodecInfo       = codecInfo != nullptr;

            if (!isFragmentedStream) {
                ASSERT_VA(hasCodecInfo, "%s\n", "Mpeg4Container::GetMetadata - Complete stream but no codec info.\n");
                ASSERT_VA(hasCodecInfo, "%s\n", "Mpeg4Container::GetMetadata - Complete stream but no codec info.\n");
            }

            // NOTE: Some codecs provide the required stream & seek information encoded as part of their own data.
            //       For these, we don't want to emit anything extra.
            const TBool codecMpeg4InfoHeader     = iStreamInfo.Codec() != Brn("fLaC");
            const TBool codecRequiresSampleTable = iStreamInfo.Codec() == Brn("dOps");
            const TBool codecRequiresSeekTable   = false;

            const TBool doMpeg4Info   = (!isFragmentedStream || (isFragmentedStream && hasCodecInfo)) && codecMpeg4InfoHeader;
            const TBool doCodecInfo   = (!isFragmentedStream || (isFragmentedStream && hasCodecInfo)) && hasCodecInfo;
            const TBool doSampleTable = codecRequiresSampleTable;
            const TBool doSeekTable   = codecRequiresSeekTable;

            if (doMpeg4Info) {
                Mpeg4Info info(iStreamInfo.Codec(), iStreamInfo.SampleRate(),
                               iDurationInfo.Timescale(), iStreamInfo.Channels(),
                               iStreamInfo.BitDepth(), iDurationInfo.Duration(), codecInfo->Bytes());

                Mpeg4InfoWriter writer(info);
                Bws<Mpeg4InfoWriter::kMaxBytes> infoBuf;
                WriterBuffer writerBuf(infoBuf);
                writer.Write(writerBuf);

                // Need to create MsgAudioEncoded w/ data for codec.
                msg = iMsgFactory->CreateMsgAudioEncoded(infoBuf);
                msg->Add(codecInfo);

                iSampleSizeTable.WriteInit();
                iMdataState = eMdataSizeTab;    // For these codecs, we alwyas provide a sample & seek table
            }
            else if (doCodecInfo) {
                // Make sure to include the codec name at the beginning to ensure our codecs will recognise it property
                msg = iMsgFactory->CreateMsgAudioEncoded(iStreamInfo.Codec());
                msg->Add(codecInfo);
                iMdataState = eMdataComplete;   // For these codecs, a sample & seek table are NEVER provided
            }
            else if (doSampleTable) {
                iSampleSizeTable.WriteInit();
                iMdataState = eMdataSizeTab;
            }
            else if (doSeekTable) {
                iSeekTable.WriteInit();
                iMdataState = eMdataSeekTab;
            }
            else {
                iMdataState = eMdataComplete;
            }
        }
        break;

    case eMdataSizeTab:
        {
            MsgAudioEncodedWriter writerMsg(*iMsgFactory);
            iSampleSizeTable.Write(writerMsg, EncodedAudio::kMaxBytes);
            writerMsg.WriteFlush();
            msg = writerMsg.Msg();
            if (iSampleSizeTable.WriteComplete()) {
                iSeekTable.WriteInit();
                iMdataState = eMdataSeekTab;
            }
        }
        break;

    case eMdataSeekTab:
        {
            MsgAudioEncodedWriter writerMsg(*iMsgFactory);
            iSeekTable.Write(writerMsg, EncodedAudio::kMaxBytes);
            writerMsg.WriteFlush();
            msg = writerMsg.Msg();
            if (iSeekTable.WriteComplete()) {
                iMdataState = eMdataComplete;
            }
        }
        break;

    case eMdataComplete:
        // Should not be called again after complete, without resetting first
        ASSERTS();
    }

    return msg;
}

TBool Mpeg4Container::Complete()
{
    return (iMdataState == eMdataComplete);
}

void Mpeg4Container::RegisterChunkSeekObserver(
        IMpeg4ChunkSeekObserver& aChunkSeekObserver)
{
    iSeekObserver = &aChunkSeekObserver;
}
