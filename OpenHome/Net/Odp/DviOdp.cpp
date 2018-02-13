#include <OpenHome/Net/Odp/DviOdp.h>
#include <OpenHome/Net/Odp/Odp.h>
#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Net/Private/DviService.h>
#include <OpenHome/Net/Private/Service.h>
#include <OpenHome/Json.h>
#include <OpenHome/Debug-ohMediaPlayer.h>
#include <OpenHome/Net/Core/OhNet.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Net/Private/DviDevice.h>
#include <OpenHome/Net/Private/DviService.h>

#include <atomic>
#include <map>
#include <limits.h>

using namespace OpenHome;
using namespace OpenHome::Net;

// PropertyWriterFactoryOdp

PropertyWriterFactoryOdp::PropertyWriterFactoryOdp(IOdpSession& aSession, DvStack& aDvStack)
    : iLock("OdpP")
    , iSession(aSession)
    , iSubscriptionManager(aDvStack.SubscriptionManager())
    , iEnabled(true)
    , iRefCount(1)
    , iWriter(nullptr)
    , iDuration(aDvStack.Env().InitParams()->DvMaxUpdateTimeSecs())
{
    ASSERT(iRefCount.is_lock_free());
    iRenewTimer = new Timer(aDvStack.Env(),
                            MakeFunctor(*this, &PropertyWriterFactoryOdp::Renew),
                            "PropertyWriterFactoryOdp");
}

void PropertyWriterFactoryOdp::Disable()
{
    iRenewTimer->Cancel();
    std::vector<DviSubscription*> subscriptions;
    subscriptions.reserve(iSubscriptions.size());
    {
        AutoMutex _(iLock);
        iEnabled = false;
        for (auto it=iSubscriptions.begin(); it!=iSubscriptions.end(); ++it) {
            auto subscription = iSubscriptionManager.Find(it->second);
            if (subscription != nullptr) {
                subscriptions.push_back(subscription);
            }
        }
    }
    for (auto it=subscriptions.begin(); it!=subscriptions.end(); ++it) {
        (*it)->Remove();
        (*it)->RemoveRef();
    }
    RemoveRef();
}

PropertyWriterFactoryOdp::~PropertyWriterFactoryOdp()
{
    delete iRenewTimer;
}

void PropertyWriterFactoryOdp::AddRef()
{
    ++iRefCount;
}

void PropertyWriterFactoryOdp::RemoveRef()
{
    if (--iRefCount == 0) {
        delete this;
    }
}

void PropertyWriterFactoryOdp::Renew()
{
    std::vector<DviSubscription*> subscriptions;
    subscriptions.reserve(iSubscriptions.size());
    {
        AutoMutex _(iLock);
        for (auto it=iSubscriptions.begin(); it!=iSubscriptions.end(); ++it) {
            auto subscription = iSubscriptionManager.Find(it->second);
            if (subscription != nullptr) {
                subscriptions.push_back(subscription);
            }
        }
    }
    for (auto it=subscriptions.begin(); it!=subscriptions.end(); ++it) {
        try {
            (*it)->Renew(iDuration);
        }
        catch (DvSubscriptionError&) {}
        (*it)->RemoveRef();
    }
    ScheduleRenewTimer();
}

void PropertyWriterFactoryOdp::ScheduleRenewTimer()
{
    const TUint renewMs = (iDuration * 1000) / 20;
    iRenewTimer->FireIn(renewMs);
}

IPropertyWriter* PropertyWriterFactoryOdp::ClaimWriter(const IDviSubscriptionUserData* /*aUserData*/,
                                                       const Brx& aSid, TUint /*aSequenceNumber*/)
{
    {
        AutoMutex _(iLock);
        if (!iEnabled) {
            return nullptr;
        }
        iWriter = &iSession.WriteLock();
    }
    try {
        iWriterNotify.Set(*iWriter);
        iWriterNotify.WriteString(Odp::kKeyType, Odp::kTypeNotify);
        iWriterNotify.WriteString(Odp::kKeySid, aSid);
        iWriterProperties = iWriterNotify.CreateArray(Odp::kKeyProperties);
    }
    catch (WriterError&) {
        iSession.WriteUnlock();
        throw;
    }
    return this;
}

void PropertyWriterFactoryOdp::ReleaseWriter(IPropertyWriter* /*aWriter*/)
{
    AutoOdpSession _(iSession);
    iWriterProperties.WriteEnd();
    iWriterNotify.WriteEnd();
    iSession.WriteEnd();
}

void PropertyWriterFactoryOdp::NotifySubscriptionCreated(const Brx& aSid)
{
    TBool startTimer = false;
    {
        AutoMutex _(iLock);
        Brn sid(aSid);
        iSubscriptions.insert(std::pair<Brn, Brn>(sid, sid));
        AddRef();
        startTimer = (iSubscriptions.size() == 1); // 1 => first subscription created
    }
    if (startTimer) {
        ScheduleRenewTimer();
    }
}

void PropertyWriterFactoryOdp::NotifySubscriptionDeleted(const Brx& aSid)
{
    TBool knownSubscription = false;
    TBool cancelTimer = false;
    {
        AutoMutex _(iLock);
        Brn sid(aSid);
        auto it = iSubscriptions.find(sid);
        if (it != iSubscriptions.end()) {
            iSubscriptions.erase(it);
            knownSubscription = true;
        }
        cancelTimer = (iSubscriptions.size() == 0);
    }
    if (cancelTimer) {
        iRenewTimer->Cancel();
    }
    if (knownSubscription) {
        RemoveRef();
    }
}

void PropertyWriterFactoryOdp::NotifySubscriptionExpired(const Brx& /*aSid*/)
{
}

void PropertyWriterFactoryOdp::LogUserData(IWriter& aWriter, const IDviSubscriptionUserData& /*aUserData*/)
{
    aWriter.Write(Brn(", protocol: Odp"));
}

void PropertyWriterFactoryOdp::PropertyWriteString(const Brx& aName, const Brx& aValue)
{
    auto writerObj = iWriterProperties.CreateObject();
    AutoWriterJson _(writerObj);
    {
        writerObj.WriteString(Odp::kKeyName, aName);
        auto writerString = writerObj.CreateStringStreamed(Odp::kKeyValue);
        AutoWriterJson __(writerString);
        writerString.WriteEscaped(aValue);
    }
}

void PropertyWriterFactoryOdp::PropertyWriteInt(const Brx& aName, TInt aValue)
{
    Bws<Ascii::kMaxIntStringBytes> valBuf;
    Ascii::AppendDec(valBuf, aValue);
    auto writerObj = iWriterProperties.CreateObject();
    AutoWriterJson _(writerObj);
    writerObj.WriteString(Odp::kKeyName, aName);
    writerObj.WriteString(Odp::kKeyValue, valBuf);
}

void PropertyWriterFactoryOdp::PropertyWriteUint(const Brx& aName, TUint aValue)
{
    Bws<Ascii::kMaxUintStringBytes> valBuf;
    Ascii::AppendDec(valBuf, aValue);
    auto writerObj = iWriterProperties.CreateObject();
    AutoWriterJson _(writerObj);
    writerObj.WriteString(Odp::kKeyName, aName);
    writerObj.WriteString(Odp::kKeyValue, valBuf);
}

void PropertyWriterFactoryOdp::PropertyWriteBool(const Brx& aName, TBool aValue)
{
    auto writerObj = iWriterProperties.CreateObject();
    AutoWriterJson _(writerObj);
    writerObj.WriteString(Odp::kKeyName, aName);
    writerObj.WriteString(Odp::kKeyValue, aValue ? WriterJson::kBoolTrue : WriterJson::kBoolFalse);
}

void PropertyWriterFactoryOdp::PropertyWriteBinary(const Brx& aName, const Brx& aValue)
{
    auto writerObj = iWriterProperties.CreateObject();
    AutoWriterJson _(writerObj);
    {
        writerObj.WriteString(Odp::kKeyName, aName);
        auto writerString = writerObj.CreateStringStreamed(Odp::kKeyValue);
        AutoWriterJson __(writerString);
        Converter::ToBase64(writerString, aValue);
    }
}

void PropertyWriterFactoryOdp::PropertyWriteEnd()
{
}


// DviOdp

const TUint DviOdp::kServiceVersionInvalid = UINT_MAX;

const TUint DviOdp::kErrCodeSubscriptionUnknown             = 800;
const Brn DviOdp::kErrMsgSubscriptionUnknown("Unknown");
const TUint DviOdp::kErrCodeSubscriptionNoDevice            = 801;
const Brn DviOdp::kErrMsgSubscriptionNoDevice("Device not found");
const TUint DviOdp::kErrCodeSubscriptionNoService           = 802;
const Brn DviOdp::kErrMsgSubscriptionNoService("Service not found");
const TUint DviOdp::kErrCodeSubscriptionNoServiceVersion    = 803;
const Brn DviOdp::kErrMsgSubscriptionNoServiceVersion("Service version not found");

DviOdp::DviOdp(DvStack& aDvStack, IOdpSession& aSession)
    : iDvStack(aDvStack)
    , iSession(aSession)
    , iPropertyWriterFactory(nullptr)
    , iWriter(nullptr)
{
}

void DviOdp::Announce()
{
    iPropertyWriterFactory = new PropertyWriterFactoryOdp(iSession, iDvStack);
    auto deviceMap = iDvStack.DeviceMap().CopyMap();
    iWriter = &iSession.WriteLock();
    AutoOdpSession _(iSession);
    iResponseStarted = true;
    try {
        WriterJsonObject writer(*iWriter);
        writer.WriteString(Odp::kKeyType, Odp::kTypeAnnouncement);
        writer.WriteInt(Odp::kKeyProtocolVersion, 2);
        auto writerDevices = writer.CreateArray(Odp::kKeyDevices);
        for (auto it=deviceMap.begin(); it!=deviceMap.end(); ++it) {
            auto device = it->second;
            const TChar* alias = nullptr;
            device->GetAttribute("Odp.Name", &alias);
            const Brx& udn = device->Udn();
            if (alias == nullptr) {
                LOG(kOdp, "Device %.*s has no alias set, skipping...\n", PBUF(udn))
                continue;
            }
            auto writerDevice = writerDevices.CreateObject();
            writerDevice.WriteString(Odp::kKeyId, udn);
            Brn aliasBuf(alias);
            writerDevice.WriteString(Odp::kKeyAlias, aliasBuf);
            auto writerServices = writerDevice.CreateArray(Odp::kKeyServices);
            const TUint count = device->ServiceCount();
            for (TUint i=0; i<count; i++) {
                const auto serviceType = device->Service(i).ServiceType();
                auto writerService = writerServices.CreateObject();
                writerService.WriteString(Odp::kKeyName, serviceType.Name());
                writerService.WriteInt(Odp::kKeyVersion, serviceType.Version());
                writerService.WriteEnd();
            }
            writerServices.WriteEnd();
            writerDevice.WriteEnd();
        }
        writerDevices.WriteEnd();
        writer.WriteEnd();

        iResponseEnded = true;
        iSession.WriteEnd();
        iWriter = nullptr;
        iDvStack.DeviceMap().ClearMap(deviceMap);
    }
    catch (Exception&) {
        iWriter = nullptr;
        iDvStack.DeviceMap().ClearMap(deviceMap);
        throw;
    }
}

void DviOdp::Disable()
{
    if (iPropertyWriterFactory != nullptr) {
        iPropertyWriterFactory->Disable();
        iPropertyWriterFactory = nullptr;
    }
}

void DviOdp::Process(const Brx& aJsonRequest)
{
    iResponseStarted = iResponseEnded = false;

    Bwn buf(aJsonRequest.Ptr(), aJsonRequest.Bytes(), aJsonRequest.Bytes());
    try {
        iParserReq.ParseAndUnescape(buf);
    }
    catch (JsonInvalid&) {
        LogParseErrorThrow("JsonInvalid", aJsonRequest);
    }
    catch (JsonUnsupported&) {
        LogParseErrorThrow("JsonUnsupported", aJsonRequest);
    }
    catch (JsonCorrupt&) {
        LogParseErrorThrow("JsonCorrupt", aJsonRequest);
    }

    Brn typeBuf;
    try {
        typeBuf.Set(iParserReq.String(Odp::kKeyType));
        iCorrelationId.Set(iParserReq.StringOptional(Odp::kKeyCorrelationId));
    }
    catch (JsonKeyNotFound&) {
        LOG_ERROR(kOdp, "Odp: No type on request\n%.*s\n", PBUF(aJsonRequest));
        THROW(OdpError);
    }
    if (typeBuf == Odp::kTypeAction) {
        Action();
    }
    else if (typeBuf == Odp::kTypeSubscribe) {
        Subscribe();
    }
    else if (typeBuf == Odp::kTypeUnsubscribe) {
        Unsubscribe();
    }
    else {
        LOG_ERROR(kOdp, "Odp: Unknown type on request - %.*s\n", PBUF(typeBuf));
        THROW(OdpError);
    }
    iCorrelationId.Set(Brx::Empty());
}

void DviOdp::LogParseErrorThrow(const TChar* aEx, const Brx& aJson)
{
    LOG_ERROR(kOdp, "Odp: %s parsing %.*s\n", aEx, PBUF(aJson));
    THROW(OdpError);
}

void DviOdp::Action()
{
    ParseDeviceAndService();
    Brn actionName;
    try {
        actionName.Set(iParserReq.String(Odp::kKeyAction));
    }
    catch (JsonKeyNotFound&) {
        LOG_ERROR(kOdp, "Odp: no action specified\n");
        THROW(OdpError);
    }
    Brn args = iParserReq.StringOptional(Odp::kKeyArguments);
    try {
        iArgs.clear();
        auto parserArgs = JsonParserArray::Create(args);
        if (parserArgs.Type() != JsonParserArray::ValType::Null) {
            try {
                for (;;) {
                    JsonParser parserArg;
                    parserArg.Parse(parserArgs.NextObject());
                    Brn argName = parserArg.String(Odp::kKeyName);
                    Brn argVal = parserArg.String(Odp::kKeyValue);
                    iArgs.insert(std::pair<Brn, Brn>(argName, argVal));
                }
            }
            catch (JsonArrayEnumerationComplete&) {}
        }
    }
    catch (JsonInvalid&) {
        LogParseErrorThrow("JsonInvalid", args);
    }
    catch (JsonUnsupported&) {
        LogParseErrorThrow("JsonUnsupported", args);
    }
    catch (JsonCorrupt&) {
        LogParseErrorThrow("JsonCorrupt", args);
    }
    catch (JsonKeyNotFound&) {
        LogParseErrorThrow("JsonKeyNotFound", args);
    }

    AutoOdpSession _(iSession);
    try {
        iService->InvokeDirect(*this, actionName);
    }
    catch (InvocationError&) {
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (...) {
        ASSERTS(); // don't expect InvokeDirect to throw anything other than InvocationError
    }
    iSession.WriteEnd();
    iWriter = nullptr;
}

void DviOdp::Subscribe()
{
    Brn deviceAlias;
    Brn serviceName;
    TUint serviceVersion = kServiceVersionInvalid;
    try {
        ParseDeviceAndService(deviceAlias, serviceName, serviceVersion);
    }
    catch (OdpError&) {
        iWriter = &iSession.WriteLock();
        AutoOdpSession _(iSession);
        iResponseStarted = true;
        WriterJsonObject writer(*iWriter);
        writer.WriteString(Odp::kKeyType, Odp::kTypeSubscribeResponse);
        writer.WriteString(Odp::kKeyDevice, deviceAlias);
        auto writerService = writer.CreateObject(Odp::kKeyService);
        writerService.WriteString(Odp::kKeyName, serviceName);
        writerService.WriteInt(Odp::kKeyVersion, serviceVersion);
        writerService.WriteEnd();
        auto writerErr = writer.CreateObject(Odp::kKeyError);
        TUint code = kErrCodeSubscriptionUnknown;
        Brn desc(kErrMsgSubscriptionUnknown);
        if (iDevice == nullptr) {
            code = kErrCodeSubscriptionNoDevice;
            desc.Set(kErrMsgSubscriptionNoDevice);
        }
        else if (iService == nullptr) {
            code = kErrCodeSubscriptionNoService;
            desc.Set(kErrMsgSubscriptionNoService);
        }
        else if (iServiceVersion == kServiceVersionInvalid) {
            code = kErrCodeSubscriptionNoServiceVersion;
            desc.Set(kErrMsgSubscriptionNoServiceVersion);
        }
        writerErr.WriteInt(Odp::kKeyCode, code);
        writerErr.WriteString(Odp::kKeyDescription, desc);
        writerErr.WriteEnd();
        if (iCorrelationId.Bytes() > 0) {
            writer.WriteString(Odp::kKeyCorrelationId, iCorrelationId);
        }
        auto writerSid = writer.CreateObject(Odp::kKeySid);
        writerSid.WriteEnd();
        writer.WriteEnd();

        iResponseEnded = true;
        iSession.WriteEnd();
        iWriter = nullptr;
        throw;
    }

    // create subscription
    Brh sid;
    iDevice->CreateSid(sid);
    auto subscription = new DviSubscription(iDvStack, *iDevice, *iPropertyWriterFactory, nullptr, sid);
    iDvStack.SubscriptionManager().AddSubscription(*subscription);

    // respond to subscription request
    iWriter = &iSession.WriteLock();
    AutoOdpSession _(iSession);
    iResponseStarted = true;
    WriterJsonObject writer(*iWriter);
    writer.WriteString(Odp::kKeyType, Odp::kTypeSubscribeResponse);
    writer.WriteString(Odp::kKeyDevice, deviceAlias);
    auto writerService = writer.CreateObject(Odp::kKeyService);
    writerService.WriteString(Odp::kKeyName, serviceName);
    writerService.WriteInt(Odp::kKeyVersion, serviceVersion);
    writerService.WriteEnd();
    auto writerError = writer.CreateObject(Odp::kKeyError);
    writerError.WriteEnd();
    if (iCorrelationId.Bytes() > 0) {
        writer.WriteString(Odp::kKeyCorrelationId, iCorrelationId);
    }
    writer.WriteString(Odp::kKeySid, subscription->Sid());
    writer.WriteEnd();
    iResponseEnded = true;
    iSession.WriteEnd();
    iWriter = nullptr;

    // Start subscription, prompting delivery of the first update (covering all state variables)
    iService->AddSubscription(subscription);
}

void DviOdp::Unsubscribe()
{
    Brn sid;
    try {
        sid.Set(iParserReq.String(Odp::kKeySid));
    }
    catch (JsonKeyNotFound&) {
        LOG_ERROR(kOdp, "Odp: No sid for unsubscribe\n");
        THROW(OdpError);
    }

    iService->RemoveSubscription(sid);

    iWriter = &iSession.WriteLock();
    AutoOdpSession _(iSession);
    iResponseStarted = true;
    WriterJsonObject writer(*iWriter);
    writer.WriteString(Odp::kKeyType, Odp::kTypeUnsubscribeResponse);
    if (iCorrelationId.Bytes() > 0) {
        writer.WriteString(Odp::kKeyCorrelationId, iCorrelationId);
    }
    writer.WriteEnd();
    iResponseEnded = true;
    iSession.WriteEnd();
    iWriter = nullptr;
}

void DviOdp::ParseDeviceAndService()
{
    Brn deviceAlias;
    Brn serviceName;
    TUint serviceVersion;
    ParseDeviceAndService(deviceAlias, serviceName, serviceVersion);
}

void DviOdp::ParseDeviceAndService(Brn& aDeviceAlias, Brn& aServiceName, TUint& aServiceVersion)
{
    iDevice = nullptr;
    iService = nullptr;
    iServiceVersion = kServiceVersionInvalid;

    try {
        Brn alias = iParserReq.String(Odp::kKeyDevice);
        auto deviceMap = iDvStack.DeviceMap().CopyMap();
        for (auto it=deviceMap.begin(); it!=deviceMap.end(); ++it) {
            auto device = it->second;
            const TChar* deviceAlias = nullptr;
            device->GetAttribute("Odp.Name", &deviceAlias);
            if (deviceAlias != nullptr) {
                Brn deviceAliasBuf(deviceAlias);
                if (deviceAliasBuf == alias) {
                    iDevice = device;
                    aDeviceAlias.Set(deviceAliasBuf);
                    break;
                }
            }
        }
        iDvStack.DeviceMap().ClearMap(deviceMap);
        if (iDevice == nullptr) {
            LOG_ERROR(kOdp, "Odp: device %.*s not present\n", PBUF(alias));
            THROW(OdpError);
        }
    }
    catch (JsonKeyNotFound&) {
        LOG_ERROR(kOdp, "Odp: No device specified for action\n");
        THROW(OdpError);
    }
    try {
        Brn serviceBuf = iParserReq.String(Odp::kKeyService);
        JsonParser parserService;
        try {
            parserService.Parse(serviceBuf);
        }
        catch (JsonInvalid&) {
            LogParseErrorThrow("JsonInvalid", serviceBuf);
        }
        catch (JsonUnsupported&) {
            LogParseErrorThrow("JsonUnsupported", serviceBuf);
        }
        catch (JsonCorrupt&) {
            LogParseErrorThrow("JsonCorrupt", serviceBuf);
        }
        try {
            aServiceName.Set(parserService.String(Odp::kKeyName));
            iServiceVersion = parserService.Num(Odp::kKeyVersion);
            aServiceVersion = iServiceVersion;
        }
        catch (JsonKeyNotFound&) {
            LOG_ERROR(kOdp, "Odp: incomplete service description - %.*s\n", PBUF(serviceBuf));
            THROW(OdpError);
        }
        const TUint count = iDevice->ServiceCount();
        for (TUint i=0; i<count; i++) {
            DviService& service = iDevice->Service(i);
            if (service.ServiceType().Name() == aServiceName) {
                iService = &service;
                break;
            }
        }
        if (iService == nullptr) {
            LOG_ERROR(kOdp, "Odp: service %.*s not present\n", PBUF(serviceBuf));
            THROW(OdpError);
        }
    }
    catch (JsonKeyNotFound&) {
        LOG_ERROR(kOdp, "Odp: No service specified for action\n");
        THROW(OdpError);
    }
}

Brn DviOdp::Arg(const TChar* aName)
{
    /* Questionable reusing Json exceptions.  I'll justify it since its an implementation
       detail that we've already parsed args out from json */
    Brn name(aName);
    const auto it = iArgs.find(name);
    if (it == iArgs.end()) {
        THROW(JsonKeyNotFound);
    }
    if (it->second == WriterJson::kNull) {
        THROW(JsonValueNull);
    }
    return it->second;
}

void DviOdp::Invoke()
{
    ASSERTS(); // FIXME - this seems inappropriate for IDviInvocation.  Can it be removed?
}

TUint DviOdp::Version() const
{
    return iServiceVersion;
}

TIpAddress DviOdp::Adapter() const
{
    return iSession.Adapter();
}

const char* DviOdp::ResourceUriPrefix() const
{
    ASSERTS();
    return "";
}

Endpoint DviOdp::ClientEndpoint() const
{
    THROW(OdpUnsupported); // we can't serve resources via Odp so there's no obvious need to know the port of the session socket
}

const Brx& DviOdp::ClientUserAgent() const
{
    try {
        iClientUserAgent.Set(iParserReq.String(Odp::kKeyUserAgent));
        if (iClientUserAgent.Bytes() > 0) {
            return iClientUserAgent;
        }
    }
    catch (JsonKeyNotFound&) {}
    return iSession.ClientUserAgentDefault();
}

void DviOdp::InvocationReadStart()
{
}

TBool DviOdp::InvocationReadBool(const TChar* aName)
{
    Brn buf = Arg(aName);
    return Ascii::CaseInsensitiveEquals(buf, WriterJson::kBoolTrue);
}

void DviOdp::InvocationReadString(const TChar* aName, Brhz& aString)
{
    Brn buf = Arg(aName);
    Bwn bufW(buf.Ptr(), buf.Bytes(), buf.Bytes());
    Json::Unescape(bufW);
    aString.Set(bufW);
}

TInt DviOdp::InvocationReadInt(const TChar* aName)
{
    Brn buf = Arg(aName);
    return Ascii::Int(buf);
}

TUint DviOdp::InvocationReadUint(const TChar* aName)
{
    Brn buf = Arg(aName);
    return Ascii::Uint(buf);
}

void DviOdp::InvocationReadBinary(const TChar* aName, Brh& aData)
{
    Brn buf = Arg(aName);
    Bwn bufW(buf.Ptr(), buf.Bytes(), buf.Bytes());
    Converter::FromBase64(bufW);
    aData.Set(bufW);
}

void DviOdp::InvocationReadEnd()
{
}

void DviOdp::InvocationReportError(TUint aCode, const Brx& aDescription)
{
    if (iResponseStarted) {
        THROW(InvocationError);
    }
    iWriter = &iSession.WriteLock();
    iResponseStarted = true;
    WriterJsonObject writer(*iWriter);
    writer.WriteString(Odp::kKeyType, Odp::kTypeActionResponse);
    auto writerErr = writer.CreateObject(Odp::kKeyError);
    writerErr.WriteInt(Odp::kKeyCode, (TInt)aCode);
    writerErr.WriteString(Odp::kKeyDescription, aDescription);
    writerErr.WriteEnd();
    if (iCorrelationId.Bytes() > 0) {
        writer.WriteString(Odp::kKeyCorrelationId, iCorrelationId);
    }
    auto writerArgs = writer.CreateObject(Odp::kKeyArguments);
    writerArgs.WriteEnd();
    writer.WriteEnd();

    iResponseEnded = true;
    iWriter->WriteFlush();
    THROW(InvocationError);
}

void DviOdp::InvocationWriteStart()
{
    iWriter = &iSession.WriteLock();
    iResponseStarted = true;
    iWriterResponse.Set(*iWriter);
    iWriterResponse.WriteString(Odp::kKeyType, Odp::kTypeActionResponse);
    auto writerErr = iWriterResponse.CreateObject(Odp::kKeyError);
    writerErr.WriteEnd();
    if (iCorrelationId.Bytes() > 0) {
        iWriterResponse.WriteString(Odp::kKeyCorrelationId, iCorrelationId);
    }
    iWriterResponseArgs = iWriterResponse.CreateArray(Odp::kKeyArguments);
}

void DviOdp::InvocationWriteBool(const TChar* aName, TBool aValue)
{
    auto writerObj = iWriterResponseArgs.CreateObject();
    AutoWriterJson _(writerObj);
    Brn argName(aName);
    writerObj.WriteString(Odp::kKeyName, argName);
    writerObj.WriteString(Odp::kKeyValue, aValue ? WriterJson::kBoolTrue : WriterJson::kBoolFalse);
}

void DviOdp::InvocationWriteInt(const TChar* aName, TInt aValue)
{
    Bws<Ascii::kMaxIntStringBytes> valBuf;
    Ascii::AppendDec(valBuf, aValue);
    auto writerObj = iWriterResponseArgs.CreateObject();
    AutoWriterJson _(writerObj);
    Brn argName(aName);
    writerObj.WriteString(Odp::kKeyName, argName);
    writerObj.WriteString(Odp::kKeyValue, valBuf);
}

void DviOdp::InvocationWriteUint(const TChar* aName, TUint aValue)
{
    Bws<Ascii::kMaxUintStringBytes> valBuf;
    Ascii::AppendDec(valBuf, aValue);
    auto writerObj = iWriterResponseArgs.CreateObject();
    AutoWriterJson _(writerObj);
    Brn argName(aName);
    writerObj.WriteString(Odp::kKeyName, argName);
    writerObj.WriteString(Odp::kKeyValue, valBuf);
}

void DviOdp::InvocationWriteBinaryStart(const TChar* aName)
{
    iWriterStringStreamedObj = iWriterResponseArgs.CreateObject();
    Brn argName(aName);
    iWriterStringStreamedObj.WriteString(Odp::kKeyName, argName);
    iWriterStringStreamed = iWriterStringStreamedObj.CreateStringStreamed(Odp::kKeyValue);
}

void DviOdp::InvocationWriteBinary(TByte aValue)
{
    Brn buf(&aValue, 1);
    InvocationWriteBinary(buf);
}

void DviOdp::InvocationWriteBinary(const Brx& aValue)
{
    Converter::ToBase64(iWriterStringStreamed, aValue);
}

void DviOdp::InvocationWriteBinaryEnd(const TChar* /*aName*/)
{
    iWriterStringStreamed.WriteEnd();
    iWriterStringStreamedObj.WriteEnd();
}

void DviOdp::InvocationWriteStringStart(const TChar* aName)
{
    iWriterStringStreamedObj = iWriterResponseArgs.CreateObject();
    Brn argName(aName);
    iWriterStringStreamedObj.WriteString(Odp::kKeyName, argName);
    iWriterStringStreamed = iWriterStringStreamedObj.CreateStringStreamed(Odp::kKeyValue);
}

void DviOdp::InvocationWriteString(TByte aValue)
{
    Brn buf(&aValue, 1);
    InvocationWriteString(buf);
}

void DviOdp::InvocationWriteString(const Brx& aValue)
{
    iWriterStringStreamed.WriteEscaped(aValue);
}

void DviOdp::InvocationWriteStringEnd(const TChar* /*aName*/)
{
    iWriterStringStreamed.WriteEnd();
    iWriterStringStreamedObj.WriteEnd();
}

void DviOdp::InvocationWriteEnd()
{
    iWriterResponseArgs.WriteEnd();
    iWriterResponse.WriteEnd();
    iResponseEnded = true;
    iWriter->WriteFlush();
}


// AutoOdpSession

AutoOdpSession::AutoOdpSession(IOdpSession& aSession)
    : iSession(aSession)
{
}

AutoOdpSession::~AutoOdpSession()
{
    iSession.WriteUnlock();
}
