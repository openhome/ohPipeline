#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Av/Pins/Pins.h>
#include <OpenHome/Media/Pipeline/Msg.h>

#include <map>
#include <vector>
#include <utility>

namespace OpenHome {
    class Environment;
    class IThreadPool;
    class IThreadPoolHandle;
    namespace Net {
        class CpStack;
        class CpDeviceDv;
        class CpProxyAvOpenhomeOrgPlaylist1;
        class DvDevice;
    }
namespace Av {
    class DeviceListMediaServer;
    
class PinInvokerKazooServer : public IPinInvoker
{
    static const TChar* kMode;
    static const Brn kModeBuf;
    static const Brn kDomainUpnp;
    static const Brn kServiceContentDirectory;
    static const TUint kConnectTimeoutMs;
    static const TUint kReadBufBytes = 4 * 1024;
    static const Brn kHostAlbum;
    static const Brn kHostArtist;
    static const Brn kHostContainer;
    static const Brn kHostGenre;
    static const Brn kHostPlaylist;
    static const Brn kResponseTracks;
    static const Brn kResponseAlbums;

    const TUint kMinSupportedVersion = 1;
    const TUint kMaxSupportedVersion = 1;

public:
    PinInvokerKazooServer(Environment& aEnv,
                          Net::CpStack& aCpStack,
                          Net::DvDevice& aDevice,
                          IThreadPool& aThreadPool,
                          DeviceListMediaServer& aDeviceList);
    ~PinInvokerKazooServer();
private: // from IPinInvoker
    void BeginInvoke(const IPin& aPin, Functor aCompleted) override;
    void Cancel() override;
    const TChar* Mode() const override;
    TBool SupportsVersion(TUint version) const override;
private:
    Brn FromQuery(const TChar* aKey) const;
    void ReadFromServer();
    void WriteRequestReadResponse(const Brx& aPathAndQuery, TBool aKeepAlive);
    TUint ParseTotal();
    TUint Browse(const Brx& aMePath, const Brx& aSessionId, const Brx& aId);
    TUint List(const Brx& aMePath, const Brx& aSessionId, const Brx& aTag);
    void Read(const Brx& aMePath, const Brx& aSessionId, TUint aIndex, TUint aCount);
    void ReadIdAddAlbum(const Brx& aMePath, const Brx& aSessionId, TUint aIndex,
                        TUint& aInsertAfterId, TUint& aPlaylistCapacity);
    void BrowseReadIdAddAlbum(const Brx& aMePath, const Brx& aSessionId, const Brx& aContainerId,
                              TUint aIndex, TUint& aInsertAfterId, TUint& aPlaylistCapacity,
                              TBool& aRepositionCursor);
    void ListReadIdAddAlbum(const Brx& aMePath, const Brx& aSessionId, const Brx& aContainerId,
                            TUint aIndex, TUint& aInsertAfterId, TUint& aPlaylistCapacity,
                            TBool& aRepositionCursor);
    void AddAlbum(const Brx& aMePath, const Brx& aSessionId, const Brx& aId,
                  TUint& aInsertAfterId, TUint& aPlaylistCapacity);
    void AddTrack(TUint& aInsertAfterId);
    static const TChar* OhMetadataKey(TUint aKsTag);
private:
    Environment & iEnv;
    DeviceListMediaServer& iDeviceList;
    IThreadPoolHandle* iThreadPoolHandle;
    Net::CpDeviceDv* iCpDeviceSelf;
    Net::CpProxyAvOpenhomeOrgPlaylist1* iProxyPlaylist;
    SocketTcpClient iSocket;
    Srs<1024> iReaderBuf;
    ReaderUntilS<1024> iReaderUntil1;
    Sws<1024> iWriterBuf;
    WriterHttpRequest iWriterRequest;
    ReaderHttpResponse iReaderResponse;
    ReaderHttpChunked iDechunker;
    ReaderUntilS<kReadBufBytes> iReaderUntil2;
    HttpHeaderContentLength iHeaderContentLength;
    HttpHeaderTransferEncoding iHeaderTransferEncoding;
    WriterBwh iResponseBody;
    Uri iPinUri;
    std::vector<std::pair<Brn, Brn>> iQueryKvps;
    Uri iEndpointUri;
    Semaphore iSemDeviceFound;
    Functor iCompleted;
    Media::BwsTrackUri iTrackUri;
    Media::BwsTrackMetaData iTrackMetadata;
    TBool iShuffle;
    TBool iPlaying;
};

}
}
