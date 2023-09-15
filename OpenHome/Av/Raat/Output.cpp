#include <OpenHome/Av/Raat/Output.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Types.h>
#include <OpenHome/OsWrapper.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/Pipeline/StarterTimed.h>
#include <OpenHome/Media/ClockPuller.h>
#include <OpenHome/Av/Raat/SourceRaat.h>
#include <OpenHome/Av/Raat/Transport.h>

#include <map>
#include <stdlib.h>

#include <raat_plugin_output.h>
#include <rc_status.h>
#include <rc_allocator.h>
#include <jansson.h>
#include <raat_stream.h>

static inline OpenHome::Av::RaatOutput* Output(void *self)
{
    auto ext = reinterpret_cast<OpenHome::Av::RaatOutputPluginExt*>(self);
    return ext->iSelf;
}


extern "C"
RC__Status Raat_Output_Get_Info(void *self, json_t **out_info)
{
    Output(self)->GetInfo(out_info);
    return RC__STATUS_SUCCESS;
}

extern "C"
RC__Status Raat_Output_Get_Supported_Formats(void *self, RC__Allocator *alloc, size_t *out_nformats, RAAT__StreamFormat **out_formats)
{
    LOG(kMedia, "Raat_Output_Get_Supported_Formats\n");
    Output(self)->GetSupportedFormats(alloc, out_nformats, out_formats);
    return RC__STATUS_SUCCESS;
}

extern "C"
void Raat_Output_Setup(
    void *self,
    RAAT__StreamFormat *format,
    RAAT__OutputSetupCallback cb_setup, void *cb_setup_userdata,
    RAAT__OutputLostCallback cb_lost, void *cb_lost_userdata)
{
    Output(self)->SetupStream(format, cb_setup, cb_setup_userdata, cb_lost, cb_lost_userdata);
}

extern "C"
RC__Status Raat_Output_Teardown(void *self, int token)
{
    auto ret = Output(self)->TeardownStream(token);
    return ret;
}

extern "C"
RC__Status Raat_Output_Start(void *self, int token, int64_t walltime, int64_t streamtime, RAAT__Stream *stream)
{
    auto ret = Output(self)->StartStream(token, walltime, streamtime, stream);
    return ret;
}

extern "C"
RC__Status Raat_Output_Get_Local_Time(void *self, int token, int64_t *out_time)
{
    auto ret = Output(self)->GetLocalTime(token, out_time);
    return ret;
}

extern "C"
RC__Status Raat_Output_Set_Remote_Time(void *self, int token, int64_t clock_offset, bool new_source)
{
    auto ret = Output(self)->SetRemoteTime(token, clock_offset, new_source);
    return ret;
}

extern "C"
RC__Status Raat_Output_Stop(void *self, int token)
{
    auto ret = Output(self)->TryStop(token);
    return ret;
}

extern "C"
RC__Status Raat_Output_Force_Teardown(void *self, json_t* /*reason*/)
{
    auto ret = Output(self)->Stop();
    return ret;
}

extern "C"
RC__Status Raat_Output_Add_Message_Listener(void *self, RAAT__OutputMessageCallback cb, void *cb_userdata)
{
    auto ret = Output(self)->AddListener(cb, cb_userdata);
    return ret;
}

extern "C"
RC__Status Raat_Output_Remove_Message_Listener(void *self, RAAT__OutputMessageCallback cb, void *cb_userdata)
{
    Output(self)->RemoveListener(cb, cb_userdata);
    return RC__STATUS_SUCCESS;
}

extern "C"
RC__Status Raat_Output_Get_Output_Delay(void *self, int token, int64_t *out_delay)
{
    Output(self)->GetDelay(token, out_delay);
    return RC__STATUS_SUCCESS;
}



using namespace OpenHome;
using namespace OpenHome::Av;


// RaatUri

const Brn RaatUri::kKeyFormat("fmt");
const Brn RaatUri::kKeySampleRate("sr");
const Brn RaatUri::kKeyBitDepth("bd");
const Brn RaatUri::kKeyNumChannels("ch");
const Brn RaatUri::kKeySampleStart("ss");

const Brn RaatUri::kScheme("raat");
const Brn RaatUri::kFormatPcm("pcm");
const Brn RaatUri::kFormatDsd("dsd");

void RaatUri::Set(
    Media::AudioFormat aFormat,
    TUint aSampleRate,
    TUint aBitDepth,
    TUint aNumChannels,
    TUint64 aSampleStart)
{
    iFormat = aFormat;
    iSampleRate = aSampleRate;
    iBitDepth = aBitDepth;
    iNumChannels = aNumChannels;
    iSampleStart = aSampleStart;
}

void RaatUri::SetSampleStart(TUint64 aSampleStart)
{
    iSampleStart = aSampleStart;
}

void RaatUri::GetUri(Bwx& aUri)
{ // static
    aUri.Replace(Brx::Empty());
    aUri.AppendThrow(kScheme);
    aUri.AppendThrow("://stream?");

    ASSERT(iFormat != Media::AudioFormat::Undefined);
    aUri.AppendThrow(kKeyFormat);
    aUri.AppendThrow("=");
    aUri.AppendThrow(iFormat == Media::AudioFormat::Pcm ? kFormatPcm : kFormatDsd);

    aUri.AppendThrow("&");
    aUri.AppendThrow(kKeySampleRate);
    aUri.AppendThrow("=");
    Ascii::AppendDec(aUri, iSampleRate);

    aUri.AppendThrow("&");
    aUri.AppendThrow(kKeyBitDepth);
    aUri.AppendThrow("=");
    Ascii::AppendDec(aUri, iBitDepth);

    aUri.AppendThrow("&");
    aUri.AppendThrow(kKeyNumChannels);
    aUri.AppendThrow("=");
    Ascii::AppendDec(aUri, iNumChannels);

    aUri.AppendThrow("&");
    aUri.AppendThrow(kKeySampleStart);
    aUri.AppendThrow("=");
    Ascii::AppendDec(aUri, iSampleStart);
}

RaatUri::RaatUri()
{
    Reset();
}

void RaatUri::Parse(const Brx& aUri)
{
    Reset();
    iUri.Replace(aUri);

    if (iUri.Scheme() != kScheme) {
        THROW(RaatUriError);
    }
    Brn query(iUri.Query());
    if (query.Bytes() == 0) {
        THROW(RaatUriError);
    }
    query.Set(query.Ptr() + 1, query.Bytes() - 1); // remove leading '?'
    Parser parser(query);

    Brn key, val;
    std::map<Brn, Brn, BufferCmp> kvps;
    for (;;) {
        key.Set(parser.Next('='));
        if (key.Bytes() == 0) {
            break;
        }
        val.Set(parser.Next('&'));
        kvps.insert(std::pair<Brn, Brn>(key, val));
    }
    //    for (auto it : kvps) {
    //        OpenHome::Log::Print("  key: %.*s,  val: %.*s\n", PBUF(it.first), PBUF(it.second));
    //    }

    SetValUint(kvps, kKeySampleRate, iSampleRate);
    SetValUint(kvps, kKeyBitDepth, iBitDepth);
    SetValUint(kvps, kKeyNumChannels, iNumChannels);
    SetValUint64(kvps, kKeySampleStart, iSampleStart);
    Brn keyFormat(kKeyFormat);
    auto it = kvps.find(keyFormat);
    if (it == kvps.end()) {
        THROW(RaatUriError);
    }
    iFormat = it->second == kFormatPcm ? Media::AudioFormat::Pcm : Media::AudioFormat::Dsd;
}

const Brx& RaatUri::AbsoluteUri() const
{
    return iUri.AbsoluteUri();
}

Media::AudioFormat RaatUri::Format() const
{
    ASSERT(iFormat != Media::AudioFormat::Undefined);
    return iFormat;
}

TUint RaatUri::SampleRate() const
{
    ASSERT(iSampleRate != 0);
    return iSampleRate;
}

TUint RaatUri::BitDepth() const
{
    ASSERT(iBitDepth != 0);
    return iBitDepth;
}

TUint RaatUri::NumChannels() const
{
    ASSERT(iNumChannels != 0);
    return iNumChannels;
}

TUint RaatUri::SampleStart() const
{
    return iSampleStart;
}

void RaatUri::Reset()
{
    iFormat = Media::AudioFormat::Undefined;
    iSampleRate = 0;
    iBitDepth = 0;
    iNumChannels = 0;
    iSampleStart = 0;
}

Brn RaatUri::Val(const std::map<Brn, Brn, BufferCmp>& aKvps, const Brx& aKey)
{ // static
    Brn key(aKey);
    auto it = aKvps.find(key);
    if (it == aKvps.end()) {
        THROW(RaatUriError);
    }
    return it->second;
}

void RaatUri::SetValUint(const std::map<Brn, Brn, BufferCmp>& aKvps, const Brx& aKey, TUint& aVal)
{ // static
    Brn val = Val(aKvps, aKey);
    aVal = Ascii::Uint(val);
}

void RaatUri::SetValUint64(const std::map<Brn, Brn, BufferCmp>& aKvps, const Brx& aKey, TUint64& aVal)
{ // static
    Brn val = Val(aKvps, aKey);
    aVal = Ascii::Uint64(val);
}


// RaatOutput

const TUint RaatOutput::kPendingPacketsMax = 20;
const TUint RaatOutput::kFreqNs = 1000000000;

RaatOutput::RaatOutput(
    IMediaPlayer& aMediaPlayer,
    ISourceRaat& aSourceRaat,
    Media::IAudioTime& aAudioTime,
    Media::IPullableClock& aPullableClock,
    IRaatSignalPathObservable& aSignalPathObservable)
    : RaatPluginAsync(aMediaPlayer.ThreadPool())
    , iEnv(aMediaPlayer.Env())
    , iPipeline(aMediaPlayer.Pipeline())
    , iSourceRaat(aSourceRaat)
    , iAudioTime(aAudioTime)
    , iPullableClock(aPullableClock)
    , iLockStream("Rat1")
    , iStream(nullptr)
    , iSemStarted("ROut", 0)
    , iSampleRate(0)
    , iPipelineDelayNs(1000 * 1000 * 1000) // 1 second - Roon will use this to set startTime and draining/restarting pipeline sometimes has quiet spells of hundreds of millisecs
    , iStarted(false)
    , iRunning(false)
    , iLockMetadata("Rat2")
    , iLockSignalPath("Rat3")
    , iSignalPathExakt(false)
    , iSignalPathAmplifier(false)
    , iSignalPathSpeaker(false)
{
    iPluginExt.iPlugin.get_info = Raat_Output_Get_Info;
    iPluginExt.iPlugin.get_supported_formats = Raat_Output_Get_Supported_Formats;
    iPluginExt.iPlugin.setup = Raat_Output_Setup;
    iPluginExt.iPlugin.teardown = Raat_Output_Teardown;
    iPluginExt.iPlugin.start = Raat_Output_Start;
    iPluginExt.iPlugin.get_local_time = Raat_Output_Get_Local_Time;
    iPluginExt.iPlugin.set_remote_time = Raat_Output_Set_Remote_Time;
    iPluginExt.iPlugin.stop = Raat_Output_Stop;
    iPluginExt.iPlugin.force_teardown = Raat_Output_Force_Teardown;
    iPluginExt.iPlugin.set_software_volume = nullptr;
    iPluginExt.iPlugin.set_software_volume_signal_path = nullptr;
    iPluginExt.iPlugin.send_message = nullptr;
    iPluginExt.iPlugin.add_message_listener = Raat_Output_Add_Message_Listener;
    iPluginExt.iPlugin.remove_message_listener = Raat_Output_Remove_Message_Listener;
    iPluginExt.iPlugin.get_output_delay = Raat_Output_Get_Output_Delay;
    iPluginExt.iSelf = this;

    RAAT__output_message_listeners_init(&iListeners, RC__allocator_malloc());
    iPendingPackets.reserve(kPendingPacketsMax);

    aSignalPathObservable.RegisterObserver(*this);
}

RaatOutput::~RaatOutput()
{
    RAAT__output_message_listeners_destroy(&iListeners);
}

RAAT__OutputPlugin* RaatOutput::Plugin()
{
    return (RAAT__OutputPlugin*)&iPluginExt;
}

void RaatOutput::GetInfo(json_t** aInfo)
{
    // FIXME - check what needs to be communicated - docs are *very* vague

    json_t *obj = json_object();
    ASSERT(obj != nullptr);
    json_object_set_new(obj, "refresh_supported_formats_before_playback", json_true());
    *aInfo = obj;
}

void RaatOutput::AddFormatPcm(RAAT__StreamFormat* aFormat, TUint aSampleRate, TUint aBitDepth)
{ // static
    aFormat->sample_type = RAAT__SAMPLE_TYPE_PCM;
    aFormat->sample_rate = (int)aSampleRate;
    aFormat->bits_per_sample = (int)aBitDepth;
    aFormat->channels = 2;
    aFormat->sample_subtype = RAAT__SAMPLE_SUBTYPE_NONE;
    aFormat->mqa_original_sample_rate = 0;
}

void RaatOutput::AddFormatDsd(RAAT__StreamFormat* aFormat, TUint aSampleRate)
{ // static
    aFormat->sample_type = RAAT__SAMPLE_TYPE_DSD;
    aFormat->sample_rate = (int)aSampleRate;
    aFormat->bits_per_sample = 1;
    aFormat->channels = 2;
    aFormat->sample_subtype = RAAT__SAMPLE_SUBTYPE_NONE;
    aFormat->mqa_original_sample_rate = 0;
}

void RaatOutput::GetSupportedFormats(RC__Allocator* aAlloc, size_t* aNumFormats, RAAT__StreamFormat** aFormats)
{
    // FIXME - check whether all formats need to be listed, or only highest sample rate / bit depth
#define NumElems(arr) (sizeof arr / sizeof arr[0])
    static const TUint kStandardRatesPcm[] = { 32000, 44100, 48000, 88200, 96000, 176400, 192000 };
    static const TUint kHigherRatesPcm[] = { 352800, 384000 };
    static const TUint kStandardRatesDsd[] = { 2822400, 5644800 };
    static const TUint kHigherRatesDsd[] = { 11289600 };
    TUint maxPcm, maxDsd;
    iPipeline.GetMaxSupportedSampleRates(maxPcm, maxDsd);
    TUint num = NumElems(kStandardRatesPcm);
    if (maxPcm > kStandardRatesPcm[NumElems(kStandardRatesPcm) - 1]) {
        num += NumElems(kHigherRatesPcm);
    }
    num *= 2; // we'll report support for 16 + 24 bit at each sample rate
    if (maxDsd > 0) {
        num += NumElems(kStandardRatesDsd);
    }
    if (maxDsd > kStandardRatesDsd[NumElems(kStandardRatesDsd) - 1]) {
        num += NumElems(kHigherRatesDsd);
    }
    RAAT__StreamFormat* formats = (RAAT__StreamFormat*)aAlloc->alloc(num * sizeof *formats);
    ASSERT(formats != nullptr);
    *aFormats = formats;
    *aNumFormats = num;
    TUint i, j;
    TUint count = 2 * NumElems(kStandardRatesPcm);
    for (i = 0, j=0; i < count; i+=2, j++) {
        AddFormatPcm(&formats[i], kStandardRatesPcm[j], 16);
        AddFormatPcm(&formats[i+1], kStandardRatesPcm[j], 24);
    }
    if (maxPcm > kStandardRatesPcm[NumElems(kStandardRatesPcm) - 1]) {
        count += 2 * NumElems(kHigherRatesPcm);
    }
    for (j = 0; i < count; i += 2, j++) {
        AddFormatPcm(&formats[i], kHigherRatesPcm[j], 16);
        AddFormatPcm(&formats[i + 1], kHigherRatesPcm[j], 24);
    }
    if (maxDsd > 0) {
        count += NumElems(kStandardRatesDsd);
        for (j = 0; i < count; i++, j++) {
            AddFormatDsd(&formats[i], kStandardRatesDsd[j]);
        }
        if (maxDsd > kStandardRatesDsd[NumElems(kStandardRatesDsd) - 1]) {
            count += NumElems(kHigherRatesDsd);
            for (j = 0; i < count; i++, j++) {
                AddFormatDsd(&formats[i], kHigherRatesDsd[j]);
            }
        }
    }
}

void RaatOutput::SetupStream(
    RAAT__StreamFormat* aFormat,
    RAAT__OutputSetupCallback aCbSetup, void* aCbSetupData,
    RAAT__OutputLostCallback aCbLost, void* aCbLostData)
{
    ASSERT(aFormat != nullptr);
    iStarted = false;
    iSetupCb.Set(aCbSetup, aCbSetupData, aCbLost, aCbLostData);
    iSampleRate = (TUint)aFormat->sample_rate;
    iUri.Set(
        aFormat->sample_type == RAAT__SAMPLE_TYPE_PCM ? Media::AudioFormat::Pcm : Media::AudioFormat::Dsd,
        iSampleRate,
        (TUint)aFormat->bits_per_sample,
        (TUint)aFormat->channels,
        0LL);
    Bws<256> uri;
    iUri.GetUri(uri);

    LOG(kMedia, "RaatOutput::SetupStream uri=%.*s\n", PBUF(uri));
    iSourceRaat.Play(uri);
}

RC__Status RaatOutput::TeardownStream(int aToken)
{
    LOG(kMedia, "RaatOutput::TeardownStream(%d) iToken=%d\n", aToken, iToken);
    if (aToken != iToken) {
        return RAAT__OUTPUT_PLUGIN_STATUS_INVALID_TOKEN;
    }
    return Stop();
}

RC__Status RaatOutput::StartStream(int aToken, int64_t aWallTime, int64_t aStreamTime, RAAT__Stream* aStream)
{
    {
        TUint64 localTime = MclkToNs();
        LOG(kMedia, "RaatOutput::StartStream(%d, %lld, %lld, %p) iToken=%d, localTime=%llu\n",
                    aToken, aWallTime, aStreamTime, aStream, iToken, localTime);
    }

    if (aToken != iToken) {
        return RAAT__OUTPUT_PLUGIN_STATUS_INVALID_TOKEN;
    }
    Interrupt();
    ChangeStream(aStream);
    iStreamPos = aStreamTime;
    const TUint64 startTicks = NsToMclk((TUint64)aWallTime);
    static_cast<Media::IStarterTimed&>(iPipeline).StartAt(startTicks);
    iClockSyncStarted = false;
    iLastClockPullTicks = startTicks; // doesn't really matter - will be overridden when iClockSyncStarted is set true
    iClockPull = Media::IPullableClock::kNominalFreq;

    iUri.SetSampleStart((TUint64)aStreamTime);
    Bws<256> uri;
    iUri.GetUri(uri);
    LOG(kMedia, "RaatOutput::StartStream uri=%.*s\n", PBUF(uri));
    iSourceRaat.Play(uri);
    iSemStarted.Signal();
    return RC__STATUS_SUCCESS;
}

RC__Status RaatOutput::GetLocalTime(int aToken, int64_t* aTime)
{
    if (aToken != iToken) {
        LOG(kMedia, "RaatOutput::GetLocalTime(%d) iToken=%d\n", aToken, iToken);
        return RAAT__OUTPUT_PLUGIN_STATUS_INVALID_TOKEN;
    }
    *aTime = MclkToNs();
    LOG(kMedia, "RaatOutput::GetLocalTime(%d) time=%lld\n", aToken, *aTime);
    return RC__STATUS_SUCCESS;
}

TUint64 RaatOutput::MclkToNs()
{
    TUint64 ticks;
    TUint freq;
    iAudioTime.GetTickCount(iSampleRate, ticks, freq);
    return ConvertTime(ticks, freq, kFreqNs);
}

TUint64 RaatOutput::NsToMclk(TUint64 aTimeNs)
{
    TUint64 ticksNow;
    TUint freq;
    iAudioTime.GetTickCount(iSampleRate, ticksNow, freq);
    const auto ticks = ConvertTime(aTimeNs, kFreqNs, freq);
    LOG(kMedia, "RaatOutput::NsToMclk: aTimeNs=%llu (mclck=%llu), freq=%u, ticks=%llu, ticksNow=%llu\n", aTimeNs, MclkToNs(), freq, ticks, ticksNow);
    return ticks;
}

TUint64 RaatOutput::ConvertTime(TUint64 aTicksFrom, TUint aFreqFrom, TUint aFreqTo)
{
    TUint64 secs = aTicksFrom / aFreqFrom;
    TUint64 ticks = aTicksFrom % aFreqFrom;
    ticks *= aFreqTo;
    ticks /= aFreqFrom;
    ticks += (secs * aFreqTo);
    ticks &= ~0x8000000000000000LL; // Roon deals in 63-bit signed times so clear the top bit of our 64-bit unsigned value
    return ticks;
}

RC__Status RaatOutput::SetRemoteTime(int aToken, int64_t aClockOffset, bool aNewSource)
{
    // FIXME
    LOG(kMedia, "RaatOutput::SetRemoteTime(%d, %lld, %u)\n", aToken, aClockOffset, aNewSource);
    TUint64 ticksNow;
    TUint freq;
    iAudioTime.GetTickCount(iSampleRate, ticksNow, freq);
    const TUint64 remoteTicksDelta = RaatOutput::ConvertTime(abs(aClockOffset), kFreqNs, freq);
    if (!iClockSyncStarted) {
        TUint64 remoteTicks = ticksNow;
        if (aClockOffset > 0) {
            remoteTicks -= remoteTicksDelta;
        }
        else {
            remoteTicks += remoteTicksDelta;
        }
        iAudioTime.SetTickCount(remoteTicks);
        iClockSyncStarted = true;
        iLastClockPullTicks = ticksNow;
    }
    else {
        TUint64 delta = (remoteTicksDelta * iClockPull) / (ticksNow - iLastClockPullTicks);
        if (aClockOffset > 0) {
            //iClockPull -= (TUint)delta;
            iClockPull = Media::IPullableClock::kNominalFreq - (TUint)delta;
        }
        else {
            //iClockPull += (TUint)delta;
            iClockPull = Media::IPullableClock::kNominalFreq + (TUint)delta;
        }
        LOG(kMedia, "RaatOutput::SetRemoteTime delta=%llx, pull=%x\n", delta, iClockPull);
        iPullableClock.PullClock(iClockPull);
    }
    return RC__STATUS_SUCCESS;
}

RC__Status RaatOutput::TryStop(int aToken)
{
    LOG(kMedia, "RaatOutput::TryStop(%d) iToken=%d\n", aToken, iToken);
    if (aToken != iToken) {
        return RAAT__OUTPUT_PLUGIN_STATUS_INVALID_TOKEN;
    }
    return Stop();
}

RC__Status RaatOutput::Stop()
{
    iPipeline.Stop();
    Interrupt();
    ChangeStream(nullptr);
    return RC__STATUS_SUCCESS;
}

RC__Status RaatOutput::AddListener(RAAT__OutputMessageCallback aCb, void* aCbUserdata)
{
    LOG(kMedia, "RaatOutput::AddListener\n");
    auto err = RAAT__output_message_listeners_add(&iListeners, aCb, aCbUserdata);
    TryReportState();
    return err;
}

void RaatOutput::RemoveListener(RAAT__OutputMessageCallback aCb, void* aCbUserdata)
{
    LOG(kMedia, "RaatOutput::RemoveListener\n");
    (void)RAAT__output_message_listeners_remove(&iListeners, aCb, aCbUserdata);
}

void RaatOutput::GetDelay(int /*aToken*/, int64_t* aDelay)
{
    //LOG(kMedia, "RaatOutput::GetDelay(%d)\n", aToken);
    *aDelay = iPipelineDelayNs;
}

RAAT__Stream* RaatOutput::StreamRef()
{
    AutoMutex _(iLockStream);
    if (iStream != nullptr) {
        RAAT__stream_incref(iStream);
    }
    return iStream;
}

void RaatOutput::ChangeStream(RAAT__Stream* aStream)
{
    AutoMutex _(iLockStream);
    if (iStream != nullptr) {
        for (auto it = iPendingPackets.begin(); it != iPendingPackets.end(); ++it) {
            RAAT__AudioPacket p = *it;
            RAAT__stream_destroy_packet(iStream, &p);
        }
        (void)iPendingPackets.clear();
        RAAT__stream_decref(iStream);
    }
    iStream = aStream;
    if (iStream != nullptr) {
        RAAT__stream_incref(iStream);
    }
}

void RaatOutput::NotifyReady()
{
    iToken = iSetupCb.NotifyReady();
}

void RaatOutput::Read(IRaatWriter& aWriter)
{
    if (!iStarted) {
        iSemStarted.Wait();
        iStarted = true;
        static const TUint kMsPerRead = 2;
        iSamplesPerRead = (iSampleRate * kMsPerRead) / 1000;
    }
    {
        Bws<RaatTransport::kMaxBytesMetadataTitle> title;
        Bws<RaatTransport::kMaxBytesMetadataSubtitle> subtitle;
        TUint seconds = 0, duration = 0;
        {
            AutoMutex _(iLockMetadata);
            if (iMetadataTitle.Bytes() > 0) {
                title.Replace(iMetadataTitle);
                iMetadataTitle.Replace(Brx::Empty());
                subtitle.Replace(iMetadataSubtitle);
                iMetadataSubtitle.Replace(Brx::Empty());
                seconds = iPosSeconds;
                duration = iDurationSeconds;
            }
        }
        if (title.Bytes() > 0) {
            //Log::Print("RaatOutput::Read writing metadata %.*s\n", PBUF(iMetadataTemp));
            aWriter.WriteMetadata(title, subtitle, seconds, duration);
        }
    }

    RAAT__AudioPacket packet;
    RAAT__Stream* stream = StreamRef();
    if (stream == nullptr) {
        THROW(RaatReaderStopped);
    }
    AutoStreamRef _(stream);
    auto err = RAAT__stream_consume_packet(stream, &packet);
    if (err != RC__STATUS_SUCCESS) {
        LOG(kMedia, "Error: %d from RAAT__stream_consume_packet\n", err);
        THROW(RaatReaderStopped);
    }

    if (!iRunning || iStreamPos == packet.streamsample) {
        iRunning = true;
        // current packet is suitable to send into pipeline immediately
//        Log::Print("[%u] RaatOutput::Read: pushing %d samples into the pipeline\n", Os::TimeInMs(iEnv.OsCtx()), packet.nsamples);
        TUint packetBytes = ((TUint)packet.nsamples * iUri.BitDepth() * iUri.NumChannels()) / 8;
        Brn audio((const TByte*)packet.buf, packetBytes);
        aWriter.WriteData(audio);
        iStreamPos = packet.streamsample + packet.nsamples;
        RAAT__stream_destroy_packet(stream, &packet);

        // check whether above packet unblocks any pending ones
        auto it = iPendingPackets.begin();
        for (; it != iPendingPackets.end(); ++it) {
            if (it->streamsample == iStreamPos) {
                packetBytes = ((TUint)it->nsamples * iUri.BitDepth() * iUri.NumChannels()) / 8;
                audio.Set((const TByte*)it->buf, packetBytes);
                Log::Print("[%u] RaatOutput::Read: (delayed) push %d samples into the pipeline\n", Os::TimeInMs(iEnv.OsCtx()), it->nsamples);
                aWriter.WriteData(audio);
                iStreamPos = it->streamsample + it->nsamples;
            }
            else {
                break;
            }
        }
        if (it != iPendingPackets.begin()) {
            --it;
            for (auto it2 = iPendingPackets.begin(); it2 != it; ++it2) {
                RAAT__AudioPacket p = *it2;
                RAAT__stream_destroy_packet(stream, &p);
            }
            iPendingPackets.erase(iPendingPackets.begin(), it);
        }
    }
    else {
        if (iPendingPackets.size() == kPendingPacketsMax) {
            // we've exceeded our capacity for audio backlog - instruct the pipeline to drain then start again
            LOG(kMedia, "RaatOutput::Read: too many out of order packets, THROW(RaatPacketError)\n");
            THROW(RaatPacketError);
        }
        TBool done = false;
        auto it = iPendingPackets.begin();
        for (; it != iPendingPackets.end(); ++it) {
            if (it->streamsample >= packet.streamsample) {
                if (it->streamsample > packet.streamsample) {
                    Log::Print("[%u] RaatOutput::Read: out of order, hold off on block starting %lld\n", Os::TimeInMs(iEnv.OsCtx()), packet.streamsample);
                    iPendingPackets.insert(it, packet);
                }
                done = true;
                break;
            }
        }
        if (!done) {
            Log::Print("[%u] RaatOutput::Read: out of order, hold off on block starting %lld\n", Os::TimeInMs(iEnv.OsCtx()), packet.streamsample);
            iPendingPackets.push_back(packet);
        }
    }
}

void RaatOutput::Interrupt()
{
    RAAT__Stream* stream = StreamRef();
    if (stream != nullptr) {
        auto ret = RAAT__stream_cancel_consume_packet(stream);
        if (ret != RC__STATUS_SUCCESS) {
            LOG(kMedia, "RaatOutput::Interrupt() Warning: RAAT__stream_cancel_consume_packet failed (%d)\n", ret);
        }
        RAAT__stream_decref(stream);
    }
}

void RaatOutput::MetadataChanged(const Brx& aTitle, const Brx& aSubtitle, TUint aPosSeconds, TUint aDurationSeconds)
{
    AutoMutex _(iLockMetadata);
    iMetadataTitle.ReplaceThrow(aTitle);
    iMetadataSubtitle.ReplaceThrow(aSubtitle);
    iPosSeconds = aPosSeconds;
    iDurationSeconds = aDurationSeconds;
}

void RaatOutput::ReportState()
{
    json_t* message = json_object();
    json_t* signal_path = json_array();
    {
        AutoMutex _(iLockSignalPath);

        if (iSignalPathExakt) {
            json_t* exakt = json_object();
            json_object_set_new(exakt, "type", json_string("linn"));
            json_object_set_new(exakt, "method", json_string("exakt"));
            json_object_set_new(exakt, "quality", json_string("enhanced"));
            json_array_append_new(signal_path, exakt);
        }
        if (iSignalPathAmplifier) {
            json_t* amplifier = json_object();
            json_object_set_new(amplifier, "type", json_string("amplifier"));
            json_object_set_new(amplifier, "method", json_string("analog"));
            json_object_set_new(amplifier, "quality", json_string("lossless"));
            json_array_append_new(signal_path, amplifier);
        }
        if (iSignalPathSpeaker) {
            json_t* output = json_object();
            json_object_set_new(output, "type", json_string("output"));
            json_object_set_new(output, "method", json_string("speakers"));
            json_object_set_new(output, "quality", json_string("lossless"));
            json_array_append_new(signal_path, output);
        }
        else {
            json_t* output = json_object();
            json_object_set_new(output, "type", json_string("output"));
            json_object_set_new(output, "method", json_string("analog"));
            json_object_set_new(output, "quality", json_string("lossless"));
            json_array_append_new(signal_path, output);
        }
    }

    json_object_set_new(message, "signal_path", signal_path);
    RAAT__output_message_listeners_invoke(&iListeners, message);
    json_decref(message);
}

void RaatOutput::SignalPathChanged(TBool aExakt, TBool aAmplifier, TBool aSpeaker)
{
    LOG(kMedia, "RaatOutput::SignalPathChanged(%u,%u,%u)\n", aExakt, aAmplifier, aSpeaker);
    {
        AutoMutex _(iLockSignalPath);
        iSignalPathExakt = aExakt;
        iSignalPathAmplifier = aAmplifier;
        iSignalPathSpeaker = aSpeaker;
    }
    TryReportState();
}


// RaatOutput::SetupCb

const int RaatOutput::SetupCb::kTokenInvalid = 0;

RaatOutput::SetupCb::SetupCb()
    : iNextToken(kTokenInvalid+1)
{
    Reset();
}

void RaatOutput::SetupCb::Set(
    RAAT__OutputSetupCallback aCbSetup, void* aCbSetupData,
    RAAT__OutputLostCallback aCbLost, void* aCbLostData)
{
    iCbSetup = aCbSetup;
    iCbSetupData = aCbSetupData;
    iCbLost = aCbLost;
    iCbLostData = aCbLostData;
}

TUint RaatOutput::SetupCb::NotifyReady()
{
    auto token = iNextToken;
    if (iCbSetup) {
        token = ++iNextToken;
        iCbSetup(iCbSetupData, RC__STATUS_SUCCESS, (int)token);
        Reset();
    }
    return token;
}

void RaatOutput::SetupCb::NotifyFailed()
{
    iCbLost(iCbLostData, nullptr);
    Reset();
}

void RaatOutput::SetupCb::Reset()
{
    iCbSetup = nullptr;
    iCbSetupData = nullptr;
    iCbLost = nullptr;
    iCbLostData = nullptr;
}


// AutoStreamRef

AutoStreamRef::AutoStreamRef(RAAT__Stream* aStream)
    : iStream(aStream)
{
    ASSERT(iStream != nullptr);
}

AutoStreamRef::~AutoStreamRef()
{
    RAAT__stream_decref(iStream);
}
