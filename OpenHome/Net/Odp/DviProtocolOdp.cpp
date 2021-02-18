#include <OpenHome/Net/Odp/DviProtocolOdp.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviDevice.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Private/NetworkAdapterList.h>

using namespace OpenHome;
using namespace OpenHome::Net;

// DviProtocolFactoryOdp

IDvProtocol* DviProtocolFactoryOdp::CreateProtocol(DviDevice& aDevice)
{
    return new DviProtocolOdp(aDevice);
}

void DviProtocolFactoryOdp::Start()
{
}

// DviProtocolOdp

const Brn DviProtocolOdp::kProtocolName("Odp");

DviProtocolOdp::DviProtocolOdp(DviDevice& /*aDevice*/)
{
}

DviProtocolOdp::~DviProtocolOdp()
{
}

void DviProtocolOdp::WriteResource(const Brx& /*aUriTail*/,
                                   const TIpAddress& /*aAdapter*/,
                                   std::vector<char*>& /*aLanguageList*/,
                                   IResourceWriter& /*aResourceWriter*/)
{
    ASSERTS(); // resources aren't served over Odp
}

void DviProtocolOdp::Enable()
{
}

void DviProtocolOdp::Disable(Functor& aComplete)
{
    aComplete();
}

void DviProtocolOdp::SendAnnouncements()
{
    // FIXME - no obvious benefit to repeating announcements on a persistent connection but really should for completeness...
}

const Brx& DviProtocolOdp::ProtocolName() const
{
    return kProtocolName;
}

void DviProtocolOdp::GetAttribute(const TChar* aKey, const TChar** aValue) const
{
    *aValue = iAttributeMap.Get(aKey);
}

void DviProtocolOdp::SetAttribute(const TChar* aKey, const TChar* aValue)
{
    iAttributeMap.Set(aKey, aValue);
}

void DviProtocolOdp::SetCustomData(const TChar* /*aTag*/, void* /*aData*/)
{
    ASSERTS(); // can't think why this'd be called
}

void DviProtocolOdp::GetResourceManagerUri(const NetworkAdapter& /*aAdapter*/, Brh& /*aUri*/)
{
    // don't supply any resources over Odp so leave aUri unchanged
}
