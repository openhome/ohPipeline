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

EXCEPTION(PropertyServerNotFound);

namespace OpenHome {
    class Environment;
    class IThreadPool;
    class IThreadPoolHandle;
    namespace Net {
        class CpStack;
        class CpDevice;
        class CpDeviceList;
        class CpDeviceDv;
        class CpProxyAvOpenhomeOrgPlaylist1;
        class DvDevice;
    }
namespace Av {
    class ITrackDatabase;

class DeviceListKazooServer
{
    static const Brn kDomainUpnp;
    static const Brn kServiceContentDirectory;
public:
    DeviceListKazooServer(Environment& aEnv, Net::CpStack& aCpStack);
    ~DeviceListKazooServer();
    void GetPropertyServerUri(const Brx& aUdn, Bwx& aPsUri, TUint aTimeoutMs);
    void Cancel();
private:
    void DeviceAdded(Net::CpDevice& aDevice);
    void DeviceRemoved(Net::CpDevice& aDevice);
private:
    Environment & iEnv;
    Mutex iLock;
    Semaphore iSemAdded;
    Net::CpDeviceList* iDeviceList;
    std::map<Brn, Net::CpDevice*, BufferCmp> iMap;
    Uri iUri; // only used in GetPropertyServerUri but too large for the stack
    TBool iCancelled;
};

    
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
public:
    PinInvokerKazooServer(Environment& aEnv,
                          Net::CpStack& aCpStack,
                          Net::DvDevice& aDevice,
                          IThreadPool& aThreadPool);
    ~PinInvokerKazooServer();
private: // from IPinInvoker
    void BeginInvoke(const IPin& aPin, Functor aCompleted) override;
    void Cancel() override;
    const TChar* Mode() const override;
private:
    Brn FromQuery(const TChar* aKey) const;
    void ReadFromServer();
    void WriteRequestReadResponse(const Brx& aPathAndQuery, TBool aKeepAlive);
    TUint ParseTotal();
    TUint Browse(const Brx& aMePath, const Brx& aSessionId, const Brx& aId);
    TUint List(const Brx& aMePath, const Brx& aSessionId, const Brx& aTag);
    void Read(const Brx& aMePath, const Brx& aSessionId, TUint aIndex, TUint aCount);
    void ReadIdAddAlbum(const Brx& aMePath, const Brx& aSessionId, const Brx& aContainerId,
                        TUint aIndex, TUint& aInsertAfterId, TUint& aPlaylistCapacity);
    void AddAlbum(const Brx& aMePath, const Brx& aSessionId, const Brx& aId,
                  TUint& aInsertAfterId, TUint& aPlaylistCapacity);
    void AddTrack(TUint& aInsertAfterId);
    static const TChar* OhMetadataKey(TUint aKsTag);
private:
    Environment & iEnv;
    IThreadPoolHandle* iThreadPoolHandle;
    DeviceListKazooServer* iDeviceList;
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
    Bws<64> iUdn;
    Media::BwsTrackUri iTrackUri;
    Media::BwsTrackMetaData iTrackMetadata;
    TBool iShuffle;
    TBool iPlaying;
};

}
}