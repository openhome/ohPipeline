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

#include <atomic>
#include <vector>

namespace OpenHome {
    class Environment;
    class IThreadPool;
    class IThreadPoolHandle;
    namespace Net {
        class CpStack;
        class CpDevice;
        class CpProxyUpnpOrgContentDirectory1;
        class CpProxyAvOpenhomeOrgPlaylist1;
        class DvDevice;
        class IAsync;
    }
namespace Av {
    class ITrackDatabase;
    class DeviceListMediaServer;

class PinInvokerUpnpServer : public IPinInvoker
{
    static const TChar* kMode;
    static const Brn kModeBuf;
    static const TChar* kQueryContainer;
    static const TChar* kQueryTrack;

    const TUint kMinSupportedVersion = 1;
    const TUint kMaxSupportedVersion = 1;
public:
    PinInvokerUpnpServer(Environment& aEnv,
                         Net::CpStack& aCpStack,
                         Net::DvDevice& aDevice,
                         IThreadPool& aThreadPool,
                         ITrackDatabase& aTrackDatabase,
                         DeviceListMediaServer& aDeviceList);
    ~PinInvokerUpnpServer();
private: // from IPinInvoker
    void BeginInvoke(const IPin& aPin, Functor aCompleted) override;
    void Cancel() override;
    const TChar* Mode() const override;
    TBool SupportsVersion(TUint version) const override;
private:
    enum class Ns {
        Dc,
        Upnp
    };
private:
    Brn FromQuery(const TChar* aKey) const;
    void CheckCancelled();
    void Complete();
    void ReadContainer();
    void ReadTrack();
    void BrowseContainerCallback(Net::IAsync& aAsync);
    void BrowseTrackCallback(Net::IAsync& aAsync);
    TBool TryAddItem(const Brx& aItemDidl);
    void TryAddTag(const Brx& aItemDidl, const TChar* aTag, Ns aNs);
    void TryAddArtistTags(const Brx& aItemDidl);
    void TryAddTag(const TChar* aTag, const Brx& aVal, Ns aNs, const Brx& aRole);
private:
    Environment & iEnv;
    ITrackDatabase& iTrackDatabase;
    DeviceListMediaServer& iDeviceList;
    Net::CpProxyAvOpenhomeOrgPlaylist1* iProxyPlaylist;
    IThreadPoolHandle* iTphContainer;
    IThreadPoolHandle* iTphTrack;
    Net::CpProxyUpnpOrgContentDirectory1* iProxyContentDirectory;
    Uri iPinUri;
    std::vector<std::pair<Brn, Brn>> iQueryKvps;
    Uri iEndpointUri;
    Semaphore iSemDeviceFound;
    Functor iCompleted;
    TUint iTrackIdInsertAfter;
    Media::BwsTrackUri iTrackUri;
    Media::BwsTrackMetaData iTrackMetadata;
    TBool iShuffle;
    TBool iPlaying;
    TBool iStarted;
    std::atomic<TBool> iCancel;
    std::vector<Brh*> iContainers;
    TUint iContainersIndex;
    Brh* iTrackId;
};

}
}
