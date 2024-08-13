#include <OpenHome/Net/Odp/CpiOdp.h>
#include <OpenHome/Net/Odp/Odp.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Json.h>
#include <OpenHome/Net/Private/Service.h>
#include <OpenHome/Net/Private/CpiService.h>
#include <OpenHome/Net/Private/CpiSubscription.h>
#include <OpenHome/Net/Private/Error.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Debug-ohMediaPlayer.h>

#include <atomic>
#include <vector>

using namespace OpenHome;
using namespace OpenHome::Net;

// CpiOdpResponseHandler

CpiOdpResponseHandler::CpiOdpResponseHandler(ICpiOdpDevice& aDevice)
    : iDevice(aDevice)
    , iSem("OdpA", 0)
    , iResponsePending(false)
{
}

void CpiOdpResponseHandler::WriteCorrelationId(WriterJsonObject& aWriterRequest)
{
    const TUint id = iDevice.RegisterResponseHandler(*this);
    iResponsePending = true;
    Bws<Ascii::kMaxUintStringBytes> idBuf;
    Ascii::AppendDec(idBuf, id);
    aWriterRequest.WriteString(Odp::kKeyCorrelationId, idBuf);
}

void CpiOdpResponseHandler::WaitForResponse()
{
    if (iResponsePending) {
        iSem.Wait();
        iResponsePending = false;
    }
}

void CpiOdpResponseHandler::HandleOdpResponse(const JsonParser& aJsonParser)
{
    AutoSemaphoreSignal _(iSem);
    DoHandleResponse(aJsonParser);
}

void CpiOdpResponseHandler::HandleError()
{
    iSem.Signal();
}


// CpiOdpInvocable

CpiOdpInvocable::CpiOdpInvocable(ICpiOdpDevice& aDevice)
    : CpiOdpResponseHandler(aDevice)
    , iInvocation(nullptr)
{
}

void CpiOdpInvocable::InvokeAction(Invocation& aInvocation)
{
    try {
        iWriter = &iDevice.WriteLock();
        {
            AutoOdpDevice _(iDevice);
            iInvocation = &aInvocation;
            WriterJsonObject writerAction(*iWriter);
            writerAction.WriteString(Odp::kKeyType, Odp::kTypeAction);
            writerAction.WriteString(Odp::kKeyId, iDevice.Udn());
            writerAction.WriteString(Odp::kKeyDevice, iDevice.Alias());
            CpiOdpWriterService::Write(writerAction, aInvocation.ServiceType());
            writerAction.WriteString(Odp::kKeyAction, aInvocation.Action().Name());
            auto args = aInvocation.InputArguments();
            if (args.size() > 0) {
                auto writerArgs = writerAction.CreateArray(Odp::kKeyArguments);
                CpiOdpWriterArgs writerArgValues(writerArgs);
                for (auto it = args.begin(); it != args.end(); ++it) {
                    writerArgValues.Process(**it);
                }
                writerArgs.WriteEnd();
            }
            WriteCorrelationId(writerAction);
            writerAction.WriteEnd();
            iDevice.WriteEnd(*iWriter);
        }

        WaitForResponse();
        iInvocation = nullptr;
    }
    catch (...) {
        WaitForResponse();
        iInvocation = nullptr;
        throw;
    }
}

void CpiOdpInvocable::DoHandleResponse(const JsonParser& aParser)
{
    ASSERT_VA(iInvocation != nullptr, "CpiOdpInvocable::DoHandleResponse this: %p, iDevice: %p\n", this, &iDevice);

    if (!aParser.IsNull(Odp::kKeyError)) {
        JsonParser error;
        error.Parse(aParser.String(Odp::kKeyError));
        const TUint code = (TUint)error.Num(Odp::kKeyCode);
        Brn desc = error.String(Odp::kKeyDescription);
        iInvocation->SetError(Error::eUpnp, code, desc); // FIXME - Error::ELevel doesn't have a level for non-UPnP protocol
        return;
    }
    const auto& outArgs = iInvocation->OutputArguments();
    if (aParser.IsNull(Odp::kKeyArguments)) {
        if (outArgs.size() > 0) {
            THROW(OdpError);
        }
    }
    else {
        auto argsParser = JsonParserArray::Create(aParser.String(Odp::kKeyArguments));
        CpiOdpOutputProcessor outputProcessor;
        JsonParser argParser;
        try {
            for (;;) {
                argParser.Reset();
                argParser.Parse(argsParser.NextObject());
                Brn argName = argParser.String(Odp::kKeyName);
                Brn argVal = argParser.String(Odp::kKeyValue);
                for (auto it=outArgs.begin(); it!=outArgs.end(); ++it) {
                    if ((*it)->Parameter().Name() == argName) {
                        (*it)->ProcessOutput(outputProcessor, argVal);
                        break;
                    }
                }
            }
        }
        catch (JsonArrayEnumerationComplete&) {}
    }
}


// CpiOdpWriterArgs

CpiOdpWriterArgs::CpiOdpWriterArgs(WriterJsonArray& aWriter)
    : iWriter(aWriter)
{
}

void CpiOdpWriterArgs::Process(Argument& aArg)
{
    iArg = &aArg;
    iArg->ProcessInput(*this);
}

void CpiOdpWriterArgs::ProcessString(const Brx& aVal)
{
    WriteString(aVal);
}

void CpiOdpWriterArgs::ProcessInt(TInt aVal)
{
    Bws<Ascii::kMaxIntStringBytes> buf;
    (void)Ascii::AppendDec(buf, aVal);
    WriteString(buf);
}

void CpiOdpWriterArgs::ProcessUint(TUint aVal)
{
    Bws<Ascii::kMaxUintStringBytes> buf;
    (void)Ascii::AppendDec(buf, aVal);
    WriteString(buf);
}

void CpiOdpWriterArgs::ProcessBool(TBool aVal)
{
    WriteString(aVal? WriterJson::kBoolTrue : WriterJson::kBoolFalse);
}

void CpiOdpWriterArgs::ProcessBinary(const Brx& aVal)
{
    auto writerObj = iWriter.CreateObject();
    AutoWriterJson _(writerObj);
    writerObj.WriteString(Odp::kKeyName, iArg->Parameter().Name());
    writerObj.WriteBinary(Odp::kKeyValue, aVal);
}

void CpiOdpWriterArgs::WriteString(const Brx& aVal)
{
    auto writerObj = iWriter.CreateObject();
    AutoWriterJson _(writerObj);
    writerObj.WriteString(Odp::kKeyName, iArg->Parameter().Name());
    writerObj.WriteString(Odp::kKeyValue, aVal);
}


// CpiOdpOutputProcessor

void CpiOdpOutputProcessor::ProcessString(const Brx& aBuffer, Brhz& aVal)
{
    // Constructs Bwn over the Brhz in order to save copying twice
    aVal.Set(aBuffer);
    Bwn writeable(aVal.Ptr(), aVal.Bytes(), aVal.Bytes());
    Json::Unescape(writeable);
    aVal.Shrink(writeable.Bytes());
}

void CpiOdpOutputProcessor::ProcessInt(const Brx& aBuffer, TInt& aVal)
{
    aVal = Ascii::Int(aBuffer);
}

void CpiOdpOutputProcessor::ProcessUint(const Brx& aBuffer, TUint& aVal)
{
    aVal = Ascii::Uint(aBuffer);
}

void CpiOdpOutputProcessor::ProcessBool(const Brx& aBuffer, TBool& aVal)
{
    if (aBuffer == WriterJson::kBoolTrue) {
        aVal = true;
    }
    else if (aBuffer == WriterJson::kBoolFalse) {
        aVal = false;
    }
    else {
        THROW(JsonInvalid);
    }
}

void CpiOdpOutputProcessor::ProcessBinary(const Brx& aBuffer, Brh& aVal)
{
    Bwh copy(aBuffer);
    Converter::FromBase64(copy);
    copy.TransferTo(aVal);
}


// CpiOdpSubscriber

CpiOdpSubscriber::CpiOdpSubscriber(ICpiOdpDevice& aDevice)
    : CpiOdpResponseHandler(aDevice)
    , iSubscription(nullptr)
{
}

void CpiOdpSubscriber::Subscribe(CpiSubscription& aSubscription)
{
    try {
        iSubscription = &aSubscription;
        auto& writer = iDevice.WriteLock();
        {
            AutoOdpDevice _(iDevice);
            WriterJsonObject writerSubs(writer);
            writerSubs.WriteString(Odp::kKeyType, Odp::kTypeSubscribe);
            writerSubs.WriteString(Odp::kKeyId, iDevice.Udn());
            writerSubs.WriteString(Odp::kKeyDevice, iDevice.Alias());
            CpiOdpWriterService::Write(writerSubs, aSubscription.ServiceType());
            WriteCorrelationId(writerSubs);
            writerSubs.WriteEnd();
            iDevice.WriteEnd(writer);
        }

        WaitForResponse();
        iSubscription = nullptr;
    }
    catch (...) {
        WaitForResponse();
        iSubscription = nullptr;
        throw;
    }
}

void CpiOdpSubscriber::DoHandleResponse(const JsonParser& aParser)
{
    ASSERT_VA(iSubscription != nullptr, "CpiOdpSubscriber::DoHandleResponse this: %p, iDevice: %p\n", this, &iDevice);

    if (!aParser.IsNull(Odp::kKeyError)) {
        THROW(OdpError);
    }
    Brh sid(aParser.String(Odp::kKeySid));
    iSubscription->SetSid(sid);
}


// CpiOdpUnsubscriber

CpiOdpUnsubscriber::CpiOdpUnsubscriber(ICpiOdpDevice& aDevice)
    : CpiOdpResponseHandler(aDevice)
{
}

void CpiOdpUnsubscriber::Unsubscribe(const Brx& aSid)
{
    try {
        auto& writer = iDevice.WriteLock();
        {
            AutoOdpDevice _(iDevice);
            WriterJsonObject writerUnsubs(writer);
            writerUnsubs.WriteString(Odp::kKeyType, Odp::kTypeUnsubscribe);
            writerUnsubs.WriteString(Odp::kKeySid, aSid);
            WriteCorrelationId(writerUnsubs);
            writerUnsubs.WriteEnd();
            iDevice.WriteEnd(writer);
        }

        WaitForResponse();
    }
    catch (...) {
        WaitForResponse();
        throw;
    }
}

void CpiOdpUnsubscriber::DoHandleResponse(const JsonParser& /*aParser*/)
{
}


// CpiOdpWriterService

void CpiOdpWriterService::Write(WriterJsonObject& aWriter, const ServiceType& aServiceType)
{
    auto writerService = aWriter.CreateObject(Odp::kKeyService);
    Bwh domain(aServiceType.Domain());
    Ssdp::UpnpDomainToCanonical(domain, domain);
    writerService.WriteString(Odp::kKeyDomain, domain);
    writerService.WriteString(Odp::kKeyName, aServiceType.Name());
    writerService.WriteInt(Odp::kKeyVersion, aServiceType.Version());
    writerService.WriteEnd();
}


// AutoOdpDevice

AutoOdpDevice::AutoOdpDevice(ICpiOdpDevice& aDevice)
    : iDevice(aDevice)
{
}

AutoOdpDevice::~AutoOdpDevice()
{
    iDevice.WriteUnlock();
}


// CpiOdpInvocableQueueItem

CpiOdpInvocableQueueItem::CpiOdpInvocableQueueItem(ICpiOdpDevice& aDevice, Fifo<IInvocable*>& aQueue)
    : iQueue(aQueue)
    , iInvocable(aDevice)
{
}

void CpiOdpInvocableQueueItem::InvokeAction(Invocation& aInvocation)
{
    // CpiOdpInvocable::InvokeAction is synchronous.
    try {
        iInvocable.InvokeAction(aInvocation);
        iQueue.Write(this);
    }
    catch (...) {
        iQueue.Write(this);
        throw;
    }
}
