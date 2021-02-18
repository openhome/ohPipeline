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
public: // from IDvProtocolFactory
    void Start() override;
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
                       const TIpAddress& aAdapter,
                       std::vector<char*>& aLanguageList,
                       IResourceWriter& aResourceWriter) override;
private: // from IDvProtocol
    const Brx& ProtocolName() const override;
    void Enable() override;
    void Disable(Functor& aComplete) override;
    void SendAnnouncements() override;
    void GetAttribute(const TChar* aKey, const TChar** aValue) const override;
    void SetAttribute(const TChar* aKey, const TChar* aValue) override;
    void SetCustomData(const TChar* aTag, void* aData) override;
    void GetResourceManagerUri(const NetworkAdapter& aAdapter, Brh& aUri) override;
private:
    AttributeMap iAttributeMap;
};

} // namespace Net
} // namespace OpenHome
