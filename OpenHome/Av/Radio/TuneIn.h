#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Av/Radio/Presets.h>
#include <OpenHome/Av/Radio/PresetDatabase.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Av/Credentials.h>

#include <memory>
#include <vector>
#include <algorithm>

namespace OpenHome {
    class Environment;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigText;
}
namespace Media {
    class PipelineManager;
    class MimeTypeList;
}
namespace Av {

class TuneInApi
{
public:
    static const Brn kTuneInPresetsRequest;
    static const Brn kFormats;
    static const Brn kPartnerId;
    static const Brn kUsername;
    static const Brn kTuneInStationRequest;
    static const Brn kTuneInPodcastBrowse;
    static const Brn kTuneInItemId;
};

class RadioPresetsTuneIn : public IRadioPresetProvider
{
private:
    static const TUint kReadBufBytes = Media::kTrackMetaDataMaxBytes + 1024;
    static const TUint kWriteBufBytes = 1024;
    static const TUint kMinUserNameBytes = 1;
    static const TUint kMaxUserNameBytes = 64;
    static const TUint kMaxPartnerIdBytes = 64;
    static const TUint kReadResponseTimeoutMs = 30 * 1000; // 30 seconds
    static const TUint kMaxPresetTitleBytes = 256;
    static const Brn kConfigKeyUsername;
    static const Brn kConfigUsernameDefault;
    static const Brn kDisplayName;
public:
    RadioPresetsTuneIn(
        Environment& aEnv,
        const Brx& aPartnerId,
        Configuration::IConfigInitialiser& aConfigInit,
        Credentials& aCredentialsManager,
        Media::MimeTypeList& aMimeTypeList);
    ~RadioPresetsTuneIn();
private: // from IRadioPresetProvider
    const Brx& DisplayName() const override;
    void Activate(IRadioPresetWriter& aWriter) override;
    void Deactivate() override;
    void RefreshPresets() override;
private:
    void UpdateUsername(const Brx& aUsername);
    void UsernameChanged(Configuration::KeyValuePair<const Brx&>& aKvp);
    TBool ReadElement(Parser& aParser, const TChar* aKey, Bwx& aValue);
    TBool ValidateKey(Parser& aParser, const TChar* aKey, TBool aLogErrors);
    TBool ReadValue(Parser& aParser, const TChar* aKey, Bwx& aValue);
private:
    Mutex iLock;
    Environment& iEnv;
    IRadioPresetWriter* iPresetWriter;
    SocketTcpClient iSocket;
    Uri iRequestUri;
    Sws<kWriteBufBytes> iWriteBuffer;
    WriterHttpRequest iWriterRequest;
    Srs<1024> iReadBuffer;
    ReaderUntilS<kReadBufBytes> iReaderUntil;
    ReaderHttpResponse iReaderResponse;
    HttpHeaderContentLength iHeaderContentLength;
    Bws<40> iSupportedFormats;
    // Following members provide temp storage used while converting OPML elements to Didl-Lite
    Bws<Media::kTrackMetaDataMaxBytes> iDidlLite;
    Bws<Media::kTrackUriMaxBytes> iPresetUrl;
    Uri iPresetUri;
    Bws<Media::kTrackUriMaxBytes> iPresetArtUrl;
    Bws<kMaxPresetTitleBytes> iPresetTitle;
    Configuration::ConfigText* iConfigUsername;
    TUint iListenerId;
    const Bws<kMaxPartnerIdBytes> iPartnerId;
};

class CredentialsTuneIn : public ICredentialConsumer, private INonCopyable
{
    static const Brn kId;
public:
    CredentialsTuneIn(Credentials& aCredentialsManager, const Brx& aPartnerId);
private: // from ICredentialConsumer
    const Brx& Id() const override;
    void CredentialsChanged(const Brx& aUsername, const Brx& aPassword) override;
    void UpdateStatus() override;
    void Login(Bwx& aToken) override;
    void ReLogin(const Brx& aCurrentToken, Bwx& aNewToken) override;
};

} // namespace Av
} // namespace OpenHome

