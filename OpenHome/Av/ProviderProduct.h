#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Standard.h>
#include <Generated/DvAvOpenhomeOrgProduct4.h>
#include <OpenHome/Net/Core/DvInvocationResponse.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Thread.h>

namespace OpenHome {
    class PowerManager;
    class IStandbyObserver;

namespace Av {

class ProviderProduct : public Net::DvProviderAvOpenhomeOrgProduct4
                      , private IProductObserver
                      , private IProductNameObserver
                      , IProductAttributesObserver
                      , private IStandbyHandler
{
private:
    static const TUint kSourceXmlGranularityBytes = 4 * 1024;
    static const TUint kAttributeGranularityBytes = 128;
public:
    ProviderProduct(Net::DvDevice& aDevice, Av::Product& aProduct, IPowerManager& aPowerManager);
    ~ProviderProduct();
private: // from DvProviderAvOpenhomeOrgProduct1
    void Manufacturer(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseString& aName, Net::IDvInvocationResponseString& aInfo, Net::IDvInvocationResponseString& aUrl, Net::IDvInvocationResponseString& aImageUri) override;
    void Model(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseString& aName, Net::IDvInvocationResponseString& aInfo, Net::IDvInvocationResponseString& aUrl, Net::IDvInvocationResponseString& aImageUri) override;
    void Product(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseString& aRoom, Net::IDvInvocationResponseString& aName, Net::IDvInvocationResponseString& aInfo, Net::IDvInvocationResponseString& aUrl, Net::IDvInvocationResponseString& aImageUri, Net::IDvInvocationResponseString& aImageHiresUri) override;
    void Standby(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseBool& aValue) override;
    void StandbyTransitioning(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseBool& aValue) override;
    void SetStandby(Net::IDvInvocation& aInvocation, TBool aValue) override;
    void SourceCount(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseUint& aValue) override;
    void SourceXml(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseString& aValue) override;
    void SourceIndex(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseUint& aValue) override;
    void SetSourceIndex(Net::IDvInvocation& aInvocation, TUint aValue) override;
    void SetSourceIndexByName(Net::IDvInvocation& aInvocation, const Brx& aValue) override;
    void SetSourceBySystemName(Net::IDvInvocation& aInvocation, const Brx& aValue) override;
    void Source(Net::IDvInvocation& aInvocation, TUint aIndex, Net::IDvInvocationResponseString& aSystemName, Net::IDvInvocationResponseString& aType, Net::IDvInvocationResponseString& aName, Net::IDvInvocationResponseBool& aVisible) override;
    void Attributes(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseString& aValue) override;
    void SourceXmlChangeCount(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseUint& aValue) override;
    void GetImageUri(Net::IDvInvocation& aInvocation, const Brx& aResolution, Net::IDvInvocationResponseString& aImageUri) override;
private: // from IProductObserver
    void Started() override;
    void SourceIndexChanged() override;
    void SourceXmlChanged() override;
    void ProductUrisChanged() override;
private: // from IProductNameObserver
    void RoomChanged(const Brx& aRoom) override;
    void NameChanged(const Brx& aName) override;
private: // from IProductAttributesObserver
    void AttributesChanged() override;
private: // from IStandbyHandler
    void StandbyEnabled() override;
    void StandbyTransitioning() override;
    void StandbyDisabled(StandbyDisableReason aReason) override;
private:
    void UpdatePresentationUrlLocked();
private:
    Net::DvDevice& iDevice;
    Av::Product& iProduct;
    IPowerManager& iPowerManager;
    Mutex iLock;
    WriterBwh iSourceXml;
    IStandbyObserver* iStandbyObserver;
    WriterBwh iAttributes;
    Bws<512> iPresentationUrl;
};

} // namespace Av
} // namespace OpenHome

