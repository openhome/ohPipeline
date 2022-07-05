#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Optional.h>

namespace OpenHome {
namespace Net {
    class DvDevice;
    class IMdnsProvider;
}
namespace Media {
    class IClockPuller;
}
namespace Av {

class ISource;
class IMediaPlayer;
class IPlaylistLoader;
class IOhmTimestamper;
class IOhmMsgProcessor;
class IRaatTime;
class IRaatSignalPathObservable;

class SourceFactory
{
public:
    static ISource* NewPlaylist(IMediaPlayer& aMediaPlayer, Optional<IPlaylistLoader> aPlaylistLoader);
    static ISource* NewRadio(IMediaPlayer& aMediaPlayer);
    static ISource* NewRadio(IMediaPlayer& aMediaPlayer, const Brx& aTuneInPartnerId);
    static ISource* NewUpnpAv(IMediaPlayer& aMediaPlayer, Net::DvDevice& aDevice);
    static ISource* NewRaop(IMediaPlayer& aMediaPlayer, Optional<Media::IClockPuller> aClockPuller, const Brx& aMacAddr, TUint aServerThreadPriority, Net::IMdnsProvider& aMdnsProvider);
    static ISource* NewReceiver(IMediaPlayer& aMediaPlayer,
                                Optional<Media::IClockPuller> aClockPuller,
                                Optional<IOhmTimestamper> aTxTimestamper,
                                Optional<IOhmTimestamper> aRxTimestamper,
                                Optional<IOhmMsgProcessor> aOhmMsgObserver);
    static ISource* NewScd(IMediaPlayer& aMediaPlayer, TUint aDsdSampleBlockWords, TUint aDsdPadBytesPerChunk);
    static ISource* NewRaat(
        IMediaPlayer& aMediaPlayer,
        IRaatTime* aRaatTime,
        IRaatSignalPathObservable* aSignalPathObservable);

    static const TChar* kSourceTypePlaylist;
    static const TChar* kSourceTypeRadio;
    static const TChar* kSourceTypeUpnpAv;
    static const TChar* kSourceTypeRaop;
    static const TChar* kSourceTypeReceiver;
    static const TChar* kSourceTypeScd;
    static const TChar* kSourceTypeRaat;

    static const Brn kSourceNamePlaylist;
    static const Brn kSourceNameRadio;
    static const Brn kSourceNameUpnpAv;
    static const Brn kSourceNameRaop;
    static const Brn kSourceNameReceiver;
    static const Brn kSourceNameScd;
    static const Brn kSourceNameRaat;
};

} // namespace Av
} // namespaceOpenHome

