#include <OpenHome/Av/Raat/Output.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Types.h>
#include <OpenHome/Media/PipelineManager.h>

#include <stdlib.h>

#include <raat_plugin_output.h>
#include <rc_status.h>
#include <rc_allocator.h>
#include <jansson.h>
#include <raat_stream.h>

static inline OpenHome::Av::RaatOutput* Output(void *self)
{
    return reinterpret_cast<OpenHome::Av::RaatOutput*>(self);
}

extern "C"
static RC__Status Raat_Output_Get_Info(void *self, json_t **out_info)
{
    return Output(self)->GetInfo(out_info);
}

extern "C"
static RC__Status Raat_Output_Get_Supported_Formats(void *self, RC__Allocator *alloc, size_t *out_nformats, RAAT__StreamFormat **out_formats)
{
    return Output(self)->GetSupportedFormats(alloc, out_nformats, out_formats);
}

extern "C"
static void Raat_Output_Setup(
    void *self,
    RAAT__StreamFormat *format,
    RAAT__OutputSetupCallback cb_setup, void *cb_setup_userdata,
    RAAT__OutputLostCallback cb_lost, void *cb_lost_userdata)
{
    Output(self)->SetupStream(format, cb_setup, cb_setup_userdata, cb_lost, cb_lost_userdata);
}

extern "C"
static RC__Status Raat_Output_Teardown(void *self, int token)
{
    return Output(self)->TeardownStream(token);
}

extern "C"
static RC__Status Raat_Output_Start(void *self, int token, int64_t walltime, int64_t streamtime, RAAT__Stream *stream)
{
    return Output(self)->StartStream(token, walltime, streamtime, stream);
}

extern "C"
static RC__Status Raat_Output_Get_Local_Time(void *self, int token, int64_t *out_time)
{
    return Output(self)->GetLocalTime(token, out_time);
}

extern "C"
static RC__Status Raat_Output_Set_Remote_Time(void *self, int token, int64_t clock_offset, bool new_source)
{
    return Output(self)->SetRemoteTime(token, clock_offset, new_source);
}

extern "C"
static RC__Status Raat_Output_Stop(void *self, int token)
{
    return Output(self)->TryStop(token);
}

extern "C"
static RC__Status Raat_Output_Force_Teardown(void *self, json_t* /*reason*/)
{
    return Output(self)->Stop();
}


using namespace OpenHome;
using namespace OpenHome::Av;


RaatOutput::RaatOutput(Media::PipelineManager& aPipeline)
    : iPipeline(aPipeline)
{
    iPlugin = (RAAT__OutputPlugin*)calloc(1, sizeof *iPlugin);
    ASSERT(iPlugin != nullptr);
    iPlugin->get_info = Raat_Output_Get_Info;
    iPlugin->get_supported_formats = Raat_Output_Get_Supported_Formats;
    iPlugin->setup = Raat_Output_Setup;
    iPlugin->teardown = Raat_Output_Teardown;
    iPlugin->start = Raat_Output_Start;
    iPlugin->get_local_time = Raat_Output_Get_Local_Time;
    iPlugin->set_remote_time = Raat_Output_Set_Remote_Time;
    iPlugin->stop = Raat_Output_Stop;
    iPlugin->force_teardown = Raat_Output_Force_Teardown;
    iPlugin->set_software_volume = nullptr;
    iPlugin->set_software_volume_signal_path = nullptr;
    iPlugin->send_message = nullptr;
    iPlugin->add_message_listener = nullptr;
    iPlugin->remove_message_listener = nullptr;
    iPlugin->get_output_delay = nullptr;
}

RaatOutput::~RaatOutput()
{
    free(iPlugin);
}

RAAT__OutputPlugin* RaatOutput::OutputPlugin()
{
    return iPlugin;
}

RC__Status RaatOutput::GetInfo(json_t** aInfo)
{
    // FIXME - check what needs to be communicated - docs are *very* vague
    json_t *obj = json_object();
    ASSERT(obj != nullptr);
    json_object_set_new(obj, "refresh_supported_formats_before_playback", json_true());
    *aInfo = obj;
    return RC__STATUS_SUCCESS;
}

void RaatOutput::AddFormatPcm(RAAT__StreamFormat** aFormats, int aSampleRate)
{
    AddFormatPcm(aFormats, aSampleRate, 16);
    *aFormats++;
    AddFormatPcm(aFormats, aSampleRate, 24);
    *aFormats++;
}

void RaatOutput::AddFormatPcm(RAAT__StreamFormat** aFormats, int aSampleRate, int aBitDepth)
{
    (*aFormats)->sample_type = RAAT__SAMPLE_TYPE_PCM;
    (*aFormats)->sample_rate = aSampleRate;
    (*aFormats)->bits_per_sample = aBitDepth;
    (*aFormats)->channels = 2;
    (*aFormats)->sample_subtype = RAAT__SAMPLE_SUBTYPE_NONE;
    (*aFormats)->mqa_original_sample_rate = 0;
}

RC__Status RaatOutput::GetSupportedFormats(RC__Allocator* aAlloc, size_t* aNumFormats, RAAT__StreamFormat** aFormats)
{
    // FIXME - check whether all formats need to be listed, or only highest sample rate / bit depth
#define NumElems(arr) (sizeof arr / sizeof arr[0])
    static const int kStandardRatesPcm[] = { 32000, 41000, 48000, 88200, 96000, 176400, 192000 };
    static const int kHigherRatesPcm[] = { 352800, 384000 };
    static const int kStandardRatesDsd[] = { 2822400, 5644800 };
    static const int kHigherRatesDsd[] = { 11289600 };
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
}

void RaatOutput::SetupStream(
    RAAT__StreamFormat* aFormat,
    RAAT__OutputSetupCallback aCbSetup, void* aCbSetupData,
    RAAT__OutputLostCallback aCbLost, void* aCbLostData)
{
    // FIXME
}

RC__Status RaatOutput::TeardownStream(int aToken)
{
    // FIXME
}

RC__Status RaatOutput::StartStream(int aToken, int64_t aWallTime, int64_t aStreamTime, RAAT__Stream* aStream)
{
    // FIXME
}

RC__Status RaatOutput::GetLocalTime(int aToken, int64_t* aTime)
{
    // FIXME
}

RC__Status RaatOutput::SetRemoteTime(int /*aToken*/, int64_t /*aClockOffset*/, bool /*aNewSource*/)
{
    // FIXME
}

RC__Status RaatOutput::TryStop(int aToken)
{
    // FIXME
}

RC__Status RaatOutput::Stop()
{
    iPipeline.Stop();
}
