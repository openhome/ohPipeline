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

extern "C" {

RC__Status Raat_Output_Get_Info(void *self, json_t **out_info)
{
    Output(self)->GetInfo(out_info);
    return RC__STATUS_SUCCESS;
}

RC__Status Raat_Output_Get_Supported_Formats(void *self, RC__Allocator *alloc, size_t *out_nformats, RAAT__StreamFormat **out_formats)
{
    Output(self)->GetSupportedFormats(alloc, out_nformats, out_formats);
    return RC__STATUS_SUCCESS;
}

void Raat_Output_Setup(
    void *self,
    RAAT__StreamFormat *format,
    RAAT__OutputSetupCallback cb_setup, void *cb_setup_userdata,
    RAAT__OutputLostCallback cb_lost, void *cb_lost_userdata)
{
    Output(self)->SetupStream(format, cb_setup, cb_setup_userdata, cb_lost, cb_lost_userdata);
}

RC__Status Raat_Output_Teardown(void *self, int token)
{
    auto ret = Output(self)->TeardownStream(token);
    return ret;
}

RC__Status Raat_Output_Start(void *self, int token, int64_t walltime, int64_t streamtime, RAAT__Stream *stream)
{
    auto ret = Output(self)->StartStream(token, walltime, streamtime, stream);
    return ret;
}

RC__Status Raat_Output_Get_Local_Time(void *self, int token, int64_t *out_time)
{
    auto ret = Output(self)->GetLocalTime(token, out_time);
    return ret;
}

RC__Status Raat_Output_Set_Remote_Time(void *self, int token, int64_t clock_offset, bool new_source)
{
    auto ret = Output(self)->SetRemoteTime(token, clock_offset, new_source);
    return ret;
}

RC__Status Raat_Output_Stop(void *self, int token)
{
    auto ret = Output(self)->StopStream(token);
    return ret;
}

RC__Status Raat_Output_Force_Teardown(void *self, json_t* /*reason*/)
{
    auto ret = Output(self)->ForceTeardownStream();
    return ret;
}

RC__Status Raat_Output_Add_Message_Listener(void *self, RAAT__OutputMessageCallback cb, void *cb_userdata)
{
    auto ret = Output(self)->AddListener(cb, cb_userdata);
    return ret;
}

RC__Status Raat_Output_Remove_Message_Listener(void *self, RAAT__OutputMessageCallback cb, void *cb_userdata)
{
    auto ret = Output(self)->RemoveListener(cb, cb_userdata);
    if (ret != RC__STATUS_SUCCESS) {
        OpenHome::Log::Print("[RAAT DEBUG] - Raat_Output_Remove_Message_Listener() ERROR: returned RC__STATUS_SUCCESS - actual '%s'\n", RC__status_to_string(ret));
    }
    return RC__STATUS_SUCCESS;
}

RC__Status Raat_Output_Get_Output_Delay(void *self, int token, int64_t *out_delay)
{
    Output(self)->GetDelay(token, out_delay);
    return RC__STATUS_SUCCESS;
}

}


using namespace OpenHome;
using namespace OpenHome::Av;

// RaatStreamFormat

RaatStreamFormat::RaatStreamFormat()
    : iFormat(Media::AudioFormat::Undefined)
    , iSampleRate(0)
    , iBitDepth(0)
    , iNumChannels(0)
    , iLock("RSTR")
{
}

void RaatStreamFormat::Set(RAAT__StreamFormat*& aFormat)
{
    AutoMutex _(iLock);
    iFormat = (aFormat->sample_type == RAAT__SAMPLE_TYPE_PCM) ? Media::AudioFormat::Pcm : Media::AudioFormat::Dsd;
    iSampleRate = (TUint)aFormat->sample_rate;
    iBitDepth = (TUint)aFormat->bits_per_sample;
    iNumChannels = (TUint)aFormat->channels;
}

Media::AudioFormat RaatStreamFormat::Format() const
{
    AutoMutex _(iLock);
    return iFormat;
}

TUint RaatStreamFormat::SampleRate() const
{
    AutoMutex _(iLock);
    return iSampleRate;
}

TUint RaatStreamFormat::BitDepth() const
{
    AutoMutex _(iLock);
    return iBitDepth;
}

TUint RaatStreamFormat::NumChannels() const
{
    AutoMutex _(iLock);
    return iNumChannels;
}


// RaatOutput

const Brn RaatOutput::kKeyDsdEnable("Raat.DsdEnable");
const TUint RaatOutput::kValDsdDisabled = 0;
const TUint RaatOutput::kValDsdEnabled = 1;

RaatOutput::RaatOutput(
    IMediaPlayer&               aMediaPlayer,
    ISourceRaat&                aSourceRaat,
    Media::IAudioTime&          aAudioTime,
    Media::IPullableClock&      aPullableClock,
    IRaatSignalPathObservable&  aSignalPathObservable)

    : RaatPluginAsync(aMediaPlayer.ThreadPool())
    , iEnv(aMediaPlayer.Env())
    , iPipeline(aMediaPlayer.Pipeline())
    , iSourceRaat(aSourceRaat)
    , iAudioTime(aAudioTime)
    , iPullableClock(aPullableClock)
    , iLockStream("RAT1")
    , iLockSignalPath("RAT2")
    , iLockConfig("RAT3")
    , iConfigDsdEnable(nullptr)
    , iSubscriberIdDsdEnable(Configuration::IConfigManager::kSubscriptionIdInvalid)
    , iStream(nullptr)
    , iSampleRate(0)
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
    aSignalPathObservable.RegisterObserver(*this);

    TUint maxPcm, maxDsd;
    iPipeline.GetMaxSupportedSampleRates(maxPcm, maxDsd);
    if (maxDsd != 0) {
        const int arr[] = { kValDsdDisabled, kValDsdEnabled };
        std::vector<TUint> opts(arr, arr + sizeof(arr) / sizeof(arr[0]));
        iConfigDsdEnable = new Configuration::ConfigChoice(
            aMediaPlayer.ConfigInitialiser(),
            kKeyDsdEnable,
            opts,
            kValDsdEnabled);
        iSubscriberIdDsdEnable = iConfigDsdEnable->Subscribe(
            Configuration::MakeFunctorConfigChoice(*this, &RaatOutput::DsdEnableChanged));
    }
}

RaatOutput::~RaatOutput()
{
    if (iConfigDsdEnable != nullptr) {
        iConfigDsdEnable->Unsubscribe(iSubscriberIdDsdEnable);
        delete iConfigDsdEnable;
    }
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
    iLockConfig.Wait();
    const TBool dsdSupported = maxDsd > 0 && iDsdEnabled;
    iLockConfig.Signal();
    if (dsdSupported) {
        num += NumElems(kStandardRatesDsd);
        if (maxDsd > kStandardRatesDsd[NumElems(kStandardRatesDsd) - 1]) {
            num += NumElems(kHigherRatesDsd);
        }
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
    if (dsdSupported) {
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
    LOG(kRaat, "RaatOutput::SetupStream()\n");

    ASSERT(aFormat != nullptr);
    iStreamFormat.Set(aFormat);
    iSampleRate = iStreamFormat.SampleRate();
    iControlCallback.Set(aCbSetup, aCbSetupData, aCbLost, aCbLostData);
    iStreamPos = 0;

    TryReportState();
    iSourceRaat.NotifySetup();
}

RC__Status RaatOutput::TeardownStream(int aToken)
{
    LOG(kRaat, "RaatOutput::TeardownStream(%d) iToken=%d\n", aToken, iToken);
    if (aToken != iToken) {
        return RAAT__OUTPUT_PLUGIN_STATUS_INVALID_TOKEN;
    }
    Stop();
    iControlCallback.Reset();
    return RC__STATUS_SUCCESS;
}

RC__Status RaatOutput::StartStream(int aToken, int64_t aWallTime, int64_t aStreamTime, RAAT__Stream* aStream)
{
    TUint64 localTime = MclkToNs();
    LOG(kRaat, "RaatOutput::StartStream() aWallTime=%lld, localTime=%llu\n", aWallTime, localTime);
    if (aToken != iToken) {
        return RAAT__OUTPUT_PLUGIN_STATUS_INVALID_TOKEN;
    }
    Interrupt();
    ChangeStream(aStream);
    iStreamPos = (aStreamTime == 0) ? 0 : iStreamPos;
    const TUint64 startTicks = NsToMclk((TUint64)aWallTime - kFixedOffsetNs);
    static_cast<Media::IStarterTimed&>(iPipeline).StartAt(startTicks);
    iClockSyncStarted = false;
    iClockPull = Media::IPullableClock::kNominalFreq;

    iSourceRaat.NotifyStart();
    return RC__STATUS_SUCCESS;
}

RC__Status RaatOutput::GetLocalTime(int aToken, int64_t* aTime)
{
    if (aToken != iToken) {
        return RAAT__OUTPUT_PLUGIN_STATUS_INVALID_TOKEN;
    }
    *aTime = MclkToNs();
    return RC__STATUS_SUCCESS;
}

TUint64 RaatOutput::MclkToNs()
{
    TUint64 ticks;
    TUint freq;
    iAudioTime.GetTickCount(iSampleRate, ticks, freq);
    return ConvertTime(ticks, freq, kNanoSecsPerSec);
}

TUint64 RaatOutput::NsToMclk(TUint64 aTimeNs)
{
    TUint64 ticksNow;
    TUint freq;
    iAudioTime.GetTickCount(iSampleRate, ticksNow, freq);
    const auto ticks = ConvertTime(aTimeNs, kNanoSecsPerSec, freq);
    LOG(kRaat, "RaatOutput::NsToMclk: aTimeNs=%llu (mclck=%llu), freq=%u, ticks=%llu, ticksNow=%llu\n", aTimeNs, MclkToNs(), freq, ticks, ticksNow);
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

RC__Status RaatOutput::SetRemoteTime(int /*aToken*/, int64_t aClockOffset, bool /*aNewSource*/)
{
    // A positive value for aClockOffset indicates we are leading the master clock
    LOG(kRaat, "RaatOutput::SetRemoteTime() aClockOffset: %llius\n", (aClockOffset / 1000));
    TUint64 ticksNow;
    TUint freq;
    iAudioTime.GetTickCount(iSampleRate, ticksNow, freq);

    const TUint64 ticksDelta = RaatOutput::ConvertTime(abs(aClockOffset), kNanoSecsPerSec, freq);
    if (!iClockSyncStarted) {
        TUint64 remoteTicks = ticksNow;
        if (aClockOffset > 0) {
            remoteTicks -= ticksDelta;
        }
        else {
            remoteTicks += ticksDelta;
        }
        iAudioTime.SetTickCount(remoteTicks);
        iClockSyncStarted = true;
    }
    else {
        const TUint64 delta = (ticksDelta * Media::IPullableClock::kNominalFreq) / (freq * kClockAdjustmentGradientSecs);
        if (aClockOffset > 0) {
            iClockPull = Media::IPullableClock::kNominalFreq - (TUint)delta;
        }
        else {
            iClockPull = Media::IPullableClock::kNominalFreq + (TUint)delta;
        }
        iPullableClock.PullClock(iClockPull);
    }
    return RC__STATUS_SUCCESS;
}

RC__Status RaatOutput::StopStream(int aToken)
{
    LOG(kRaat, "RaatOutput::StopStream(%d) iToken=%d\n", aToken, iToken);
    if (aToken != iToken) {
        return RAAT__OUTPUT_PLUGIN_STATUS_INVALID_TOKEN;
    }
    Stop();
    return RC__STATUS_SUCCESS;
}

RC__Status RaatOutput::ForceTeardownStream()
{
    LOG(kRaat, "RaatOutput::ForceTeardownStream()\n");
    Stop();
    iControlCallback.Reset();
    return RC__STATUS_SUCCESS;
}

RC__Status RaatOutput::AddListener(RAAT__OutputMessageCallback aCb, void* aCbUserdata)
{
    LOG(kMedia, "RaatOutput::AddListener\n");
    auto err = RAAT__output_message_listeners_add(&iListeners, aCb, aCbUserdata);
    return err;
}

RC__Status RaatOutput::RemoveListener(RAAT__OutputMessageCallback aCb, void* aCbUserdata)
{
    LOG(kRaat, "RaatOutput::RemoveListener\n");
    auto err = RAAT__output_message_listeners_remove(&iListeners, aCb, aCbUserdata);
    return err;
}

void RaatOutput::GetDelay(int /*aToken*/, int64_t* aDelay)
{
    *aDelay = kDefaultDelayNs;
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
        RAAT__stream_decref(iStream);
    }
    iStream = aStream;
    if (iStream != nullptr) {
        RAAT__stream_incref(iStream);
    }
}

void RaatOutput::DsdEnableChanged(Configuration::KeyValuePair<TUint>& aKvp)
{
    AutoMutex _(iLockConfig);
    iDsdEnabled = aKvp.Value() == kValDsdEnabled;
}

void RaatOutput::Stop()
{
    iSourceRaat.NotifyStop();
    Interrupt();
    ChangeStream(nullptr);
}

const RaatStreamFormat& RaatOutput::StreamFormat()
{
    // RaatStreamFormat handles its own thread safety
    return iStreamFormat;
}

void RaatOutput::NotifyReady()
{
    iToken = iControlCallback.NotifyReady();
}

void RaatOutput::Read(IRaatWriter& aWriter)
{
    RAAT__AudioPacket packet;
    RAAT__Stream* stream = StreamRef();
    if (stream == nullptr) {
        THROW(RaatReaderStopped);
    }
    AutoStreamRef _(stream);
    auto err = RAAT__stream_consume_packet(stream, &packet);
    if (err != RC__STATUS_SUCCESS) {
        if (err != RC__STATUS_CANCELED) {
            LOG(kRaat, "RaatOutput::Read() RAAT__stream_consume_packet unexpected error (%d)\n", err);
        }
        THROW(RaatReaderStopped);
    }
    if (iStreamPos != packet.streamsample) {
        LOG(kRaat, "RaatOutput::Read() Unexpected packet order. iStreamPos: %lli, packet.streamsample: %lli\n", iStreamPos, packet.streamsample);
        THROW(RaatReaderStopped);
    }

    TUint packetBytes = ((TUint)packet.nsamples * iStreamFormat.BitDepth() * iStreamFormat.NumChannels()) / 8;
    Brn audio((const TByte*)packet.buf, packetBytes);
    aWriter.Write(audio);
    iStreamPos += packet.nsamples;
    RAAT__stream_destroy_packet(stream, &packet);
}

void RaatOutput::Interrupt()
{
    RAAT__Stream* stream = StreamRef();
    if (stream == nullptr) {
        return;
    }

    auto ret = RAAT__stream_cancel_consume_packet(stream);
    if (ret != RC__STATUS_SUCCESS) {
        LOG(kRaat, "RaatOutput::Interrupt() Warning: RAAT__stream_cancel_consume_packet failed (%d)\n", ret);
    }
    RAAT__stream_decref(stream);
}

void RaatOutput::NotifyStandby()
{
    iControlCallback.NotifyFinalise("standby");
}

void RaatOutput::NotifyDeselected()
{
    iControlCallback.NotifyFinalise("source_deselected");
}

void RaatOutput::SignalPathChanged(const IRaatSignalPath& aSignalPath)
{
    LOG(kRaat, "RaatOutput::SignalPathChanged(%u,%u,%u,%u)\n",
        aSignalPath.Exakt(),
        aSignalPath.SpaceOptimisation(),
        aSignalPath.Amplifier(),
        aSignalPath.Output());

    AutoMutex _(iLockSignalPath);
    iSignalPath.Set(aSignalPath);
    TryReportState();
}

void RaatOutput::ReportState()
{
    json_t* message = json_object();
    json_t* signal_path = json_array();
    {
        AutoMutex _(iLockSignalPath);
        if (iSignalPath.Exakt()) {
            json_t* exakt = json_object();
            json_object_set_new(exakt, "type", json_string("linn"));
            json_object_set_new(exakt, "method", json_string("exakt"));
            json_object_set_new(exakt, "quality", json_string("enhanced"));
            json_array_append_new(signal_path, exakt);
        }
        if (iSignalPath.SpaceOptimisation()) {
            json_t* exakt = json_object();
            json_object_set_new(exakt, "type", json_string("linn"));
            json_object_set_new(exakt, "method", json_string("space_optimisation"));
            json_object_set_new(exakt, "quality", json_string("enhanced"));
            json_array_append_new(signal_path, exakt);
        }
        if (iSignalPath.Amplifier()) {
            json_t* amplifier = json_object();
            json_object_set_new(amplifier, "type", json_string("amplifier"));
            json_object_set_new(amplifier, "method", json_string("analog"));
            json_object_set_new(amplifier, "quality", json_string("lossless"));
            json_array_append_new(signal_path, amplifier);
        }

        Brhz outputString;
        if (iSignalPath.Output() == IRaatSignalPath::EOutput::eHeadphones) {
            outputString.Set("headphones");
        }
        else if (iSignalPath.Output() == IRaatSignalPath::EOutput::eSpeakers){
            outputString.Set("speakers");
        }
        else {
            outputString.Set("analog_digital");
        }

        json_t* output = json_object();
        json_object_set_new(output, "type", json_string("output"));
        json_object_set_new(output, "method", json_string(outputString.CString()));
        json_object_set_new(output, "quality", json_string("lossless"));
        json_array_append_new(signal_path, output);
    }

    json_object_set_new(message, "signal_path", signal_path);
    RAAT__output_message_listeners_invoke(&iListeners, message);
    json_decref(message);
}


// RaatOutput::ControlCallback

RaatOutput::ControlCallback::ControlCallback()
    : iToken(kTokenInvalid)
{
    Reset();
}

void RaatOutput::ControlCallback::Set(
    RAAT__OutputSetupCallback aCbSetup, void* aCbSetupData,
    RAAT__OutputLostCallback aCbLost, void* aCbLostData)
{
    iCbSetup = aCbSetup;
    iCbSetupData = aCbSetupData;
    iCbLost = aCbLost;
    iCbLostData = aCbLostData;
    iToken++;
}

TUint RaatOutput::ControlCallback::NotifyReady()
{
    ASSERT(iCbSetup);
    iCbSetup(iCbSetupData, RC__STATUS_SUCCESS, iToken);
    return iToken;
}

void RaatOutput::ControlCallback::NotifyFinalise(const TChar* aReason)
{
    if (iCbLost == nullptr) {
        return;
    }
    json_t *reason = json_object();
    json_object_set_new(reason, "reason", json_string(aReason));
    iCbLost(iCbLostData, reason);
    json_decref(reason);
    Reset();
}

void RaatOutput::ControlCallback::Reset()
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
