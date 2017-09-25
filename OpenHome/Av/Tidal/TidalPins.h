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
#include <OpenHome/DebugManager.h>
        
namespace OpenHome {
    class Environment;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Av {

class TidalPins
    : public IDebugTestHandler
{
    static const TUint kJsonResponseChunks = 4 * 1024;
    static const TUint kTrackLimitPerRequest = 10;
    static const TUint kMaxTracks = 1000;
public:
    static const Brn kConfigKeySoundQuality;
public:
    TidalPins(Tidal& aTidal, Net::DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack);
    ~TidalPins();
    TBool LoadTracksByArtistId(const Brx& aArtistId, TUint aMaxTracks = kMaxTracks);
public:  // IDebugTestHandler
    TBool Test(const OpenHome::Brx& aType, const OpenHome::Brx& aInput, OpenHome::IWriterAscii& aWriter);
private:
    TBool IsNumber(const Brx& aRequest);
private:
    Mutex iLock;
    Tidal& iTidal;
    WriterBwh iJsonResponse;
    Media::TrackFactory& iTrackFactory;
    Net::CpProxyAvOpenhomeOrgPlaylist1* iCpPlaylist;
    Net::CpStack& iCpStack;
};

};  // namespace Av
};  // namespace OpenHome


