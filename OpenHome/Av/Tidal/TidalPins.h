#pragma once

#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Types.h>
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
        
namespace OpenHome {
    class Environment;
    class IThreadPool;
    class IThreadPoolHandle;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Av {

class TidalPins
    : public IPinInvoker
{
    static const TUint kItemLimitPerRequest = 10;
    static const TUint kJsonResponseChunks = 4 * 1024;
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
private:
    void Invoke();
    TBool LoadByPath(const Brx& aPath, const PinUri& aPinUri, TBool aShuffle);
    TBool LoadTracks(const Brx& aPath, TBool aShuffle);
    TBool LoadContainers(const Brx& aPath, TidalMetadata::EIdType aIdType, TBool aShuffle);
    TBool LoadByStringQuery(const Brx& aQuery, TidalMetadata::EIdType aIdType, TBool aShuffle);
    TUint LoadTracksById(const Brx& aId, TidalMetadata::EIdType aIdType, TUint aPlaylistId, TUint& aCount);
private: // helpers
    TUint GetTotalItems(JsonParser& aParser, const Brx& aId, TidalMetadata::EIdType aIdType, TBool aIsContainer, TUint& aStartIndex, TUint& aEndIndex);
    void UpdateOffset(TUint aTotalItems, TUint aEndIndex, TBool aIsContainer, TUint& aOffset);
    TBool IsValidId(const Brx& aRequest, TidalMetadata::EIdType aIdType);
    TBool IsValidUuid(const Brx& aRequest);
    void InitPlaylist(TBool aShuffle);
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
};

};  // namespace Av
};  // namespace OpenHome
