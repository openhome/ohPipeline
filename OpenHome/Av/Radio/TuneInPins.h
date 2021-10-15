#pragma once

#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <Generated/CpAvOpenhomeOrgRadio2.h>
#include <OpenHome/Av/Pins/Pins.h>
#include <OpenHome/Av/Pins/PodcastPinsTuneIn.h>
        
namespace OpenHome {
    class Environment;
    class IThreadPool;
    class IThreadPoolHandle;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Av {

class TuneInPins
    : public IPinInvoker
{
    const TUint kMinSupportedVersion = 1;
    const TUint kMaxSupportedVersion = 1;

public:
    TuneInPins(Net::DvDeviceStandard& aDevice, OpenHome::Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack, Configuration::IStoreReadWrite& aStore, IThreadPool& aThreadPool, const Brx& aPartnerId);
    ~TuneInPins();

private: // from IPinInvoker
    void BeginInvoke(const IPin& aPin, Functor aCompleted) override;
    void Cancel() override;
    const TChar* Mode() const override;
    TBool SupportsVersion(TUint version) const override;
private:
    TBool LoadStream(const Brx& aStream, const IPin& aPin); // playable stream (tunein url)
    TBool LoadStation(const Brx& aStation, const IPin& aPin); // tunein station id (ie s1234)
    void Invoke();
private:
    Mutex iLock;
    Net::CpProxyAvOpenhomeOrgRadio2* iCpRadio;
    Av::PodcastPinsLatestEpisodeTuneIn* iPodcastPinsEpisode;
    IThreadPoolHandle* iThreadPoolHandle;
    Bws<128> iToken;
    Functor iCompleted;
    PinIdProvider iPinIdProvider;
    Pin iPin;
};

};  // namespace Av
};  // namespace OpenHome


