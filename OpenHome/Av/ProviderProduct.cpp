#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <Generated/DvAvOpenhomeOrgProduct3.h>
#include <OpenHome/Av/ProviderProduct.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Av/Utils/FaultCode.h>
#include <OpenHome/PowerManager.h>
#include <OpenHome/Net/Private/DviStack.h>

using namespace OpenHome;
using namespace OpenHome::Net;
using namespace OpenHome::Av;

const TUint ProviderProduct::kSourceXmlGranularityBytes;
const TUint ProviderProduct::kAttributeGranularityBytes;

ProviderProduct::ProviderProduct(Net::DvDevice& aDevice, Av::Product& aProduct, IPowerManager& aPowerManager)
    : DvProviderAvOpenhomeOrgProduct3(aDevice)
    , iDevice(aDevice)
    , iProduct(aProduct)
    , iPowerManager(aPowerManager)
    , iLock("PrPr")
    , iSourceXml(kSourceXmlGranularityBytes)
    , iAttributes(kAttributeGranularityBytes)
{
    EnablePropertyManufacturerName();
    EnablePropertyManufacturerInfo();
    EnablePropertyManufacturerUrl();
    EnablePropertyManufacturerImageUri();
    EnablePropertyModelName();
    EnablePropertyModelInfo();
    EnablePropertyModelUrl();
    EnablePropertyModelImageUri();
    EnablePropertyProductRoom();
    EnablePropertyProductName();
    EnablePropertyProductInfo();
    EnablePropertyProductUrl();
    EnablePropertyProductImageUri();
    EnablePropertyStandby();
    EnablePropertyStandbyTransitioning();
    EnablePropertySourceIndex();
    EnablePropertySourceCount();
    EnablePropertySourceXml();
    EnablePropertyAttributes();

    EnableActionManufacturer();
    EnableActionModel();
    EnableActionProduct();
    EnableActionStandby();
    EnableActionStandbyTransitioning();
    EnableActionSetStandby();
    EnableActionSourceCount();
    EnableActionSourceXml();
    EnableActionSourceIndex();
    EnableActionSetSourceIndex();
    EnableActionSetSourceIndexByName();
    EnableActionSetSourceBySystemName();
    EnableActionSource();
    EnableActionAttributes();
    EnableActionSourceXmlChangeCount();

    {
        Brn name;
        Brn info;
        Bws<Product::kMaxUriBytes> url;
        Bws<Product::kMaxUriBytes> imageUri;
        iProduct.GetManufacturerDetails(name, info, url, imageUri);
        SetPropertyManufacturerName(name);
        SetPropertyManufacturerInfo(info);
        SetPropertyManufacturerUrl(url);
        SetPropertyManufacturerImageUri(imageUri);

        iProduct.GetModelDetails(name, info, url, imageUri);
        SetPropertyModelName(name);
        SetPropertyModelInfo(info);
        SetPropertyModelUrl(url);
        SetPropertyModelImageUri(imageUri);
    }

    {
        Bws<Product::kMaxRoomBytes> room;
        Bws<Product::kMaxNameBytes> name;
        Brn info;
        Bws<Product::kMaxUriBytes> imageUri;
        iProduct.GetProductDetails(room, name, info, imageUri);
        SetPropertyProductRoom(room);
        SetPropertyProductName(name);
        SetPropertyProductInfo(info);
        SetPropertyProductImageUri(imageUri);
    }
    UpdatePresentationUrlLocked(); // no need for lock yet - observers aren't registered so no other functions will run in other threads
    SetPropertyProductUrl(iPresentationUrl);

    iStandbyObserver = aPowerManager.RegisterStandbyHandler(*this, kStandbyHandlerPriorityLowest, "ProviderProduct");
    iProduct.AddObserver(*this);
    iProduct.AddNameObserver(*this);
    iProduct.AddAttributesObserver(*this);
}

ProviderProduct::~ProviderProduct()
{
    delete iStandbyObserver;
}

void ProviderProduct::Manufacturer(IDvInvocation& aInvocation, IDvInvocationResponseString& aName, IDvInvocationResponseString& aInfo, IDvInvocationResponseString& aUrl, IDvInvocationResponseString& aImageUri)
{
    Brn name;
    Brn info;
    Bws<Product::kMaxUriBytes> url;
    Bws<Product::kMaxUriBytes> imageUri;
    iProduct.GetManufacturerDetails(name, info, url, imageUri);

    aInvocation.StartResponse();
    aName.Write(name);
    aName.WriteFlush();
    aInfo.Write(info);
    aInfo.WriteFlush();
    aUrl.Write(url);
    aUrl.WriteFlush();
    aImageUri.Write(imageUri);
    aImageUri.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderProduct::Model(IDvInvocation& aInvocation, IDvInvocationResponseString& aName, IDvInvocationResponseString& aInfo, IDvInvocationResponseString& aUrl, IDvInvocationResponseString& aImageUri)
{
    Brn name;
    Brn info;
    Bws<Product::kMaxUriBytes> url;
    Bws<Product::kMaxUriBytes> imageUri;
    iProduct.GetModelDetails(name, info, url, imageUri);

    aInvocation.StartResponse();
    aName.Write(name);
    aName.WriteFlush();
    aInfo.Write(info);
    aInfo.WriteFlush();
    aUrl.Write(url);
    aUrl.WriteFlush();
    aImageUri.Write(imageUri);
    aImageUri.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderProduct::Product(IDvInvocation& aInvocation, IDvInvocationResponseString& aRoom, IDvInvocationResponseString& aName, IDvInvocationResponseString& aInfo, IDvInvocationResponseString& aUrl, IDvInvocationResponseString& aImageUri)
{
    Bws<Product::kMaxRoomBytes> room;
    Bws<Product::kMaxNameBytes> name;
    Brn info;
    Bws<Product::kMaxUriBytes> imageUri;
    iProduct.GetProductDetails(room, name, info, imageUri);

    aInvocation.StartResponse();
    aRoom.Write(room);
    aRoom.WriteFlush();
    aName.Write(name);
    aName.WriteFlush();
    aInfo.Write(info);
    aInfo.WriteFlush();
    {
        AutoMutex _(iLock);
        UpdatePresentationUrlLocked();
        aUrl.Write(iPresentationUrl);
    }
    aUrl.WriteFlush();
    aImageUri.Write(imageUri);
    aImageUri.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderProduct::Standby(IDvInvocation& aInvocation, IDvInvocationResponseBool& aValue)
{
    TBool standby;
    GetPropertyStandby(standby);
    aInvocation.StartResponse();
    aValue.Write(standby);
    aInvocation.EndResponse();
}

void ProviderProduct::StandbyTransitioning(IDvInvocation& aInvocation, IDvInvocationResponseBool& aValue)
{
    TBool st;
    GetPropertyStandbyTransitioning(st);
    aInvocation.StartResponse();
    aValue.Write(st);
    aInvocation.EndResponse();
}

void ProviderProduct::SetStandby(IDvInvocation& aInvocation, TBool aValue)
{
    if (aValue) {
        iPowerManager.StandbyEnable();
    }
    else {
        iPowerManager.StandbyDisable(StandbyDisableReason::Product);
    }
    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderProduct::SourceCount(IDvInvocation& aInvocation, IDvInvocationResponseUint& aValue)
{
    aInvocation.StartResponse();
    aValue.Write(iProduct.SourceCount());
    aInvocation.EndResponse();
}

void ProviderProduct::SourceXml(IDvInvocation& aInvocation, IDvInvocationResponseString& aValue)
{
    aInvocation.StartResponse();
    {
        AutoMutex amx(iLock);
        aValue.Write(iSourceXml.Buffer());
    }
    aValue.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderProduct::SourceIndex(IDvInvocation& aInvocation, IDvInvocationResponseUint& aValue)
{
    aInvocation.StartResponse();
    aValue.Write(iProduct.CurrentSourceIndex());
    aInvocation.EndResponse();
}

void ProviderProduct::SetSourceIndex(IDvInvocation& aInvocation, TUint aValue)
{
    try {
        (void)iProduct.SetCurrentSource(aValue);
    }
    catch(AvSourceNotFound& ) {
        FaultCode::Report(aInvocation, FaultCode::kSourceNotFound);
    }
    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderProduct::SetSourceIndexByName(IDvInvocation& aInvocation, const Brx& aValue)
{
    try {
        iProduct.SetCurrentSourceByName(aValue);
    }
    catch(AvSourceNotFound& ) {
        FaultCode::Report(aInvocation, FaultCode::kSourceNotFound);
    }
    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderProduct::SetSourceBySystemName(Net::IDvInvocation& aInvocation, const Brx& aValue)
{
    try {
        iProduct.SetCurrentSourceBySystemName(aValue);
    }
    catch (AvSourceNotFound&) {
        FaultCode::Report(aInvocation, FaultCode::kSourceNotFound);
    }
    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderProduct::Source(IDvInvocation& aInvocation, TUint aIndex, IDvInvocationResponseString& aSystemName, IDvInvocationResponseString& aType, IDvInvocationResponseString& aName, IDvInvocationResponseBool& aVisible)
{
    Bws<ISource::kMaxSystemNameBytes> systemName;
    Bws<ISource::kMaxSourceTypeBytes> type;
    Bws<ISource::kMaxSourceTypeBytes> name;
    TBool visible = false;
    try {
        iProduct.GetSourceDetails(aIndex, systemName, type, name, visible);
    }
    catch(AvSourceNotFound& ) {
        FaultCode::Report(aInvocation, FaultCode::kSourceNotFound);
    }

    aInvocation.StartResponse();
    aSystemName.Write(systemName);
    aSystemName.WriteFlush();
    aType.Write(type);
    aType.WriteFlush();
    aName.Write(name);
    aName.WriteFlush();
    aVisible.Write(visible);
    aInvocation.EndResponse();
}

void ProviderProduct::Attributes(IDvInvocation& aInvocation, IDvInvocationResponseString& aValue)
{
    aInvocation.StartResponse();
    {
        AutoMutex _(iLock);
        aValue.Write(iAttributes.Buffer());
    }
    aValue.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderProduct::SourceXmlChangeCount(IDvInvocation& aInvocation, IDvInvocationResponseUint& aValue)
{
    aInvocation.StartResponse();
    aValue.Write(iProduct.SourceXmlChangeCount());
    aInvocation.EndResponse();
}

void ProviderProduct::Started()
{
    SetPropertySourceIndex(iProduct.CurrentSourceIndex());
    SetPropertySourceCount(iProduct.SourceCount());
    SourceXmlChanged();
}

void ProviderProduct::ProductUrisChanged()
{
    {
        Brn name;
        Brn info;
        Bws<Product::kMaxUriBytes> url;
        Bws<Product::kMaxUriBytes> imageUri;
        iProduct.GetManufacturerDetails(name, info, url, imageUri);
        SetPropertyManufacturerUrl(url);
        SetPropertyManufacturerImageUri(imageUri);

        iProduct.GetModelDetails(name, info, url, imageUri);
        SetPropertyModelUrl(url);
        SetPropertyModelImageUri(imageUri);
    }

    {
        Bws<Product::kMaxRoomBytes> room;
        Bws<Product::kMaxNameBytes> name;
        Brn info;
        Bws<Product::kMaxUriBytes> imageUri;
        iProduct.GetProductDetails(room, name, info, imageUri);
        SetPropertyProductImageUri(imageUri);
    }

    {
        AutoMutex _(iLock);
        UpdatePresentationUrlLocked();
        SetPropertyProductUrl(iPresentationUrl);
    }
}

void ProviderProduct::SourceIndexChanged()
{
    SetPropertySourceIndex(iProduct.CurrentSourceIndex());
}

void ProviderProduct::SourceXmlChanged()
{
    AutoMutex amx(iLock);
    iSourceXml.Reset();
    iProduct.GetSourceXml(iSourceXml);
    SetPropertySourceXml(iSourceXml.Buffer());
}

void ProviderProduct::RoomChanged(const Brx& aRoom)
{
    SetPropertyProductRoom(aRoom);
}

void ProviderProduct::NameChanged(const Brx& aName)
{
    SetPropertyProductName(aName);
}

void ProviderProduct::AttributesChanged()
{
    AutoMutex _(iLock);
    iAttributes.Reset();
    iProduct.GetAttributes(iAttributes);
    SetPropertyAttributes(iAttributes.Buffer());
}

void ProviderProduct::StandbyEnabled()
{
    SetPropertyStandby(true);
    SetPropertyStandbyTransitioning(false);
}

void ProviderProduct::StandbyTransitioning()
{
    SetPropertyStandbyTransitioning(true);
}

void ProviderProduct::StandbyDisabled(StandbyDisableReason /*aReason*/)
{
    SetPropertyStandby(false);
    SetPropertyStandbyTransitioning(false);
}

void ProviderProduct::UpdatePresentationUrlLocked()
{
    const TChar* presentationUrl;
    iDevice.GetAttribute("Upnp.PresentationUrl", &presentationUrl);
    if (presentationUrl == nullptr) {
        presentationUrl = "";
    }
    if (presentationUrl[0] != ':' && presentationUrl[0] != '/') {
        iPresentationUrl.Replace(presentationUrl);
        return;
    }
    iPresentationUrl.Replace("http://");

    AutoNetworkAdapterRef ar(iDvStack.Env(), "Av::Product");
    auto current = ar.Adapter();
    auto addr = (current == nullptr) ? kTIpAddressEmpty : current->Address();
    Endpoint::AppendAddress(iPresentationUrl, addr);

    iPresentationUrl.Append(presentationUrl);
}
