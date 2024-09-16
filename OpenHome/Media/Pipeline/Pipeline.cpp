#include <OpenHome/Media/Pipeline/Pipeline.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Media/Pipeline/ElementObserver.h>
#include <OpenHome/Media/Pipeline/AudioDumper.h>
#include <OpenHome/Media/Pipeline/EncodedAudioReservoir.h>
#include <OpenHome/Media/Codec/Container.h>
#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/Codec/Id3v2.h>
#include <OpenHome/Media/Codec/Mpeg4.h>
#include <OpenHome/Media/Codec/MpegTs.h>
#include <OpenHome/Media/Pipeline/DecodedAudioValidator.h>
#include <OpenHome/Media/Pipeline/DecodedAudioAggregator.h>
#include <OpenHome/Media/Pipeline/StreamValidator.h>
#include <OpenHome/Media/Pipeline/DecodedAudioReservoir.h>
#include <OpenHome/Media/Pipeline/Ramper.h>
#include <OpenHome/Media/Pipeline/RampValidator.h>
#include <OpenHome/Media/Pipeline/Seeker.h>
#include <OpenHome/Media/Pipeline/VariableDelay.h>
#include <OpenHome/Media/Pipeline/TrackInspector.h>
#include <OpenHome/Media/Pipeline/Skipper.h>
#include <OpenHome/Media/Pipeline/Stopper.h>
#include <OpenHome/Media/Pipeline/Reporter.h>
#include <OpenHome/Media/Pipeline/AsyncTrackObserver.h>
#include <OpenHome/Media/Pipeline/AirplayReporter.h>
#include <OpenHome/Media/Pipeline/SpotifyReporter.h>
#include <OpenHome/Media/Pipeline/Brancher.h>
#include <OpenHome/Media/Pipeline/Drainer.h>
#include <OpenHome/Media/Pipeline/Attenuator.h>
#include <OpenHome/Media/Pipeline/Logger.h>
#include <OpenHome/Media/Pipeline/PhaseAdjuster.h>
#include <OpenHome/Media/Pipeline/StarterTimed.h>
#include <OpenHome/Media/Pipeline/StarvationRamper.h>
#include <OpenHome/Media/Pipeline/Muter.h>
#include <OpenHome/Media/Pipeline/VolumeRamper.h>
#include <OpenHome/Media/Pipeline/PreDriver.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Debug.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Media;

// PipelineInitParams

PipelineInitParams* PipelineInitParams::New()
{ // static
    return new PipelineInitParams();
}

PipelineInitParams::PipelineInitParams()
    : iEncodedReservoirBytes(kEncodedReservoirSizeBytes)
    , iDecodedReservoirJiffies(kDecodedReservoirSize)
    , iGorgeDurationJiffies(kGorgerSizeDefault)
    , iStarvationRamperMinJiffies(kStarvationRamperSizeDefault)
    , iMaxStreamsPerReservoir(kMaxReservoirStreamsDefault)
    , iRampLongJiffies(kLongRampDurationDefault)
    , iRampShortJiffies(kShortRampDurationDefault)
    , iRampEmergencyJiffies(kEmergencyRampDurationDefault)
    , iSenderMinLatency(kSenderMinLatency)
    , iMaxLatencyJiffies(kMaxLatencyDefault)
    , iSupportElements(EPipelineSupportElementsAll)
    , iMuter(kMuterDefault)
    , iDsdMaxSampleRate(kDsdMaxSampleRateDefault)
{
    SetThreadPriorityMax(kThreadPriorityMax);
}

PipelineInitParams::~PipelineInitParams()
{
}

void PipelineInitParams::SetEncodedReservoirSize(TUint aBytes)
{
    iEncodedReservoirBytes = aBytes;
}

void PipelineInitParams::SetDecodedReservoirSize(TUint aJiffies)
{
    iDecodedReservoirJiffies = aJiffies;
}

void PipelineInitParams::SetGorgerDuration(TUint aJiffies)
{
    iGorgeDurationJiffies = aJiffies;
}

void PipelineInitParams::SetStarvationRamperMinSize(TUint aJiffies)
{
    iStarvationRamperMinJiffies = aJiffies;
}

void PipelineInitParams::SetMaxStreamsPerReservoir(TUint aCount)
{
    iMaxStreamsPerReservoir = aCount;
}

void PipelineInitParams::SetLongRamp(TUint aJiffies)
{
    iRampLongJiffies = aJiffies;
}

void PipelineInitParams::SetShortRamp(TUint aJiffies)
{
    iRampShortJiffies = aJiffies;
}

void PipelineInitParams::SetEmergencyRamp(TUint aJiffies)
{
    iRampEmergencyJiffies = aJiffies;
}

void PipelineInitParams::SetSenderMinLatency(TUint aJiffies)
{
    iSenderMinLatency = aJiffies;
}

void PipelineInitParams::SetThreadPriorityMax(TUint aPriority)
{
    iThreadPriorityStarvationRamper = aPriority;
    iThreadPriorityCodec            = iThreadPriorityStarvationRamper - 1;
    iThreadPriorityEvent            = iThreadPriorityCodec - 1;
}

void PipelineInitParams::SetThreadPriorities(TUint aStarvationRamper, TUint aCodec, TUint aEvent)
{
    iThreadPriorityStarvationRamper = aStarvationRamper;
    iThreadPriorityCodec            = aCodec;
    iThreadPriorityEvent            = aEvent;
}

void PipelineInitParams::SetMaxLatency(TUint aJiffies)
{
    iMaxLatencyJiffies = aJiffies;
}

void PipelineInitParams::SetSupportElements(TUint aElements)
{
    iSupportElements = aElements;
}

void PipelineInitParams::SetMuter(MuterImpl aMuter)
{
    iMuter = aMuter;
}

void PipelineInitParams::SetDsdMaxSampleRate(TUint aMaxSampleRate)
{
    iDsdMaxSampleRate = aMaxSampleRate;
}

TUint PipelineInitParams::EncodedReservoirBytes() const
{
    return iEncodedReservoirBytes;
}

TUint PipelineInitParams::DecodedReservoirJiffies() const
{
    return iDecodedReservoirJiffies;
}

TUint PipelineInitParams::GorgeDurationJiffies() const
{
   return iGorgeDurationJiffies;
}

TUint PipelineInitParams::StarvationRamperMinJiffies() const
{
    return iStarvationRamperMinJiffies;
}

TUint PipelineInitParams::MaxStreamsPerReservoir() const
{
    return iMaxStreamsPerReservoir;
}

TUint PipelineInitParams::RampLongJiffies() const
{
    return iRampLongJiffies;
}

TUint PipelineInitParams::RampShortJiffies() const
{
    return iRampShortJiffies;
}

TUint PipelineInitParams::RampEmergencyJiffies() const
{
    return iRampEmergencyJiffies;
}

TUint PipelineInitParams::SenderMinLatency() const
{
    return iSenderMinLatency;
}

TUint PipelineInitParams::ThreadPriorityStarvationRamper() const
{
    return iThreadPriorityStarvationRamper;
}

TUint PipelineInitParams::ThreadPriorityCodec() const
{
    return iThreadPriorityCodec;
}

TUint PipelineInitParams::ThreadPriorityEvent() const
{
    return iThreadPriorityEvent;
}

TUint PipelineInitParams::MaxLatencyJiffies() const
{
    return iMaxLatencyJiffies;
}

TUint PipelineInitParams::SupportElements() const
{
    return iSupportElements;
}

PipelineInitParams::MuterImpl PipelineInitParams::Muter() const
{
    return iMuter;
}

TUint OpenHome::Media::PipelineInitParams::DsdMaxSampleRate() const
{
    return iDsdMaxSampleRate;
}


// Pipeline

#define ATTACH_ELEMENT(elem, ctor, prev_elem, supported, type)  \
    do {                                                        \
        if ((supported & (type)) ||                             \
            (type) == EPipelineSupportElementsMandatory) {      \
            elem = ctor;                                        \
            prev_elem = elem;                                   \
        }                                                       \
        else {                                                  \
            elem = nullptr;                                     \
        }                                                       \
    } while (0)

static Pipeline* gPipeline = nullptr;
Pipeline::Pipeline(
    PipelineInitParams* aInitParams,
    IInfoAggregator& aInfoAggregator,
    TrackFactory& aTrackFactory,
    IPipelineObserver& aObserver,
    IStreamPlayObserver& aStreamPlayObserver,
    ISeekRestreamer& aSeekRestreamer,
    IUrlBlockWriter& aUrlBlockWriter,
    Optional<IAudioTime> aAudioTime)
    : iInitParams(aInitParams)
    , iLock("PLMG")
    , iState(EStopped)
    , iLastReportedState(EPipelineStateCount)
    , iBuffering(false)
    , iWaiting(false)
    , iQuitting(false)
    , iNextFlushId(MsgFlush::kIdInvalid + 1)
    , iMaxSampleRatePcm(0)
    , iMaxSampleRateDsd(0)
{
    const TUint perStreamMsgCount = aInitParams->MaxStreamsPerReservoir() * kReservoirCount;
    TUint encodedAudioCount = ((aInitParams->EncodedReservoirBytes() + EncodedAudio::kMaxBytes - 1) / EncodedAudio::kMaxBytes); // this may only be required on platforms that don't guarantee priority based thread scheduling
    encodedAudioCount = std::max(encodedAudioCount, // songcast and some hardware inputs won't use the full capacity of each encodedAudio
                                 (kReceiverMaxLatency + kSongcastFrameJiffies - 1) / kSongcastFrameJiffies);
    const TUint maxEncodedReservoirMsgs = encodedAudioCount;
    encodedAudioCount += kRewinderMaxMsgs; // this may only be required on platforms that don't guarantee priority based thread scheduling
    const TUint msgEncodedAudioCount = encodedAudioCount + 100; // +100 allows for Split()ing by Container and CodecController
    const TUint decodedReservoirSize = aInitParams->DecodedReservoirJiffies() + aInitParams->StarvationRamperMinJiffies();

    // Work out number of decoded audio (AudioData) and MsgAudioDsd required, based on the maximum DSD sample rate supported.
    // Where empirical measurements are referenced below, these were achieved in the following way:
    // - Boot the DS.
    // - Connect to the shell.
    // - Execute "info memory" to get a baseline of pipeline peak message usage.
    // - Play a DSD track at a given sample rate.
    // - Execute "info memory" in the shell again to get the pipeline peak message usage.
    // - Compare the post-playback peak usage to the baseline peak usage to identify how many messages were required for DSD at the given sample rate.
    // - Repeat the above process for other sample rates and use this to identify a pattern in how many extra messages need to be allocated as sample rate increases.
    const auto dsdMaxSampleRate = aInitParams->DsdMaxSampleRate();
    TUint dsdExtraDecodedAudioCount = 0;
    TUint msgAudioDsdCount = 0;
    if (dsdMaxSampleRate > 0) {
        TUint dsdMultiplier = dsdMaxSampleRate / 44100;
        if (dsdMaxSampleRate % 48000 == 0) {
            dsdMultiplier = dsdMaxSampleRate / 48000;
        }

        // Existing decodedAudioCount parameter catered for up to DSD128, so let's ensure its not increased for up to DSD128 as it may result in running out of memory on some platforms.
        if (dsdMaxSampleRate > 128 * 44100) {
            // Empirically, for DSD the decodedAudioCount needs to have (2 * DSD multiplier) msgs added to it.
            // E.g., for DSD256, would require 2 * 256 = 512 additional msgs.
            static const TUint kDsdAudioMsgMultiplier = 2;
            dsdExtraDecodedAudioCount = dsdMultiplier * kDsdAudioMsgMultiplier;
        }

        // Empirically, the pipeline requires just under (4 * DSD multiplier) MsgAudioDsd.
        // Want to give some headroom in allocated messages. Previous approach was to allocate ~1.5x the messages required for the maximum supported DSD sample rate.
        // As we're using a multiplier of 4 here, add 2 to it to increase it to 1.5x to save scaling the message count to 1.5x later on.
        static const TUint kDsdMsgMultiplier = 4 + 2;
        msgAudioDsdCount = dsdMultiplier * kDsdMsgMultiplier;
    }

    TUint decodedAudioCount = ((decodedReservoirSize + iInitParams->SenderMinLatency()) / DecodedAudioAggregator::kMaxJiffies) + 200; // +200 allows for DSD support (not 256), songcast sender, some smaller msgs and some buffering in non-reservoir elements
    decodedAudioCount += dsdExtraDecodedAudioCount;

    const TUint msgAudioPcmCount = decodedAudioCount + 100; // +100 allows for Split()ing in various elements
    const TUint msgHaltCount = perStreamMsgCount * 2; // worst case is tiny Vorbis track with embedded metatext in a single-track playlist with repeat
    MsgFactoryInitParams msgInit;
    msgInit.SetMsgModeCount(kMsgCountMode);
    msgInit.SetMsgTrackCount(perStreamMsgCount);
    msgInit.SetMsgDrainCount(kMsgCountDrain);
    msgInit.SetMsgDelayCount(perStreamMsgCount);
    msgInit.SetMsgEncodedStreamCount(perStreamMsgCount);
    msgInit.SetMsgStreamSegmentCount(perStreamMsgCount);
    msgInit.SetMsgAudioEncodedCount(msgEncodedAudioCount, encodedAudioCount);
    msgInit.SetMsgMetaTextCount(perStreamMsgCount);
    msgInit.SetMsgStreamInterruptedCount(perStreamMsgCount);
    msgInit.SetMsgHaltCount(msgHaltCount);
    msgInit.SetMsgFlushCount(kMsgCountFlush);
    msgInit.SetMsgWaitCount(perStreamMsgCount);
    msgInit.SetMsgDecodedStreamCount(perStreamMsgCount);
    msgInit.SetMsgAudioPcmCount(msgAudioPcmCount, decodedAudioCount);
    if (msgAudioDsdCount > 0) {
        msgInit.SetMsgAudioDsdCount(msgAudioDsdCount);
    }
    msgInit.SetMsgSilenceCount(kMsgCountSilence);
    msgInit.SetMsgPlayableCount(kMsgCountPlayablePcm, kMsgCountPlayableDsd, kMsgCountPlayableSilence);
    msgInit.SetMsgQuitCount(kMsgCountQuit);
    iMsgFactory = new MsgFactory(aInfoAggregator, msgInit);

    iEventThread = new PipelineElementObserverThread(aInitParams->ThreadPriorityEvent());
    iBranchController = new BranchController();
    IPipelineElementDownstream* downstream = nullptr;
    IPipelineElementUpstream* upstream = nullptr;
    const auto elementsSupported = aInitParams->SupportElements();

    // disable "conditional expression is constant" warnings from ATTACH_ELEMENT
#ifdef _WIN32
# pragma warning( push )
# pragma warning( disable : 4127)
#endif // _WIN32

    // Construct encoded reservoir out of sequence.  It doesn't pull from the left so doesn't need to know its preceding element
    iEncodedAudioReservoir = new EncodedAudioReservoir(*iMsgFactory, *this, maxEncodedReservoirMsgs, aInitParams->MaxStreamsPerReservoir());
    upstream = iEncodedAudioReservoir;
    ATTACH_ELEMENT(iLoggerEncodedAudioReservoir, new Logger(*upstream, "Encoded Audio Reservoir"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);

    // Construct audio dumper out of sequence. It doesn't pull from left so doesn't need to know it's preceding element (but it does need to know the element it's pushing to).
    downstream = iEncodedAudioReservoir;
    ATTACH_ELEMENT(iAudioDumper, new AudioDumper(*iEncodedAudioReservoir),
                   downstream, elementsSupported, EPipelineSupportElementsAudioDumper);
    iPipelineStart = iAudioDumper;
    if (iPipelineStart == nullptr) {
        iPipelineStart = iEncodedAudioReservoir;
    }

    const TBool createLoggers = (elementsSupported & EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iContainer, new Codec::ContainerController(*iMsgFactory, *upstream, aUrlBlockWriter, createLoggers),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerContainer, new Logger(*iContainer, "Codec Container"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);

    // Construct decoded reservoir out of sequence.  It doesn't pull from the left so doesn't need to know its preceding element
    iDecodedAudioReservoir = new DecodedAudioReservoir(*iMsgFactory, *this,
                                                       aInitParams->DecodedReservoirJiffies(),
                                                       aInitParams->MaxStreamsPerReservoir(),
                                                       aInitParams->GorgeDurationJiffies());
    downstream = iDecodedAudioReservoir;

    ATTACH_ELEMENT(iDecodedAudioValidatorDecodedAudioAggregator, new DecodedAudioValidator("Decoded Audio Aggregator", *iDecodedAudioReservoir),
                   downstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iLoggerDecodedAudioAggregator,
                   new Logger("Decoded Audio Aggregator", *downstream),
                   downstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iDecodedAudioAggregator, new DecodedAudioAggregator(*downstream),
                   downstream, elementsSupported, EPipelineSupportElementsMandatory);

    ATTACH_ELEMENT(iDecodedAudioValidatorStreamValidator, new DecodedAudioValidator("StreamValidator", *iDecodedAudioAggregator),
                   downstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iLoggerStreamValidator, new Logger("StreamValidator", *downstream),
                   downstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iStreamValidator, new StreamValidator(*iMsgFactory, *downstream),
                   downstream, elementsSupported, EPipelineSupportElementsMandatory);

    // construct push logger slightly out of sequence
    ATTACH_ELEMENT(iDecodedAudioValidatorCodec, new DecodedAudioValidator("Codec Controller", *iStreamValidator),
                   downstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iRampValidatorCodec, new RampValidator("Codec Controller", *downstream),
                   downstream, elementsSupported, EPipelineSupportElementsRampValidator);
    ATTACH_ELEMENT(iLoggerCodecController, new Logger("Codec Controller", *downstream),
                   downstream, elementsSupported, EPipelineSupportElementsLogger);
    iCodecController = new Codec::CodecController(*iMsgFactory, *upstream, *downstream, aUrlBlockWriter,
                                                  kSongcastFrameJiffies, aInitParams->ThreadPriorityCodec(),
                                                  createLoggers);

    upstream = iDecodedAudioReservoir;
    ATTACH_ELEMENT(iLoggerDecodedAudioReservoir,
                   new Logger(*iDecodedAudioReservoir, "Decoded Audio Reservoir"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iDecodedAudioValidatorDecodedAudioReservoir, new DecodedAudioValidator(*upstream, "Decoded Audio Reservoir"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iRamper, new Ramper(*upstream, aInitParams->RampLongJiffies(), aInitParams->RampShortJiffies()),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerRamper, new Logger(*iRamper, "Ramper"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iRampValidatorRamper, new RampValidator(*upstream, "Ramper"),
                   upstream, elementsSupported, EPipelineSupportElementsRampValidator);
    ATTACH_ELEMENT(iDecodedAudioValidatorRamper, new DecodedAudioValidator(*upstream, "Ramper"),
                    upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iSeeker, new Seeker(*iMsgFactory, *upstream, *iCodecController, aSeekRestreamer, aInitParams->RampShortJiffies()),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerSeeker, new Logger(*iSeeker, "Seeker"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iRampValidatorSeeker, new RampValidator(*upstream, "Seeker"),
                   upstream, elementsSupported, EPipelineSupportElementsRampValidator);
    ATTACH_ELEMENT(iDecodedAudioValidatorSeeker, new DecodedAudioValidator(*upstream, "Seeker"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iDrainer1, new DrainerLeft(*iMsgFactory, *upstream),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerDrainer1, new Logger(*iDrainer1, "DrainerLeft"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iVariableDelay1,
                   new VariableDelayLeft(*iMsgFactory, *upstream,
                                         aInitParams->RampEmergencyJiffies(),
                                         iInitParams->SenderMinLatency()),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerVariableDelay1, new Logger(*iVariableDelay1, "VariableDelay1"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iRampValidatorDelay1, new RampValidator(*upstream, "VariableDelay1"),
                   upstream, elementsSupported, EPipelineSupportElementsRampValidator);
    ATTACH_ELEMENT(iDecodedAudioValidatorDelay1, new DecodedAudioValidator(*upstream, "VariableDelay1"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iSkipper, new Skipper(*iMsgFactory, *upstream,
                                         aInitParams->RampLongJiffies(), aInitParams->RampShortJiffies()),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerSkipper, new Logger(*iSkipper, "Skipper"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iRampValidatorSkipper, new RampValidator(*upstream, "Skipper"),
                   upstream, elementsSupported, EPipelineSupportElementsRampValidator);
    ATTACH_ELEMENT(iDecodedAudioValidatorSkipper, new DecodedAudioValidator(*upstream, "Skipper"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iTrackInspector, new TrackInspector(*upstream),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerTrackInspector, new Logger(*iTrackInspector, "TrackInspector"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iWaiter, new Waiter(*iMsgFactory, *upstream, *this, *iEventThread, aInitParams->RampShortJiffies()),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerWaiter, new Logger(*iWaiter, "Waiter"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iRampValidatorWaiter, new RampValidator(*upstream, "Waiter"),
                   upstream, elementsSupported, EPipelineSupportElementsRampValidator);
    ATTACH_ELEMENT(iDecodedAudioValidatorWaiter, new DecodedAudioValidator(*upstream, "Waiter"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iStopper, new Stopper(*iMsgFactory, *upstream, *this, *iEventThread,
                                         aInitParams->RampLongJiffies(), aInitParams->RampShortJiffies()),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    iStopper->SetStreamPlayObserver(aStreamPlayObserver);
    ATTACH_ELEMENT(iLoggerStopper, new Logger(*iStopper, "Stopper"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iRampValidatorStopper, new RampValidator(*upstream, "Stopper"),
                   upstream, elementsSupported, EPipelineSupportElementsRampValidator);
    ATTACH_ELEMENT(iDecodedAudioValidatorStopper, new DecodedAudioValidator(*upstream, "Stopper"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iAsyncTrackObserver, new Media::AsyncTrackObserver(*upstream, *iMsgFactory, aTrackFactory),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerTrackReporter, new Logger(*iAsyncTrackObserver, "AsyncTrackObserver"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iAirplayReporter, new Media::AirplayReporter(*upstream, *iMsgFactory, aTrackFactory),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iSpotifyReporter, new Media::SpotifyReporter(*upstream, *iMsgFactory, aTrackFactory),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerSpotifyReporter, new Logger(*iSpotifyReporter, "SpotifyReporter"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iReporter, new Reporter(*upstream, aObserver, *iEventThread),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerReporter, new Logger(*iReporter, "Reporter"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iBrancherSongcast, new Brancher(*upstream, Brn("BrancherSongcast"), IBrancher::EPriority::Default),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerBrancherSongcast, new Logger(*iBrancherSongcast, "BrancherSongcast"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iDecodedAudioValidatorBrancher, new DecodedAudioValidator(*upstream, "BrancherSongcast"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iAttenuator, new Attenuator(*upstream),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerAttenuator, new Logger(*iAttenuator, "Attenuator"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iDrainer2, new DrainerRight(*iMsgFactory, *upstream),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerDrainer2, new Logger(*iDrainer2, "DrainerRight"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iVariableDelay2,
                   new VariableDelayRight(*iMsgFactory, *upstream,
                                          aInitParams->RampEmergencyJiffies(),
                                          aInitParams->StarvationRamperMinJiffies()),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    iVariableDelay1->SetObserver(*iVariableDelay2);
    ATTACH_ELEMENT(iLoggerVariableDelay2, new Logger(*iVariableDelay2, "VariableDelay2"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iRampValidatorDelay2, new RampValidator(*upstream, "VariableDelay2"),
                   upstream, elementsSupported, EPipelineSupportElementsRampValidator);
    ATTACH_ELEMENT(iDecodedAudioValidatorDelay2, new DecodedAudioValidator(*upstream, "VariableDelay2"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iStarvationRamper,
                   new StarvationRamper(*iMsgFactory, *upstream, *this, *iEventThread,
                                        aInitParams->StarvationRamperMinJiffies(),
                                        aInitParams->ThreadPriorityStarvationRamper(),
                                        aInitParams->RampShortJiffies(), aInitParams->MaxStreamsPerReservoir()),
                                        upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerStarvationRamper, new Logger(*iStarvationRamper, "StarvationRamper"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iRampValidatorStarvationRamper, new RampValidator(*upstream, "StarvationRamper"),
                   upstream, elementsSupported, EPipelineSupportElementsRampValidator | EPipelineSupportElementsValidatorMinimal);
    ATTACH_ELEMENT(iDecodedAudioValidatorStarvationRamper,
                   new DecodedAudioValidator(*upstream, "StarvationRamper"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iPhaseAdjuster, new PhaseAdjuster(*iMsgFactory, *upstream, *iStarvationRamper,
        aInitParams->RampLongJiffies(),
        aInitParams->RampShortJiffies(),
        aInitParams->StarvationRamperMinJiffies()),
        upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerPhaseAdjuster, new Logger(*iPhaseAdjuster, "PhaseAdjuster"),
        upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iRampValidatorPhaseAdjuster, new RampValidator(*upstream, "PhaseAdjuster"),
        upstream, elementsSupported, EPipelineSupportElementsRampValidator | EPipelineSupportElementsValidatorMinimal);
    ATTACH_ELEMENT(iDecodedAudioValidatorPhaseAdjuster,
        new DecodedAudioValidator(*upstream, "PhaseAdjuster"),
        upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    if (aAudioTime.Ok()) {
        ATTACH_ELEMENT(iStarterTimed, new StarterTimed(*iMsgFactory, *upstream, aAudioTime.Unwrap()),
                       upstream, elementsSupported, EPipelineSupportElementsMandatory);
        ATTACH_ELEMENT(iLoggerStarterTimed, new Logger(*iStarterTimed, "StarterTimed"),
                       upstream, elementsSupported, EPipelineSupportElementsLogger);
    }
    else {
        iStarterTimed = nullptr;
        iLoggerStarterTimed = nullptr;
    }
    IMute* muter = nullptr;
    if (aInitParams->Muter() == PipelineInitParams::MuterImpl::eRampSamples) {
        ATTACH_ELEMENT(iMuterSamples, new Muter(*iMsgFactory, *upstream, aInitParams->RampLongJiffies()),
                       upstream, elementsSupported, EPipelineSupportElementsMandatory);
        muter = iMuterSamples;
        iMuterVolume = nullptr;
        ATTACH_ELEMENT(iLoggerMuter, new Logger(*iMuterSamples, "Muter"),
                       upstream, elementsSupported, EPipelineSupportElementsLogger);
    }
    else {
        ATTACH_ELEMENT(iMuterVolume, new MuterVolume(*iMsgFactory, *upstream),
                       upstream, elementsSupported, EPipelineSupportElementsMandatory);
        muter = iMuterVolume;
        iMuterSamples = nullptr;
        ATTACH_ELEMENT(iLoggerMuter, new Logger(*iMuterVolume, "Muter"),
                       upstream, elementsSupported, EPipelineSupportElementsLogger);
    }
    ATTACH_ELEMENT(iDecodedAudioValidatorMuter, new DecodedAudioValidator(*upstream, "Muter"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator | EPipelineSupportElementsValidatorMinimal);
    ATTACH_ELEMENT(iVolumeRamper, new VolumeRamper(*iMsgFactory, *upstream),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerVolumeRamper, new Logger(*iVolumeRamper, "VolumeRamper"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iBrancherBluez, new Brancher(*upstream, Brn("BrancherBluez"), IBrancher::EPriority::Exclusive),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerBrancherBluez, new Logger(*iBrancherBluez, "BrancherBluez"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iPreDriver, new PreDriver(*upstream),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    iLoggerPreDriver = new Logger(*iPreDriver, "PreDriver");

#ifdef _WIN32
# pragma warning( pop )
#endif // _WIN32

    iPipelineEnd = iLoggerPreDriver;
    if (iPipelineEnd == nullptr) {
        iPipelineEnd = iPreDriver;
    }
    iMuteCounted = new MuteCounted(*muter);

    gPipeline = this;

    iBranchController->AttachBrancher(*iBrancherSongcast);
    iBranchController->AttachBrancher(*iBrancherBluez);

    //iAudioDumper->SetEnabled(true);

    //iLoggerEncodedAudioReservoir->SetEnabled(true);
    //iLoggerContainer->SetEnabled(true);
    //iLoggerCodecController->SetEnabled(true);
    //iLoggerStreamValidator->SetEnabled(true);
    //iLoggerDecodedAudioAggregator->SetEnabled(true);
    //iLoggerDecodedAudioReservoir->SetEnabled(true);
    //iLoggerRamper->SetEnabled(true);
    //iLoggerSeeker->SetEnabled(true);
    //iLoggerDrainer1->SetEnabled(true);
    //iLoggerVariableDelay1->SetEnabled(true);
    //iLoggerSkipper->SetEnabled(true);
    //iLoggerTrackInspector->SetEnabled(true);
    //iLoggerWaiter->SetEnabled(true);
    //iLoggerStopper->SetEnabled(true);
    //iLoggerSpotifyReporter->SetEnabled(true);
    //iLoggerReporter->SetEnabled(true);
    //iLoggerBrancherSongcast->SetEnabled(true);
    //iLoggerAttenuator->SetEnabled(true);
    //iLoggerDrainer2->SetEnabled(true);
    //iLoggerVariableDelay2->SetEnabled(true);
    //iLoggerStarvationRamper->SetEnabled(true);
    //iLoggerPhaseAdjuster->SetEnabled(true);
    //iLoggerMuter->SetEnabled(true);
    //iLoggerVolumeRamper->SetEnabled(true);
    //iLoggerBrancherBluez->SetEnabled(true);

    // A logger that is enabled will block waiting for MsgQuit in its dtor
    // ~Pipeline (below) relies on this to synchronise its destruction
    // i.e. NEVER DISABLE THIS LOGGER
    iLoggerPreDriver->SetEnabled(true);

    //iLoggerEncodedAudioReservoir->SetFilter(Logger::EMsgAll);
    //iLoggerContainer->SetFilter(Logger::EMsgAll);
    //iLoggerCodecController->SetFilter(Logger::EMsgAll);
    //iLoggerStreamValidator->SetFilter(Logger::EMsgAll);
    //iLoggerDecodedAudioAggregator->SetFilter(Logger::EMsgAll);
    //iLoggerDecodedAudioReservoir->SetFilter(Logger::EMsgAll);
    //iLoggerRamper->SetFilter(Logger::EMsgAll);
    //iLoggerSeeker->SetFilter(Logger::EMsgAll);
    //iLoggerDrainer1->SetFilter(Logger::EMsgAll);
    //iLoggerVariableDelay1->SetFilter(Logger::EMsgAll);
    //iLoggerSkipper->SetFilter(Logger::EMsgAll);
    //iLoggerTrackInspector->SetFilter(Logger::EMsgAll);
    //iLoggerWaiter->SetFilter(Logger::EMsgAll);
    //iLoggerStopper->SetFilter(Logger::EMsgAll);
    //iLoggerSpotifyReporter->SetFilter(Logger::EMsgAll);
    //iLoggerReporter->SetFilter(Logger::EMsgAll);
    //iLoggerBrancherSongcast->SetFilter(Logger::EMsgAll);
    //iLoggerAttenuator->SetFilter(Logger::EMsgAll);
    //iLoggerDrainer2->SetFilter(Logger::EMsgAll);
    //iLoggerVariableDelay2->SetFilter(Logger::EMsgAll);
    //iLoggerStarvationRamper->SetFilter(Logger::EMsgAll);
    //iLoggerPhaseAdjuster->SetFilter(Logger::EMsgAll);
    //iLoggerMuter->SetFilter(Logger::EMsgAll);
    //iLoggerVolumeRamper->SetFilter(Logger::EMsgAll);
    //iLoggerBrancherBluez->SetFilter(Logger::EMsgAll);
    //iLoggerPreDriver->SetFilter(Logger::EMsgAll);
}

Pipeline::~Pipeline()
{
    // FIXME - should we wait for the pipeline to be halted before issuing a Quit?
    //         ...otherwise, MsgQuit goes down the pipeline ahead of final audio
    Quit();
    iEventThread->Stop();

    // iBranchController->RemoveBrancher(Brn("BrancherSongcast"));
    iBranchController->RemoveBrancher(Brn("BrancherBluez"));


    // loggers (if non-null) and iPreDriver will block until they receive the Quit msg
    delete iMuteCounted;
    delete iLoggerPreDriver;
    delete iPreDriver;
    delete iLoggerBrancherBluez;
    delete iBrancherBluez;
    delete iLoggerVolumeRamper;
    delete iVolumeRamper;
    delete iDecodedAudioValidatorMuter;
    delete iLoggerMuter;
    delete iMuterVolume;
    delete iMuterSamples;
    delete iDecodedAudioValidatorPhaseAdjuster;
    delete iRampValidatorPhaseAdjuster;
    delete iLoggerPhaseAdjuster;
    delete iPhaseAdjuster;
    delete iDecodedAudioValidatorStarvationRamper;
    delete iRampValidatorStarvationRamper;
    delete iLoggerStarvationRamper;
    delete iStarvationRamper;
    delete iLoggerAttenuator;
    delete iAttenuator;
    delete iDecodedAudioValidatorDelay2;
    delete iRampValidatorDelay2;
    delete iLoggerVariableDelay2;
    delete iVariableDelay2;
    delete iStarterTimed;
    delete iLoggerStarterTimed;
    delete iLoggerDrainer2;
    delete iDrainer2;
    delete iDecodedAudioValidatorBrancher;
    delete iLoggerBrancherSongcast;
    delete iBrancherSongcast;
    delete iLoggerTrackReporter;
    delete iLoggerReporter;
    delete iReporter;
    delete iAsyncTrackObserver;
    delete iAirplayReporter;
    delete iLoggerSpotifyReporter;
    delete iSpotifyReporter;
    delete iDecodedAudioValidatorStopper;
    delete iRampValidatorStopper;
    delete iLoggerStopper;
    delete iStopper;
    delete iDecodedAudioValidatorWaiter;
    delete iRampValidatorWaiter;
    delete iLoggerWaiter;
    delete iWaiter;
    delete iLoggerTrackInspector;
    delete iTrackInspector;
    delete iDecodedAudioValidatorSkipper;
    delete iRampValidatorSkipper;
    delete iLoggerSkipper;
    delete iSkipper;
    delete iDecodedAudioValidatorDelay1;
    delete iRampValidatorDelay1;
    delete iLoggerDrainer1;
    delete iDrainer1;
    delete iLoggerVariableDelay1;
    delete iVariableDelay1;
    delete iDecodedAudioValidatorSeeker;
    delete iRampValidatorSeeker;
    delete iLoggerSeeker;
    delete iSeeker;
    delete iRampValidatorRamper;
    delete iDecodedAudioValidatorRamper;
    delete iLoggerRamper;
    delete iRamper;
    delete iLoggerDecodedAudioReservoir;
    delete iDecodedAudioValidatorDecodedAudioReservoir;
    delete iCodecController; // out of order - the start of a chain of pushers from iLoggerCodecController to iDecodedAudioReservoir
    delete iDecodedAudioReservoir;
    delete iDecodedAudioValidatorDecodedAudioAggregator;
    delete iLoggerDecodedAudioAggregator;
    delete iDecodedAudioAggregator;
    delete iDecodedAudioValidatorStreamValidator;
    delete iLoggerStreamValidator;
    delete iStreamValidator;
    delete iDecodedAudioValidatorCodec;
    delete iRampValidatorCodec;
    delete iLoggerCodecController;
    delete iLoggerContainer;
    delete iContainer;
    delete iAudioDumper;
    delete iLoggerEncodedAudioReservoir;
    delete iEncodedAudioReservoir;
    delete iBranchController;
    delete iEventThread;
    delete iMsgFactory;
    delete iInitParams;
}

void Pipeline::AddContainer(Codec::ContainerBase* aContainer)
{
    iContainer->AddContainer(aContainer);
}

void Pipeline::AddCodec(Codec::CodecBase* aCodec)
{
    iCodecController->AddCodec(aCodec);
}

void Pipeline::Start(IVolumeRamper& aVolumeRamper, IVolumeMuterStepped& aVolumeMuter)
{
    iVolumeRamper->SetVolumeRamper(aVolumeRamper);
    if (iMuterVolume != nullptr) {
        iMuterVolume->Start(aVolumeMuter);
    }
    iCodecController->Start();
    iEventThread->Start();
}

void Pipeline::Quit()
{
    LOG(kPipeline, "> Pipeline::Quit()\n");
    if (iQuitting) {
        return;
    }
    iQuitting = true;
    DoPlay(true);
}

void Pipeline::NotifyStatus()
{
    EPipelineState state;
    {
        AutoMutex _(iLock);
        if (iQuitting) {
            return;
        }
        switch (iState)
        {
        case EPlaying:
            state = (iWaiting? EPipelineWaiting : (iBuffering? EPipelineBuffering : EPipelinePlaying));
            break;
        case EPaused:
            state = EPipelinePaused;
            break;
        case EStopped:
            state = EPipelineStopped;
            break;
        default:
            ASSERTS();
            state = EPipelineBuffering; // will never reach here but the compiler doesn't realise this
        }
        if (state == iLastReportedState) {
            return;
        }
        iLastReportedState = state;
    }
    iReporter->SetPipelineState(state); // Use Reporter's event callback mechanism to notify observers asynchronously.
}

MsgFactory& Pipeline::Factory()
{
    return *iMsgFactory;
}

void Pipeline::Play()
{
    DoPlay(false);
}

void Pipeline::DoPlay(TBool aQuit)
{
    TBool notifyStatus = true;
    iLock.Wait();
    if (iState == EPlaying) {
        notifyStatus = false;
    }
    iState = EPlaying;
    iLock.Signal();

    if (aQuit) {
        iStopper->Quit();
    }
    else {
        iStopper->Play();
    }
    if (notifyStatus) {
        NotifyStatus();
    }
}

void Pipeline::Pause()
{
    try {
        iStopper->BeginPause();
    }
    catch (StopperStreamNotPausable&) {
        THROW(PipelineStreamNotPausable);
    }
}

void Pipeline::Wait(TUint aFlushId)
{
    const TBool rampDown = (iState == EPlaying);
    iWaiter->Wait(aFlushId, rampDown);
}

void Pipeline::FlushQuick(TUint aFlushId)
{
    Wait(aFlushId);
    // iCodecController->Flush(aFlushId);
    iStarvationRamper->Flush(aFlushId);
}

void Pipeline::Stop(TUint aHaltId)
{
    iLock.Wait();
    /* FIXME - is there any race where iBuffering is true but the pipeline is also
               running, meaning that we want to allow Stopper to ramp down? */
    if (iBuffering) {
        iSkipper->RemoveAll(aHaltId, false);
    }
    iStopper->BeginStop(aHaltId);
    iLock.Signal();
}

void Pipeline::RemoveCurrentStream()
{
    const TBool rampDown = (iState == EPlaying);
    iSkipper->RemoveCurrentStream(rampDown);
}

void Pipeline::RemoveAll(TUint aHaltId)
{
    const TBool rampDown = (iState == EPlaying);
    iSkipper->RemoveAll(aHaltId, rampDown);
}

void Pipeline::Block()
{
    iSkipper->Block();
}

void Pipeline::Unblock()
{
    iSkipper->Unblock();
}

void Pipeline::Seek(TUint aStreamId, TUint aSecondsAbsolute)
{
    const TBool rampDown = (iState == EPlaying);
    iSeeker->Seek(aStreamId, aSecondsAbsolute, rampDown);
}

void Pipeline::AddObserver(ITrackObserver& aObserver)
{
    iTrackInspector->AddObserver(aObserver);
}

IAsyncTrackObserver& Pipeline::AsyncTrackObserver() const
{
    return *iAsyncTrackObserver;
}

IAirplayReporter& Pipeline::AirplayReporter() const
{
    return *iAirplayReporter;
}

IAirplayTrackObserver& Pipeline::AirplayTrackObserver() const
{
    return *iAirplayReporter;
}

ISpotifyReporter& Pipeline::SpotifyReporter() const
{
    return *iSpotifyReporter;
}

ISpotifyTrackObserver& Pipeline::SpotifyTrackObserver() const
{
    return *iSpotifyReporter;
}

IClockPuller& Pipeline::GetPhaseAdjuster()
{
    return *iPhaseAdjuster;
}

IBranchController& Pipeline::GetBranchController() const
{
    return *iBranchController;
}

TUint Pipeline::SenderMinLatencyMs() const
{
    return Jiffies::ToMs(iInitParams->SenderMinLatency());
}

void Pipeline::GetThreadPriorityRange(TUint& aMin, TUint& aMax) const
{
    aMax = iInitParams->ThreadPriorityStarvationRamper();
    aMin = iInitParams->ThreadPriorityCodec();
}

void Pipeline::GetThreadPriorities(TUint& aFlywheelRamper, TUint& aStarvationRamper, TUint& aCodec, TUint& aEvent)
{
    aFlywheelRamper = iStarvationRamper->ThreadPriorityFlywheelRamper();
    aStarvationRamper = iStarvationRamper->ThreadPriorityStarvationRamper();
    aCodec = iInitParams->ThreadPriorityCodec();
    aEvent = iInitParams->ThreadPriorityEvent();
}

void Pipeline::GetMaxSupportedSampleRates(TUint& aPcm, TUint& aDsd) const
{
    aPcm = iMaxSampleRatePcm;
    aDsd = iMaxSampleRateDsd;
}

void PipelineLogBuffers()
{
    if (gPipeline != nullptr) {
        gPipeline->LogBuffers();
    }
}

#ifdef PIPELINE_LOG_AUDIO_THROUGHPUT
static void LogComponentAudioThroughput(Logger* aLogger) {
    if (aLogger != nullptr) {
        aLogger->LogAudio();
    }
}
#endif // PIPELINE_LOG_AUDIO_THROUGHPUT

void Pipeline::LogBuffers() const
{
    const TUint encodedBytes = iEncodedAudioReservoir->SizeInBytes();
    const TUint decodedMs = Jiffies::ToMs(iDecodedAudioReservoir->SizeInJiffies());
    const TUint starvationMs = Jiffies::ToMs(iStarvationRamper->SizeInJiffies());
    Log::Print("Pipeline utilisation: encodedBytes=%u, decodedMs=%u, starvationRamper=%u\n",
               encodedBytes, decodedMs, starvationMs);
#ifdef PIPELINE_LOG_AUDIO_THROUGHPUT
    LogComponentAudioThroughput(iLoggerCodecController);
    LogComponentAudioThroughput(iLoggerStreamValidator);
    LogComponentAudioThroughput(iLoggerDecodedAudioAggregator);
    LogComponentAudioThroughput(iLoggerDecodedAudioReservoir);
    LogComponentAudioThroughput(iLoggerRamper);
    LogComponentAudioThroughput(iLoggerSeeker);
    LogComponentAudioThroughput(iLoggerDrainer1);
    LogComponentAudioThroughput(iLoggerVariableDelay1);
    LogComponentAudioThroughput(iLoggerSkipper);
    LogComponentAudioThroughput(iLoggerTrackInspector);
    LogComponentAudioThroughput(iLoggerWaiter);
    LogComponentAudioThroughput(iLoggerStopper);
    LogComponentAudioThroughput(iLoggerSpotifyReporter);
    LogComponentAudioThroughput(iLoggerReporter);
    LogComponentAudioThroughput(iLoggerRouter);
    LogComponentAudioThroughput(iLoggerAttenuator);
    LogComponentAudioThroughput(iLoggerDrainer2);
    LogComponentAudioThroughput(iLoggerVariableDelay2);
    LogComponentAudioThroughput(iLoggerStarvationRamper);
    LogComponentAudioThroughput(iLoggerPhaseAdjuster);
    LogComponentAudioThroughput(iLoggerMuter);
    LogComponentAudioThroughput(iLoggerVolumeRamper);
    LogComponentAudioThroughput(iLoggerPreDriver);
#endif // PIPELINE_LOG_AUDIO_THROUGHPUT
}

void Pipeline::Push(Msg* aMsg)
{
    iPipelineStart->Push(aMsg);
}

Msg* Pipeline::Pull()
{
    return iPipelineEnd->Pull();
}

void Pipeline::SetAnimator(IPipelineAnimator& aAnimator)
{
    iCodecController->SetAnimator(aAnimator);
    iStreamValidator->SetAnimator(aAnimator);
    iVariableDelay1->SetAnimator(aAnimator);
    iVariableDelay2->SetAnimator(aAnimator);
    iPhaseAdjuster->SetAnimator(aAnimator);
    if (iStarterTimed != nullptr) {
        iStarterTimed->SetAnimator(aAnimator);
    }
    if (iMuterSamples != nullptr) {
        iMuterSamples->SetAnimator(aAnimator);
    }
    aAnimator.PipelineAnimatorGetMaxSampleRates(iMaxSampleRatePcm, iMaxSampleRateDsd);
}

void Pipeline::PipelinePaused()
{
    iLock.Wait();
    iState = EPaused;
    iLock.Signal();
    NotifyStatus();
}

void Pipeline::PipelineStopped()
{
    iLock.Wait();
    iState = EStopped;
    iLock.Signal();
    NotifyStatus();
}

void Pipeline::PipelinePlaying()
{
    iLock.Wait();
    iState = EPlaying;
    iLock.Signal();
    NotifyStatus();
}

TUint Pipeline::NextFlushId()
{
    /* non-use of iLock is deliberate.  It isn't absolutely required since all callers
       run in the Filler thread.  If we re-instate the lock, the call to
       RemoveCurrentStream() in Stop() will need to move outside its lock. */
    TUint id = iNextFlushId++;
    return id;
}

void Pipeline::PipelineWaiting(TBool aWaiting)
{
    iLock.Wait();
    iWaiting = aWaiting;
    iLock.Signal();
    NotifyStatus();
}

void Pipeline::RemoveStream(TUint aStreamId)
{
    (void)iSkipper->TryRemoveStream(aStreamId, !iBuffering);
}

void Pipeline::Mute()
{
    iMuteCounted->Mute();
}

void Pipeline::Unmute()
{
    iMuteCounted->Unmute();
}

void Pipeline::PostPipelineLatencyChanged()
{
    // Nothing to do here
}

void Pipeline::SetAttenuation(TUint aAttenuation)
{
    iAttenuator->SetAttenuation(aAttenuation);
}

void Pipeline::DrainAllAudio()
{
    iStarvationRamper->DrainAllAudio();
}

void Pipeline::StartAt(TUint64 aTime)
{
    if (iStarterTimed == nullptr) {
        THROW(AudioTimeNotSupported);
    }
    iStarterTimed->StartAt(aTime);
}

void Pipeline::NotifyStarvationRamperBuffering(TBool aBuffering)
{
    iLock.Wait();
    iBuffering = aBuffering;
    const TBool notify = (iState == EPlaying);
    iLock.Signal();
    if (notify) {
        NotifyStatus();
#if 1
        if (aBuffering && !iWaiting) {
            const TUint encodedBytes = iEncodedAudioReservoir->SizeInBytes();
            const TUint decodedMs = Jiffies::ToMs(iDecodedAudioReservoir->SizeInJiffies());
            Log::Print("Pipeline utilisation: encodedBytes=%u, decodedMs=%u\n", encodedBytes, decodedMs);
        }
#endif
    }
}
