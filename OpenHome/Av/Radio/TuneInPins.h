#pragma once

#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <Generated/CpAvOpenhomeOrgRadio1.h>
#include <OpenHome/Av/Pins/Pins.h>
#include <OpenHome/Av/Pins/PodcastPinsTuneIn.h>
        
namespace OpenHome {
    class Environment;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Av {

class TuneInPins
    : public IPinInvoker
{
public:
    TuneInPins(Net::DvDeviceStandard& aDevice, OpenHome::Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack, Configuration::IStoreReadWrite& aStore, const Brx& aPartnerId);
    ~TuneInPins();

private: // from IPinInvoker
    void BeginInvoke(const IPin& aPin, Functor aCompleted) override;
    void Cancel() override;
    const TChar* Mode() const override;
private:
    TBool LoadStream(const Brx& aStream, const IPin& aPin); // playable stream (tunein url)
    TBool LoadStation(const Brx& aStation, const IPin& aPin); // tunein station id (ie s1234)
private:
    Mutex iLock;
    Net::CpProxyAvOpenhomeOrgRadio1* iCpRadio;
    Net::CpStack& iCpStack;
    Av::PodcastPinsLatestEpisodeTuneIn* iPodcastPinsEpisode;
};

};  // namespace Av
};  // namespace OpenHome


