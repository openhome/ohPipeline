#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/MimeTypeList.h>
#include <OpenHome/Optional.h>
#include <OpenHome/Av/Logger.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Av/TransportControl.h>

namespace OpenHome {
    class Environment;
    class IPowerManager;
    class IThreadPool;
    class PowerManager;
    class RingBufferLogger;
    class IUnixTimestamp;
    class IShell;
    class IInfoAggregator;
namespace Net {
    class DvStack;
    class DvDeviceStandard;
}
namespace Media {
    class PipelineManager;
    class PipelineInitParams;
    class IMute;
    class UriProvider;
    class Protocol;
    namespace Codec {
        class ContainerBase;
        class CodecBase;
    }
    class TrackFactory;
}
namespace Configuration {
    class ConfigManager;
    class IConfigManager;
    class IConfigInitialiser;
    class IStoreReadWrite;
    class ConfigText;
    class ConfigChoice;
    class ProviderConfig;
    class ProviderConfigApp;
}
namespace Av {

class IFriendlyNameObservable;
class FriendlyNameManager;
class IReadStore;
class ISource;
class IStaticDataSource;
class Product;
class ProviderTime;
class ProviderInfo;
class ProviderTransport;
class KvpStore;
class Credentials;
class VolumeManager;
class VolumeConfig;
class VolumeConsumer;
class IVolumeManager;
class IVolumeProfile;
class ConfigStartupSource;
class IRebootHandler;
class IPinsAccountStore;
class IPinsInvocable;
class IPinSetObservable;
class PinsManager;
class ProviderPins;
class TransportPins;

class IMediaPlayer
{
public:
    virtual ~IMediaPlayer() {}
    virtual Environment& Env() = 0;
    virtual Net::DvStack& DvStack() = 0;
    virtual Net::CpStack& CpStack() = 0;
    virtual Net::DvDeviceStandard& Device() = 0;
    virtual Media::PipelineManager& Pipeline() = 0;
    virtual Media::TrackFactory& TrackFactory() = 0;
    virtual IReadStore& ReadStore() = 0;
    virtual Configuration::IStoreReadWrite& ReadWriteStore() = 0;
    virtual Configuration::IConfigManager& ConfigManager() = 0;
    virtual Configuration::IConfigInitialiser& ConfigInitialiser() = 0;
    virtual IPowerManager& PowerManager() = 0;
    virtual IThreadPool& ThreadPool() = 0;
    virtual Av::Product& Product() = 0;
    virtual Av::IFriendlyNameObservable& FriendlyNameObservable() = 0;
    virtual IVolumeManager& VolumeManager() = 0;
    virtual Media::IMute& SystemMute() = 0;
    virtual Credentials& CredentialsManager() = 0;
    virtual Media::MimeTypeList& MimeTypes() = 0;
    virtual void Add(Media::UriProvider* aUriProvider) = 0;
    virtual void AddAttribute(const TChar* aAttribute) = 0;
    virtual ILoggerSerial& BufferLogOutput(TUint aBytes, IShell& aShell, Optional<ILogPoster> aLogPoster) = 0; // must be called before Start()
    virtual IUnixTimestamp& UnixTimestamp() = 0;
    virtual ITransportRepeatRandom& TransportRepeatRandom() = 0;
    virtual Optional<IPinsAccountStore> PinsAccountStore() = 0;
    virtual Optional<IPinsInvocable> PinsInvocable() = 0;
    virtual Optional<IPinSetObservable> PinSetObservable() = 0;
};


class MediaPlayerInitParams
{
public:
    static MediaPlayerInitParams* New(const Brx& aDefaultRoom, const Brx& aDefaultName);
    void EnableConfigApp();
    void EnablePins(TUint aMaxDevice);
    void SetThreadPoolSize(TUint aCountHigh, TUint aCountMedium, TUint aCountLow);
    const Brx& DefaultRoom() const;
    const Brx& DefaultName() const;
    TBool ConfigAppEnabled() const;
    TBool PinsEnabled(TUint& aMaxDevice) const;
    TUint ThreadPoolCountHigh() const;
    TUint ThreadPoolCountMedium() const;
    TUint ThreadPoolCountLow() const;
private:
    MediaPlayerInitParams(const Brx& aDefaultRoom, const Brx& aDefaultName);
private:
    Bws<Product::kMaxRoomBytes> iDefaultRoom;
    Bws<Product::kMaxNameBytes> iDefaultName;
    TUint iThreadPoolHigh;
    TUint iThreadPoolMedium;
    TUint iThreadPoolLow;
    TBool iConfigAppEnable;
    TBool iPinsEnable;
    TUint iMaxDevicePins;
};


class MediaPlayer : public IMediaPlayer, private INonCopyable
{
    static const TUint kTrackCount = 1200;
public:
    MediaPlayer(Net::DvStack& aDvStack, Net::CpStack& aCpStack, Net::DvDeviceStandard& aDevice,
                IStaticDataSource& aStaticDataSource,
                Configuration::IStoreReadWrite& aReadWriteStore,
                Media::PipelineInitParams* aPipelineInitParams,
                VolumeConsumer& aVolumeConsumer, IVolumeProfile& aVolumeProfile,
                IInfoAggregator& aInfoAggregator,
                const Brx& aEntropy,
                MediaPlayerInitParams* aInitParams);
    ~MediaPlayer();
    void Quit();
    void Add(Media::Codec::ContainerBase* aContainer);
    void Add(Media::Codec::CodecBase* aCodec);
    void Add(Media::Protocol* aProtocol);
    void Add(ISource* aSource);
    RingBufferLogger* LogBuffer(); // an optional component. returns nullptr if not available. no transfer of ownership.
    void Start(IRebootHandler& aRebootHandler);
public: // from IMediaPlayer
    Environment& Env() override;
    Net::DvStack& DvStack() override;
    Net::CpStack& CpStack() override;
    Net::DvDeviceStandard& Device() override;
    Media::PipelineManager& Pipeline() override;
    Media::TrackFactory& TrackFactory() override;
    IReadStore& ReadStore() override;
    Configuration::IStoreReadWrite& ReadWriteStore() override;
    Configuration::IConfigManager& ConfigManager() override;
    Configuration::IConfigInitialiser& ConfigInitialiser() override;
    IPowerManager& PowerManager() override;
    IThreadPool& ThreadPool() override;
    Av::Product& Product() override;
    Av::IFriendlyNameObservable& FriendlyNameObservable() override;
    OpenHome::Av::IVolumeManager& VolumeManager() override;
    Media::IMute& SystemMute() override;
    Credentials& CredentialsManager() override;
    Media::MimeTypeList& MimeTypes() override;
    void Add(Media::UriProvider* aUriProvider) override;
    void AddAttribute(const TChar* aAttribute) override;
    ILoggerSerial& BufferLogOutput(TUint aBytes, IShell& aShell, Optional<ILogPoster> aLogPoster) override; // must be called before Start()
    IUnixTimestamp& UnixTimestamp() override;
    ITransportRepeatRandom& TransportRepeatRandom() override;
    Optional<IPinsAccountStore> PinsAccountStore() override;
    Optional<IPinsInvocable> PinsInvocable() override;
    Optional<IPinSetObservable> PinSetObservable() override;
private:
    Net::DvStack& iDvStack;
    Net::CpStack& iCpStack;
    Net::DvDeviceStandard& iDevice;
    KvpStore* iKvpStore;
    Media::PipelineManager* iPipeline;
    Media::TrackFactory* iTrackFactory;
    Configuration::IStoreReadWrite& iReadWriteStore;
    Configuration::ConfigManager* iConfigManager;
    OpenHome::PowerManager* iPowerManager;
    IThreadPool* iThreadPool;
    Configuration::ConfigText* iConfigProductRoom;
    Configuration::ConfigText* iConfigProductName;
    Configuration::ConfigChoice* iConfigAutoPlay;
    Av::Product* iProduct;
    Av::FriendlyNameManager* iFriendlyNameManager;
    VolumeConfig* iVolumeConfig;
    Av::VolumeManager* iVolumeManager;
    ConfigStartupSource* iConfigStartupSource;
    Credentials* iCredentials;
    Media::MimeTypeList iMimeTypes;
    ProviderTime* iProviderTime;
    ProviderInfo* iProviderInfo;
    ProviderTransport* iProviderTransport;
    Av::TransportRepeatRandom iTransportRepeatRandom;
    Configuration::ProviderConfig* iProviderConfig;
    Configuration::ProviderConfigApp* iProviderConfigApp;
    LoggerBuffered* iLoggerBuffered;
    IUnixTimestamp* iUnixTimestamp;
    PinsManager* iPinsManager;
    ProviderPins* iProviderPins;
    Av::TransportPins* iTransportPins;
};

} // namespace Av
} // namespace OpenHome

