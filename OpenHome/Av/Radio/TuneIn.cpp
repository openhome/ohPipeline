#include <OpenHome/Av/Radio/TuneIn.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Functor.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/Net/Core/OhNet.h>
#include <OpenHome/Av/Radio/PresetDatabase.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/DnsChangeNotifier.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/MimeTypeList.h>
#include <OpenHome/Private/NetworkAdapterList.h>

#include <limits.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;

const Brn TuneInApi::kTuneInPresetsRequest("http://opml.radiotime.com/Browse.ashx?&c=presets&options=recurse:tuneShows");
const Brn TuneInApi::kPartnerId("&partnerId=");
const Brn TuneInApi::kUsername("&username=");

const Brn TuneInApi::kTuneInStationRequest("http://opml.radiotime.com/Tune.ashx?");
const Brn TuneInApi::kTuneInPodcastBrowse("&c=pbrowse");
const Brn TuneInApi::kFormats("&formats=mp3,wma,aac,wmvideo,ogg,hls");
const Brn TuneInApi::kTuneInItemId("&id=");


// RadioPresetsTuneIn

const Brn RadioPresetsTuneIn::kConfigKeyUsername("Radio.TuneInUserName");
const Brn RadioPresetsTuneIn::kConfigUsernameDefault("linnproducts");
const Brn RadioPresetsTuneIn::kDisplayName("TuneIn");

typedef struct MimeTuneInPair
{
    const TChar* iMimeType;
    const TChar* iTuneInFormat;
} MimeTuneInPair;

RadioPresetsTuneIn::RadioPresetsTuneIn(Environment& aEnv,
                                       const Brx& aPartnerId,
                                       IConfigInitialiser& aConfigInit,
                                       Credentials& aCredentialsManager,
                                       Media::MimeTypeList& aMimeTypeList)
    : iLock("RPTI")
    , iEnv(aEnv)
    , iPresetWriter(nullptr)
    , iWriteBuffer(iSocket)
    , iWriterRequest(iWriteBuffer)
    , iReadBuffer(iSocket)
    , iReaderUntil(iReadBuffer)
    , iReaderResponse(aEnv, iReaderUntil)
    , iSupportedFormats("&formats=")
    , iPartnerId(aPartnerId)
{
    const MimeTuneInPair kTypes[] = {{"audio/mpeg", "mp3"}
                                    ,{"audio/x-ms-wma", "wma"}
                                    ,{"audio/aac", "aac"}
                                    ,{"video/x-ms-wmv", "wmvideo"}
                                    ,{"application/ogg", "ogg"}
                                    ,{"application/vnd.apple.mpegurl", "hls"} // https://tools.ietf.org/html/draft-pantos-http-live-streaming-14#section-10
                                    };
    const TUint maxFormats = sizeof(kTypes)/sizeof(kTypes[0]);
    TBool first = true;
    for (TUint i=0; i<maxFormats; i++) {
        Brn mimeType(kTypes[i].iMimeType);
        if (aMimeTypeList.Contains(kTypes[i].iMimeType)) {
            if (first) {
                first = false;
            }
            else {
                iSupportedFormats.Append(",");
            }
            iSupportedFormats.Append(kTypes[i].iTuneInFormat);
        }
    }
    Log::Print("iSupportedFormats = ");
    Log::Print(iSupportedFormats);
    Log::Print("\n");

    iReaderResponse.AddHeader(iHeaderContentLength);

    // Get username from store.
    iConfigUsername = new ConfigText(aConfigInit, kConfigKeyUsername, kMinUserNameBytes, kMaxUserNameBytes, kConfigUsernameDefault);
    // Results in initial UsernameChanged() callback, which triggers Refresh().
    iListenerId = iConfigUsername->Subscribe(MakeFunctorConfigText(*this, &RadioPresetsTuneIn::UsernameChanged));

    new CredentialsTuneIn(aCredentialsManager, aPartnerId); // ownership transferred to aCredentialsManager
}

RadioPresetsTuneIn::~RadioPresetsTuneIn()
{
    iSocket.Interrupt(true);
    iConfigUsername->Unsubscribe(iListenerId);
    delete iConfigUsername;
}

const Brx& RadioPresetsTuneIn::DisplayName() const
{
    return kDisplayName;
}

void RadioPresetsTuneIn::Activate(IRadioPresetWriter& aWriter)
{
    AutoMutex _(iLock);
    iPresetWriter = &aWriter;
}

void RadioPresetsTuneIn::Deactivate()
{
    AutoMutex _(iLock);
    iPresetWriter = nullptr;
}

void RadioPresetsTuneIn::RefreshPresets()
{
    iSocket.Open(iEnv);
    AutoSocket _(iSocket);
    AutoSocket autoSocket(iSocket); // Ensure socket is closed before any path out of this block.
    Endpoint ep(80, iRequestUri.Host());
    iSocket.Connect(ep, 20 * 1000); // hard-coded timeout.  Ignores .InitParams().TcpConnectTimeoutMs() on the assumption that is set for lan connections

    // FIXME - try sending If-Modified-Since header with request. See rfc2616 14.25
    // ... this may require that we use http 1.1 in the request, so cope with a chunked response
    iWriterRequest.WriteMethod(Http::kMethodGet, iRequestUri.PathAndQuery(), Http::eHttp10);
    const TUint port = (iRequestUri.Port() == -1? 80 : (TUint)iRequestUri.Port());
    Http::WriteHeaderHostAndPort(iWriterRequest, iRequestUri.Host(), port);
    Http::WriteHeaderConnectionClose(iWriterRequest);
    iWriterRequest.WriteFlush();

    iReaderResponse.Read(kReadResponseTimeoutMs);
    const HttpStatus& status = iReaderResponse.Status();
    if (status != HttpStatus::kOk) {
        LOG_ERROR(kSources, "Error fetching TuneIn xml - status=%u\n", status.Code());
        THROW(HttpError);
    }

    Brn buf;
    for (;;) {
        iReaderUntil.ReadUntil('<');
        buf.Set(iReaderUntil.ReadUntil('>'));
        if (buf.BeginsWith(Brn("opml version="))) {
            break;
        }
    }
    for (;;) {
        iReaderUntil.ReadUntil('<');
        buf.Set(iReaderUntil.ReadUntil('>'));
        if (buf == Brn("status")) {
            break;
        }
    }
    buf.Set(iReaderUntil.ReadUntil('<'));
    const TUint statusCode = Ascii::Uint(buf);
    if (statusCode != 200) {
        LOG_ERROR(kSources, "Error in TuneIn xml - statusCode=%u\n", statusCode);
        return;
    }

    // Find the default container (there may be multiple containers if TuneIn folders are used)
    TBool foundDefault = false;
    for (; !foundDefault;) {
        iReaderUntil.ReadUntil('<');
        buf.Set(iReaderUntil.ReadUntil('>'));
        const TBool isContainer = buf.BeginsWith(Brn("outline type=\"container\""));
        if (!isContainer) {
            continue;
        }
        Parser parser(buf);
        Brn attr;
        static const Brn kAttrDefault("is_default=\"true\"");
        while (parser.Remaining().Bytes() > 0) {
            attr.Set(parser.Next());
            if (attr.BeginsWith(kAttrDefault)) {
                foundDefault = true;
                if (attr[attr.Bytes()-1] == '/') {
                    LOG_INFO(kSources, "No presets for query %.*s\n", PBUF(iRequestUri.PathAndQuery()));
                    return;
                }
                break;
            }
        }
    }
    // Read presets for the current container only
    for (;;) {
        iReaderUntil.ReadUntil('<');
        buf.Set(iReaderUntil.ReadUntil('>'));
        if (buf == Brn("/outline")) {
            break;
        }
        const TBool isAudio = buf.BeginsWith(Brn("outline type=\"audio\""));
        const TBool isLink = buf.BeginsWith(Brn("outline type=\"link\""));
        if (!(isAudio || isLink)) {
            continue;
        }
        Parser parser(buf);
        (void)parser.Next('='); // outline type="audio" - ignore
        (void)parser.Next('\"');
        (void)parser.Next('\"');

        if (!ReadElement(parser, "text", iPresetTitle) ||
            !ReadElement(parser, "URL", iPresetUrl)) {
            continue;
        }
        Converter::FromXmlEscaped(iPresetUrl);
        if (isAudio) {
            iPresetUri.Replace(iPresetUrl);
            if (iPresetUri.Query().Bytes() > 0) {
                iPresetUrl.Append(Brn("&c=ebrowse")); // ensure best quality stream is selected
            }
        }
        TUint byteRate = 0;
        if (ValidateKey(parser, "bitrate", false)) {
            (void)parser.Next('\"');
            Brn value = parser.Next('\"');
            byteRate = Ascii::Uint(value);
            byteRate *= 125; // convert from kbits/sec to bytes/sec
        }
        const TChar* imageKey = "image";
        Brn imageKeyBuf(imageKey);
        const TChar* presetNumberKey = "preset_number";
        Brn presetNumberBuf(presetNumberKey);
        Brn key = parser.Next('=');
        TBool foundImage = false, foundPresetNumber = false;
        TUint presetNumber = UINT_MAX;
        while (key.Bytes() > 0 && !(foundImage && foundPresetNumber)) {
        if (key == imageKeyBuf) {
            foundImage = ReadValue(parser, imageKey, iPresetArtUrl);
        }
        else if (key == presetNumberBuf) {
            Bws<Ascii::kMaxUintStringBytes> presetBuf;
            if (ReadValue(parser, presetNumberKey, presetBuf)) {
                try {
                    presetNumber = Ascii::Uint(presetBuf);
                    foundPresetNumber = true;
                }
                catch (AsciiError&) {}
            }
        }
        else {
            (void)parser.Next('\"');
            (void)parser.Next('\"');
        }
        key.Set(parser.Next('='));
    }
    if (!foundPresetNumber) {
        LOG_ERROR(kSources, "No preset_id for TuneIn preset %.*s\n", PBUF(iPresetTitle));
        continue;
    }
    {
        AutoMutex __(iLock);
        if (iPresetWriter == nullptr) {
            THROW(WriterError);
        }
        try {
            iPresetWriter->SetPreset(presetNumber - 1, iPresetUrl, iPresetTitle, iPresetArtUrl, byteRate);
        }
        catch (PresetIndexOutOfRange&) {
            LOG_ERROR(kSources, "Ignoring preset number %u (index too high)\n", presetNumber);
        }
    }
    }
}

void RadioPresetsTuneIn::UpdateUsername(const Brx& aUsername)
{
    Bws<256> uriBuf;
    uriBuf.Append(TuneInApi::kTuneInPresetsRequest);
    uriBuf.Append(iSupportedFormats);
    uriBuf.Append(TuneInApi::kPartnerId);
    uriBuf.Append(iPartnerId);
    uriBuf.Append(TuneInApi::kUsername);
    uriBuf.Append(aUsername);
    iRequestUri.Replace(uriBuf);
}

void RadioPresetsTuneIn::UsernameChanged(KeyValuePair<const Brx&>& aKvp)
{
    UpdateUsername(aKvp.Value());
    AutoMutex _(iLock);
    if (iPresetWriter != nullptr) {
        iPresetWriter->ScheduleRefresh();
    }
}

TBool RadioPresetsTuneIn::ReadElement(Parser& aParser, const TChar* aKey, Bwx& aValue)
{
    if (!ValidateKey(aParser, aKey, true)) {
        return false;
    }
    return ReadValue(aParser, aKey, aValue);
}

TBool RadioPresetsTuneIn::ValidateKey(Parser& aParser, const TChar* aKey, TBool aLogErrors)
{
    Brn key = aParser.Next('=');
    if (key != Brn(aKey)) {
        if (aLogErrors) {
            LOG_ERROR(kSources, "Unexpected order of OPML elements.  Expected \"%s\", got %.*s\n", aKey, PBUF(key));
        }
        return false;
    }
    return true;
}

TBool RadioPresetsTuneIn::ReadValue(Parser& aParser, const TChar* aKey, Bwx& aValue)
{
    (void)aParser.Next('\"');
    Brn value = aParser.Next('\"');
    if (value.Bytes() > aValue.MaxBytes()) {
        LOG_ERROR(kSources, "Unexpectedly long %s for preset - %.*s\n", aKey, PBUF(value));
        return false;
    }
    aValue.Replace(value);
//    Converter::FromXmlEscaped(aValue);
    return true;
}


// CredentialsTuneIn

const Brn CredentialsTuneIn::kId("tunein.com");

CredentialsTuneIn::CredentialsTuneIn(Credentials& aCredentialsManager, const Brx& aPartnerId)
{
    aCredentialsManager.Add(this);
    Bws<128> data("{\"partnerId\": \"");
    data.Append(aPartnerId);
    data.Append("\"}");
    aCredentialsManager.SetState(kId, Brx::Empty(), data);
}

const Brx& CredentialsTuneIn::Id() const
{
    return kId;
}

void CredentialsTuneIn::CredentialsChanged(const Brx& /*aUsername*/, const Brx& /*aPassword*/)
{
}

void CredentialsTuneIn::UpdateStatus()
{
}

void CredentialsTuneIn::Login(Bwx& aToken)
{
    aToken.Replace(Brx::Empty());
}

void CredentialsTuneIn::ReLogin(const Brx& /*aCurrentToken*/, Bwx& aNewToken)
{
    aNewToken.Replace(Brx::Empty());
}
