#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Pipeline/RampArray.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Optional.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/InfoProvider.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Arch.h>
#include <OpenHome/Media/ClockPuller.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Net/Private/Globals.h>

#include <string.h>
#include <climits>
#include <algorithm>
#include <cstdint>

using namespace OpenHome;
using namespace OpenHome::Media;

const TChar* OpenHome::Media::kStreamPlayNames[] = { "Yes", "No", "Later" };


// AllocatorBase

const Brn AllocatorBase::kQueryMemory = Brn("memory");

AllocatorBase::~AllocatorBase()
{
    LOG(kPipeline, "> ~AllocatorBase for %s. (Peak %u/%u)\n", iName, iCellsUsedMax, iFree.Slots());
    const TUint slots = iFree.Slots();
    for (TUint i=0; i<slots; i++) {
        //Log::Print("  %u", i);
        try {
            Allocated* ptr = Read();
            //Log::Print("(%p)", ptr);
            delete ptr;
        }
        catch (AssertionFailed&) {
            Log::Print("...leak at %u of %u\n", i+1, slots);
            ASSERTS();
        }
    }
    LOG(kPipeline, "< ~AllocatorBase for %s\n", iName);
}

void AllocatorBase::Free(Allocated* aPtr)
{
    iLock.Wait();
    iCellsUsed--;
    iFree.Write(aPtr);
    iLock.Signal();
}

TUint AllocatorBase::CellsTotal() const
{
    return iCellsTotal;
}

TUint AllocatorBase::CellBytes() const
{
    return iCellBytes;
}

TUint AllocatorBase::CellsUsed() const
{
    iLock.Wait();
    TUint cellsUsed = iCellsUsed;
    iLock.Signal();
    return cellsUsed;
}

TUint AllocatorBase::CellsUsedMax() const
{
    iLock.Wait();
    TUint cellsUsedMax = iCellsUsedMax;
    iLock.Signal();
    return cellsUsedMax;
}

void AllocatorBase::GetStats(TUint& aCellsTotal, TUint& aCellBytes, TUint& aCellsUsed, TUint& aCellsUsedMax) const
{
    aCellsTotal = iCellsTotal;
    aCellBytes = iCellBytes;
    iLock.Wait();
    aCellsUsed = iCellsUsed;
    aCellsUsedMax = iCellsUsedMax;
    iLock.Signal();
}

AllocatorBase::AllocatorBase(const TChar* aName, TUint aNumCells, TUint aCellBytes, IInfoAggregator& aInfoAggregator)
    : iFree(aNumCells)
    , iLock("PAL1")
    , iName(aName)
    , iCellsTotal(aNumCells)
    , iCellBytes(aCellBytes)
    , iCellsUsed(0)
    , iCellsUsedMax(0)
{
    std::vector<Brn> infoQueries;
    infoQueries.push_back(kQueryMemory);
    aInfoAggregator.Register(*this, infoQueries);
}

Allocated* AllocatorBase::DoAllocate()
{
    iLock.Wait();
    Allocated* cell = Read();
    ASSERT_VA(cell->iRefCount == 0, "%s has count %u\n", iName, cell->iRefCount.load());
    cell->iRefCount = 1;
    iCellsUsed++;
    if (iCellsUsed > iCellsUsedMax) {
        iCellsUsedMax = iCellsUsed;
    }
    iLock.Signal();
    return cell;
}

Allocated* AllocatorBase::Read()
{
    Allocated* p = nullptr;
    try {
        p = iFree.Read();
    }
    catch (FifoReadError&) {
        Log::Print("Warning: Allocator error for %s\n", iName);
        ASSERTS();
    }
    return p;
}

void AllocatorBase::QueryInfo(const Brx& aQuery, IWriter& aWriter)
{
    // Note that value of iCellsUsed may be slightly out of date as Allocator doesn't hold any lock while updating its fifo and iCellsUsed
    AutoMutex a(iLock);
    if (aQuery == kQueryMemory) {
        WriterAscii writer(aWriter);
        writer.Write(Brn("Allocator: "));
        writer.Write(Brn(iName));
        writer.Write(Brn(", capacity:"));
        writer.WriteUint(iCellsTotal);
        writer.Write(Brn(" cells x "));
        writer.WriteUint(iCellBytes);
        writer.Write(Brn(" bytes, in use:"));
        writer.WriteUint(iCellsUsed);
        writer.Write(Brn(" cells, peak:"));
        writer.WriteUint(iCellsUsedMax);
        aWriter.Write(Brn(" cells\n"));
    }
}


// Allocated

void Allocated::AddRef()
{
    iRefCount++;
}

void Allocated::RemoveRef()
{
    ASSERT_VA(iRefCount != 0, "Allocated::RemoveRef() for %s - already freed\n", iAllocator.Name());
    TBool free = (--iRefCount == 0);
    if (free) {
        Clear();
        iAllocator.Free(this);
    }
}

void Allocated::Clear()
{
}

Allocated::Allocated(AllocatorBase& aAllocator)
    : iAllocator(aAllocator)
    , iRefCount(0)
{
    ASSERT(iRefCount.is_lock_free());
}

Allocated::~Allocated()
{
}


// AudioData

const TUint AudioData::kMaxBytes;

AudioData::AudioData(AllocatorBase& aAllocator)
    : Allocated(aAllocator)
{
#ifdef TIMESTAMP_LOGGING_ENABLE
    iOsCtx = gEnv->OsCtx();
    iNextTimestampIndex = 0;
    (void)memset(iTimestamps, 0, sizeof iTimestamps);
#endif
}

const TByte* AudioData::Ptr(TUint aBytes) const
{
    ASSERT(aBytes < iData.Bytes());
    return iData.Ptr() + aBytes;
}

TUint AudioData::Bytes() const
{
    return iData.Bytes();
}

TByte* AudioData::PtrW()
{
    return const_cast<TByte*>(iData.Ptr());
}

void AudioData::SetBytes(TUint aBytes)
{
    iData.SetBytes(aBytes);
}

#ifdef TIMESTAMP_LOGGING_ENABLE
void AudioData::SetTimestamp(const TChar* aId)
{
    if (iNextTimestampIndex < kMaxTimestamps-1) {
        const TUint64 ts = OsTimeInUs(iOsCtx);
        iTimestamps[iNextTimestampIndex++].Set(aId, ts);
    }
}

TBool AudioData::TryLogTimestamps()
{
    if (iNextTimestampIndex == 0) {
        return false;
    }
    Log::Print("Timestamps:\n");
    for (TUint i=0; i<iNextTimestampIndex; i++) {
        (void)iTimestamps[i].TryLog();
    }
    return true;
}
#endif // TIMESTAMP_LOGGING_ENABLE

void AudioData::Clear()
{
#ifdef DEFINE_DEBUG
    // fill in all members with recognisable 'bad' values to make ref counting bugs more obvious
    memset(const_cast<TByte*>(iData.Ptr()), 0xde, iData.Bytes());
#endif // DEFINE_DEBUG
    iData.SetBytes(0);
#ifdef TIMESTAMP_LOGGING_ENABLE
    for (TUint i=0; i<iNextTimestampIndex; i++) {
        iTimestamps[i].Reset();
    }
    iNextTimestampIndex = 0;
#endif
}

#ifdef TIMESTAMP_LOGGING_ENABLE
AudioData::Timestamp::Timestamp()
{
    Reset();
}

void AudioData::Timestamp::Reset()
{
    iId = nullptr;
    iTimestamp = 0;
}

void AudioData::Timestamp::Set(const TChar* aId, TUint64 aTimestamp)
{
    iId = aId;
    iTimestamp = aTimestamp;
}

TBool AudioData::Timestamp::TryLog()
{
    if (iId == nullptr) {
        return false;
    }
    Log::Print("\t%s: \t%llu\n", iId, iTimestamp);
    return true;
}
#endif


// EncodedAudio

EncodedAudio::EncodedAudio(AllocatorBase& aAllocator)
    : AudioData(aAllocator)
{
}

TUint EncodedAudio::Append(const Brx& aData)
{
    return DoAppend(aData, iData.MaxBytes());
}

TUint EncodedAudio::Append(const Brx& aData, TUint aMaxBytes)
{
    ASSERT(aMaxBytes <= iData.MaxBytes());
    return DoAppend(aData, aMaxBytes);
}

void EncodedAudio::Construct(const Brx& aData)
{
    ASSERT(Append(aData) == aData.Bytes());
}

TUint EncodedAudio::DoAppend(const Brx& aData, TUint aMaxBytes)
{
    if (iData.Bytes() >= aMaxBytes) {
        return 0;
    }
    const TUint avail = aMaxBytes - iData.Bytes();
    if (avail < aData.Bytes()) {
        Brn data(aData.Ptr(), avail);
        iData.Append(data);
        return avail;
    }
    iData.Append(aData);
    return aData.Bytes();
}


// DecodedAudio

DecodedAudio::DecodedAudio(AllocatorBase& aAllocator)
    : AudioData(aAllocator)
{
}

void DecodedAudio::Aggregate(DecodedAudio& aDecodedAudio)
{
    iData.Append(aDecodedAudio.iData);
}

void DecodedAudio::SetBytes(TUint aBytes)
{
    iData.SetBytes(aBytes);
}

void DecodedAudio::ConstructPcm(const Brx& aData, TUint aBitDepth, AudioDataEndian aEndian)
{
    ASSERT((aBitDepth & 7) == 0);
    ASSERT(aData.Bytes() % (aBitDepth/8) == 0);
    TByte* ptr = const_cast<TByte*>(iData.Ptr());
    if (aEndian == AudioDataEndian::Big || aBitDepth == 8) {
        (void)memcpy(ptr, aData.Ptr(), aData.Bytes());
    }
    else if (aBitDepth == 16) {
        CopyToBigEndian16(aData, ptr);
    }
    else if (aBitDepth == 24) {
        CopyToBigEndian24(aData, ptr);
    }
    else if (aBitDepth == 32) {
        CopyToBigEndian32(aData, ptr);
    }
    else { // unsupported bit depth
        ASSERTS();
    }
    iData.SetBytes(aData.Bytes());
}

void DecodedAudio::ConstructDsd(const Brx& aData)
{
    iData.Replace(aData);
}

void DecodedAudio::Construct()
{
    iData.Replace(Brx::Empty());
}

void DecodedAudio::CopyToBigEndian16(const Brx& aData, TByte* aDest)
{ // static
    const TByte* src = aData.Ptr();
    for (TUint i=0; i<aData.Bytes(); i+=2) {
        *aDest++ = src[i+1];
        *aDest++ = src[i];
    }
}

void DecodedAudio::CopyToBigEndian24(const Brx& aData, TByte* aDest)
{ // static
    const TByte* src = aData.Ptr();
    for (TUint i=0; i<aData.Bytes(); i+=3) {
        *aDest++ = src[i+2];
        *aDest++ = src[i+1];
        *aDest++ = src[i];
    }
}

void DecodedAudio::CopyToBigEndian32(const Brx& aData, TByte* aDest)
{ // static
    const TByte* src = aData.Ptr();
    for (TUint i=0; i<aData.Bytes(); i+=4) {
        *aDest++ = src[i+3];
        *aDest++ = src[i+2];
        *aDest++ = src[i+1];
        *aDest++ = src[i];
    }
}


// Jiffies

TBool Jiffies::IsValidSampleRate(TUint aSampleRate)
{ // static
    try {
        PerSample(aSampleRate); // only want to check if sample rate is supported
    }
    catch (SampleRateInvalid&) {
        return false;
    }
    return true;
}

TUint Jiffies::PerSample(TUint aSampleRate)
{ // static
    switch (aSampleRate)
    {
    case 7350:
        return kJiffies7350;
    case 8000:
        return kJiffies8000;
    case 11025:
        return kJiffies11025;
    case 12000:
        return kJiffies12000;
    case 14700:
        return kJiffies14700;
    case 16000:
        return kJiffies16000;
    case 22050:
        return kJiffies22050;
    case 24000:
        return kJiffies24000;
    case 29400:
        return kJiffies29400;
    case 32000:
        return kJiffies32000;
    case 44100:
        return kJiffies44100;
    case 48000:
        return kJiffies48000;
    case 88200:
        return kJiffies88200;
    case 96000:
        return kJiffies96000;
    case 176400:
        return kJiffies176400;
    case 192000:
        return kJiffies192000;
    case 352800:
        return kJiffies352800;
    case 384000:
        return kJiffies384000;
    case 2822400:
        return kJiffies2822400;
    case 5644800:
        return kJiffies5644800;
    case 11289600:
        return kJiffies11289600;
    default:
        LOG_ERROR(kApplication6, "JiffiesPerSample - invalid sample rate: %u\n", aSampleRate);
        THROW(SampleRateInvalid);
    }
}

TUint Jiffies::ToBytes(TUint& aJiffies, TUint aJiffiesPerSample, TUint aNumChannels, TUint aBitsPerSubsample)
{ // static
    return ToBytesSampleBlock(aJiffies, aJiffiesPerSample, aNumChannels, aBitsPerSubsample, 1);
}

TUint Jiffies::ToBytesSampleBlock(TUint& aJiffies, TUint aJiffiesPerSample, TUint aNumChannels, TUint aBitsPerSubsample, TUint aSamplesPerBlock)
{
    ASSERT(aSamplesPerBlock != 0);
    aJiffies -= aJiffies % (aJiffiesPerSample * aSamplesPerBlock);
    const TUint numSamples = aJiffies / aJiffiesPerSample;
    const TUint numSubsamples = numSamples * aNumChannels;
    const TUint bytes = ((numSubsamples * aBitsPerSubsample) + 7) / 8;
    return bytes;
}

void Jiffies::RoundDown(TUint& aJiffies, TUint aSampleRate)
{
    const TUint jiffiesPerSample = PerSample(aSampleRate);
    aJiffies -= aJiffies % jiffiesPerSample;
}

void Jiffies::RoundUp(TUint& aJiffies, TUint aSampleRate)
{
    const TUint jiffiesPerSample = PerSample(aSampleRate);
    aJiffies += jiffiesPerSample - 1;
    aJiffies -= aJiffies % jiffiesPerSample;
}

void Jiffies::RoundDownNonZeroSampleBlock(TUint& aJiffies, TUint aSampleBlockJiffies)
{
    TUint jiffies = aJiffies;
    jiffies -= jiffies % aSampleBlockJiffies;
    if (jiffies == 0) {
        jiffies = aJiffies;
        jiffies += aSampleBlockJiffies - 1;
        jiffies -= jiffies % aSampleBlockJiffies;
    }
    aJiffies = jiffies;
}

TUint Jiffies::ToSongcastTime(TUint aJiffies, TUint aSampleRate)
{ // static
    return static_cast<TUint>((static_cast<TUint64>(aJiffies) * SongcastTicksPerSecond(aSampleRate)) / kPerSecond);
}

TUint64 Jiffies::FromSongcastTime(TUint64 aSongcastTime, TUint aSampleRate)
{ // static
    return (aSongcastTime * kPerSecond) / SongcastTicksPerSecond(aSampleRate);
}

TUint Jiffies::SongcastTicksPerSecond(TUint aSampleRate)
{ // static
    switch (aSampleRate)
    {
    case 7350:
    case 11025:
    case 14700:
    case 22050:
    case 29400:
    case 44100:
    case 88200:
    case 176400:
    case 352800:
        return kSongcastTicksPerSec44k;

    case 8000:
    case 12000:
    case 16000:
    case 24000:
    case 32000:
    case 48000:
    case 96000:
    case 192000:
    case 384000:
        return kSongcastTicksPerSec48k;

    default:
        THROW(SampleRateInvalid);
    }
}


// Msg

Msg::Msg(AllocatorBase& aAllocator)
    : Allocated(aAllocator)
    , iNextMsg(nullptr)
{
}


// Ramp

Ramp::Ramp()
{
    Reset();

    // confirm some assumptions made elsewhere in this class
    // these could be compile-time or one-off run-time assertions
#ifdef _WIN32
# pragma warning (disable: 4127)
#endif // _WIN32
    ASSERT_DEBUG(kMax <= 1<<30); // some 32-bit values in ramp calculations should become 64-bit if this increases
    ASSERT_DEBUG(kRampArrayCount == 512); // <<22 and >>23 code needs review if this changes
}

void Ramp::Reset()
{
    iStart = kMax;
    iEnd = kMax;
    iDirection = ENone;
    iEnabled = false;
}

TBool Ramp::Set(TUint aStart, TUint aFragmentSize, TUint aRemainingDuration, EDirection aDirection, Ramp& aSplit, TUint& aSplitPos)
{
    /*Bws<256> buf("Ramp::Set (");
    buf.Append(Thread::CurrentThreadName());
    buf.AppendPrintf("), aDirection=%d, aStart=%08x, aFragmentSize=%08x, aRemainingDuration=%08x, current=[%08x..%08x]\n",
                     aDirection, aStart, aFragmentSize, aRemainingDuration, iStart, iEnd);
    Log::Print(buf);*/
    Ramp before(*this);
    ASSERT(aRemainingDuration >=  aFragmentSize);
    ASSERT(aDirection != ENone);
    iEnabled = true;
    aSplit.Reset();
    aSplitPos = 0xffffffff;
    const TUint rampRemaining = (aDirection == EDown? aStart : kMax-aStart);
    // Always round up rampDelta values to avoid rounding errors leading to a ramp failing to complete in its duration
    TUint rampDelta = ((rampRemaining * (TUint64)aFragmentSize) + aRemainingDuration - 1) / aRemainingDuration;
    // Rounding up rampDelta means that a ramp may overshoot.
    // ...check for this and clamp end values to min/max dependent on direction
    TUint rampEnd;
    if (aDirection == EDown) {
        if (rampDelta > aStart) {
            ASSERT(rampDelta - aStart <= aFragmentSize - 1); // anything larger must be the result of programming rather than rounding error
            rampEnd = 0;
        }
        else {
            rampEnd = (TUint)(aStart - rampDelta);
        }
    }
    else { // aDirection == EUp
        if (aStart + rampDelta > kMax) {
            ASSERT(aStart + rampDelta - kMax <= aFragmentSize - 1); // anything larger must be the result of programming rather than rounding error
            rampEnd = kMax;
        }
        else {
            rampEnd = aStart + rampDelta;
        }
    }
    if (iDirection == ENone) {
        // No previous ramp.  Trivial to apply the suggested values
        iDirection = aDirection;
        iStart = aStart;
        iEnd = rampEnd;
    }
    else if (iDirection == aDirection) {
        // New ramp runs in the same direction as existing one.  Choose the lower start/end points
        SelectLowerRampPoints(aStart, rampEnd);
    }
    else {
        // Previous and new ramps run in opposite directions.
        // Calculate the point at which they intersect.
        // ... if they don't intersect, use the new values
        // ...if they do intersect, split the message, selecting ramp with lower start for first and lower end for second
        /*
        We start with two lines defined by the points
            (0, y1), (aFragmentSize, y2)
            (0, y3), (aFragmentSize, y4)
        ...we can describe these using the equations y=mx+b & y=nx+c
        ...where y=mx+b has the lower initial y pos (start ramp)
        ...we calculate m, n as follows
            m=(y2-y1)/aFragmentSize
            n=(y4-y3)/aFragmentSize
        ... then x, y as follows
            x=(y3-y1)/(m-n)
            y=((iEnd-iStart)/aFragmentSize) * x + iStart
        If x is outside 0..aFragmentSize then the ramps don't intersect during this message
        ...in this case we fall back to choosing the lower values for start/end

        To avoid rounding errors ('slopes' m,n may be fractional) we change the calculations to
            x=(aFragmentSize*(y3-y1))/((y2-y1)-(y4-y3))
            y=((y2-y1)*(y3-y1))/((y2-y1)-(y4-y3)) + y1
        */
        TInt64 y1, y2, y3, y4;
        if (iStart < aStart) {
            y1 = iStart;
            y2 = iEnd;
            y3 = aStart;
            y4 = rampEnd;
        }
        else {
            y1 = aStart;
            y2 = rampEnd;
            y3 = iStart;
            y4 = iEnd;
        }
        if ((y2-y1) == (y4-y3)) {
            // ramp lines are parallel so won't ever intersect
            SelectLowerRampPoints(aStart, rampEnd);
        }
        else {
            TInt64 intersectX = (aFragmentSize*(y3-y1))/((y2-y1)-(y4-y3));
            TInt64 intersectY = (((y2-y1)*(y3-y1))/((y2-y1)-(y4-y3))) + y1;
            // calculation of intersectY may overflow a TUint.
            // intersectX will tell us we have no useful intersection in these cases and we'll ignore the Y value.
            if (intersectX <= 0 || (TUint)intersectX >= aFragmentSize) {
                // ramp lines don't intersect inside this Msg
                SelectLowerRampPoints(aStart, rampEnd);
            }
            else {
                // split this Ramp; the first portion rises to the intersection of the two ramps, the second drops to the lower final value
                aSplitPos = (TUint)intersectX;
                aSplit.iStart = (TUint)intersectY;
                aSplit.iEnd = std::min(iEnd, rampEnd);
                aSplit.iDirection = (aSplit.iStart == aSplit.iEnd? ENone : EDown);
                aSplit.iEnabled = true;
                const TUint start = std::min(iStart, aStart);
                const TUint end = (TUint)intersectY;
                iDirection = (start == end? ENone : EUp);
                /*Log::Print("Split [%08x : %08x] ramp into [%08x : %08x] and [%08x : %08x]\n",
                           iStart, iEnd, start, end, aSplit.iStart, aSplit.iEnd);*/
                iStart = start;
                iEnd = end;
            }
        }
    }
    if (!DoValidate()) {
        Log::Print("Ramp::Set(%04x, %u, %u, %u) created invalid ramp.\n", aStart, aFragmentSize, aRemainingDuration, aDirection);
        Log::Print("  before: [%04x..%04x], direction=%u\n", before.iStart, before.iEnd, before.iDirection);
        Log::Print("  after:  [%04x..%04x], direction=%u\n", iStart, iEnd, iDirection);
        Log::Print("  split:  [%04x..%04x], direction=%u\n", aSplit.iStart, aSplit.iEnd, aSplit.iDirection);
        ASSERTS();
    }
    return aSplit.IsEnabled();
}

void Ramp::SetMuted()
{
    iStart = iEnd = kMin;
    iDirection = EMute;
    iEnabled = true;
}

void Ramp::SelectLowerRampPoints(TUint aRequestedStart, TUint aRequestedEnd)
{
    iStart = std::min(iStart, aRequestedStart);
    iEnd = std::min(iEnd, aRequestedEnd);
    if (iStart == iEnd) {
        iDirection = ENone;
    }
    else if (iStart > iEnd) {
        iDirection = EDown;
    }
    else { // iStart < iEnd
        iDirection = EUp;
    }
}

void Ramp::Validate(const TChar* aId)
{
    if (!DoValidate()) {
        Log::Print("Ramp::Validate failure %s)\n", aId);
        Log::Print("  ramp: [%04x..%04x], direction=%u\n", iStart, iEnd, iDirection);
        ASSERTS();
    }
}

TBool Ramp::DoValidate()
{
    if (iStart > kMax) {
        return false;
    }
    if (iEnd > kMax) {
        return false;
    }
    switch (iDirection)
    {
    case ENone:
        if (iStart != iEnd) {
            return false;
        }
        break;
    case EUp:
        if (iStart >= iEnd) {
            return false;
        }
        break;
    case EDown:
        if (iStart <= iEnd) {
            return false;
        }
        break;
    case EMute:
        if (iStart != iEnd) {
            return false;
        }
        if (iStart != kMin) {
            return false;
        }
        break;
    default:
        ASSERTS();
    }
    return true;
}

Ramp Ramp::Split(TUint aNewSize, TUint aCurrentSize)
{
    //TUint start = iStart, end = iEnd;
    Ramp remaining;
    remaining.iEnd = iEnd;
    remaining.iDirection = iDirection;
    remaining.iEnabled = true;
    if (iDirection == EUp) {
        TUint ramp = ((iEnd-iStart) * (TUint64)aNewSize) / aCurrentSize;
        iEnd = iStart + ramp;
    }
    else {
        TUint ramp = ((iStart-iEnd) * (TUint64)aNewSize) / aCurrentSize;
        iEnd = iStart - ramp;
    }
    if (iStart == iEnd) {
        iDirection = ENone;
    }
    remaining.iStart = iEnd; // FIXME - remaining.iStart is one sample on from iEnd so should have a ramp value that progresses one 'step'
    //Log::Print("Split [%04x : %04x] ramp into [%04x : %04x] and [%04x : %04x]\n", start, end, iStart, iEnd, remaining.iStart, remaining.iEnd);
    Validate("Split");
    remaining.Validate("Split - remaining");
    return remaining;
}


// RampApplicator

const TUint RampApplicator::kFullRampSpan = Ramp::kMax - Ramp::kMin;

RampApplicator::RampApplicator(const Media::Ramp& aRamp)
    : iRamp(aRamp)
    , iPtr(nullptr)
{
}

TUint RampApplicator::Start(const Brx& aData, TUint aBitDepth, TUint aNumChannels)
{
    iPtr = aData.Ptr();
    iBitDepth = aBitDepth;
    iNumChannels = aNumChannels;
    ASSERT_DEBUG(aData.Bytes() % ((iBitDepth/8) * iNumChannels) == 0);
    iNumSamples = (TInt)(aData.Bytes() / ((iBitDepth/8) * iNumChannels));
    iTotalRamp = (iRamp.Start() - iRamp.End());
    iLoopCount = 0;
    return iNumSamples;
}

void RampApplicator::GetNextSample(TByte* aDest)
{
    ASSERT_DEBUG(iPtr != nullptr);
    const TUint16 ramp = (iNumSamples==1? (TUint16)iRamp.Start() : (TUint16)(iRamp.Start() - ((iLoopCount * iTotalRamp)/(iNumSamples-1))));
    //Log::Print(" %04x ", ramp);
    const TUint rampIndex = std::min(kRampArrayCount-1, (kFullRampSpan - ramp + (1<<4)) >> 5); // assumes fullRampSpan==2^14 and kRampArray has 512 (2^9) items. (1<<4 allows rounding up)
    for (TUint i=0; i<iNumChannels; i++) {
        TInt16 subsample16 = 0;
        switch (iBitDepth)
        {
        case 8:
            subsample16 = *iPtr++ << 8;
            break;
        case 16:
            subsample16 = *iPtr++ << 8;
            subsample16 += *iPtr++;
            break;
        case 24:
            subsample16 = *iPtr++ << 8;
            subsample16 += *iPtr++;
            iPtr++;
            break;
        case 32:
            subsample16 = *iPtr++ << 8;
            subsample16 += *iPtr++;
            iPtr++;
            iPtr++;
            break;
        default:
            ASSERTS();
        }
        //Log::Print(" %03u ", rampIndex);
        const TUint16 rampMult = static_cast<TUint16>(kRampArray[rampIndex]);
        TInt rampedSubsample = (rampIndex==512? 0 : (subsample16 * rampMult) >> 15);
        //Log::Print("Original=%04x (%d), ramped=%04x (%d), rampIndex=%u\n", subsample, subsample, rampedSubsample, rampedSubsample, rampIndex);

        switch (iBitDepth)
        {
        case 8:
            *aDest++ = (TByte)(rampedSubsample >> 8);
            break;
        case 16:
            *aDest++ = (TByte)(rampedSubsample >> 8);
            *aDest++ = (TByte)rampedSubsample;
            break;
        case 24:
            *aDest++ = (TByte)(rampedSubsample >> 8);
            *aDest++ = (TByte)rampedSubsample;
            *aDest++ = (TByte)0;
            break;
        case 32:
            *aDest++ = (TByte)(rampedSubsample >> 8);
            *aDest++ = (TByte)rampedSubsample;
            *aDest++ = (TByte)0;
            if(iNumChannels == 6) {
                *aDest++ = (TByte)i<<4; //set channel id for efficiency on 6channel 192k
            }
            else {
                *aDest++ = (TByte)0;
            }
            break;
        default:
            ASSERTS();
        }
    }

    iLoopCount++;
}

TUint RampApplicator::MedianMultiplier(const Media::Ramp& aRamp)
{ // static
    TUint medRamp;
    switch (aRamp.Direction())
    {
    case Ramp::EUp:
        medRamp = aRamp.Start() + ((aRamp.End() - aRamp.Start()) / 2);
        break;
    case Ramp::EDown:
        medRamp = aRamp.Start() - ((aRamp.Start() - aRamp.End()) / 2);
        break;
    case Ramp::EMute:
        return 0;
    default:
        medRamp = aRamp.Start();
        break;
    }
    const TUint rampIndex = (Ramp::kMax - Ramp::kMin - medRamp + (1<<4)) >> 5; // assumes (Ramp::kMax - Ramp::kMin)==2^14 and kRampArray has 512 (2^9) items. (1<<4 allows rounding up)
    return kRampArray[rampIndex];
}


// Track

Track::Track(AllocatorBase& aAllocator)
    : Allocated(aAllocator)
{
    Clear();
}

const Brx& Track::Uri() const
{
    return iUri;
}

const Brx& Track::MetaData() const
{
    return iMetaData;
}

TUint Track::Id() const
{
    return iId;
}

void Track::Initialise(const Brx& aUri, const Brx& aMetaData, TUint aId)
{
    iUri.ReplaceThrow(aUri);
    if (aMetaData.Bytes() > iMetaData.MaxBytes()) {
        iMetaData.Replace(aMetaData.Split(0, iMetaData.MaxBytes()));
    }
    else {
        iMetaData.Replace(aMetaData);
    }
    iId = aId;
}

void Track::Clear()
{
#ifdef DEFINE_DEBUG
    iUri.SetBytes(0);
    iMetaData.SetBytes(0);
    iId = UINT_MAX;
#endif // DEFINE_DEBUG
}


// ModeInfo

void ModeInfo::Clear()
{
    iLatencyMode         = Latency::NotSupported;
    iSupportsPause       = false;
    iSupportsNext        = false;
    iSupportsPrev        = false;
    iSupportsRepeat      = false;
    iSupportsRandom      = false;
    iRampPauseResumeLong = true;
    iRampSkipLong        = false;
}


// ModeTransportControls

ModeTransportControls::ModeTransportControls()
{
}

void ModeTransportControls::Clear()
{
    iPlay  = Functor();
    iPause = Functor();
    iStop  = Functor();
    iNext  = Functor();
    iPrev  = Functor();
    iSeek  = FunctorGeneric<TUint>();
}


// MsgMode

MsgMode::MsgMode(AllocatorBase& aAllocator)
    : Msg(aAllocator)
{
}

const Brx& MsgMode::Mode() const
{
    return iMode;
}

const ModeInfo& MsgMode::Info() const
{
    return iInfo;
}

Optional<IClockPuller> MsgMode::ClockPuller() const
{
    return iClockPuller;
}

const ModeTransportControls& MsgMode::TransportControls() const
{
    return iTransportControls;
}

void MsgMode::Initialise(const Brx& aMode, const ModeInfo& aInfo,
                         Optional<IClockPuller> aClockPuller,
                         const ModeTransportControls& aTransportControls)
{
    iMode.Replace(aMode);
    iInfo = aInfo;
    iClockPuller = aClockPuller;
    iTransportControls = aTransportControls;
}

void MsgMode::Clear()
{
    iMode.Replace(Brx::Empty());
    iInfo.Clear();
    iClockPuller = nullptr;
    iTransportControls.Clear();
}

Msg* MsgMode::Process(IMsgProcessor& aProcessor)
{
    return aProcessor.ProcessMsg(this);
}


// MsgDrain

MsgDrain::MsgDrain(AllocatorBase& aAllocator)
    : Msg(aAllocator)
{
}

void MsgDrain::ReportDrained()
{
    if (iCallback) {
        iCallback();
        iCallbackPending = false;
    }
}

TUint MsgDrain::Id() const
{
    return iId;
}

void MsgDrain::Initialise(TUint aId, Functor aCallback)
{
    iId = aId;
    iCallback = aCallback;
    iCallbackPending = iCallback? true : false;
}

void MsgDrain::Clear()
{
    ASSERT(!iCallbackPending);
    iCallback = Functor();
}

Msg* MsgDrain::Process(IMsgProcessor& aProcessor)
{
    return aProcessor.ProcessMsg(this);
}


// MsgTrack

MsgTrack::MsgTrack(AllocatorBase& aAllocator)
    : Msg(aAllocator)
{
}

Media::Track& MsgTrack::Track() const
{
    ASSERT(iTrack != nullptr);
    return *iTrack;
}

TBool MsgTrack::StartOfStream() const
{
    return iStartOfStream;
}

void MsgTrack::Initialise(Media::Track& aTrack, TBool aStartOfStream)
{
    iTrack = &aTrack;
    iTrack->AddRef();
    iStartOfStream = aStartOfStream;
}

void MsgTrack::Clear()
{
    iTrack->RemoveRef();
    iTrack = nullptr;
    iStartOfStream = false;
}

Msg* MsgTrack::Process(IMsgProcessor& aProcessor)
{
    return aProcessor.ProcessMsg(this);
}


// MsgDelay

MsgDelay::MsgDelay(AllocatorBase& aAllocator)
    : Msg(aAllocator)
{
}

TUint MsgDelay::RemainingJiffies() const
{
    return iRemainingJiffies;
}

TUint MsgDelay::TotalJiffies() const
{
    return iTotalJiffies;
}

void MsgDelay::Initialise(TUint aTotalJiffies)
{
    Initialise(aTotalJiffies, aTotalJiffies);
}

void MsgDelay::Initialise(TUint aRemainingJiffies, TUint aTotalJiffies)
{
    iRemainingJiffies = aRemainingJiffies;
    iTotalJiffies = aTotalJiffies;
}

void MsgDelay::Clear()
{
    iRemainingJiffies = iTotalJiffies  = UINT_MAX;
}

Msg* MsgDelay::Process(IMsgProcessor& aProcessor)
{
    return aProcessor.ProcessMsg(this);
}

// SpeakerProfile
SpeakerProfile::SpeakerProfile(TUint aNumFronts)
    : SpeakerProfile(aNumFronts, 0, 0)
{
}

SpeakerProfile::SpeakerProfile(TUint aNumFronts, TUint aNumSurrounds, TUint aNumSubs)
    : iNumFronts(aNumFronts)
    , iNumSurrounds(aNumSurrounds)
    , iNumSubs(aNumSubs)
{
    ASSERT(iNumFronts <= 3);
    ASSERT(iNumSurrounds <= 4);
    ASSERT(iNumSubs <= 2);

    // Create the string representation
    iName.AppendPrintf("%u/%u.%u", iNumFronts, iNumSurrounds, iNumSubs);
}

TUint SpeakerProfile::NumFronts() const         { return iNumFronts; }
TUint SpeakerProfile::NumSurrounds() const      { return iNumSurrounds; }
TUint SpeakerProfile::NumSubs() const           { return iNumSubs; }
const TChar* SpeakerProfile::ToString() const   { return iName.PtrZ(); }

TBool SpeakerProfile::operator==(const SpeakerProfile& aOther) const
{
    return (iNumFronts == aOther.iNumFronts) &&
           (iNumSurrounds == aOther.iNumSurrounds) &&
           (iNumSubs == aOther.iNumSubs);
}

TBool SpeakerProfile::operator!=(const SpeakerProfile& aOther) const
{
    return !(*this == aOther);
}

const SpeakerProfile& SpeakerProfile::operator=(const SpeakerProfile& aOther)
{
    iNumFronts = aOther.iNumFronts;
    iNumSurrounds = aOther.iNumSurrounds;
    iNumSubs = aOther.iNumSubs;
    iName.Replace(aOther.iName);
    return *this;
}

// PcmStreamInfo

PcmStreamInfo::PcmStreamInfo()
{
    Clear();
}

void PcmStreamInfo::Set(TUint aBitDepth, TUint aSampleRate, TUint aNumChannels, AudioDataEndian aEndian, const SpeakerProfile& aProfile, TUint64 aStartSample)
{
    iBitDepth = aBitDepth;
    iSampleRate = aSampleRate;
    iNumChannels = aNumChannels;
    iEndian = aEndian;
    iProfile = aProfile;
    iStartSample = aStartSample;
    iCodecName.Replace(Brn("PCM"));
    iLossless = true;
}

void PcmStreamInfo::SetAnalogBypass()
{
    iAnalogBypass = true;
}

void PcmStreamInfo::SetCodec(const Brx& aCodecName, TBool aLossless)
{
    iCodecName.Replace(aCodecName);
    iLossless = aLossless;
}

void PcmStreamInfo::Clear()
{
    iBitDepth = iSampleRate = iNumChannels = 0;
    iEndian = AudioDataEndian::Invalid;
    iAnalogBypass = false;
    iCodecName.Replace(Brx::Empty());
    iLossless = true;
}

TUint PcmStreamInfo::BitDepth() const
{
    return iBitDepth;
}

TUint PcmStreamInfo::SampleRate() const
{
    return iSampleRate;
}

TUint PcmStreamInfo::NumChannels() const
{
    return iNumChannels;
}

AudioDataEndian PcmStreamInfo::Endian() const
{
    return iEndian;
}

const SpeakerProfile& PcmStreamInfo::Profile() const
{
    return iProfile;
}

TUint64 PcmStreamInfo::StartSample() const
{
    return iStartSample;
}

TBool PcmStreamInfo::AnalogBypass() const
{
    return iAnalogBypass;
}

const Brx& PcmStreamInfo::CodecName() const
{
    return iCodecName;
}

TBool PcmStreamInfo::Lossless() const
{
    return iLossless;
}

void PcmStreamInfo::operator=(const PcmStreamInfo& aPcmStream)
{
    iBitDepth = aPcmStream.BitDepth();
    iSampleRate = aPcmStream.SampleRate();
    iNumChannels = aPcmStream.NumChannels();
    iEndian = aPcmStream.Endian();
    iProfile = aPcmStream.Profile();
    iStartSample = aPcmStream.StartSample();
    iAnalogBypass = aPcmStream.AnalogBypass();
    iCodecName.Replace(aPcmStream.CodecName());
    iLossless = aPcmStream.Lossless();
}

PcmStreamInfo::operator TBool() const
{
    return iSampleRate != 0;
}


// DsdStreamInfo

DsdStreamInfo::DsdStreamInfo()
{
    Clear();
}

void DsdStreamInfo::Set(TUint aSampleRate, TUint aNumChannels, TUint aSampleBlockWords, TUint64 aStartSample)
{
    iSampleRate = aSampleRate;
    iNumChannels = aNumChannels;
    iSampleBlockWords = aSampleBlockWords;
    iStartSample = aStartSample;
}

void DsdStreamInfo::SetCodec(const Brx& aCodecName)
{
    iCodecName.Replace(aCodecName);
}

void DsdStreamInfo::Clear()
{
    iSampleRate = 0;
    iNumChannels = 0;
    iSampleBlockWords = 0;
    iStartSample = 0LL;
    iCodecName.Replace(Brx::Empty());
}

TUint DsdStreamInfo::SampleRate() const
{
    return iSampleRate;
}

TUint DsdStreamInfo::SampleBlockWords() const
{
    return iSampleBlockWords;
}

TUint DsdStreamInfo::NumChannels() const
{
    return iNumChannels;
}

TUint64 DsdStreamInfo::StartSample() const
{
    return iStartSample;
}

const Brx& DsdStreamInfo::CodecName() const
{
    return iCodecName;
}

void DsdStreamInfo::operator=(const DsdStreamInfo &aInfo)
{
    iSampleRate      = aInfo.SampleRate();
    iNumChannels     = aInfo.NumChannels();
    iSampleBlockWords = aInfo.SampleBlockWords();
    iStartSample     = aInfo.StartSample();
    iCodecName.Replace(aInfo.CodecName());
}

DsdStreamInfo::operator TBool() const
{
    return iSampleRate != 0;
}


// MsgEncodedStream

const TUint MsgEncodedStream::kMaxUriBytes;
const RampType MsgEncodedStream::kRampDefault;
const RampType MsgEncodedStream::kRampDsd;

MsgEncodedStream::MsgEncodedStream(AllocatorBase& aAllocator)
    : Msg(aAllocator)
{
}

const Brx& MsgEncodedStream::Uri() const
{
    return iUri;
}

const Brx& MsgEncodedStream::MetaText() const
{
    return iMetaText;
}

TUint64 MsgEncodedStream::TotalBytes() const
{
    return iTotalBytes;
}

TUint64 MsgEncodedStream::StartPos() const
{
    return iStartPos;
}

TUint MsgEncodedStream::StreamId() const
{
    return iStreamId;
}

TBool MsgEncodedStream::Seekable() const
{
    return iSeekCapability == Media::SeekCapability::SeekCache
        || iSeekCapability == Media::SeekCapability::SeekSource;
}

SeekCapability MsgEncodedStream::SeekCapability() const
{
    return iSeekCapability;
}

TBool MsgEncodedStream::Live() const
{
    return iLive;
}

Multiroom MsgEncodedStream::Multiroom() const
{
    return iMultiroom;
}

IStreamHandler* MsgEncodedStream::StreamHandler() const
{
    return iStreamHandler;
}

MsgEncodedStream::Format MsgEncodedStream::StreamFormat() const
{
    return iStreamFormat;
}

const PcmStreamInfo& MsgEncodedStream::PcmStream() const
{
    ASSERT(iStreamFormat == Format::Pcm);
    return iPcmStreamInfo;
}

const DsdStreamInfo& MsgEncodedStream::DsdStream() const
{
    ASSERT(iStreamFormat == Format::Dsd);
    return iDsdStreamInfo;
}

RampType MsgEncodedStream::Ramp() const
{
    return iRamp;
}

TUint MsgEncodedStream::SeekPosMs() const
{
    return iSeekPos;
}

void MsgEncodedStream::Initialise(const Brx& aUri, const Brx& aMetaText, TUint64 aTotalBytes, TUint64 aStartPos, TUint aStreamId, Media::SeekCapability aSeek, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler* aStreamHandler, TUint aSeekPosMs)
{
    iUri.Replace(aUri);
    iMetaText.Replace(aMetaText);
    iTotalBytes = aTotalBytes;
    iStartPos= aStartPos;
    iStreamId = aStreamId;
    iSeekCapability = aSeek;
    iLive = aLive;
    iMultiroom = aMultiroom;
    iStreamHandler = aStreamHandler;
    iStreamFormat = Format::Encoded;
    iPcmStreamInfo.Clear();
    iDsdStreamInfo.Clear();
    iRamp = kRampDefault;
    iSeekPos = aSeekPosMs;
}

void MsgEncodedStream::Initialise(const Brx& aUri, const Brx& aMetaText, TUint64 aTotalBytes, TUint64 aStartPos, TUint aStreamId, Media::SeekCapability aSeek, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler* aStreamHandler, const PcmStreamInfo& aPcmStream, RampType aRamp)
{
    iUri.Replace(aUri);
    iMetaText.Replace(aMetaText);
    iTotalBytes = aTotalBytes;
    iStartPos= aStartPos;
    iStreamId = aStreamId;
    iSeekCapability = aSeek;
    iLive = aLive;
    iMultiroom = aMultiroom;
    iStreamHandler = aStreamHandler;
    iStreamFormat = Format::Pcm;
    iPcmStreamInfo = aPcmStream;
    iDsdStreamInfo.Clear();
    iRamp = aRamp;
    iSeekPos = 0;
}

void MsgEncodedStream::Initialise(const Brx& aUri, const Brx& aMetaText, TUint64 aTotalBytes,
                                  TUint64 aStartPos, TUint aStreamId, Media::SeekCapability aSeek, TBool aLive,
                                  Media::Multiroom aMultiroom, IStreamHandler* aStreamHandler,
                                  const DsdStreamInfo& aDsdStream)
{
    iUri.Replace(aUri);
    iMetaText.Replace(aMetaText);
    iTotalBytes = aTotalBytes;
    iStartPos = aStartPos;
    iStreamId = aStreamId;
    iSeekCapability = aSeek;
    iLive = aLive;
    iMultiroom = aMultiroom;
    iStreamHandler = aStreamHandler;
    iStreamFormat = Format::Dsd;
    iPcmStreamInfo.Clear();
    iDsdStreamInfo = aDsdStream;
    iRamp = kRampDsd;
    iSeekPos = 0;
}

void MsgEncodedStream::Clear()
{
#ifdef DEFINE_DEBUG
    iUri.SetBytes(0);
    iMetaText.SetBytes(0);
    iTotalBytes = UINT_MAX;
    iStreamId = UINT_MAX;
    iSeekCapability = Media::SeekCapability::None;
    iLive = false;
    iStreamFormat = Format::Encoded;
    iStreamHandler = nullptr;
    iPcmStreamInfo.Clear();
    iRamp = kRampDefault;
    iSeekPos = 0;
#endif
}

Msg* MsgEncodedStream::Process(IMsgProcessor& aProcessor)
{
    return aProcessor.ProcessMsg(this);
}


// MsgStreamSegment

const TUint MsgStreamSegment::kMaxIdBytes;

MsgStreamSegment::MsgStreamSegment(AllocatorBase& aAllocator)
    : Msg(aAllocator)
{
}

const Brx& MsgStreamSegment::Id() const
{
    return iId;
}

void MsgStreamSegment::Initialise(const Brx& aId)
{
    iId.Replace(aId);
}

void MsgStreamSegment::Clear()
{
#ifdef DEFINE_DEBUG
    iId.SetBytes(0);
#endif // DEFINE_DEBUG
}

Msg* MsgStreamSegment::Process(IMsgProcessor& aProcessor)
{
    return aProcessor.ProcessMsg(this);
}


// MsgAudioEncoded

MsgAudioEncoded::MsgAudioEncoded(AllocatorBase& aAllocator)
    : Msg(aAllocator)
{
}

MsgAudioEncoded* MsgAudioEncoded::Split(TUint aBytes)
{
    if (aBytes > iSize) {
        ASSERT(iNextAudio != nullptr);
        return iNextAudio->Split(aBytes - iSize);
    }
    if (aBytes == iSize) {
        ASSERT(iNextAudio != nullptr);
        MsgAudioEncoded* next = iNextAudio;
        iNextAudio = nullptr;
        return next;
    }
    ASSERT(aBytes > 0);
    ASSERT(aBytes < iSize);
    MsgAudioEncoded* remaining = static_cast<Allocator<MsgAudioEncoded>&>(iAllocator).Allocate();
    remaining->iNextAudio = iNextAudio;
    remaining->iOffset = iOffset + aBytes;
    remaining->iSize = iSize - aBytes;
    remaining->iAudioData = iAudioData;
    remaining->iAudioData->AddRef();
    iSize = aBytes;
    iNextAudio = nullptr;
    return remaining;
}

void MsgAudioEncoded::Add(MsgAudioEncoded* aMsg)
{
    MsgAudioEncoded* end = this;
    MsgAudioEncoded* next = iNextAudio;
    while (next != nullptr) {
        end = next;
        next = next->iNextAudio;
    }
    end->iNextAudio = aMsg;
}

TUint MsgAudioEncoded::Append(const Brx& aData)
{
    ASSERT(iNextAudio == nullptr);
    const TUint consumed = iAudioData->Append(aData);
    iSize += consumed;
    return consumed;
}

TUint MsgAudioEncoded::Append(const Brx& aData, TUint aMaxBytes)
{
    ASSERT(iNextAudio == nullptr);
    const TUint consumed = iAudioData->Append(aData, aMaxBytes);
    iSize += consumed;
    return consumed;
}

TUint MsgAudioEncoded::Bytes() const
{
    TUint bytes = iSize;
    MsgAudioEncoded* next = iNextAudio;
    while (next != nullptr) {
        bytes += next->iSize;
        next = next->iNextAudio;
    }
    return bytes;
}

void MsgAudioEncoded::CopyTo(TByte* aPtr)
{
    const TByte* src = iAudioData->Ptr(iOffset);
    (void)memcpy(aPtr, src, iSize);
    if (iNextAudio != nullptr) {
        iNextAudio->CopyTo(aPtr + iSize);
    }
}

MsgAudioEncoded* MsgAudioEncoded::Clone()
{
    MsgAudioEncoded* clone = static_cast<Allocator<MsgAudioEncoded>&>(iAllocator).Allocate();
    clone->iNextAudio = nullptr;
    if (iNextAudio) {
        clone->iNextAudio = iNextAudio->Clone();
    }
    clone->iSize = iSize;
    clone->iOffset = iOffset;
    clone->iAudioData = iAudioData;
    iAudioData->AddRef();
    return clone;
}

const EncodedAudio& MsgAudioEncoded::AudioData() const
{
    return *iAudioData;
}

TUint MsgAudioEncoded::AudioDataOffset() const
{
    return iOffset;
}

void MsgAudioEncoded::Initialise(EncodedAudio* aEncodedAudio)
{
    iAudioData = aEncodedAudio;
    iSize = iAudioData->Bytes();
    iOffset = 0;
    iNextAudio = nullptr;
}

void MsgAudioEncoded::Clear()
{
    if (iNextAudio != nullptr) {
        iNextAudio->RemoveRef();
    }
    iAudioData->RemoveRef();
}

Msg* MsgAudioEncoded::Process(IMsgProcessor& aProcessor)
{
    return aProcessor.ProcessMsg(this);
}


// MsgMetaText

MsgMetaText::MsgMetaText(AllocatorBase& aAllocator)
    : Msg(aAllocator)
{
}

const Brx& MsgMetaText::MetaText() const
{
    return iMetaText;
}

void MsgMetaText::Initialise(const Brx& aMetaText)
{
    iMetaText.Replace(aMetaText);
}

void MsgMetaText::Clear()
{
#ifdef DEFINE_DEBUG
    iMetaText.SetBytes(0);
#endif
}

Msg* MsgMetaText::Process(IMsgProcessor& aProcessor)
{
    return aProcessor.ProcessMsg(this);
}


// MsgStreamInterrupted

MsgStreamInterrupted::MsgStreamInterrupted(AllocatorBase& aAllocator)
    : Msg(aAllocator)
{
}

TUint MsgStreamInterrupted::Jiffies() const
{
    return iJiffies;
}

void MsgStreamInterrupted::Initialise(TUint aJiffies)
{
    iJiffies = aJiffies;
}

void MsgStreamInterrupted::Clear()
{
    iJiffies = 0;
}

Msg* MsgStreamInterrupted::Process(IMsgProcessor& aProcessor)
{
    return aProcessor.ProcessMsg(this);
}


// MsgHalt

MsgHalt::MsgHalt(AllocatorBase& aAllocator)
    : Msg(aAllocator)
{
}

TUint MsgHalt::Id() const
{
    return iId;
}

void MsgHalt::ReportHalted()
{
    if (iCallback) {
        iCallback();
        iCallback = Functor();
    }
}

void MsgHalt::Initialise(TUint aId)
{
    Initialise(aId, Functor());
}

void MsgHalt::Initialise(TUint aId, Functor aCallback)
{
    iId = aId;
    iCallback = aCallback;
}

void MsgHalt::Clear()
{
    ASSERT(!iCallback);
    iId = UINT_MAX;
    iCallback = Functor();
}

Msg* MsgHalt::Process(IMsgProcessor& aProcessor)
{
    return aProcessor.ProcessMsg(this);
}


// MsgFlush

const TUint MsgFlush::kIdInvalid  = 0;

MsgFlush::MsgFlush(AllocatorBase& aAllocator)
    : Msg(aAllocator)
{
}

void MsgFlush::Initialise(TUint aId)
{
    iId = aId;
}

TUint MsgFlush::Id() const
{
    return iId;
}

void MsgFlush::Clear()
{
    iId = kIdInvalid;
}

Msg* MsgFlush::Process(IMsgProcessor& aProcessor)
{
    return aProcessor.ProcessMsg(this);
}


// MsgWait

MsgWait::MsgWait(AllocatorBase& aAllocator)
    : Msg(aAllocator)
{
}

Msg* MsgWait::Process(IMsgProcessor& aProcessor)
{
    return aProcessor.ProcessMsg(this);
}


// DecodedStreamInfo

const RampType DecodedStreamInfo::kRampDefault;

DecodedStreamInfo::DecodedStreamInfo()
    : iBitRate(0)
    , iBitDepth(0)
    , iSampleRate(0)
    , iNumChannels(0)
    , iCodecName("")
    , iTrackLength(0)
    , iLossless(false)
    , iFormat(AudioFormat::Pcm)
    , iStreamHandler(nullptr)
    , iRamp(kRampDefault)
{
}

void DecodedStreamInfo::Set(TUint aStreamId, TUint aBitRate, TUint aBitDepth, TUint aSampleRate,
                            TUint aNumChannels, const Brx& aCodecName, TUint64 aTrackLength,
                            TUint64 aSampleStart, TBool aLossless, TBool aSeekable, TBool aLive,
                            TBool aAnalogBypass, AudioFormat aFormat, Media::Multiroom aMultiroom,
                            const SpeakerProfile& aProfile, IStreamHandler* aStreamHandler,
                            RampType aRamp)
{
    iStreamId = aStreamId;
    iBitRate = aBitRate;
    iBitDepth = aBitDepth;
    iSampleRate = aSampleRate;
    iNumChannels = aNumChannels;
    iCodecName.Replace(aCodecName);
    iTrackLength = aTrackLength;
    iSampleStart = aSampleStart;
    iLossless = aLossless;
    iSeekable = aSeekable;
    iLive = aLive;
    iAnalogBypass = aAnalogBypass;
    iFormat = aFormat;
    iMultiroom = aMultiroom;
    iProfile = aProfile;
    iStreamHandler = aStreamHandler;
    iRamp = aRamp;
}


// MsgDecodedStream

const RampType MsgDecodedStream::kRampDefault;

MsgDecodedStream::MsgDecodedStream(AllocatorBase& aAllocator)
    : Msg(aAllocator)
{
}

const DecodedStreamInfo& MsgDecodedStream::StreamInfo() const
{
    return iStreamInfo;
}

void MsgDecodedStream::Initialise(TUint aStreamId, TUint aBitRate, TUint aBitDepth, TUint aSampleRate,
                                  TUint aNumChannels, const Brx& aCodecName, TUint64 aTrackLength,
                                  TUint64 aSampleStart, TBool aLossless, TBool aSeekable, TBool aLive,
                                  TBool aAnalogBypass, AudioFormat aFormat, Media::Multiroom aMultiroom,
                                  const SpeakerProfile& aProfile, IStreamHandler* aStreamHandler,
                                  RampType aRamp)
{
    iStreamInfo.Set(aStreamId, aBitRate, aBitDepth, aSampleRate, aNumChannels, aCodecName,
                    aTrackLength, aSampleStart, aLossless, aSeekable, aLive, aAnalogBypass,
                    aFormat, aMultiroom, aProfile, aStreamHandler, aRamp);
}

void MsgDecodedStream::Clear()
{
#ifdef DEFINE_DEBUG
    iStreamInfo.Set(UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, Brx::Empty(),
                    ULONG_MAX, ULONG_MAX, false, false, false, false,
                    AudioFormat::Pcm, Multiroom::Allowed, SpeakerProfile(), nullptr, kRampDefault);
#endif
}

Msg* MsgDecodedStream::Process(IMsgProcessor& aProcessor)
{
    return aProcessor.ProcessMsg(this);
}


// MsgAudio

void MsgAudio::SetObserver(IPipelineBufferObserver& aPipelineBufferObserver)
{
    ASSERT(iPipelineBufferObserver == nullptr);
    iPipelineBufferObserver = &aPipelineBufferObserver;
    iPipelineBufferObserver->Update((TInt)iSize);
}

MsgAudio* MsgAudio::Split(TUint aJiffies)
{
    ASSERT(aJiffies > 0);
    ASSERT(aJiffies < iSize);
    MsgAudio* remaining = Allocate();
    remaining->iOffset = iOffset + aJiffies;
    remaining->iSize = iSize - aJiffies;
    remaining->iSampleRate = iSampleRate;
    remaining->iBitDepth = iBitDepth;
    remaining->iNumChannels = iNumChannels;
    remaining->iPipelineBufferObserver = iPipelineBufferObserver;
    if (iRamp.IsEnabled()) {
        remaining->iRamp = iRamp.Split(aJiffies, iSize);
    }
    else {
        remaining->iRamp.Reset();
    }
    iSize = aJiffies;
    SplitCompleted(*remaining);
    return remaining;
}

MsgAudio* MsgAudio::Clone()
{
    MsgAudio* clone = Allocate();
    clone->iSize = iSize;
    clone->iOffset = iOffset;
    clone->iRamp = iRamp;
    clone->iSampleRate = iSampleRate;
    clone->iBitDepth = iBitDepth;
    clone->iNumChannels = iNumChannels;
    clone->iPipelineBufferObserver = nullptr;
    return clone;
}

TUint MsgAudio::Jiffies() const
{
    return iSize;
}

TUint MsgAudio::SetRamp(TUint aStart, TUint& aRemainingDuration, Ramp::EDirection aDirection, MsgAudio*& aSplit)  // FIXME
{
    const TUint remainingDuration = aRemainingDuration;
    Media::Ramp split;
    TUint splitPos;
    aSplit = nullptr;

    ASSERT(aDirection == Ramp::EUp || aDirection == Ramp::EDown);
    if (iRamp.IsEnabled() && iRamp.Direction() == Ramp::EMute) {
        if (aDirection == Ramp::EDown) {
            aRemainingDuration = 0;
        }
        return iRamp.End();
    }

    /*TBool logAppliedRamp = false;
    if (iRamp.IsEnabled()) {
        Log::Print("++ MsgAudio::SetRamp(%08x, %u, %u): Existing ramp is [%08x...%08x]\n", aStart, aRemainingDuration, aDirection, iRamp.Start(), iRamp.End());
        logAppliedRamp = true;
    }*/
    if (iRamp.Set(aStart, iSize, remainingDuration, aDirection, split, splitPos)) {
        if (splitPos == 0) {
            iRamp = split;
        }
        else if (splitPos != iSize) {
            Media::Ramp ramp = iRamp; // Split() will muck about with ramps.  Allow this to happen then reset the correct values
            aSplit = Split(splitPos);
            iRamp = ramp;
            aSplit->iRamp = split;
            ASSERT_DEBUG(iRamp.End() == split.Start());
            //Log::Print("\nSplit msg at %u jiffies.  First ramp=[%08x...%08x], second ramp=[%08x...%08x]\n",
            //            splitPos, iRamp.Start(), iRamp.End(), split.Start(), split.End());
        }
    }
    /*if (logAppliedRamp) {
        Log::Print("++ final ramp: [%08x...%08x]", iRamp.Start(), iRamp.End());
        if (aSplit != nullptr) {
            Log::Print("   split: [%08x...%08x]", aSplit->iRamp.Start(), aSplit->iRamp.End());
        }
        Log::Print("\n");
    }*/

    aRemainingDuration -= iSize;
    if (aSplit != nullptr && aSplit->Ramp().Direction() != aDirection && aDirection == Ramp::EUp) {
        aRemainingDuration += aSplit->iSize; // roughly compensate for split running in opposite direction to requested ramp
    }

    // It may be possible to terminate ramps down early if the msg had previously been ramped
    if (aDirection == Ramp::EDown && iRamp.End() == Ramp::kMin) {
        aRemainingDuration = 0;
    }
    else if (aDirection == Ramp::EUp && iRamp.End() == Ramp::kMax) {
        // shorter ramp range now allows ramps up to complete a few samples early
        aRemainingDuration = 0;
    }

    return iRamp.End();
}

void MsgAudio::ClearRamp()
{
    iRamp.Reset();
}

void MsgAudio::SetMuted()
{
    iRamp.SetMuted();
}

const Media::Ramp& MsgAudio::Ramp() const
{
    return iRamp;
}

TUint MsgAudio::MedianRampMultiplier()
{
    if (!iRamp.IsEnabled()) {
        return 0x8000; // see RampArray.h
    }
    else if (iRamp.Direction() == Ramp::EMute) {
        return 0;
    }
    const TUint mult = RampApplicator::MedianMultiplier(iRamp);
    iRamp.Reset();
    return mult;
}

TBool MsgAudio::HasBufferObserver() const
{
    return iPipelineBufferObserver != nullptr;
}

MsgAudio::MsgAudio(AllocatorBase& aAllocator)
    : Msg(aAllocator)
    , iPipelineBufferObserver(nullptr)
{
}

void MsgAudio::Initialise(TUint aSampleRate, TUint aBitDepth, TUint aChannels)
{
    iRamp.Reset();
    iSampleRate = aSampleRate;
    iBitDepth = aBitDepth;
    iNumChannels = aChannels;
}

void MsgAudio::Clear()
{
    if (iPipelineBufferObserver != nullptr) {
        TInt jiffies = (TInt)iSize;
        iPipelineBufferObserver->Update(-jiffies);
    }
    iSize = 0;
    iPipelineBufferObserver = nullptr;
}

void MsgAudio::SplitCompleted(MsgAudio& /*aMsg*/)
{
    // nothing to do by default
}


// MsgAudioDecoded

const TUint64 MsgAudioDecoded::kTrackOffsetInvalid = UINT64_MAX;

void MsgAudioDecoded::Aggregate(MsgAudioDecoded* aMsg)
{
    ASSERT(aMsg->iSampleRate == iSampleRate);
    ASSERT(aMsg->iBitDepth == iBitDepth);
    ASSERT(aMsg->iNumChannels == iNumChannels);
    ASSERT(aMsg->iTrackOffset == iTrackOffset + Jiffies()); // aMsg must logically follow this one
    ASSERT(!iRamp.IsEnabled() && !aMsg->iRamp.IsEnabled()); // no ramps allowed

    iAudioData->Aggregate(*(aMsg->iAudioData));
    iSize += aMsg->Jiffies();
    aMsg->RemoveRef();

    AggregateComplete();
}

TUint64 MsgAudioDecoded::TrackOffset() const
{
    return iTrackOffset;
}

MsgAudioDecoded::MsgAudioDecoded(AllocatorBase& aAllocator)
    : MsgAudio(aAllocator)
{
}

MsgAudio* MsgAudioDecoded::Clone()
{
    MsgAudioDecoded* clone = static_cast<MsgAudioDecoded*>(MsgAudio::Clone());
    clone->iAudioData = iAudioData;
    if (iAllocatorPlayableSilence != nullptr) {
        clone->iAllocatorPlayableSilence = iAllocatorPlayableSilence;
    }
    else {
        clone->iAllocatorPlayableSilenceDsd = iAllocatorPlayableSilenceDsd;
    }
    clone->iTrackOffset = iTrackOffset;
    iAudioData->AddRef();
    return clone;
}

void MsgAudioDecoded::Initialise(DecodedAudio* aDecodedAudio, TUint aSampleRate, TUint aBitDepth,
                                 TUint aChannels, TUint64 aTrackOffset, TUint aNumSubsamples,
                                 Allocator<MsgPlayableSilence>& aAllocatorPlayableSilence)
{
    MsgAudio::Initialise(aSampleRate, aBitDepth, aChannels);
    iAllocatorPlayableSilence = &aAllocatorPlayableSilence;
    iAllocatorPlayableSilenceDsd = nullptr;
    iAudioData = aDecodedAudio;
    iTrackOffset = aTrackOffset;
    ASSERT_VA(aNumSubsamples % iNumChannels == 0, "Invalid number of subsamples. aNumSubsamples: %u, aSampleRate: %u, aBitDepth: %u, aChannels: %u, aTrackOffset: %llu, aDecodedAudio->Bytes(): %u\n", aNumSubsamples, aSampleRate, aBitDepth, aChannels, aTrackOffset, aDecodedAudio->Bytes());
    iSize = (aNumSubsamples / iNumChannels) * Jiffies::PerSample(iSampleRate);
    ASSERT(iSize > 0);
    iOffset = 0;
}

void MsgAudioDecoded::InitialiseDsd(DecodedAudio* aDecodedAudio, TUint aSampleRate, TUint aChannels, TUint aSampleBlockWords,
                                    TUint64 aTrackOffset, TUint aNumSubsamples, Allocator<MsgPlayableSilenceDsd>& aAllocatorPlayableSilenceDsd)
{
    MsgAudio::Initialise(aSampleRate, 1, aChannels);
    iAllocatorPlayableSilenceDsd = &aAllocatorPlayableSilenceDsd;
    iAllocatorPlayableSilence = nullptr;
    iAudioData = aDecodedAudio;
    iTrackOffset = aTrackOffset;
    iSampleBlockWords = aSampleBlockWords;
    ASSERT(aNumSubsamples % iNumChannels == 0);
    iSize = (aNumSubsamples / iNumChannels) * Jiffies::PerSample(iSampleRate);
    ASSERT(iSize > 0);
    iOffset = 0;
}

void MsgAudioDecoded::SplitCompleted(MsgAudio& aRemaining)
{
    iAudioData->AddRef();
    MsgAudioDecoded& remaining = static_cast<MsgAudioDecoded&>(aRemaining);
    remaining.iAudioData = iAudioData;
    if (iTrackOffset == kTrackOffsetInvalid) {
        remaining.iTrackOffset = iTrackOffset;
    }
    else {
        remaining.iTrackOffset = iTrackOffset + iSize;
    }
    if (iAllocatorPlayableSilence != nullptr) {
        remaining.iAllocatorPlayableSilence = iAllocatorPlayableSilence;
    }
    else {
        remaining.iAllocatorPlayableSilenceDsd = iAllocatorPlayableSilenceDsd;
    }
}

void MsgAudioDecoded::Clear()
{
    MsgAudio::Clear();
    iAudioData->RemoveRef();
    iTrackOffset = kTrackOffsetInvalid;
}

void MsgAudioDecoded::AggregateComplete()
{
    // Nothing to do by default.
}


// MsgAudioPcm

const TUint MsgAudioPcm::kUnityAttenuation = 256;

MsgAudioPcm::MsgAudioPcm(AllocatorBase& aAllocator)
    : MsgAudioDecoded(aAllocator)
{
}

MsgAudio* MsgAudioPcm::Clone()
{
    MsgAudioPcm* clone = static_cast<MsgAudioPcm*>(MsgAudioDecoded::Clone());
    clone->iAllocatorPlayablePcm = iAllocatorPlayablePcm;
    clone->iAttenuation = iAttenuation;
    return clone;
}

MsgPlayable* MsgAudioPcm::CreatePlayable()
{
    TUint offsetJiffies = iOffset;
    const TUint jiffiesPerSample = Jiffies::PerSample(iSampleRate);
    const TUint offsetBytes = Jiffies::ToBytes(offsetJiffies, jiffiesPerSample, iNumChannels, iBitDepth);
    TUint sizeJiffies = iSize + (iOffset - offsetJiffies);
    const TUint sizeBytes = Jiffies::ToBytes(sizeJiffies, jiffiesPerSample, iNumChannels, iBitDepth);
    // both size & offset will be rounded down if they don't fall on a sample boundary
    // we don't risk losing any data doing this as the start and end of each DecodedAudio's data fall on sample boundaries

    MsgPlayable* playable;
    if (iRamp.Direction() != Ramp::EMute) {
        auto playablePcm = iAllocatorPlayablePcm->Allocate();
        Optional<IPipelineBufferObserver> bufferObserver(iPipelineBufferObserver);
        playablePcm->Initialise(iAudioData, sizeBytes, iSize, iSampleRate, iBitDepth, iNumChannels,
                                offsetBytes, iAttenuation, iRamp, bufferObserver);
        playable = playablePcm;
    }
    else {
        MsgPlayableSilence* silence = iAllocatorPlayableSilence->Allocate();
        Media::Ramp noRamp;
        Optional<IPipelineBufferObserver> bufferObserver(iPipelineBufferObserver);
        silence->Initialise(sizeBytes, iSize, iSampleRate, iBitDepth, iNumChannels, noRamp, bufferObserver);
        playable = silence;
    }
    iPipelineBufferObserver = nullptr;
    RemoveRef();
    return playable;
}

void MsgAudioPcm::Initialise(DecodedAudio* aDecodedAudio, TUint aSampleRate, TUint aBitDepth, TUint aChannels, TUint64 aTrackOffset,
                             Allocator<MsgPlayablePcm>& aAllocatorPlayablePcm,
                             Allocator<MsgPlayableSilence>& aAllocatorPlayableSilence)
{
    const TUint bytes = aDecodedAudio->Bytes();
    const TUint byteDepth = aBitDepth / 8;
    ASSERT(bytes % byteDepth == 0);
    const TUint numSubsamples = bytes / byteDepth;
    MsgAudioDecoded::Initialise(aDecodedAudio, aSampleRate, aBitDepth, aChannels,
                                aTrackOffset, numSubsamples, aAllocatorPlayableSilence);
    iAllocatorPlayablePcm = &aAllocatorPlayablePcm;
    iAttenuation = MsgAudioPcm::kUnityAttenuation;
}

void MsgAudioPcm::SplitCompleted(MsgAudio& aRemaining)
{
    MsgAudioDecoded::SplitCompleted(aRemaining);
    MsgAudioPcm& remaining = static_cast<MsgAudioPcm&>(aRemaining);
    remaining.iAllocatorPlayablePcm = iAllocatorPlayablePcm;
    remaining.iAttenuation = iAttenuation;
}

MsgAudio* MsgAudioPcm::Allocate()
{
    return static_cast<Allocator<MsgAudioPcm>&>(iAllocator).Allocate();
}

void MsgAudioPcm::Clear()
{
    MsgAudioDecoded::Clear();
    iAttenuation = MsgAudioPcm::kUnityAttenuation;
}

Msg* MsgAudioPcm::Process(IMsgProcessor& aProcessor)
{
    return aProcessor.ProcessMsg(this);
}

void MsgAudioPcm::SetAttenuation(TUint aAttenuation)
{
    iAttenuation = aAttenuation;
}


// MsgAudioDsd

MsgAudioDsd::MsgAudioDsd(AllocatorBase& aAllocator)
    : MsgAudioDecoded(aAllocator)
    , iSampleBlockWords(0)
    , iBlockWordsNoPad(0)
    , iSizeTotalJiffies(0)
    , iJiffiesNonPlayable(0)
{
}

TUint MsgAudioDsd::JiffiesNonPlayable()
{
    return iJiffiesNonPlayable;
}

TUint MsgAudioDsd::SizeTotalJiffies()
{
    return iSizeTotalJiffies;
}

MsgAudio* MsgAudioDsd::Clone()
{
    auto clone = static_cast<MsgAudioDsd*>(MsgAudioDecoded::Clone());
    clone->iAllocatorPlayableDsd = iAllocatorPlayableDsd;
    clone->iSampleBlockWords = iSampleBlockWords;
    clone->iBlockWordsNoPad = iBlockWordsNoPad;
    clone->iSizeTotalJiffies = iSizeTotalJiffies;
    clone->iJiffiesNonPlayable = iJiffiesNonPlayable;
    return clone;
}

MsgPlayable* MsgAudioDsd::CreatePlayable()
{
    // (Mostly) taken from MsgAudioPcm::CreatePlayable(). Round down size and offset if they don't fall on a sample block boundary.
    const TUint jiffiesPerSample = Jiffies::PerSample(iSampleRate);
    const TUint jiffiesPerSampleBlockPlayable = SamplesPerBlock(iBlockWordsNoPad) * jiffiesPerSample;
    const TUint samplesPerBlockTotal = SamplesPerBlock(iSampleBlockWords);

    // Calculate offset jiffies.
    TUint offsetJiffiesTotal = JiffiesPlayableToJiffiesTotal(iOffset, jiffiesPerSampleBlockPlayable);
    // Calculate offset bytes.
    const TUint offsetBytes = Jiffies::ToBytesSampleBlock(offsetJiffiesTotal, jiffiesPerSample, iNumChannels, iBitDepth, samplesPerBlockTotal);

    // Calculate size jiffies.
    TUint sizeJiffiesTotal = iSizeTotalJiffies;
    // Calculate size bytes.
    const TUint sizeBytes = Jiffies::ToBytesSampleBlock(sizeJiffiesTotal, jiffiesPerSample, iNumChannels, iBitDepth, samplesPerBlockTotal);

    // Log::Print("MsgAudioDsd::CreatePlayable(), iOffset: %u, sizeBytes %u, offsetBytes %u, sizeJiffiesTotal: %u, iSize: %u, iSampleRate: %u, iAudioData->Bytes(): %u\n", iOffset, sizeBytes, offsetBytes, sizeJiffiesTotal, iSize, iSampleRate, iAudioData->Bytes());

    MsgPlayable* playable;
    if (iRamp.Direction() != Ramp::EMute) {
        auto playableDsd = iAllocatorPlayableDsd->Allocate();
        Optional<IPipelineBufferObserver> bufferObserver(iPipelineBufferObserver);
        playableDsd->Initialise(iAudioData, sizeBytes, iSize, iSampleRate, iNumChannels,
                                iSampleBlockWords, offsetBytes, iRamp, bufferObserver);
        playable = playableDsd;
    }
    else {
        MsgPlayableSilenceDsd* silence = iAllocatorPlayableSilenceDsd->Allocate();
        Media::Ramp noRamp;
        Optional<IPipelineBufferObserver> bufferObserver(iPipelineBufferObserver);
        silence->Initialise(sizeBytes, iSize, iSampleRate, iBitDepth, iNumChannels, iSampleBlockWords, noRamp, bufferObserver);
        playable = silence;
    }
    iPipelineBufferObserver = nullptr;
    RemoveRef();
    return playable;
}

void MsgAudioDsd::Initialise(DecodedAudio* aDecodedAudio, TUint aSampleRate, TUint aChannels,
                             TUint aSampleBlockWords, TUint64 aTrackOffset, TUint aPadBytesPerChunk,
                             Allocator<MsgPlayableDsd>& aAllocatorPlayableDsd,
                             Allocator<MsgPlayableSilenceDsd>& aAllocatorPlayableSilenceDsd)
{
    const TUint msgSubSamples = aDecodedAudio->Bytes() * 8;
    iBlockWordsNoPad = aSampleBlockWords - aPadBytesPerChunk;
    const TUint numSubSamples = (msgSubSamples * iBlockWordsNoPad) / aSampleBlockWords; // 1 Subsample per bit for DSD
    MsgAudioDecoded::InitialiseDsd(aDecodedAudio, aSampleRate, aChannels, aSampleBlockWords,
                                aTrackOffset, numSubSamples, aAllocatorPlayableSilenceDsd);
    iAllocatorPlayableDsd = &aAllocatorPlayableDsd;
    iSampleBlockWords = aSampleBlockWords;
    iSizeTotalJiffies = SizeJiffiesTotal();
    iJiffiesNonPlayable = iSizeTotalJiffies - iSize;
}

MsgAudio* MsgAudioDsd::Allocate()
{
    return static_cast<Allocator<MsgAudioDsd>&>(iAllocator).Allocate();
}

void MsgAudioDsd::SplitCompleted(MsgAudio& aRemaining)
{
    MsgAudioDecoded::SplitCompleted(aRemaining);

    MsgAudioDsd& remaining = static_cast<MsgAudioDsd&>(aRemaining);

    iSizeTotalJiffies = SizeJiffiesTotal();
    iJiffiesNonPlayable = iSizeTotalJiffies - iSize;
    remaining.iAllocatorPlayableDsd = iAllocatorPlayableDsd;
    remaining.iSampleBlockWords = iSampleBlockWords;
    remaining.iBlockWordsNoPad = iBlockWordsNoPad;
    // iSamplesBlockWords, iBlockWordsNoPad and iSize (among other member variables) must be set before SizeJiffiesTotal() can be called.
    remaining.iSizeTotalJiffies = remaining.SizeJiffiesTotal();
    remaining.iJiffiesNonPlayable = remaining.iSizeTotalJiffies - remaining.iSize;
}

void MsgAudioDsd::Clear()
{
    iSampleBlockWords = 0;
    iBlockWordsNoPad = 0;
    iSizeTotalJiffies = 0;
    iJiffiesNonPlayable = 0;
    MsgAudioDecoded::Clear();
}

Msg* MsgAudioDsd::Process(IMsgProcessor& aProcessor)
{
    return aProcessor.ProcessMsg(this);
}

void MsgAudioDsd::AggregateComplete()
{
    iSizeTotalJiffies = SizeJiffiesTotal();
    iJiffiesNonPlayable = iSizeTotalJiffies - iSize;
}

TUint MsgAudioDsd::JiffiesPlayableToJiffiesTotal(TUint aJiffies, TUint aJiffiesPerSampleBlockPlayable) const
{
    TUint jiffiesTotal = aJiffies - (aJiffies % aJiffiesPerSampleBlockPlayable);
    ASSERT(jiffiesTotal % iBlockWordsNoPad == 0);
    jiffiesTotal /= iBlockWordsNoPad;
    jiffiesTotal *= iSampleBlockWords;
    return jiffiesTotal;
}

TUint MsgAudioDsd::SamplesPerBlock(TUint aBlockWords) const
{
    return ((aBlockWords * 4) * 8) / iNumChannels; // Dsd is 1 Subsample per bit
}

TUint MsgAudioDsd::SizeJiffiesTotal() const
{
    const TUint jiffiesPerSample = Jiffies::PerSample(iSampleRate);
    const TUint jiffiesPerSampleBlockPlayable = SamplesPerBlock(iBlockWordsNoPad) * jiffiesPerSample;
    const TUint samplesPerBlockTotal = SamplesPerBlock(iSampleBlockWords);
    // Calculate offset jiffies playable.
    const TUint offsetJiffiesPlayable = iOffset - (iOffset % jiffiesPerSampleBlockPlayable);
    // Calculate size jiffies.
    const TUint sizeJiffiesPlayable = iSize + (iOffset - offsetJiffiesPlayable);
    TUint sizeJiffiesTotal = JiffiesPlayableToJiffiesTotal(sizeJiffiesPlayable, jiffiesPerSampleBlockPlayable);
    (void)Jiffies::ToBytesSampleBlock(sizeJiffiesTotal, jiffiesPerSample, iNumChannels, iBitDepth, samplesPerBlockTotal);
    return sizeJiffiesTotal;
}


// MsgSilence

MsgSilence::MsgSilence(AllocatorBase& aAllocator)
    : MsgAudio(aAllocator)
{
}

MsgPlayable* MsgSilence::CreatePlayable()
{
    const TUint kJiffiesPerSample = Jiffies::PerSample(iSampleRate);
    const TUint kBytes = Jiffies::ToBytes(iSizeJiffiesTotal, kJiffiesPerSample, iNumChannels, iBitDepth);
    if (kBytes > 0) {
        ASSERT(iSizeJiffiesTotal % iSampleBlockJiffiesTotal == 0);
    }

    if (iAllocatorPlayablePcm != nullptr) {
        MsgPlayableSilence* playable = iAllocatorPlayablePcm->Allocate();
        playable->Initialise(kBytes, iSize, iSampleRate, iBitDepth, iNumChannels, iRamp, nullptr);
        RemoveRef();
        return playable;
    }
    else {
        MsgPlayableSilenceDsd* playable = iAllocatorPlayableDsd->Allocate();
        playable->Initialise(kBytes, iSize, iSampleRate, iBitDepth, iNumChannels, iSampleBlockWords, iRamp, nullptr);
        RemoveRef();
        return playable;
    }
}

MsgAudio* MsgSilence::Clone()
{
    MsgSilence* clone = static_cast<MsgSilence*>(MsgAudio::Clone());

    clone->iSampleBlockJiffiesPlayable = iSampleBlockJiffiesPlayable;
    clone->iSampleBlockJiffiesTotal = iSampleBlockJiffiesTotal;
    clone->iSizeJiffiesTotal = iSizeJiffiesTotal;
    clone->iSampleBlockWords = iSampleBlockWords;

    if (iAllocatorPlayablePcm != nullptr) {
        clone->iAllocatorPlayablePcm = iAllocatorPlayablePcm;
    }
    else {
        clone->iAllocatorPlayableDsd = iAllocatorPlayableDsd;
    }
    return clone;
}

MsgAudio* MsgSilence::Allocate()
{
    return static_cast<Allocator<MsgSilence>&>(iAllocator).Allocate();
}

Msg* MsgSilence::Process(IMsgProcessor& aProcessor)
{
    return aProcessor.ProcessMsg(this);
}

void MsgSilence::SplitCompleted(MsgAudio& aRemaining)
{
    MsgSilence& remaining = static_cast<MsgSilence&>(aRemaining);

    remaining.iSampleBlockJiffiesPlayable = iSampleBlockJiffiesPlayable;
    remaining.iSampleBlockJiffiesTotal = iSampleBlockJiffiesTotal;
    remaining.iSampleBlockWords = iSampleBlockWords;

    const TUint kSampleBlockJiffiesRemaining = iSize % iSampleBlockJiffiesPlayable;
    iSize -= kSampleBlockJiffiesRemaining;
    iSizeJiffiesTotal = iSize / iSampleBlockJiffiesPlayable;
    iSizeJiffiesTotal *= iSampleBlockJiffiesTotal;

    remaining.iSize += kSampleBlockJiffiesRemaining;
    remaining.iSizeJiffiesTotal = remaining.iSize / iSampleBlockJiffiesPlayable;
    remaining.iSizeJiffiesTotal *= iSampleBlockJiffiesTotal;

    if (iAllocatorPlayablePcm != nullptr) {
        remaining.iAllocatorPlayablePcm = iAllocatorPlayablePcm;
    }
    else {
        remaining.iAllocatorPlayableDsd = iAllocatorPlayableDsd;
    }
}

void MsgSilence::Initialise(TUint& aJiffies, TUint aSampleRate, TUint aBitDepth, TUint aChannels, Allocator<MsgPlayableSilence>& aAllocatorPlayable)
{
    MsgAudio::Initialise(aSampleRate, aBitDepth, aChannels);
    iAllocatorPlayablePcm = &aAllocatorPlayable;
    iAllocatorPlayableDsd = nullptr;
    iSampleBlockWords = 0;

    iSampleBlockJiffiesPlayable = Jiffies::PerSample(aSampleRate);
    iSampleBlockJiffiesTotal = iSampleBlockJiffiesPlayable;
    Jiffies::RoundDownNonZeroSampleBlock(aJiffies, iSampleBlockJiffiesPlayable);
    iSize = aJiffies;
    iSizeJiffiesTotal = iSize;
    iOffset = 0;
}

void MsgSilence::InitialiseDsd(TUint& aJiffies, TUint aSampleRate, TUint aChannels, TUint aSampleBlockWords, TUint aPadBytesPerChunk, Allocator<MsgPlayableSilenceDsd>& aAllocatorPlayable)
{
    ASSERT(aSampleBlockWords != 0);
    MsgAudio::Initialise(aSampleRate, 1, aChannels);
    iAllocatorPlayableDsd = &aAllocatorPlayable;
    iAllocatorPlayablePcm = nullptr;
    iSampleBlockWords = aSampleBlockWords;

    const TUint kPaddingBytesTotal = aPadBytesPerChunk * 4;
    const TUint kSampleBlockBytesTotal = iSampleBlockWords * 4;
    ASSERT(kSampleBlockBytesTotal > kPaddingBytesTotal);
    const TUint kSampleBlockBytesPlayable = kSampleBlockBytesTotal - kPaddingBytesTotal;
    const TUint kSampleBlockSamplesTotal = (kSampleBlockBytesTotal * 8) / iNumChannels;
    const TUint kSampleBlockSamplesPlayable = (kSampleBlockBytesPlayable * 8) / iNumChannels;

    const TUint kJiffiesPerSample = Jiffies::PerSample(aSampleRate);
    iSampleBlockJiffiesTotal = kSampleBlockSamplesTotal * kJiffiesPerSample;
    iSampleBlockJiffiesPlayable = kSampleBlockSamplesPlayable * kJiffiesPerSample;
    Jiffies::RoundDownNonZeroSampleBlock(aJiffies, iSampleBlockJiffiesPlayable);
    iSize = aJiffies;
    iOffset = 0;

    iSizeJiffiesTotal = iSize / iSampleBlockJiffiesPlayable;
    iSizeJiffiesTotal *= iSampleBlockJiffiesTotal;
}


// MsgPlayable

MsgPlayable* MsgPlayable::Split(TUint aBytes)
{
    ASSERT(aBytes <= iSize);
    ASSERT(aBytes != 0);
    if (aBytes == iSize) {
        return nullptr;
    }
    /* iJiffies was originally copied from the preceeding MsgAudio and may not
       be quite accurate if that was the result of a Split().  Calculate accurate
       jiffies for the first part of this msg and ensure 'remaining' has the
       remaining (possibly innaccurate) jiffies. */
    const TUint numSamples = iBitDepth == 1 ?
        (aBytes * 8) / iNumChannels :
        aBytes / ((iBitDepth / 8) * iNumChannels);
    const TUint splitJiffies = numSamples * Jiffies::PerSample(iSampleRate);
    MsgPlayable* remaining = Allocate();
    remaining->iOffset = iOffset + aBytes;
    remaining->iSize = iSize - aBytes;
    remaining->iJiffies = iJiffies - splitJiffies;
    remaining->iSampleRate = iSampleRate;
    remaining->iBitDepth = iBitDepth;
    remaining->iNumChannels = iNumChannels;
    if (iRamp.IsEnabled()) {
        remaining->iRamp = iRamp.Split(aBytes, iSize);
    }
    else {
        remaining->iRamp.Reset();
    }
    remaining->iPipelineBufferObserver = iPipelineBufferObserver;
    iSize = aBytes;
    iJiffies = splitJiffies;
    SplitCompleted(*remaining);
    return remaining;
}

TUint MsgPlayable::Bytes() const
{
    return iSize;
}

TUint MsgPlayable::Jiffies() const
{
    return iJiffies;
}

const Media::Ramp& MsgPlayable::Ramp() const
{
    return iRamp;
}

TBool MsgPlayable::HasBufferObserver() const
{
    return iPipelineBufferObserver != nullptr;
}

void MsgPlayable::Read(IPcmProcessor& aProcessor)
{
    aProcessor.BeginBlock();
    if (iSize > 0) {
        ReadBlock(aProcessor);
    }
    aProcessor.EndBlock();
}

void MsgPlayable::Read(IDsdProcessor& aProcessor)
{
    aProcessor.BeginBlock();
    ReadBlock(aProcessor);
    aProcessor.EndBlock();
}

TBool MsgPlayable::TryLogTimestamps()
{
    return false;
}

MsgPlayable::MsgPlayable(AllocatorBase& aAllocator)
    : Msg(aAllocator)
{
}

void MsgPlayable::Initialise(TUint aSizeBytes, TUint aJiffies, TUint aSampleRate, TUint aBitDepth,
                             TUint aNumChannels, TUint aOffsetBytes, const Media::Ramp& aRamp,
                             Optional<IPipelineBufferObserver> aPipelineBufferObserver)
{
    iSize = aSizeBytes;
    iJiffies = aJiffies;
    iSampleRate = aSampleRate;
    iBitDepth = aBitDepth;
    iNumChannels = aNumChannels;
    iOffset = aOffsetBytes;
    iRamp = aRamp;
    iPipelineBufferObserver = aPipelineBufferObserver.Ptr();
}

Msg* MsgPlayable::Process(IMsgProcessor& aProcessor)
{
    return aProcessor.ProcessMsg(this);
}

void MsgPlayable::Clear()
{
    if (iPipelineBufferObserver != nullptr) {
        TInt jiffies = iJiffies;
        iPipelineBufferObserver->Update(-jiffies);
        iPipelineBufferObserver = nullptr;
    }
    iSize = iJiffies = iSampleRate = iBitDepth = iNumChannels = iOffset = 0;
    iRamp.Reset();
}

void MsgPlayable::SplitCompleted(MsgPlayable& /*aMsg*/)
{
    // nothing to do by default
}

void MsgPlayable::ReadBlock(IPcmProcessor& /*aProcessor*/)
{
    ASSERTS();
}

void MsgPlayable::ReadBlock(IDsdProcessor& /*aProcessor*/)
{
    ASSERTS();
}


// MsgPlayablePcm

MsgPlayablePcm::MsgPlayablePcm(AllocatorBase& aAllocator)
    : MsgPlayable(aAllocator)
{
}

void MsgPlayablePcm::Initialise(DecodedAudio* aDecodedAudio, TUint aSizeBytes, TUint aJiffies, TUint aSampleRate, TUint aBitDepth,
                                TUint aNumChannels, TUint aOffsetBytes, TUint aAttenuation, const Media::Ramp& aRamp,
                                Optional<IPipelineBufferObserver> aPipelineBufferObserver)
{
    MsgPlayable::Initialise(aSizeBytes, aJiffies, aSampleRate, aBitDepth, aNumChannels,
                            aOffsetBytes, aRamp, aPipelineBufferObserver);
    iAudioData = aDecodedAudio;
    iAudioData->AddRef();
    iAttenuation = aAttenuation;
}

void MsgPlayablePcm::ApplyAttenuation(Bwx& aData)
{
    if (iAttenuation == MsgAudioPcm::kUnityAttenuation) {
        return;
    }
    ASSERT(iBitDepth == 16);
    TByte* ptr = (TByte*)aData.Ptr();
    const TUint samples = aData.Bytes() / 2;
    for (TUint sampleIndex = 0; sampleIndex < samples; sampleIndex++) {
        TInt16 sample = *ptr << 8;
        sample += *(ptr + 1);
        TInt16 attenuatedSample = (TInt16)(((TInt)sample) * iAttenuation / MsgAudioPcm::kUnityAttenuation);
        *ptr++ = (TByte)(attenuatedSample >> 8);
        *ptr++ = (TByte)attenuatedSample;
    }
}

void MsgPlayablePcm::ReadBlock(IPcmProcessor& aProcessor)
{
    Bwn audioBuf(iAudioData->Ptr(iOffset), iSize, iSize);
    ApplyAttenuation(audioBuf);

    const TUint numChannels = iNumChannels;
    const TUint bitDepth = iBitDepth;
    const TUint subsampleBytes = bitDepth / 8;
    if (iRamp.IsEnabled()) {
        Bws<256> rampedBuf;
        RampApplicator ra(iRamp);
        const TUint numSamples = ra.Start(audioBuf, bitDepth, numChannels);
        const TUint bytesPerSample = subsampleBytes * numChannels;
        const TUint samplesPerFragment = rampedBuf.MaxBytes() / bytesPerSample;
        TByte* ptr = (TByte*)rampedBuf.Ptr();
        TUint fragmentSamples = 0;
        for (TUint i=0; i<numSamples; i++) {
            ra.GetNextSample(ptr);

            fragmentSamples++;
            ptr += bytesPerSample;
            if (fragmentSamples == samplesPerFragment || i == numSamples-1) {
                rampedBuf.SetBytes(fragmentSamples * bytesPerSample);
                aProcessor.ProcessFragment(rampedBuf, numChannels, subsampleBytes);
                ptr = (TByte*)rampedBuf.Ptr();
                fragmentSamples = 0;
            }
        }
    }
    else {
        aProcessor.ProcessFragment(audioBuf, numChannels, subsampleBytes);
    }

}

TBool MsgPlayablePcm::TryLogTimestamps()
{
#ifdef TIMESTAMP_LOGGING_ENABLE
    return iAudioData->TryLogTimestamps();
#else
    return true;
#endif
}

MsgPlayable* MsgPlayablePcm::Allocate()
{
    return static_cast<Allocator<MsgPlayablePcm>&>(iAllocator).Allocate();
}

void MsgPlayablePcm::SplitCompleted(MsgPlayable& aRemaining)
{
    iAudioData->AddRef();
    static_cast<MsgPlayablePcm&>(aRemaining).iAudioData = iAudioData;
}

void MsgPlayablePcm::Clear()
{
    MsgPlayable::Clear();
    iAudioData->RemoveRef();
    iAttenuation = MsgAudioPcm::kUnityAttenuation;
}


// MsgPlayableDsd

MsgPlayableDsd::MsgPlayableDsd(AllocatorBase& aAllocator)
    : MsgPlayable(aAllocator)
{
}

void MsgPlayableDsd::Initialise(DecodedAudio* aDecodedAudio, TUint aSizeBytes, TUint aJiffies, TUint aSampleRate,
                                TUint aNumChannels, TUint aSampleBlockWords, TUint aOffsetBytes,
                                const Media::Ramp& aRamp, Optional<IPipelineBufferObserver> aPipelineBufferObserver)
{
    MsgPlayable::Initialise(aSizeBytes, aJiffies, aSampleRate, 1, aNumChannels,
                            aOffsetBytes, aRamp, aPipelineBufferObserver);
    iAudioData = aDecodedAudio;
    iAudioData->AddRef();
    iSampleBlockWords = aSampleBlockWords;
}

void MsgPlayableDsd::ReadBlock(IDsdProcessor& aProcessor)
{
    Brn audioBuf(iAudioData->Ptr(iOffset), iSize);
    ASSERT(!iRamp.IsEnabled());
    aProcessor.ProcessFragment(audioBuf, iNumChannels, iSampleBlockWords);
}

MsgPlayable* MsgPlayableDsd::Allocate()
{
    return static_cast<Allocator<MsgPlayableDsd>&>(iAllocator).Allocate();
}

void MsgPlayableDsd::SplitCompleted(MsgPlayable& aRemaining)
{
    iAudioData->AddRef();
    static_cast<MsgPlayableDsd&>(aRemaining).iAudioData = iAudioData;
}

void MsgPlayableDsd::Clear()
{
    MsgPlayable::Clear();
    iAudioData->RemoveRef();
}


// MsgPlayableSilence

MsgPlayableSilence::MsgPlayableSilence(AllocatorBase& aAllocator)
    : MsgPlayable(aAllocator)
{
}

void MsgPlayableSilence::Initialise(TUint aSizeBytes, TUint aJiffies, TUint aSampleRate, TUint aBitDepth,
                                    TUint aNumChannels, const Media::Ramp& aRamp,
                                    Optional<IPipelineBufferObserver> aPipelineBufferObserver)
{
    MsgPlayable::Initialise(aSizeBytes, aJiffies, aSampleRate, aBitDepth,
                            aNumChannels, 0, aRamp, aPipelineBufferObserver);
}

void MsgPlayableSilence::ReadBlock(IPcmProcessor& aProcessor)
{
    static const TByte silence[DecodedAudio::kMaxBytes] = { 0 };
    static const TByte silence6ch[DecodedAudio::kMaxBytes] = { 0, 0, 0, 0x00,  0, 0, 0, 0x10, 0, 0, 0, 0x20,  0, 0, 0, 0x30,  0, 0, 0, 0x40,  0, 0, 0, 0x50, 0, 0, 0, 0x60,  0, 0, 0, 0x70 };
    TUint remainingBytes = iSize;
    const TUint subsampleBytes = iBitDepth / 8;
    const TUint maxBytes = DecodedAudio::kMaxBytes - (DecodedAudio::kMaxBytes % (iNumChannels * subsampleBytes));
    do {
        TUint bytes = (remainingBytes > maxBytes? maxBytes : remainingBytes);
        Brn audioBuf;
        if(iNumChannels == 6) {
            audioBuf.Set(silence6ch, bytes);
        }
        else {
            audioBuf.Set(silence, bytes);
        }
        aProcessor.ProcessSilence(audioBuf, iNumChannels, subsampleBytes);
        remainingBytes -= bytes;
    } while (remainingBytes > 0);
}

MsgPlayable* MsgPlayableSilence::Allocate()
{
    return static_cast<Allocator<MsgPlayableSilence>&>(iAllocator).Allocate();
}

// MsgPlayableSilenceDsd

MsgPlayableSilenceDsd::MsgPlayableSilenceDsd(AllocatorBase& aAllocator)
    : MsgPlayable(aAllocator)
{
}

void MsgPlayableSilenceDsd::Initialise(TUint aSizeBytes, TUint aJiffies, TUint aSampleRate, TUint aBitDepth,
                                      TUint aNumChannels, TUint aSampleBlockWords, const Media::Ramp& aRamp,
                                      Optional<IPipelineBufferObserver> aPipelineBufferObserver)
{
    MsgPlayable::Initialise(aSizeBytes, aJiffies, aSampleRate, aBitDepth,
                            aNumChannels, 0, aRamp, aPipelineBufferObserver);
    iSampleBlockWords = aSampleBlockWords;
}

void MsgPlayableSilenceDsd::ReadBlock(IDsdProcessor& aProcessor)
{
    static TByte silence[DecodedAudio::kMaxBytes];
    (void)memset(silence, 0x69, sizeof(silence));

    const TUint kSampleBlockBytes = iSampleBlockWords * 4;
    ASSERT(iSize % kSampleBlockBytes == 0);
    ASSERT(DecodedAudio::kMaxBytes % kSampleBlockBytes == 0);

    TUint remaining = iSize;
    do {
        TUint bytes = (remaining > DecodedAudio::kMaxBytes ? DecodedAudio::kMaxBytes : remaining);
        Brn audioBuf(silence, bytes);
        aProcessor.ProcessFragment(audioBuf, iNumChannels, iSampleBlockWords);
        remaining -= bytes;
    } while (remaining > 0);
}

MsgPlayable* MsgPlayableSilenceDsd::Allocate()
{
    return static_cast<Allocator<MsgPlayableSilenceDsd>&>(iAllocator).Allocate();
}


// MsgQuit

MsgQuit::MsgQuit(AllocatorBase& aAllocator)
    : Msg(aAllocator)
{
}

Msg* MsgQuit::Process(IMsgProcessor& aProcessor)
{
    return aProcessor.ProcessMsg(this);
}


// MsgQueueBase

MsgQueueBase::MsgQueueBase()
    : iHead(nullptr)
    , iTail(nullptr)
    , iNumMsgs(0)
{
}

MsgQueueBase::~MsgQueueBase()
{
    Msg* head = iHead;
    while (head != nullptr) {
        iHead = head->iNextMsg;
        head->RemoveRef();
        head = iHead;
    }
}

void MsgQueueBase::DoEnqueue(Msg* aMsg)
{
    ASSERT(aMsg != nullptr);
    CheckMsgNotQueued(aMsg); // duplicate checking
    if (iHead == nullptr) {
        iHead = aMsg;
    }
    else {
        iTail->iNextMsg = aMsg;
    }
    iTail = aMsg;
    iNumMsgs++;
}

Msg* MsgQueueBase::DoDequeue()
{
    ASSERT(iHead != nullptr);
    Msg* head = iHead;
    iHead = iHead->iNextMsg;
    head->iNextMsg = nullptr; // not strictly necessary but might make debugging simpler
    if (iHead == nullptr) {
        iTail = nullptr;
    }
    iNumMsgs--;
    return head;
}

void MsgQueueBase::DoEnqueueAtHead(Msg* aMsg)
{
    ASSERT(aMsg != nullptr);
    CheckMsgNotQueued(aMsg); // duplicate checking
    aMsg->iNextMsg = iHead;
    iHead = aMsg;
    if (iTail == nullptr) {
        iTail = aMsg;
    }
    iNumMsgs++;
}

TBool MsgQueueBase::IsEmpty() const
{
    const TBool empty = (iHead == nullptr);
    return empty;
}

void MsgQueueBase::DoClear()
{
    while (iHead != nullptr) {
        Msg* msg = DoDequeue();
        msg->RemoveRef();
    }
}

TUint MsgQueueBase::NumMsgs() const
{
    return iNumMsgs;
}

void MsgQueueBase::CheckMsgNotQueued(Msg* aMsg) const
{
    ASSERT(aMsg != iTail);
    ASSERT(aMsg != iHead);
    ASSERT(aMsg->iNextMsg == nullptr);
#ifdef DEFINE_DEBUG // iterate over queue, comparing aMsg to all msg pointers
    TUint count = 0;
    for (Msg* msg = iHead; msg != nullptr; msg = msg->iNextMsg) {
        ASSERT(aMsg != msg);
        count++;
    }
    if (count != iNumMsgs) {
        Log::Print("MsgQueueBase::CheckMsgNotQueued - iNumMsgs=%u, found %u\n",
                   iNumMsgs, count);
        ASSERTS();
    }
#endif
}


// MsgQueue

MsgQueue::MsgQueue()
    : iLock("MSGQ")
    , iSem("MSGQ", 0)
{
}

void MsgQueue::Enqueue(Msg* aMsg)
{
    AutoMutex _(iLock);
    DoEnqueue(aMsg);
    iSem.Signal();
}

Msg* MsgQueue::Dequeue()
{
    iSem.Wait();
    AutoMutex _(iLock);
    return MsgQueueBase::DoDequeue();
}

void MsgQueue::EnqueueAtHead(Msg* aMsg)
{
    AutoMutex _(iLock);
    DoEnqueueAtHead(aMsg);
    iSem.Signal();
}

TBool MsgQueue::IsEmpty() const
{
    AutoMutex _(iLock);
    return MsgQueueBase::IsEmpty();
}

void MsgQueue::Clear()
{
    AutoMutex _(iLock);
    DoClear();
    (void)iSem.Clear();
}

TUint MsgQueue::NumMsgs() const
{
    AutoMutex _(iLock);
    return MsgQueueBase::NumMsgs();
}


// MsgReservoir

MsgReservoir::MsgReservoir()
    : iLockEncoded("MSGR")
    , iEncodedBytes(0)
    , iJiffies(0)
    , iTrackCount(0)
    , iDelayCount(0)
    , iEncodedStreamCount(0)
    , iMetaTextCount(0)
    , iDecodedStreamCount(0)
    , iEncodedAudioCount(0)
    , iDecodedAudioCount(0)
{
    ASSERT(iJiffies.is_lock_free());
    ASSERT(iTrackCount.is_lock_free());
    ASSERT(iDelayCount.is_lock_free());
    ASSERT(iEncodedStreamCount.is_lock_free());
    ASSERT(iMetaTextCount.is_lock_free());
    ASSERT(iDecodedStreamCount.is_lock_free());
    ASSERT(iDecodedAudioCount.is_lock_free());
}

MsgReservoir::~MsgReservoir()
{
}

void MsgReservoir::DoEnqueue(Msg* aMsg)
{
    ASSERT(aMsg != nullptr);
    ProcessorQueueIn procIn(*this);
    Msg* msg = aMsg->Process(procIn);
    iQueue.Enqueue(msg);
}

Msg* MsgReservoir::DoDequeue(TBool aAllowNull)
{
    Msg* msg;
    do {
        msg = iQueue.Dequeue();
        ProcessorQueueOut procOut(*this);
        msg = msg->Process(procOut);
    } while (!aAllowNull && msg == nullptr);
    return msg;
}

void MsgReservoir::EnqueueAtHead(Msg* aMsg)
{
    ProcessorEnqueue proc(*this);
    Msg* msg = aMsg->Process(proc);
    iQueue.EnqueueAtHead(msg);
}

TUint MsgReservoir::Jiffies() const
{
    return iJiffies;
}

TUint MsgReservoir::EncodedBytes() const
{
    AutoMutex _(iLockEncoded);
    return iEncodedBytes;
}

TBool MsgReservoir::IsEmpty() const
{
    return iQueue.IsEmpty();
}

TUint MsgReservoir::TrackCount() const
{
    return iTrackCount;
}

TUint MsgReservoir::DelayCount() const
{
    return iDelayCount;
}

TUint MsgReservoir::EncodedStreamCount() const
{
    return iEncodedStreamCount;
}

TUint MsgReservoir::MetaTextCount() const
{
    return iMetaTextCount;
}

TUint MsgReservoir::DecodedStreamCount() const
{
    return iDecodedStreamCount;
}

TUint MsgReservoir::EncodedAudioCount() const
{
    AutoMutex _(iLockEncoded);
    return iEncodedAudioCount;
}

TUint MsgReservoir::DecodedAudioCount() const
{
    return iDecodedAudioCount;
}

TUint MsgReservoir::NumMsgs() const
{
    return iQueue.NumMsgs();
}

void MsgReservoir::ProcessMsgIn(MsgMode* /*aMsg*/)              { }
void MsgReservoir::ProcessMsgIn(MsgTrack* /*aMsg*/)             { }
void MsgReservoir::ProcessMsgIn(MsgDrain* /*aMsg*/)             { }
void MsgReservoir::ProcessMsgIn(MsgDelay* /*aMsg*/)             { }
void MsgReservoir::ProcessMsgIn(MsgEncodedStream* /*aMsg*/)     { }
void MsgReservoir::ProcessMsgIn(MsgStreamSegment* /*aMsg*/)     { }
void MsgReservoir::ProcessMsgIn(MsgAudioEncoded* /*aMsg*/)      { }
void MsgReservoir::ProcessMsgIn(MsgMetaText* /*aMsg*/)          { }
void MsgReservoir::ProcessMsgIn(MsgStreamInterrupted* /*aMsg*/) { }
void MsgReservoir::ProcessMsgIn(MsgHalt* /*aMsg*/)              { }
void MsgReservoir::ProcessMsgIn(MsgFlush* /*aMsg*/)             { }
void MsgReservoir::ProcessMsgIn(MsgWait* /*aMsg*/)              { }
void MsgReservoir::ProcessMsgIn(MsgDecodedStream* /*aMsg*/)     { }
void MsgReservoir::ProcessMsgIn(MsgAudioPcm* /*aMsg*/)          { }
void MsgReservoir::ProcessMsgIn(MsgAudioDsd* /*aMsg*/)          { }
void MsgReservoir::ProcessMsgIn(MsgSilence* /*aMsg*/)           { }
void MsgReservoir::ProcessMsgIn(MsgQuit* /*aMsg*/)              { }

Msg* MsgReservoir::ProcessMsgOut(MsgMode* aMsg)                 { return aMsg; }
Msg* MsgReservoir::ProcessMsgOut(MsgTrack* aMsg)                { return aMsg; }
Msg* MsgReservoir::ProcessMsgOut(MsgDrain* aMsg)                { return aMsg; }
Msg* MsgReservoir::ProcessMsgOut(MsgDelay* aMsg)                { return aMsg; }
Msg* MsgReservoir::ProcessMsgOut(MsgEncodedStream* aMsg)        { return aMsg; }
Msg* MsgReservoir::ProcessMsgOut(MsgStreamSegment* aMsg)        { return aMsg; }
Msg* MsgReservoir::ProcessMsgOut(MsgAudioEncoded* aMsg)         { return aMsg; }
Msg* MsgReservoir::ProcessMsgOut(MsgMetaText* aMsg)             { return aMsg; }
Msg* MsgReservoir::ProcessMsgOut(MsgStreamInterrupted* aMsg)    { return aMsg; }
Msg* MsgReservoir::ProcessMsgOut(MsgHalt* aMsg)                 { return aMsg; }
Msg* MsgReservoir::ProcessMsgOut(MsgFlush* aMsg)                { return aMsg; }
Msg* MsgReservoir::ProcessMsgOut(MsgWait* aMsg)                 { return aMsg; }
Msg* MsgReservoir::ProcessMsgOut(MsgDecodedStream* aMsg)        { return aMsg; }
Msg* MsgReservoir::ProcessMsgOut(MsgAudioPcm* aMsg)             { return aMsg; }
Msg* MsgReservoir::ProcessMsgOut(MsgAudioDsd* aMsg)             { return aMsg; }
Msg* MsgReservoir::ProcessMsgOut(MsgSilence* aMsg)              { return aMsg; }
Msg* MsgReservoir::ProcessMsgOut(MsgQuit* aMsg)                 { return aMsg; }


// MsgReservoir::ProcessorEnqueue

MsgReservoir::ProcessorEnqueue::ProcessorEnqueue(MsgReservoir& aQueue)
    : iQueue(aQueue)
{
}

Msg* MsgReservoir::ProcessorEnqueue::ProcessMsg(MsgMode* aMsg)               { return aMsg; }

Msg* MsgReservoir::ProcessorEnqueue::ProcessMsg(MsgTrack* aMsg)
{
    iQueue.iTrackCount++;
    return aMsg;
}

Msg* MsgReservoir::ProcessorEnqueue::ProcessMsg(MsgDrain* aMsg)              { return aMsg; }
Msg* MsgReservoir::ProcessorEnqueue::ProcessMsg(MsgDelay* aMsg)
{
    iQueue.iDelayCount++;
    return aMsg;
}

Msg* MsgReservoir::ProcessorEnqueue::ProcessMsg(MsgEncodedStream* aMsg)
{
    iQueue.iEncodedStreamCount++;
    return aMsg;
}

Msg* MsgReservoir::ProcessorEnqueue::ProcessMsg(MsgStreamSegment* aMsg)
{
    return aMsg;
}

Msg* MsgReservoir::ProcessorEnqueue::ProcessMsg(MsgAudioEncoded* aMsg)
{
    AutoMutex _(iQueue.iLockEncoded);
    iQueue.iEncodedAudioCount++;
    iQueue.iEncodedBytes += aMsg->Bytes();
    return aMsg;
}

Msg* MsgReservoir::ProcessorEnqueue::ProcessMsg(MsgMetaText* aMsg)
{
    iQueue.iMetaTextCount++;
    return aMsg;
}

Msg* MsgReservoir::ProcessorEnqueue::ProcessMsg(MsgStreamInterrupted* aMsg)  { return aMsg; }
Msg* MsgReservoir::ProcessorEnqueue::ProcessMsg(MsgHalt* aMsg)               { return aMsg; }
Msg* MsgReservoir::ProcessorEnqueue::ProcessMsg(MsgFlush* aMsg)              { return aMsg; }
Msg* MsgReservoir::ProcessorEnqueue::ProcessMsg(MsgWait* aMsg)               { return aMsg; }

Msg* MsgReservoir::ProcessorEnqueue::ProcessMsg(MsgDecodedStream* aMsg)
{
    iQueue.iDecodedStreamCount++;
    return aMsg;
}

Msg* MsgReservoir::ProcessorEnqueue::ProcessMsg(MsgAudioPcm* aMsg)
{
    ProcessAudio(aMsg);
    return aMsg;
}

Msg* MsgReservoir::ProcessorEnqueue::ProcessMsg(MsgAudioDsd* aMsg)
{
    ProcessAudio(aMsg);
    return aMsg;
}

void MsgReservoir::ProcessorEnqueue::ProcessAudio(MsgAudioDecoded* aMsg)
{
    iQueue.iDecodedAudioCount++;
    iQueue.iJiffies += aMsg->Jiffies();
}

Msg* MsgReservoir::ProcessorEnqueue::ProcessMsg(MsgSilence* aMsg)
{
    iQueue.iDecodedAudioCount++;
    iQueue.iJiffies += aMsg->Jiffies();
    return aMsg;
}

Msg* MsgReservoir::ProcessorEnqueue::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* MsgReservoir::ProcessorEnqueue::ProcessMsg(MsgQuit* aMsg)               { return aMsg; }


// MsgReservoir::ProcessorQueueIn

MsgReservoir::ProcessorQueueIn::ProcessorQueueIn(MsgReservoir& aQueue)
    : ProcessorEnqueue(aQueue)
{
}

Msg* MsgReservoir::ProcessorQueueIn::ProcessMsg(MsgMode* aMsg)
{
    iQueue.ProcessMsgIn(aMsg);
    return aMsg;
}

Msg* MsgReservoir::ProcessorQueueIn::ProcessMsg(MsgTrack* aMsg)
{
    (void)ProcessorEnqueue::ProcessMsg(aMsg);
    iQueue.ProcessMsgIn(aMsg);
    return aMsg;
}

Msg* MsgReservoir::ProcessorQueueIn::ProcessMsg(MsgDrain* aMsg)
{
    iQueue.ProcessMsgIn(aMsg);
    return aMsg;
}

Msg* MsgReservoir::ProcessorQueueIn::ProcessMsg(MsgDelay* aMsg)
{
    (void)ProcessorEnqueue::ProcessMsg(aMsg);
    iQueue.ProcessMsgIn(aMsg);
    return aMsg;
}

Msg* MsgReservoir::ProcessorQueueIn::ProcessMsg(MsgEncodedStream* aMsg)
{
    (void)ProcessorEnqueue::ProcessMsg(aMsg);
    iQueue.ProcessMsgIn(aMsg);
    return aMsg;
}

Msg* MsgReservoir::ProcessorQueueIn::ProcessMsg(MsgStreamSegment* aMsg)
{
    (void)ProcessorEnqueue::ProcessMsg(aMsg);
    iQueue.ProcessMsgIn(aMsg);
    return aMsg;
}

Msg* MsgReservoir::ProcessorQueueIn::ProcessMsg(MsgAudioEncoded* aMsg)
{
    (void)ProcessorEnqueue::ProcessMsg(aMsg);
    iQueue.ProcessMsgIn(aMsg);
    return aMsg;
}

Msg* MsgReservoir::ProcessorQueueIn::ProcessMsg(MsgMetaText* aMsg)
{
    (void)ProcessorEnqueue::ProcessMsg(aMsg);
    iQueue.ProcessMsgIn(aMsg);
    return aMsg;
}

Msg* MsgReservoir::ProcessorQueueIn::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    iQueue.ProcessMsgIn(aMsg);
    return aMsg;
}

Msg* MsgReservoir::ProcessorQueueIn::ProcessMsg(MsgHalt* aMsg)
{
    iQueue.ProcessMsgIn(aMsg);
    return aMsg;
}

Msg* MsgReservoir::ProcessorQueueIn::ProcessMsg(MsgFlush* aMsg)
{
    iQueue.ProcessMsgIn(aMsg);
    return aMsg;
}

Msg* MsgReservoir::ProcessorQueueIn::ProcessMsg(MsgWait* aMsg)
{
    iQueue.ProcessMsgIn(aMsg);
    return aMsg;
}

Msg* MsgReservoir::ProcessorQueueIn::ProcessMsg(MsgDecodedStream* aMsg)
{
    (void)ProcessorEnqueue::ProcessMsg(aMsg);
    iQueue.ProcessMsgIn(aMsg);
    return aMsg;
}

Msg* MsgReservoir::ProcessorQueueIn::ProcessMsg(MsgAudioPcm* aMsg)
{
    (void)ProcessorEnqueue::ProcessMsg(aMsg);
    iQueue.ProcessMsgIn(aMsg);
    return aMsg;
}

Msg* MsgReservoir::ProcessorQueueIn::ProcessMsg(MsgAudioDsd* aMsg)
{
    (void)ProcessorEnqueue::ProcessMsg(aMsg);
    iQueue.ProcessMsgIn(aMsg);
    return aMsg;
}

Msg* MsgReservoir::ProcessorQueueIn::ProcessMsg(MsgSilence* aMsg)
{
    (void)ProcessorEnqueue::ProcessMsg(aMsg);
    iQueue.ProcessMsgIn(aMsg);
    return aMsg;
}

Msg* MsgReservoir::ProcessorQueueIn::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* MsgReservoir::ProcessorQueueIn::ProcessMsg(MsgQuit* aMsg)
{
    iQueue.ProcessMsgIn(aMsg);
    return aMsg;
}


// MsgReservoir::ProcessorQueueOut

MsgReservoir::ProcessorQueueOut::ProcessorQueueOut(MsgReservoir& aQueue)
    : iQueue(aQueue)
{
}

Msg* MsgReservoir::ProcessorQueueOut::ProcessMsg(MsgMode* aMsg)
{
    return iQueue.ProcessMsgOut(aMsg);
}

Msg* MsgReservoir::ProcessorQueueOut::ProcessMsg(MsgTrack* aMsg)
{
    iQueue.iTrackCount--;
    return iQueue.ProcessMsgOut(aMsg);
}

Msg* MsgReservoir::ProcessorQueueOut::ProcessMsg(MsgDrain* aMsg)
{
    return iQueue.ProcessMsgOut(aMsg);
}

Msg* MsgReservoir::ProcessorQueueOut::ProcessMsg(MsgDelay* aMsg)
{
    iQueue.iDelayCount--;
    return iQueue.ProcessMsgOut(aMsg);
}

Msg* MsgReservoir::ProcessorQueueOut::ProcessMsg(MsgEncodedStream* aMsg)
{
    iQueue.iEncodedStreamCount--;
    return iQueue.ProcessMsgOut(aMsg);
}

Msg* MsgReservoir::ProcessorQueueOut::ProcessMsg(MsgStreamSegment* aMsg)
{
    return iQueue.ProcessMsgOut(aMsg);
}

Msg* MsgReservoir::ProcessorQueueOut::ProcessMsg(MsgAudioEncoded* aMsg)
{
    {
        AutoMutex _(iQueue.iLockEncoded);
        iQueue.iEncodedAudioCount--;
        iQueue.iEncodedBytes -= aMsg->Bytes();
    }
    return iQueue.ProcessMsgOut(aMsg);
}

Msg* MsgReservoir::ProcessorQueueOut::ProcessMsg(MsgMetaText* aMsg)
{
    iQueue.iMetaTextCount--;
    return iQueue.ProcessMsgOut(aMsg);
}

Msg* MsgReservoir::ProcessorQueueOut::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    return iQueue.ProcessMsgOut(aMsg);
}

Msg* MsgReservoir::ProcessorQueueOut::ProcessMsg(MsgHalt* aMsg)
{
    return iQueue.ProcessMsgOut(aMsg);
}

Msg* MsgReservoir::ProcessorQueueOut::ProcessMsg(MsgFlush* aMsg)
{
    return iQueue.ProcessMsgOut(aMsg);
}

Msg* MsgReservoir::ProcessorQueueOut::ProcessMsg(MsgWait* aMsg)
{
    return iQueue.ProcessMsgOut(aMsg);
}

Msg* MsgReservoir::ProcessorQueueOut::ProcessMsg(MsgDecodedStream* aMsg)
{
    iQueue.iDecodedStreamCount--;
    return iQueue.ProcessMsgOut(aMsg);
}

Msg* MsgReservoir::ProcessorQueueOut::ProcessMsg(MsgAudioPcm* aMsg)
{
    ProcessAudio(aMsg);
    return iQueue.ProcessMsgOut(aMsg);
}

Msg* MsgReservoir::ProcessorQueueOut::ProcessMsg(MsgAudioDsd* aMsg)
{
    ProcessAudio(aMsg);
    return iQueue.ProcessMsgOut(aMsg);
}

void MsgReservoir::ProcessorQueueOut::ProcessAudio(MsgAudioDecoded* aMsg)
{
    iQueue.iDecodedAudioCount--;
    iQueue.iJiffies -= aMsg->Jiffies();
}

Msg* MsgReservoir::ProcessorQueueOut::ProcessMsg(MsgSilence* aMsg)
{
    iQueue.iDecodedAudioCount--;
    iQueue.iJiffies -= aMsg->Jiffies();
    return iQueue.ProcessMsgOut(aMsg);
}

Msg* MsgReservoir::ProcessorQueueOut::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* MsgReservoir::ProcessorQueueOut::ProcessMsg(MsgQuit* aMsg)
{
    return iQueue.ProcessMsgOut(aMsg);
}


// PipelineElement

PipelineElement::PipelineElement(TUint aSupportedTypes)
    : iSupportedTypes(aSupportedTypes)
{
}

PipelineElement::~PipelineElement()
{
}

inline void PipelineElement::CheckSupported(MsgType aType) const
{
    ASSERT((iSupportedTypes & aType) == (TUint)aType);
}

Msg* PipelineElement::ProcessMsg(MsgMode* aMsg)
{
    CheckSupported(eMode);
    return aMsg;
}

Msg* PipelineElement::ProcessMsg(MsgTrack* aMsg)
{
    CheckSupported(eTrack);
    return aMsg;
}

Msg* PipelineElement::ProcessMsg(MsgDrain* aMsg)
{
    CheckSupported(eDrain);
    return aMsg;
}

Msg* PipelineElement::ProcessMsg(MsgDelay* aMsg)
{
    CheckSupported(eDelay);
    return aMsg;
}

Msg* PipelineElement::ProcessMsg(MsgEncodedStream* aMsg)
{
    CheckSupported(eEncodedStream);
    return aMsg;
}

Msg* PipelineElement::ProcessMsg(MsgStreamSegment* aMsg)
{
    CheckSupported(eStreamSegment);
    return aMsg;
}

Msg* PipelineElement::ProcessMsg(MsgAudioEncoded* aMsg)
{
    CheckSupported(eAudioEncoded);
    return aMsg;
}

Msg* PipelineElement::ProcessMsg(MsgMetaText* aMsg)
{
    CheckSupported(eMetatext);
    return aMsg;
}

Msg* PipelineElement::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    CheckSupported(eStreamInterrupted);
    return aMsg;
}

Msg* PipelineElement::ProcessMsg(MsgHalt* aMsg)
{
    CheckSupported(eHalt);
    return aMsg;
}

Msg* PipelineElement::ProcessMsg(MsgFlush* aMsg)
{
    CheckSupported(eFlush);
    return aMsg;
}

Msg* PipelineElement::ProcessMsg(MsgWait* aMsg)
{
    CheckSupported(eWait);
    return aMsg;
}

Msg* PipelineElement::ProcessMsg(MsgDecodedStream* aMsg)
{
    CheckSupported(eDecodedStream);
    return aMsg;
}

Msg* PipelineElement::ProcessMsg(MsgAudioPcm* aMsg)
{
    CheckSupported(eAudioPcm);
    return aMsg;
}

Msg* PipelineElement::ProcessMsg(MsgAudioDsd* aMsg)
{
    CheckSupported(eAudioDsd);
    return aMsg;
}

Msg* PipelineElement::ProcessMsg(MsgSilence* aMsg)
{
    CheckSupported(eSilence);
    return aMsg;
}

Msg* PipelineElement::ProcessMsg(MsgPlayable* aMsg)
{
    CheckSupported(ePlayable);
    return aMsg;
}

Msg* PipelineElement::ProcessMsg(MsgQuit* aMsg)
{
    CheckSupported(eQuit);
    return aMsg;
}


// AutoAllocatedRef

AutoAllocatedRef::AutoAllocatedRef(Allocated* aAllocated)
    : iAllocated(aAllocated)
{
}

AutoAllocatedRef::~AutoAllocatedRef()
{
    iAllocated->RemoveRef();
}


// ISupply

const TUint ISupply::kMaxDrainMs = 5000;


// TrackFactory

TrackFactory::TrackFactory(IInfoAggregator& aInfoAggregator, TUint aTrackCount)
    : iAllocatorTrack("Track", aTrackCount, aInfoAggregator)
    , iLock("TRKF")
    , iNextId(1)
{
}

Track* TrackFactory::CreateTrack(const Brx& aUri, const Brx& aMetaData)
{
    Track* track = iAllocatorTrack.Allocate();
    iLock.Wait();
    TUint id = iNextId++;
    iLock.Signal();
    track->Initialise(aUri, aMetaData, id);
    return track;
}

Track* TrackFactory::CreateNullTrack()
{
    auto track = iAllocatorTrack.Allocate();
    track->Initialise(Brx::Empty(), Brx::Empty(), Track::kIdNone);
    return track;
}


// MsgFactory

MsgFactory::MsgFactory(IInfoAggregator& aInfoAggregator, const MsgFactoryInitParams& aInitParams)
    : iAllocatorMsgMode("MsgMode", aInitParams.iMsgModeCount, aInfoAggregator)
    , iAllocatorMsgTrack("MsgTrack", aInitParams.iMsgTrackCount, aInfoAggregator)
    , iAllocatorMsgDrain("MsgDrain", aInitParams.iMsgDrainCount, aInfoAggregator)
    , iDrainId(0)
    , iAllocatorMsgDelay("MsgDelay", aInitParams.iMsgDelayCount, aInfoAggregator)
    , iAllocatorMsgEncodedStream("MsgEncodedStream", aInitParams.iMsgEncodedStreamCount, aInfoAggregator)
    , iAllocatorMsgStreamSegment("MsgStreamSegment", aInitParams.iMsgStreamSegmentCount, aInfoAggregator)
    , iAllocatorAudioData("AudioData", aInitParams.iEncodedAudioCount + aInitParams.iDecodedAudioCount, aInfoAggregator)
    , iAllocatorMsgAudioEncoded("MsgAudioEncoded", aInitParams.iMsgAudioEncodedCount, aInfoAggregator)
    , iAllocatorMsgMetaText("MsgMetaText", aInitParams.iMsgMetaTextCount, aInfoAggregator)
    , iAllocatorMsgStreamInterrupted("MsgStreamInterrupted", aInitParams.iMsgStreamInterruptedCount, aInfoAggregator)
    , iAllocatorMsgHalt("MsgHalt", aInitParams.iMsgHaltCount, aInfoAggregator)
    , iAllocatorMsgFlush("MsgFlush", aInitParams.iMsgFlushCount, aInfoAggregator)
    , iAllocatorMsgWait("MsgWait", aInitParams.iMsgWaitCount, aInfoAggregator)
    , iAllocatorMsgDecodedStream("MsgDecodedStream", aInitParams.iMsgDecodedStreamCount, aInfoAggregator)
    , iAllocatorMsgAudioPcm("MsgAudioPcm", aInitParams.iMsgAudioPcmCount, aInfoAggregator)
    , iAllocatorMsgAudioDsd("MsgAudioDsd", aInitParams.iMsgAudioDsdCount, aInfoAggregator)
    , iAllocatorMsgSilence("MsgSilence", aInitParams.iMsgSilenceCount, aInfoAggregator)
    , iAllocatorMsgPlayablePcm("MsgPlayablePcm", aInitParams.iMsgPlayablePcmCount, aInfoAggregator)
    , iAllocatorMsgPlayableDsd("MsgPlayableDsd", aInitParams.iMsgPlayableDsdCount, aInfoAggregator)
    , iAllocatorMsgPlayableSilence("MsgPlayableSilence", aInitParams.iMsgPlayableSilenceCount, aInfoAggregator)
    , iAllocatorMsgPlayableSilenceDsd("MsgPlayableSilenceDsd", aInitParams.iMsgPlayableSilenceCount, aInfoAggregator)
    , iAllocatorMsgQuit("MsgQuit", aInitParams.iMsgQuitCount, aInfoAggregator)
{
}

MsgMode* MsgFactory::CreateMsgMode(const Brx& aMode, const ModeInfo& aInfo,
                                   Optional<IClockPuller> aClockPuller,
                                   const ModeTransportControls& aTransportControls)
{
    MsgMode* msg = iAllocatorMsgMode.Allocate();
    msg->Initialise(aMode, aInfo, aClockPuller, aTransportControls);
    return msg;
}

MsgMode* MsgFactory::CreateMsgMode(const Brx& aMode)
{
    ModeInfo info;
    ModeTransportControls transportControls;
    return CreateMsgMode(aMode, info, nullptr, transportControls);
}

MsgTrack* MsgFactory::CreateMsgTrack(Media::Track& aTrack, TBool aStartOfStream)
{
    MsgTrack* msg = iAllocatorMsgTrack.Allocate();
    msg->Initialise(aTrack, aStartOfStream);
    return msg;
}

MsgDrain* MsgFactory::CreateMsgDrain(Functor aCallback)
{
    MsgDrain* msg = iAllocatorMsgDrain.Allocate();
    msg->Initialise(iDrainId++, aCallback);
    return msg;
}

MsgDelay* MsgFactory::CreateMsgDelay(TUint aTotalJiffies)
{
    MsgDelay* msg = iAllocatorMsgDelay.Allocate();
    msg->Initialise(aTotalJiffies);
    return msg;
}

MsgDelay* MsgFactory::CreateMsgDelay(TUint aRemainingJiffies, TUint aTotalJiffies)
{
    MsgDelay* msg = iAllocatorMsgDelay.Allocate();
    msg->Initialise(aRemainingJiffies, aTotalJiffies);
    return msg;
}

MsgEncodedStream* MsgFactory::CreateMsgEncodedStream(const Brx& aUri, const Brx& aMetaText, TUint64 aTotalBytes, TUint64 aStartPos, TUint aStreamId, TBool aSeekable, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler* aStreamHandler, TUint aSeekPosMs)
{
    const auto seek = aSeekable ? SeekCapability::SeekCache : SeekCapability::None;
    MsgEncodedStream* msg = iAllocatorMsgEncodedStream.Allocate();
    msg->Initialise(aUri, aMetaText, aTotalBytes, aStartPos, aStreamId, seek, aLive, aMultiroom, aStreamHandler, aSeekPosMs);
    return msg;
}

MsgEncodedStream* MsgFactory::CreateMsgEncodedStream(const Brx& aUri, const Brx& aMetaText, TUint64 aTotalBytes, TUint64 aStartPos, TUint aStreamId, Media::SeekCapability aSeek, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler* aStreamHandler, TUint aSeekPosMs)
{
    MsgEncodedStream* msg = iAllocatorMsgEncodedStream.Allocate();
    msg->Initialise(aUri, aMetaText, aTotalBytes, aStartPos, aStreamId, aSeek, aLive, aMultiroom, aStreamHandler, aSeekPosMs);
    return msg;
}

MsgEncodedStream* MsgFactory::CreateMsgEncodedStream(const Brx& aUri, const Brx& aMetaText, TUint64 aTotalBytes, TUint64 aStartPos, TUint aStreamId, TBool aSeekable, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler* aStreamHandler, const PcmStreamInfo& aPcmStream)
{
    const auto seek = aSeekable ? SeekCapability::SeekCache : SeekCapability::None;
    MsgEncodedStream* msg = iAllocatorMsgEncodedStream.Allocate();
    msg->Initialise(aUri, aMetaText, aTotalBytes, aStartPos, aStreamId, seek, aLive, aMultiroom, aStreamHandler, aPcmStream);
    return msg;
}

MsgEncodedStream* MsgFactory::CreateMsgEncodedStream(const Brx& aUri, const Brx& aMetaText, TUint64 aTotalBytes, TUint64 aStartPos, TUint aStreamId, TBool aSeekable, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler* aStreamHandler, const PcmStreamInfo& aPcmStream, RampType aRamp)
{
    const auto seek = aSeekable ? SeekCapability::SeekCache : SeekCapability::None;
    MsgEncodedStream* msg = iAllocatorMsgEncodedStream.Allocate();
    msg->Initialise(aUri, aMetaText, aTotalBytes, aStartPos, aStreamId, seek, aLive, aMultiroom, aStreamHandler, aPcmStream, aRamp);
    return msg;
}

MsgEncodedStream* MsgFactory::CreateMsgEncodedStream(const Brx& aUri, const Brx& aMetaText, TUint64 aTotalBytes, TUint64 aStartPos, TUint aStreamId, TBool aSeekable, IStreamHandler* aStreamHandler, const DsdStreamInfo& aDsdStream)
{
    const auto seek = aSeekable ? SeekCapability::SeekCache : SeekCapability::None;
    MsgEncodedStream* msg = iAllocatorMsgEncodedStream.Allocate();
    msg->Initialise(aUri, aMetaText, aTotalBytes, aStartPos, aStreamId, seek, false/*live*/,
        Multiroom::Forbidden, aStreamHandler, aDsdStream);
    return msg;
}

MsgEncodedStream* MsgFactory::CreateMsgEncodedStream(MsgEncodedStream* aMsg, IStreamHandler* aStreamHandler)
{
    MsgEncodedStream* msg = iAllocatorMsgEncodedStream.Allocate();
    if (aMsg->StreamFormat() == MsgEncodedStream::Format::Pcm) {
        msg->Initialise(aMsg->Uri(), aMsg->MetaText(), aMsg->TotalBytes(), aMsg->StartPos(), aMsg->StreamId(), aMsg->SeekCapability(), aMsg->Live(), aMsg->Multiroom(), aStreamHandler, aMsg->PcmStream(), aMsg->Ramp());
    }
    else if (aMsg->StreamFormat() == MsgEncodedStream::Format::Dsd) {
        msg->Initialise(aMsg->Uri(), aMsg->MetaText(), aMsg->TotalBytes(), aMsg->StartPos(), aMsg->StreamId(), aMsg->SeekCapability(), aMsg->Live(), aMsg->Multiroom(), aStreamHandler, aMsg->DsdStream());
    }
    else {
        msg->Initialise(aMsg->Uri(), aMsg->MetaText(), aMsg->TotalBytes(), aMsg->StartPos(), aMsg->StreamId(), aMsg->SeekCapability(), aMsg->Live(), aMsg->Multiroom(), aStreamHandler, aMsg->SeekPosMs());
    }
    return msg;
}

MsgStreamSegment* MsgFactory::CreateMsgStreamSegment(const Brx& aId)
{
    MsgStreamSegment* msg = iAllocatorMsgStreamSegment.Allocate();
    msg->Initialise(aId);
    return msg;
}

MsgAudioEncoded* MsgFactory::CreateMsgAudioEncoded(const Brx& aData)
{
    EncodedAudio* encodedAudio = CreateEncodedAudio(aData);
    MsgAudioEncoded* msg = iAllocatorMsgAudioEncoded.Allocate();
    msg->Initialise(encodedAudio);
    return msg;
}

MsgMetaText* MsgFactory::CreateMsgMetaText(const Brx& aMetaText)
{
    MsgMetaText* msg = iAllocatorMsgMetaText.Allocate();
    msg->Initialise(aMetaText);
    return msg;
}

MsgStreamInterrupted* MsgFactory::CreateMsgStreamInterrupted(TUint aJiffies)
{
    auto msg = iAllocatorMsgStreamInterrupted.Allocate();
    msg->Initialise(aJiffies);
    return msg;
}

MsgHalt* MsgFactory::CreateMsgHalt(TUint aId)
{
    MsgHalt* msg = iAllocatorMsgHalt.Allocate();
    msg->Initialise(aId);
    return msg;
}

MsgHalt* MsgFactory::CreateMsgHalt(TUint aId, Functor aCallback)
{
    MsgHalt* msg = iAllocatorMsgHalt.Allocate();
    msg->Initialise(aId, aCallback);
    return msg;
}

MsgFlush* MsgFactory::CreateMsgFlush(TUint aId)
{
    MsgFlush* flush = iAllocatorMsgFlush.Allocate();
    flush->Initialise(aId);
    return flush;
}

MsgWait* MsgFactory::CreateMsgWait()
{
    return iAllocatorMsgWait.Allocate();
}

MsgDecodedStream* MsgFactory::CreateMsgDecodedStream(TUint aStreamId, TUint aBitRate, TUint aBitDepth, TUint aSampleRate, TUint aNumChannels,
                                                     const Brx& aCodecName, TUint64 aTrackLength, TUint64 aSampleStart,
                                                     TBool aLossless, TBool aSeekable, TBool aLive, TBool aAnalogBypass,
                                                     AudioFormat aFormat, Media::Multiroom aMultiroom, const SpeakerProfile& aProfile,
                                                     IStreamHandler* aStreamHandler, RampType aRamp)
{
    MsgDecodedStream* msg = iAllocatorMsgDecodedStream.Allocate();
    msg->Initialise(aStreamId, aBitRate, aBitDepth, aSampleRate, aNumChannels, aCodecName,
                    aTrackLength, aSampleStart, aLossless, aSeekable, aLive, aAnalogBypass,
                    aFormat, aMultiroom, aProfile, aStreamHandler, aRamp);
    return msg;
}

MsgDecodedStream* MsgFactory::CreateMsgDecodedStream(MsgDecodedStream* aMsg, IStreamHandler* aStreamHandler)
{
    auto stream = aMsg->StreamInfo();
    auto msg = CreateMsgDecodedStream(stream.StreamId(), stream.BitRate(), stream.BitDepth(),
                                      stream.SampleRate(), stream.NumChannels(), stream.CodecName(),
                                      stream.TrackLength(), stream.SampleStart(), stream.Lossless(),
                                      stream.Seekable(), stream.Live(), stream.AnalogBypass(),
                                      stream.Format(), stream.Multiroom(), stream.Profile(), aStreamHandler,
                                      stream.Ramp());
    return msg;
}

MsgAudioPcm* MsgFactory::CreateMsgAudioPcm(const Brx& aData, TUint aChannels, TUint aSampleRate, TUint aBitDepth, AudioDataEndian aEndian, TUint64 aTrackOffset)
{
    DecodedAudio* decodedAudio = CreateDecodedAudio(aData, aBitDepth, aEndian);
    return CreateMsgAudioPcm(decodedAudio, aChannels, aSampleRate, aBitDepth, aTrackOffset);
}

MsgAudioPcm* MsgFactory::CreateMsgAudioPcm(MsgAudioEncoded* aAudio, TUint aChannels, TUint aSampleRate, TUint aBitDepth, TUint64 aTrackOffset)
{
    AudioData* audioData = aAudio->iAudioData;
    audioData->AddRef();
    return CreateMsgAudioPcm(static_cast<DecodedAudio*>(audioData),
                             aChannels, aSampleRate, aBitDepth, aTrackOffset);
}

MsgAudioDsd* MsgFactory::CreateMsgAudioDsd(const Brx& aData, TUint aChannels, TUint aSampleRate, TUint aSampleBlockWords, TUint64 aTrackOffset, TUint aPadBytesPerChunk)
{
    auto decodedAudio = static_cast<DecodedAudio*>(iAllocatorAudioData.Allocate());
    decodedAudio->ConstructDsd(aData);
    return CreateMsgAudioDsd(decodedAudio, aChannels, aSampleRate, aSampleBlockWords, aTrackOffset, aPadBytesPerChunk);
}

MsgAudioDsd* MsgFactory::CreateMsgAudioDsd(MsgAudioEncoded* aAudio, TUint aChannels, TUint aSampleRate, TUint aSampleBlockWords, TUint64 aTrackOffset, TUint aPadBytesPerChunk)
{
    AudioData* audioData = aAudio->iAudioData;
    audioData->AddRef();
    return CreateMsgAudioDsd(static_cast<DecodedAudio*>(audioData), aChannels, aSampleRate, aSampleBlockWords, aTrackOffset, aPadBytesPerChunk);
}

MsgSilence* MsgFactory::CreateMsgSilence(TUint& aSizeJiffies, TUint aSampleRate, TUint aBitDepth, TUint aChannels)
{
    MsgSilence* msg = iAllocatorMsgSilence.Allocate();
    msg->Initialise(aSizeJiffies, aSampleRate, aBitDepth, aChannels, iAllocatorMsgPlayableSilence);
    return msg;
}

MsgSilence* MsgFactory::CreateMsgSilenceDsd(TUint& aSizeJiffies, TUint aSampleRate, TUint aChannels, TUint aSampleBlockWords, TUint aPadBytesPerChunk)
{
    MsgSilence* msg = iAllocatorMsgSilence.Allocate();
    msg->InitialiseDsd(aSizeJiffies, aSampleRate, aChannels, aSampleBlockWords, aPadBytesPerChunk, iAllocatorMsgPlayableSilenceDsd);
    return msg;
}

MsgQuit* MsgFactory::CreateMsgQuit()
{
    return iAllocatorMsgQuit.Allocate();
}

DecodedAudio* MsgFactory::CreateDecodedAudio()
{
    return static_cast<DecodedAudio*>(iAllocatorAudioData.Allocate());
}

EncodedAudio* MsgFactory::CreateEncodedAudio(const Brx& aData)
{
    EncodedAudio* encodedAudio = static_cast<EncodedAudio*>(iAllocatorAudioData.Allocate());
    encodedAudio->Construct(aData);
    return encodedAudio;
}

DecodedAudio* MsgFactory::CreateDecodedAudio(const Brx& aData, TUint aBitDepth, AudioDataEndian aEndian)
{
    DecodedAudio* decodedAudio = static_cast<DecodedAudio*>(iAllocatorAudioData.Allocate());
    decodedAudio->ConstructPcm(aData, aBitDepth, aEndian);
    return decodedAudio;
}

MsgAudioPcm* MsgFactory::CreateMsgAudioPcm(DecodedAudio* aAudioData, TUint aChannels, TUint aSampleRate, TUint aBitDepth, TUint64 aTrackOffset)
{
    MsgAudioPcm* msg = iAllocatorMsgAudioPcm.Allocate();
    try {
        msg->Initialise(aAudioData, aSampleRate, aBitDepth, aChannels, aTrackOffset,
                        iAllocatorMsgPlayablePcm, iAllocatorMsgPlayableSilence);
    }
    catch (AssertionFailed&) { // test code helper
        msg->RemoveRef();
        throw;
    }
    return msg;
}

MsgAudioDsd* MsgFactory::CreateMsgAudioDsd(DecodedAudio* aAudioData, TUint aChannels, TUint aSampleRate, TUint aSampleBlockWords, TUint64 aTrackOffset, TUint aPadBytesPerChunk)
{
    auto audioDsd = iAllocatorMsgAudioDsd.Allocate();
    try {
        audioDsd->Initialise(aAudioData, aSampleRate, aChannels, aSampleBlockWords, aTrackOffset, aPadBytesPerChunk,
                             iAllocatorMsgPlayableDsd, iAllocatorMsgPlayableSilenceDsd);
    }
    catch (AssertionFailed&) { // test code helper
        audioDsd->RemoveRef();
        throw;
    }
    return audioDsd;
}
