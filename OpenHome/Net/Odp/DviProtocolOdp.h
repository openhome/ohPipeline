#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviDevice.h>
#include <OpenHome/Av/Product.h>

namespace OpenHome {
    class NetworkAdapter;
namespace Net {

class DviProtocolFactoryOdp : public IDvProtocolFactory
{
private: // from IDvProtocolFactory
    IDvProtocol* CreateProtocol(DviDevice& aDevice) override;
};

class DviProtocolOdp : public IDvProtocol
{
    static const Brn kProtocolName;
public:
    DviProtocolOdp(DviDevice& aDevice);
    ~DviProtocolOdp();
private: // from IResourceManager
    void WriteResource(const Brx& aUriTail,
                       TIpAddress aAdapter,
                       std::vector<char*>& aLanguageList,
                       IResourceWriter& aResourceWriter) override;
    void Register();
    void Deregister();
private: // from IDvProtocol
    const Brx& ProtocolName() const override;
    void Enable() override;
    void Disable(Functor& aComplete) override;
    void GetAttribute(const TChar* aKey, const TChar** aValue) const override;
    void SetAttribute(const TChar* aKey, const TChar* aValue) override;
    void SetCustomData(const TChar* aTag, void* aData) override;
    void GetResourceManagerUri(const NetworkAdapter& aAdapter, Brh& aUri) override;
private:
    void RegisterLocked();
    void DeregisterLocked();
    void NameChanged(const Brx& aName);
    void HandleInterfaceChange();
private:
    DviDevice& iDevice;
    Environment& iEnv;
    AttributeMap iAttributeMap;
    IMdnsProvider& iProvider;
    //Av::IFriendlyNameObservable& iFriendlyNameObservable;
    //TUint iFriendlyNameId;
    Bws<Av::IFriendlyNameObservable::kMaxFriendlyNameBytes+1> iName;    // Space for '\0'.
    //Endpoint iEndpoint;
    const TUint iHandleOdp;
    TBool iRegistered;
    Mutex iLock;
    TInt iCurrentAdapterChangeListenerId;
    TUint iSubnetListChangeListenerId;
    Endpoint iEndpoint;
};

} // namespace Net
} // namespace OpenHome
