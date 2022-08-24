#pragma once

#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Types.h>
#include <OpenHome/Exception.h>
#include <OpenHome/SocketSsl.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <Generated/CpAvOpenhomeOrgPlaylist1.h>
#include <OpenHome/Av/Playlist/TrackDatabase.h>
#include <OpenHome/Av/Pins/Pins.h>
#include <OpenHome/Av/Tidal/Tidal.h>

#include <atomic>
        
namespace OpenHome {
    class Environment;
    class IThreadPool;
    class IThreadPoolHandle;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Av {

class TidalMetadata;

class TidalPins
    : public IPinInvoker
{
    static const TUint kItemLimitPerRequest = 10;
    static const TUint kJsonResponseChunks = 4 * 1024;

    const TUint kMinSupportedVersion = 1;
    const TUint kMaxSupportedVersion = 2;

private:
    enum class EShuffleMode
    {
        None,
        Default,
        WhenRequired,
    };

public:
    TidalPins(Tidal& aTidal,
              Environment& iEnv,
              Net::DvDeviceStandard& aDevice,
              Media::TrackFactory& aTrackFactory,
              Net::CpStack& aCpStack,
              IThreadPool& aThreadPool);
    ~TidalPins();
private: // from IPinInvoker
    void BeginInvoke(const IPin& aPin, Functor aCompleted) override;
    void Cancel() override;
    const TChar* Mode() const override;
    TBool SupportsVersion(TUint version) const override;
private:
    void Invoke();
    TBool LoadByPath(const Brx& aPath, const PinUri& aPinUri, TBool aPinShuffled, EShuffleMode aShuffleMode, const Tidal::AuthenticationConfig& aAuthConfig);
    TBool LoadTracks(const Brx& aPath, TBool aPlaylistShuffle, EShuffleMode aShuffleMode, const Tidal::AuthenticationConfig& aAuthConfig);
    TBool LoadContainers(const Brx& aPath, TidalMetadata::EIdType aIdType, TBool aPinShuffled, EShuffleMode aShuffleMode, const Tidal::AuthenticationConfig& aAuthConfig);
    TBool LoadByStringQuery(const Brx& aQuery,TidalMetadata::EIdType aIdType, TBool aPinShuffled, EShuffleMode aShuffleMode, const Tidal::AuthenticationConfig& aAuthConfig);
    TUint LoadTracksById(const Brx& aId, TidalMetadata::EIdType aIdType, TUint aIsContainer, TUint& aCount, TBool aPinShuffled, EShuffleMode aShuffleMode, const Tidal::AuthenticationConfig& aAuthConfig);
private: // helpers
    TUint GetTotalItems(JsonParser& aParser, const Brx& aId, TidalMetadata::EIdType aIdType, TBool aIsContainer, TBool aShouldShuffleLoadOrder, TUint& aStartIndex, TUint& aEndIndex, const Tidal::AuthenticationConfig& aAuthConfig);
    void UpdateOffset(TUint aTotalItems, TUint aEndIndex, TBool aIsContainer, TBool aShouldShuffleLoadOrder, TUint& aOffset);
    TBool IsValidId(const Brx& aRequest, TidalMetadata::EIdType aIdType);
    void InitPlaylist(TBool aShuffle);
    EShuffleMode GetShuffleMode(PinUri& aPinUri);
    TBool ShouldShuffleLoadOrder(TBool aPinShuffled, EShuffleMode aShuffleMode);
private:
    Mutex iLock;
    Tidal& iTidal;
    IThreadPoolHandle* iThreadPoolHandle;
    WriterBwh iJsonResponse;
    TidalMetadata iTidalMetadata;
    Net::CpProxyAvOpenhomeOrgPlaylist1* iCpPlaylist;
    TUint iMaxPlaylistTracks;
    Bws<128> iToken;
    Functor iCompleted;
    PinIdProvider iPinIdProvider;
    Pin iPin;
    Environment& iEnv;
    std::atomic<TBool> iInterrupted;
};

};  // namespace Av
};  // namespace OpenHome
