#include <OpenHome/Web/ConfigUi/ConfigUi.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Web/ResourceHandler.h>
#include <OpenHome/Web/WebAppFramework.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/NetworkAdapterList.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Json.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Av/RebootHandler.h>
#include <OpenHome/Private/Debug.h>

#include <limits>
#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;
using namespace OpenHome::Media;
using namespace OpenHome::Web;


// WritableJsonEmpty

void WritableJsonEmpty::Write(IWriter& aWriter) const
{
    aWriter.Write(Brn("{}"));
}


// JsonWriterBool

JsonWriterBool::JsonWriterBool(IWriter& aWriter)
    : iWriter(aWriter)
{
}

void JsonWriterBool::Write(TBool aValue) const
{
    if (aValue) {
        iWriter.Write(Brn("true"));
    }
    else {
        iWriter.Write(Brn("false"));
    }
}


// JsonWriterString

JsonWriterString::JsonWriterString(IWriter& aWriter)
    : iWriter(aWriter)
{
}

void JsonWriterString::Write(const Brx& aValue) const
{
    iWriter.Write('\"');
    Json::Escape(iWriter, aValue);
    iWriter.Write('\"');
}


// JsonWriterReboot

JsonWriterReboot::JsonWriterReboot(TBool aRebootRequired)
    : iRebootRequired(aRebootRequired)
{
}

void JsonWriterReboot::Write(IWriter& aWriter) const
{
    aWriter.Write(Brn("\"reboot-required\":"));

    JsonWriterBool writerBool(aWriter);
    writerBool.Write(iRebootRequired);
}


// WritableJsonInfo

WritableJsonInfo::WritableJsonInfo(TBool aRebootRequired)
    : iRebootRequired(aRebootRequired)
{
}

void WritableJsonInfo::Write(IWriter& aWriter) const
{
    aWriter.Write(Brn("{"));

    JsonWriterReboot writerReboot(iRebootRequired);
    writerReboot.Write(aWriter);

    aWriter.Write(Brn("}"));
}


// ConfigMessageBase

ConfigMessageBase::ConfigMessageBase(AllocatorBase& aAllocator)
    : Allocated(aAllocator)
{
}

void ConfigMessageBase::Clear()
{
}

void ConfigMessageBase::Destroy()
{
    RemoveRef();
}


// ConfigChoiceMappingWriterJson

ConfigChoiceMappingWriterJson::ConfigChoiceMappingWriterJson()
    : iStarted(false)
{
}

void ConfigChoiceMappingWriterJson::Write(IWriter& aWriter, TUint aChoice, const Brx& aMapping)
{
    if (!iStarted) {
        aWriter.Write(Brn("\"options\":["));
    }
    else {
        aWriter.Write(',');
    }

    aWriter.Write(Brn("{"));
    aWriter.Write(Brn("\"id\": "));
    Ascii::StreamWriteUint(aWriter, aChoice);
    aWriter.Write(Brn(",\"value\": \""));
    Json::Escape(aWriter, aMapping);
    aWriter.Write(Brn("\"}"));
    iStarted = true;
}

void ConfigChoiceMappingWriterJson::WriteComplete(IWriter& aWriter)
{
    if (!iStarted) {
        aWriter.Write(Brn("\"options\":["));
        iStarted = true;
    }
    aWriter.Write(Brn("]"));
}


// ConfigChoiceMapperResourceFile

ConfigChoiceMapperResourceFile::ConfigChoiceMapperResourceFile(const Brx& aKey, const std::vector<TUint>& aChoices, IWriter& aWriter, IConfigChoiceMappingWriter& aMappingWriter)
    : iKey(aKey)
    , iChoices(aChoices)
    , iWriter(aWriter)
    , iMappingWriter(aMappingWriter)
    , iChoicesIndex(0)
    , iFoundKey(false)
{
}

TBool ConfigChoiceMapperResourceFile::ProcessLine(const Brx& aLine)
{
    if (!iFoundKey) {
        iFoundKey = Ascii::Contains(aLine, iKey);
        return true;
    }
    Parser p(aLine);
    Brn idBuf = p.Next();
    Brn valueBuf = p.NextToEnd();
    if (valueBuf.Bytes() == 0) {
        // Key has been found and now processing options, but have unexpectedly reached end of available mappings for the given key, by reading onto the empty line after the list of mappings.
        // Report that there is nothing more to be processed. Any un-mapped options will not be made available in the Web UI.
        iMappingWriter.WriteComplete(iWriter);
        return false;
    }

    TUint id = 0;
    try {
        id = Ascii::Uint(idBuf);
    }
    catch (AsciiError&) {
        // Unable to parse this value. Skip over it.
        return true;
    }

    if (id != iChoices[iChoicesIndex]) {
        // This mapped value is not in the choices list. Skip over it.
        return true;
    }

    try {
        iMappingWriter.Write(iWriter, iChoices[iChoicesIndex], valueBuf);
        if (++iChoicesIndex == iChoices.size()) {
            iMappingWriter.WriteComplete(iWriter);
            return false;
        }
    }
    catch (WriterError&) {
        LOG(kHttp, "ConfigChoiceMapperResourceFile::ProcessLine WriterError");
        return false;
    }
    return true;
}


// ConfigValBuf

ConfigValBuf::ConfigValBuf(Media::AllocatorBase& aAllocator, TUint aBytes)
    : Allocated(aAllocator)
    , iNext(nullptr)
    , iBuf(aBytes)
{
}

TUint ConfigValBuf::MaxBytes() const
{
    return iBuf.MaxBytes();
}

void ConfigValBuf::Write(IWriter& aWriter) const
{
    // Do this iteratively, rather than recursively.
    auto* next = this;
    while (next != nullptr) {
        aWriter.Write(next->iBuf);
        next = next->iNext;
    }
}

void ConfigValBuf::Clear()
{
    // If iNext->RemoveRef() results in iNext's reference count going to 0 it will call into iNext->Clear(), with potentially further recursion if there are more chained buffers.
    if (iNext != nullptr) {
        iNext->RemoveRef();
    }
}

void ConfigValBuf::Initialise(const Brx& aBuf)
{
    iNext = nullptr;
    iBuf.Replace(aBuf);
}

void ConfigValBuf::Append(ConfigValBuf* aBuf)
{
    ASSERT(iNext == nullptr); // Ensure append is always happening at tail.
    // Do this iteratively, rather than recursively.
    auto* next = this;
    while (next->iNext != nullptr) {
        next = next->iNext;
    }
    next->iNext = aBuf;
}


// ConfigMessage

ConfigMessage::ConfigMessage(AllocatorBase& aAllocator)
    : ConfigMessageBase(aAllocator)
    , iUiVal(nullptr)
    , iUpdatedVal(nullptr)
    , iLanguageResourceManager(nullptr)
    , iLanguageList(nullptr)
{
}

void ConfigMessage::Set(IConfigUiVal& aUiVal, ConfigValBuf* aUpdatedVal, ILanguageResourceManager& aLanguageResourceManager, std::vector<Bws<10>>& aLanguageList)
{
    ASSERT(iUiVal == nullptr);
    ASSERT(aUpdatedVal != nullptr);
    iUiVal = &aUiVal;
    iUpdatedVal = aUpdatedVal;
    iLanguageResourceManager = &aLanguageResourceManager;
    iLanguageList = &aLanguageList;
}

void ConfigMessage::Clear()
{
    ConfigMessageBase::Clear();
    ASSERT(iUiVal != nullptr);
    iUiVal = nullptr;
    iUpdatedVal->RemoveRef();
    iLanguageResourceManager = nullptr;
    iLanguageList = nullptr;
}

void ConfigMessage::Send(IWriter& aWriter)
{
    ASSERT(iUiVal != nullptr);
    iUiVal->WriteJson(aWriter, *this, *iLanguageResourceManager, *iLanguageList);
}

void ConfigMessage::WriteValueJson(IWriter& aWriter)
{
    ASSERT(iUiVal != nullptr);
    ASSERT(iUpdatedVal != nullptr);
    iUpdatedVal->Write(aWriter);
}


// AllocatorConfigValBuf

AllocatorConfigValBuf::AllocatorConfigValBuf(const TChar* aName, TUint aNumCells, IInfoAggregator& aInfoAggregator, TUint aBufBytes)
    : AllocatorBase(aName, aNumCells, sizeof(ConfigValBuf), aInfoAggregator)
{
    for (TUint i=0; i<aNumCells; i++) {
        iFree.Write(new ConfigValBuf(*this, aBufBytes));
    }
}

ConfigValBuf* AllocatorConfigValBuf::Allocate()
{
    return static_cast<ConfigValBuf*>(DoAllocate());
}


// ConfigMessageAllocator

ConfigMessageAllocator::ConfigMessageAllocator(IInfoAggregator& aInfoAggregator, TUint aMsgCount, TUint aMsgBufCount, TUint aMsgBufBytes, ILanguageResourceManager& aLanguageResourceManager)
    : iAllocatorMsg("ConfigMessage", aMsgCount, aInfoAggregator)
    , iAllocatorBuf("ConfigBuf", aMsgBufCount, aInfoAggregator, aMsgBufBytes)
    , iLanguageResourceManager(aLanguageResourceManager)
{
    ASSERT(aMsgBufCount >= aMsgCount); // Need at least aMsgCount buffers, to satisfy each message.
}

ITabMessage* ConfigMessageAllocator::AllocateMessage(IConfigUiVal& aUiVal, const Brx& aUpdatedVal, std::vector<Bws<10>>& aLanguageList)
{
    auto* msgBuf = AllocateBuf(aUpdatedVal);
    ConfigMessage* msg = iAllocatorMsg.Allocate();
    msg->Set(aUiVal, msgBuf, iLanguageResourceManager, aLanguageList);
    return msg;
}

ConfigValBuf* ConfigMessageAllocator::AllocateBuf(const Brx& aBuf)
{
    ASSERT(aBuf.Bytes() != 0);
    ConfigValBuf* head = nullptr;
    ConfigValBuf* tail = nullptr;
    TUint offset = 0;
    while (offset < aBuf.Bytes()) {
        auto* next = iAllocatorBuf.Allocate();
        const auto bytesRemaining = aBuf.Bytes() - offset;
        const auto msgBytes = bytesRemaining >= next->MaxBytes() ? next->MaxBytes() : bytesRemaining;
        const Brn buf(aBuf.Ptr() + offset, msgBytes);
        offset += msgBytes;
        next->Initialise(buf);

        if (tail == nullptr) {
            head = next;
            tail = head;
        }
        else {
            tail->Append(next);
            tail = next;
        }
    }

    return head;
}


// JsonStringParser

Brn JsonStringParser::ParseString(const Brx& aBuffer, Brn& aRemaining)
{
    TUint offset = 0;

    // Skip any whitespace.
    for (TUint i=0; i<aBuffer.Bytes(); i++) {
        if (!Ascii::IsWhitespace(aBuffer[i])) {
            offset = i;
            break;
        }
    }

    if (aBuffer[offset] != '"') {
        THROW(JsonStringError);
    }
    offset++;   // Move past opening '"'.

    for (TUint i=offset; i<aBuffer.Bytes(); i++) {
        if (aBuffer[i] == '"') {
            if (aBuffer[i-1] != '\\') {
                const TUint bytes = i-offset;
                i++;
                ASSERT(aBuffer.Bytes() > i);
                if (aBuffer.Bytes()-i == 0) {
                    aRemaining.Set(Brn::Empty());
                }
                else {
                    aRemaining.Set(aBuffer.Ptr()+i, aBuffer.Bytes()-i);
                }

                if (bytes == 0) {
                    return Brn::Empty();
                }
                else {
                    return Brn(aBuffer.Ptr()+offset, bytes);
                }
            }
        }
    }

    THROW(JsonStringError);
}


// ConfigTabReceiver

ConfigTabReceiver::ConfigTabReceiver()
{
}

void ConfigTabReceiver::Receive(const Brx& aMessage)
{
    // FIXME - what if aMessage is malformed? - call some form of error handler?
    // FIXME - this should maybe also take an IWriter to allow writing out of a response (which could be none if successful, and an error description if unsuccessful/malformed request).

    // Parse JSON response.
    Bws<128> keyBuf;
    Bws<1024> valueBuf;
    Brn remaining(aMessage);

    LOG(kHttp, "ConfigTabReceiver::Receive:\n%.*s\n", PBUF(aMessage));

    try {
        Parser p(aMessage);
        (void)p.Next('{');
        Brn request = JsonStringParser::ParseString(p.Remaining(), remaining);

        if (request != Brn("request")) {
            LOG(kHttp, "ConfigTabReceiver::Receive Unknown response.\n");
            return;
        }

        p.Set(remaining);
        (void)p.Next('{');
        (void)JsonStringParser::ParseString(p.Remaining(), remaining);  // "type"
        p.Set(remaining);
        (void)p.Next(':');
        Brn type = JsonStringParser::ParseString(p.Remaining(), remaining);

        if (type == Brn("update")) {
            p.Set(remaining);
            (void)p.Next(',');
            (void)JsonStringParser::ParseString(p.Remaining(), remaining);  // "key"

            p.Set(remaining);
            (void)p.Next(':');
            Brn key = JsonStringParser::ParseString(p.Remaining(), remaining);

            p.Set(remaining);
            (void)p.Next(',');
            (void)JsonStringParser::ParseString(p.Remaining(), remaining);  // "value"

            p.Set(remaining);
            (void)p.Next(':');
            Brn value = JsonStringParser::ParseString(p.Remaining(), remaining);

            keyBuf.Replace(key);
            Json::Unescape(keyBuf);
            valueBuf.Replace(value);
            Json::Unescape(valueBuf);
            Receive(keyBuf, valueBuf);
        }
        else if (type == Brn("reboot")) {
            // FIXME - passing on reboot call here means that the DS may reboot before this call returns, so the WebAppFramework may not get chance to send a response to the UI (but does that matter, as the device is going to abruptly disappear at some point in the near future?).
            Reboot();
        }
    }
    catch (JsonStringError&) {
        LOG(kHttp, "ConfigTabReceiver::Receive caught JsonStringError: %.*s\n", PBUF(aMessage));
    }
}


// ConfigTab

const TUint ConfigTab::kInvalidSubscription = OpenHome::Configuration::IConfigManager::kSubscriptionIdInvalid;

ConfigTab::ConfigTab(TUint aId, IConfigMessageAllocator& aMessageAllocator, IConfigManager& aConfigManager, IRebootHandler& aRebootHandler)
    : iId(aId)
    , iConfigManager(aConfigManager)
    , iMsgAllocator(aMessageAllocator)
    , iRebootHandler(aRebootHandler)
    , iHandler(nullptr)
    , iStarted(false)
{
}

ConfigTab::~ConfigTab()
{
    if (iHandler != nullptr) {
        Destroy();
    }
}

void ConfigTab::AddValue(IConfigUiVal& aValue)
{
    ASSERT(!iStarted);
    iConfigUiVals.push_back(SubscriptionPair(&aValue, kInvalidSubscription));
}

// FIXME - remove?
void ConfigTab::Start()
{
    ASSERT(!iStarted);
    ASSERT(iConfigUiVals.size() > 0);
    iStarted = true;
}

TBool ConfigTab::Allocated() const
{
    TBool allocated = iHandler != nullptr;
    return allocated;
}

void ConfigTab::SetHandler(ITabHandler& aHandler, const std::vector<Bws<10>>& aLanguageList)
{
    LOG(kHttp, "ConfigTab::SetHandler iId: %u\n", iId);
    ASSERT(iHandler == nullptr);
    iLanguageList.clear();
    for (auto it=aLanguageList.begin(); it!=aLanguageList.end(); ++it) {
        iLanguageList.push_back(*it);
    }
    iHandler = &aHandler;
    for (auto& val : iConfigUiVals) {
        val.second = val.first->AddObserver(*this);
    }
}

void ConfigTab::Receive(const Brx& aKey, const Brx& aValue)
{
    // FIXME - backdoor trickery going on here. Accessing iConfigManager directly, when should really be updating value via IConfigUiVal object.

    LOG(kHttp, "ConfigTab::Receive\n\taKey: %.*s\n\taValue: %.*s\n", PBUF(aKey), PBUF(aValue));

    // TabManager in HttpFramework should handle any race between Destroy() and
    // Receive() being called, as it will defer destruction of a tab until all
    // references are removed.
    if (iConfigManager.Has(aKey)) {
        ISerialisable& ser = iConfigManager.Get(aKey);
        try {
            ser.Deserialise(aValue);
        }
        // No exceptions should be thrown because all input verification should
        // be handled by browser side.
        catch (ConfigNotANumber&) {
            LOG(kHttp, "ConfigTab::Receive caught ConfigNotANumber \n\taKey: %.*s\n\taValue: %.*s\n", PBUF(aKey), PBUF(aValue));
            //ASSERTS();
        }
        catch (ConfigValueOutOfRange&) {
            LOG(kHttp, "ConfigTab::Receive caught ConfigValueOutOfRange \n\taKey: %.*s\n\taValue: %.*s\n", PBUF(aKey), PBUF(aValue));
            //ASSERTS();
        }
        catch (ConfigValueTooShort&) {
            LOG(kHttp, "ConfigTab::Receive caught ConfigValueTooShort \n\taKey: %.*s\n\taValue: %.*s\n", PBUF(aKey), PBUF(aValue));
            //ASSERTS();
        }
        catch (ConfigValueTooLong&) {
            LOG(kHttp, "ConfigTab::Receive caught ConfigValueTooLong \n\taKey: %.*s\n\taValue: %.*s\n", PBUF(aKey), PBUF(aValue));
            //ASSERTS();
        }
        catch (ConfigInvalidSelection&) {
            LOG(kHttp, "ConfigTab::Receive caught ConfigInvalidSelection \n\taKey: %.*s\n\taValue: %.*s\n", PBUF(aKey), PBUF(aValue));
            //ASSERTS();
        }
    }
    else {
        LOG(kHttp, "ConfigTab::Receive unrecognised input\n\taKey: %.*s\n\taValue: %.*s\n", PBUF(aKey), PBUF(aValue));
        //ASSERTS(); // Browser code tried to pass in an invalid ConfigVal. Developer error.
    }
}

void ConfigTab::Reboot()
{
    iRebootHandler.Reboot(Brn("ConfigTab::Reboot"));
}

void ConfigTab::Destroy()
{
    LOG(kHttp, "ConfigTab::Destroy iId: %u\n", iId);
    ASSERT(iHandler != nullptr);
    iHandler = nullptr;

    for (auto val : iConfigUiVals) {
        ASSERT(val.second != kInvalidSubscription); // Check value wasn't added after all other values were subscribed to (in SetHandler() call).
        val.first->RemoveObserver(val.second);
    }
}

void ConfigTab::ValueChanged(IConfigUiVal& aUiVal, const Brx& aUpdatedVal)
{
    ITabMessage* msg = iMsgAllocator.AllocateMessage(aUiVal, aUpdatedVal, iLanguageList);
    ASSERT(iHandler != nullptr);
    iHandler->Send(*msg);
}


// ConfigUiValBase

ConfigUiValBase::ConfigUiValBase(const IWritable& aAdditionalJson)
    : iAdditionalJson(aAdditionalJson)
    , iNextObserverId(kObserverIdInvalid+1)
    , iLockObservers("CUVL")
{
}

void ConfigUiValBase::WriteHidden(IWriter& aWriter)
{
    aWriter.Write(Brn("false"));
}

void ConfigUiValBase::WriteAdditional(IWriter& aWriter)
{
    iAdditionalJson.Write(aWriter);
}

void ConfigUiValBase::WriteJson(IWriter& aWriter, IConfigUiUpdateWriter& aValWriter, ILanguageResourceManager& aLanguageResourceManager, std::vector<Bws<10>>& aLanguageList)
{
    aWriter.Write(Brn("{"));

    aWriter.Write(Brn("\"key\":"));
    WriteKey(aWriter);
    aWriter.Write(Brn(","));

    aWriter.Write(Brn("\"value\":"));
    aValWriter.WriteValueJson(aWriter);
    aWriter.Write(Brn(","));

    aWriter.Write(Brn("\"type\":"));
    WriteType(aWriter);
    aWriter.Write(Brn(","));

    aWriter.Write(Brn("\"meta\":"));
    WriteMeta(aWriter, aLanguageResourceManager, aLanguageList);
    aWriter.Write(Brn(","));

    aWriter.Write(Brn("\"info\":"));
    WriteAdditional(aWriter);
    aWriter.Write(Brn(","));

    aWriter.Write(Brn("\"hidden\":"));
    WriteHidden(aWriter);

    aWriter.Write(Brn("}"));
}

void ConfigUiValBase::ValueChanged(const Brx& aValue)
{
    AutoMutex a(iLockObservers);
    for (auto it : iObservers) {
        it.second->ValueChanged(*this, aValue);
    }
}

TUint ConfigUiValBase::AddObserver(IConfigUiValObserver& aObserver)
{   TUint id;
    {
        AutoMutex a(iLockObservers);
        id = iNextObserverId++;
        iObservers.insert(std::pair<TUint, IConfigUiValObserver*>(id, &aObserver));
    } //Automutex in block as ObserverAdded has own lock
    ObserverAdded(aObserver);
    return id;
}

void ConfigUiValBase::RemoveObserver(TUint aObserverId)
{
    AutoMutex a(iLockObservers);
    auto it = iObservers.find(aObserverId);
    ASSERT(it != iObservers.end()); // Must be a valid observer ID.
    iObservers.erase(it);
}


// ConfigUiValRoBase

ConfigUiValRoBase::ConfigUiValRoBase(const Brx& aKey)
    : ConfigUiValBase(iAdditional)
    , iKey(aKey)
{
}

void ConfigUiValRoBase::WriteKey(IWriter& aWriter)
{
    aWriter.Write(Brn("\""));
    Json::Escape(aWriter, iKey);
    aWriter.Write(Brn("\""));
}

void ConfigUiValRoBase::WriteType(IWriter& aWriter)
{
    aWriter.Write(Brn("\"read-only\""));
}

void ConfigUiValRoBase::WriteMeta(IWriter& aWriter, ILanguageResourceManager& /*aLanguageResourceManager*/, std::vector<Bws<10>>& /*aLanguageList*/)
{
    aWriter.Write(Brn("{}"));
}


// ConfigUiValRo

ConfigUiValRo::ConfigUiValRo(const Brx& aKey, Brn aValue)
    : ConfigUiValRoBase(aKey)
    , iValue(aValue)
{
    WriterBuffer writerBuf(iJsonValue);
    writerBuf.Write('\"');
    Json::Escape(writerBuf, iValue);
    writerBuf.Write('\"');
}

void ConfigUiValRo::ObserverAdded(IConfigUiValObserver& aObserver)
{
    aObserver.ValueChanged(*this, iJsonValue);
}


// ConfigUiValRoList

ConfigUiValRoList::ConfigUiValRoList(const Brx& aKey, Brn aValue)
    : ConfigUiValRoBase(aKey)
    , iValue(aValue)
{
}

void ConfigUiValRoList::ObserverAdded(IConfigUiValObserver& aObserver)
{
    aObserver.ValueChanged(*this, iValue);
}

void ConfigUiValRoList::WriteType(IWriter& aWriter)
{
    aWriter.Write(Brn("\"read-only-list\""));
}


// ConfigUiValRoUpdatable

ConfigUiValRoUpdatable::ConfigUiValRoUpdatable(const Brx& aKey, const Brx& aValue)
    : ConfigUiValRoBase(aKey)
    , iValue(aValue)
    , iLock("CURL")
{
    UpdateJsonValLocked();
}

void ConfigUiValRoUpdatable::Update(const Brx& aValue)
{
    Bws<kMaxValueBytes> jsonVal;
    {
        AutoMutex a(iLock);
        iValue.Replace(aValue);
        UpdateJsonValLocked();
        jsonVal.Replace(iJsonValue);
    } //Automutex in block as ValueChanged has its own automutex
    ValueChanged(jsonVal);
}

void ConfigUiValRoUpdatable::ObserverAdded(IConfigUiValObserver& aObserver)
{
    AutoMutex a(iLock);
    aObserver.ValueChanged(*this, iJsonValue);
}

void ConfigUiValRoUpdatable::UpdateJsonValLocked()
{
    iJsonValue.SetBytes(0);
    WriterBuffer writerBuf(iJsonValue);
    writerBuf.Write('\"');
    Json::Escape(writerBuf, iValue);
    writerBuf.Write('\"');
}


// ConfigUiValNum

ConfigUiValNum::ConfigUiValNum(Configuration::ConfigNum& aNum, const IWritable& aAdditionalJson)
    : ConfigUiValBase(aAdditionalJson)
    , iNum(aNum)
    , iListenerId(IConfigManager::kSubscriptionIdInvalid)
    , iVal(0)
    , iLock("CUNL")
{
    iListenerId = iNum.Subscribe(MakeFunctorConfigNum(*this, &ConfigUiValNum::Update));
}

ConfigUiValNum::~ConfigUiValNum()
{
    iNum.Unsubscribe(iListenerId);
}

void ConfigUiValNum::ObserverAdded(IConfigUiValObserver& aObserver)
{
    AutoMutex a(iLock);
    Bws<Ascii::kMaxIntStringBytes> val;
    Ascii::AppendDec(val, iVal);
    aObserver.ValueChanged(*this, val);
}

void ConfigUiValNum::WriteKey(IWriter& aWriter)
{
    aWriter.Write(Brn("\""));
    Json::Escape(aWriter, iNum.Key());
    aWriter.Write(Brn("\""));
}

void ConfigUiValNum::WriteType(IWriter& aWriter)
{
    aWriter.Write(Brn("\"numeric\""));
}

void ConfigUiValNum::WriteMeta(IWriter& aWriter, ILanguageResourceManager& /*aLanguageResourceManager*/, std::vector<Bws<10>>& /*aLanguageList*/)
{
    aWriter.Write('{');
    aWriter.Write(Brn("\"default\":"));
    Ascii::StreamWriteInt(aWriter, iNum.Default());
    aWriter.Write(Brn(","));
    aWriter.Write(Brn("\"min\":"));
    Ascii::StreamWriteInt(aWriter, iNum.Min());
    aWriter.Write(Brn(","));
    aWriter.Write(Brn("\"max\":"));
    Ascii::StreamWriteInt(aWriter, iNum.Max());
    aWriter.Write('}');
}

void ConfigUiValNum::Update(Configuration::ConfigNum::KvpNum& aKvp)
{
    Bws<Ascii::kMaxIntStringBytes> val;
    {
        AutoMutex a(iLock);
        iVal = aKvp.Value();
        Ascii::AppendDec(val, iVal);
    }
    ValueChanged(val);
}


// ConfigUiValChoice

ConfigUiValChoice::ConfigUiValChoice(Configuration::ConfigChoice& aChoice, const IWritable& aAdditionalJson)
    : ConfigUiValBase(aAdditionalJson)
    , iChoice(aChoice)
    , iListenerId(IConfigManager::kSubscriptionIdInvalid)
    , iVal(0)
    , iLock("CUCL")
{
    iListenerId = iChoice.Subscribe(MakeFunctorConfigChoice(*this, &ConfigUiValChoice::Update));
}

ConfigUiValChoice::~ConfigUiValChoice()
{
    iChoice.Unsubscribe(iListenerId);
}

void ConfigUiValChoice::ObserverAdded(IConfigUiValObserver& aObserver)
{
    AutoMutex a(iLock);
    Bws<Ascii::kMaxUintStringBytes> val;
    Ascii::AppendDec(val, iVal);
    aObserver.ValueChanged(*this, val);
}

void ConfigUiValChoice::WriteKey(IWriter& aWriter)
{
    aWriter.Write(Brn("\""));
    Json::Escape(aWriter, iChoice.Key());
    aWriter.Write(Brn("\""));
}

void ConfigUiValChoice::WriteType(IWriter& aWriter)
{
    aWriter.Write(Brn("\"choice\""));
}

void ConfigUiValChoice::WriteMeta(IWriter& aWriter, ILanguageResourceManager& aLanguageResourceManager, std::vector<Bws<10>>& aLanguageList)
{
    aWriter.Write('{');
    aWriter.Write(Brn("\"default\":"));
    Ascii::StreamWriteUint(aWriter, iChoice.Default());
    aWriter.Write(Brn(","));

    if (iChoice.HasInternalMapping()) {
        IConfigChoiceMapper& mapper = iChoice.Mapper();
        ConfigChoiceMappingWriterJson mappingWriter;
        mapper.Write(aWriter, mappingWriter);
    }
    else {
        // Bodge to map certain Source.*.xxxx values to the same set of choices
        static const Brn kSourcePrefix("Source.");
        Bws<128> key(iChoice.Key());
        if (key.BeginsWith(kSourcePrefix)) {
            // Assume source-specific keys have the form Source.SystemName.Suffix
            // For selected suffices, change this to Source.Suffix
            Parser parser(key);
            (void)parser.Next('.');
            (void)parser.Next('.');
            // Local storage for suffix as its value comes from key, which is
            // later modified.
            Bws<32> suffix(parser.Remaining());
            if (   suffix == Brn("Visible")
                || suffix == Brn("UnityGain")
                || suffix == Brn("InputTransformer")
                || suffix == Brn("DigitalFeatures")
                || suffix == Brn("DelayMode")) {
                key.Replace("Source.");
                key.Append(suffix);
            }
        }

        // Read mapping from file.
        static const Brn kConfigOptionsFile("ConfigOptions.txt");
        ConfigChoiceMappingWriterJson mappingWriter;
        ConfigChoiceMapperResourceFile mapper(key, iChoice.Choices(), aWriter, mappingWriter);
        ILanguageResourceReader* resourceHandler = &aLanguageResourceManager.CreateLanguageResourceHandler(kConfigOptionsFile, aLanguageList);
        resourceHandler->Process(key, mapper);
        //resourceHandler->Destroy();
    }
    aWriter.Write('}');
}

void ConfigUiValChoice::Update(Configuration::ConfigChoice::KvpChoice& aKvp)
{
    Bws<Ascii::kMaxIntStringBytes> val;
    {
        AutoMutex a(iLock);
        iVal = aKvp.Value();
        Ascii::AppendDec(val, iVal);
    }
    ValueChanged(val);
}


// ConfigUiValText

ConfigUiValText::ConfigUiValText(Configuration::ConfigText& aText, const IWritable& aAdditionalJson)
    : ConfigUiValBase(aAdditionalJson)
    , iText(aText)
    , iListenerId(IConfigManager::kSubscriptionIdInvalid)
    , iLock("CUTL")
{
    iListenerId = iText.Subscribe(MakeFunctorConfigText(*this, &ConfigUiValText::Update));
}

ConfigUiValText::~ConfigUiValText()
{
    iText.Unsubscribe(iListenerId);
}

void ConfigUiValText::ObserverAdded(IConfigUiValObserver& aObserver)
{
    AutoMutex a(iLock);
    aObserver.ValueChanged(*this, iJsonValue);
}

void ConfigUiValText::WriteKey(IWriter& aWriter)
{
    aWriter.Write(Brn("\""));
    Json::Escape(aWriter, iText.Key());
    aWriter.Write(Brn("\""));
}

void ConfigUiValText::WriteType(IWriter& aWriter)
{
    aWriter.Write(Brn("\"text\""));
}

void ConfigUiValText::WriteMeta(IWriter& aWriter, ILanguageResourceManager& /*aLanguageResourceManager*/, std::vector<Bws<10>>& /*aLanguageList*/)
{
    aWriter.Write('{');
    aWriter.Write(Brn("\"default\":"));
    aWriter.Write(Brn("\""));
    Json::Escape(aWriter, iText.Default());
    aWriter.Write(Brn("\""));
    aWriter.Write(Brn(","));
    aWriter.Write(Brn("\"minlength\":"));
    Ascii::StreamWriteUint(aWriter, iText.MinLength());
    aWriter.Write(Brn(","));
    aWriter.Write(Brn("\"maxlength\":"));
    Ascii::StreamWriteUint(aWriter, iText.MaxLength());
    aWriter.Write('}');
}

void ConfigUiValText::Update(Configuration::ConfigText::KvpText& aKvp)
{
    AutoMutex a(iLock);
    iVal.Replace(aKvp.Value());

    // Update JSON string.
    iJsonValue.SetBytes(0);
    WriterBuffer writerBuf(iJsonValue);
    writerBuf.Write('\"');
    Json::Escape(writerBuf, iVal);
    writerBuf.Write('\"');

    ValueChanged(iJsonValue);
}


// ConfigUiValChoiceDelayed

ConfigUiValChoiceDelayed::ConfigUiValChoiceDelayed(Configuration::IConfigManager& aConfigManager, const Brx& aKey, IWritable& aAdditionalJson)
    : iConfigManager(aConfigManager)
    , iKey(aKey)
    , iAdditionalJson(aAdditionalJson)
    , iChoice(nullptr)
    , iUiChoice(nullptr)
    , iLock("CUCD")
{
}

ConfigUiValChoiceDelayed::~ConfigUiValChoiceDelayed()
{
    if (iUiChoice != nullptr) {
        delete iUiChoice;
    }
}

void ConfigUiValChoiceDelayed::WriteJson(IWriter& aWriter, IConfigUiUpdateWriter& aValWriter, ILanguageResourceManager& aLanguageResourceManager, std::vector<Bws<10>>& aLanguageList)
{
    AutoMutex a(iLock);
    ASSERT(iUiChoice != nullptr);
    iUiChoice->WriteJson(aWriter, aValWriter, aLanguageResourceManager, aLanguageList);
}

TUint ConfigUiValChoiceDelayed::AddObserver(IConfigUiValObserver& aObserver)
{
    AutoMutex a(iLock);
    if (iUiChoice == nullptr) {
        iChoice = &iConfigManager.GetChoice(iKey);
        // This dynamic allocation at runtime only happens once.
        iUiChoice = new ConfigUiValChoice(*iChoice, iAdditionalJson);
    }
    return iUiChoice->AddObserver(aObserver);
}

void ConfigUiValChoiceDelayed::RemoveObserver(TUint aObserverId)
{
    AutoMutex a(iLock);
    ASSERT(iUiChoice != nullptr);
    iUiChoice->RemoveObserver(aObserverId);
}


// ConfigUiValRoModelIcon

const Brn ConfigUiValRoModelIcon::kKey("About.ModelIcon");

ConfigUiValRoModelIcon::ConfigUiValRoModelIcon(Av::Product& aProduct)
    : iProduct(aProduct)
{
    Brn name, info;
    Bws<Product::kMaxUriBytes> url, imageUri;
    iProduct.GetModelDetails(name, info, url, imageUri);
    iUiVal = new ConfigUiValRoUpdatable(kKey, imageUri);
    iProduct.AddObserver(*this);
}

ConfigUiValRoModelIcon::~ConfigUiValRoModelIcon()
{
    delete iUiVal;
}

void ConfigUiValRoModelIcon::WriteJson(IWriter& aWriter, IConfigUiUpdateWriter& aValWriter, ILanguageResourceManager& aLanguageResourceManager, std::vector<Bws<10>>& aLanguageList)
{
    iUiVal->WriteJson(aWriter, aValWriter, aLanguageResourceManager, aLanguageList);
}

TUint ConfigUiValRoModelIcon::AddObserver(IConfigUiValObserver& aObserver)
{
    return iUiVal->AddObserver(aObserver);
}

void ConfigUiValRoModelIcon::RemoveObserver(TUint aObserverId)
{
    iUiVal->RemoveObserver(aObserverId);
}

void ConfigUiValRoModelIcon::Started()
{
}

void ConfigUiValRoModelIcon::SourceIndexChanged()
{
}

void ConfigUiValRoModelIcon::SourceXmlChanged()
{
}

void ConfigUiValRoModelIcon::ProductUrisChanged()
{
    Brn name, info;
    Bws<Product::kMaxUriBytes> url, imageUri;
    iProduct.GetModelDetails(name, info, url, imageUri);
    iUiVal->Update(imageUri);
}


// ConfigUiValRoModelName

const Brn ConfigUiValRoModelName::kKey("About.ModelName");

ConfigUiValRoModelName::ConfigUiValRoModelName(Product& aProduct)
{
    Brn name, info;
    Bws<Product::kMaxUriBytes> url, imageUri;
    aProduct.GetModelDetails(name, info, url, imageUri);
    iUiVal = new ConfigUiValRo(kKey, name);
}

ConfigUiValRoModelName::~ConfigUiValRoModelName()
{
    delete iUiVal;
}

void ConfigUiValRoModelName::WriteJson(IWriter& aWriter, IConfigUiUpdateWriter& aValWriter, ILanguageResourceManager& aLanguageResourceManager, std::vector<Bws<10>>& aLanguageList)
{
    iUiVal->WriteJson(aWriter, aValWriter, aLanguageResourceManager, aLanguageList);
}

TUint ConfigUiValRoModelName::AddObserver(IConfigUiValObserver& aObserver)
{
    return iUiVal->AddObserver(aObserver);
}

void ConfigUiValRoModelName::RemoveObserver(TUint aObserverId)
{
    iUiVal->RemoveObserver(aObserverId);
}


// ConfigUiValRoModelUrl

const Brn ConfigUiValRoModelUrl::kKey("About.ModelUrl");

ConfigUiValRoModelUrl::ConfigUiValRoModelUrl(Av::Product& aProduct)
    : iProduct(aProduct)
{
    Brn name, info;
    Bws<Product::kMaxUriBytes> url, imageUri;
    aProduct.GetModelDetails(name, info, url, imageUri);
    iUiVal = new ConfigUiValRoUpdatable(kKey, url);
    iProduct.AddObserver(*this);
}

ConfigUiValRoModelUrl::~ConfigUiValRoModelUrl()
{
    delete iUiVal;
}

void ConfigUiValRoModelUrl::WriteJson(IWriter& aWriter, IConfigUiUpdateWriter& aValWriter, ILanguageResourceManager& aLanguageResourceManager, std::vector<Bws<10>>& aLanguageList)
{
    iUiVal->WriteJson(aWriter, aValWriter, aLanguageResourceManager, aLanguageList);
}

TUint ConfigUiValRoModelUrl::AddObserver(IConfigUiValObserver& aObserver)
{
    return iUiVal->AddObserver(aObserver);
}

void ConfigUiValRoModelUrl::RemoveObserver(TUint aObserverId)
{
    iUiVal->RemoveObserver(aObserverId);
}

void ConfigUiValRoModelUrl::Started()
{
}

void ConfigUiValRoModelUrl::SourceIndexChanged()
{
}

void ConfigUiValRoModelUrl::SourceXmlChanged()
{
}

void ConfigUiValRoModelUrl::ProductUrisChanged()
{
    Brn name, info;
    Bws<Product::kMaxUriBytes> url, imageUri;
    iProduct.GetModelDetails(name, info, url, imageUri);
    iUiVal->Update(url);
}


// ConfigUiValRoManufacturerName

const Brn ConfigUiValRoManufacturerName::kKey("About.ManufacturerName");

ConfigUiValRoManufacturerName::ConfigUiValRoManufacturerName(Product& aProduct)
{
    Brn name, info;
    Bws<Product::kMaxUriBytes> url, imageUri;
    aProduct.GetManufacturerDetails(name, info, url, imageUri);
    iUiVal = new ConfigUiValRo(kKey, name);
}

ConfigUiValRoManufacturerName::~ConfigUiValRoManufacturerName()
{
    delete iUiVal;
}

void ConfigUiValRoManufacturerName::WriteJson(IWriter& aWriter, IConfigUiUpdateWriter& aValWriter, ILanguageResourceManager& aLanguageResourceManager, std::vector<Bws<10>>& aLanguageList)
{
    iUiVal->WriteJson(aWriter, aValWriter, aLanguageResourceManager, aLanguageList);
}

TUint ConfigUiValRoManufacturerName::AddObserver(IConfigUiValObserver& aObserver)
{
    return iUiVal->AddObserver(aObserver);
}

void ConfigUiValRoManufacturerName::RemoveObserver(TUint aObserverId)
{
    iUiVal->RemoveObserver(aObserverId);
}


// ConfigUiValRoManufacturerUrl

const Brn ConfigUiValRoManufacturerUrl::kKey("About.ManufacturerUrl");

ConfigUiValRoManufacturerUrl::ConfigUiValRoManufacturerUrl(Av::Product& aProduct)
    : iProduct(aProduct)
{
    Brn name, info;
    Bws<Product::kMaxUriBytes> url, imageUri;
    iProduct.GetManufacturerDetails(name, info, url, imageUri);
    iUiVal = new ConfigUiValRoUpdatable(kKey, url);
    iProduct.AddObserver(*this);
}

ConfigUiValRoManufacturerUrl::~ConfigUiValRoManufacturerUrl()
{
    delete iUiVal;
}

void ConfigUiValRoManufacturerUrl::WriteJson(IWriter& aWriter, IConfigUiUpdateWriter& aValWriter, ILanguageResourceManager& aLanguageResourceManager, std::vector<Bws<10>>& aLanguageList)
{
    iUiVal->WriteJson(aWriter, aValWriter, aLanguageResourceManager, aLanguageList);
}

TUint ConfigUiValRoManufacturerUrl::AddObserver(IConfigUiValObserver& aObserver)
{
    return iUiVal->AddObserver(aObserver);
}

void ConfigUiValRoManufacturerUrl::RemoveObserver(TUint aObserverId)
{
    iUiVal->RemoveObserver(aObserverId);
}

void ConfigUiValRoManufacturerUrl::Started()
{
}

void ConfigUiValRoManufacturerUrl::SourceIndexChanged()
{
}

void ConfigUiValRoManufacturerUrl::SourceXmlChanged()
{
}

void ConfigUiValRoManufacturerUrl::ProductUrisChanged()
{
    Brn name, info;
    Bws<Product::kMaxUriBytes> url, imageUri;
    iProduct.GetManufacturerDetails(name, info, url, imageUri);
    iUiVal->Update(url);
}


// ConfigUiValRoIpAddress

const Brn ConfigUiValRoIpAddress::kKey("About.IpAddress");
const TChar* ConfigUiValRoIpAddress::kCookie("UiValRoIp");

ConfigUiValRoIpAddress::ConfigUiValRoIpAddress(NetworkAdapterList& aAdapterList)
    : iAdapterList(aAdapterList)
{
    // Initialise with dummy value.
    iUiVal = new ConfigUiValRoUpdatable(kKey, Brx::Empty());
    iListenerId = iAdapterList.AddCurrentChangeListener(MakeFunctor(*this, &ConfigUiValRoIpAddress::CurrentAdapterChanged), "ConfigUiValRoIpAddress");

    // Callback isn't made when registering observer.
    // Make callback internally to get newest val.
    CurrentAdapterChanged();
}

ConfigUiValRoIpAddress::~ConfigUiValRoIpAddress()
{
    iAdapterList.RemoveCurrentChangeListener(iListenerId);
    delete iUiVal;
}

void ConfigUiValRoIpAddress::WriteJson(IWriter& aWriter, IConfigUiUpdateWriter& aValWriter, ILanguageResourceManager& aLanguageResourceManager, std::vector<Bws<10>>& aLanguageList)
{
    iUiVal->WriteJson(aWriter, aValWriter, aLanguageResourceManager, aLanguageList);
}

TUint ConfigUiValRoIpAddress::AddObserver(IConfigUiValObserver& aObserver)
{
    return iUiVal->AddObserver(aObserver);
}

void ConfigUiValRoIpAddress::RemoveObserver(TUint aObserverId)
{
    iUiVal->RemoveObserver(aObserverId);
}

void ConfigUiValRoIpAddress::CurrentAdapterChanged()
{
    iAddress.SetBytes(0);

    NetworkAdapter* adpt = iAdapterList.CurrentAdapter(kCookie).Ptr();
    if (adpt != nullptr) {
        Endpoint::AppendAddress(iAddress, adpt->Address());
        adpt->RemoveRef(kCookie);
    }

    iUiVal->Update(Brn(iAddress));
}


// ConfigUiValStartupSource

ConfigUiValStartupSource::ConfigUiValStartupSource(IConfigManager& aConfigManager, ConfigText& aText, const std::vector<const Brx*>& aSources, const OpenHome::Web::IWritable& aAdditionalJson)
    : ConfigUiValBase(aAdditionalJson)
    , iText(aText)
    , iListenerId(IConfigManager::kSubscriptionIdInvalid)
    , iLock("CUTL")
{
    for (TUint i = 0; i < aSources.size(); i++) {
        Bws<Source::kKeySourceNameMaxBytes> key;
        Source::GetSourceNameKey(*aSources[i], key);

        ConfigText& configText = aConfigManager.GetText(key);
        SourceNameObserver* obs = new SourceNameObserver(*aSources[i], configText, MakeFunctor(*this, &ConfigUiValStartupSource::SourceNameChanged));
        iObservers.push_back(obs);
    }

    iListenerId = iText.Subscribe(MakeFunctorConfigText(*this, &ConfigUiValStartupSource::Update));
}

ConfigUiValStartupSource::~ConfigUiValStartupSource()
{
    iText.Unsubscribe(iListenerId);
    for (TUint i = 0; i < iObservers.size(); i++) {
        delete iObservers[i];
    }
}

void ConfigUiValStartupSource::ObserverAdded(IConfigUiValObserver& aObserver)
{
    AutoMutex a(iLock);
    aObserver.ValueChanged(*this, iJsonValue);
}

void ConfigUiValStartupSource::WriteKey(IWriter& aWriter)
{
    aWriter.Write(Brn("\""));
    Json::Escape(aWriter, iText.Key());
    aWriter.Write(Brn("\""));
}

void ConfigUiValStartupSource::WriteType(IWriter& aWriter)
{
    aWriter.Write(Brn("\"text\""));
}

void ConfigUiValStartupSource::WriteMeta(IWriter& aWriter, ILanguageResourceManager& /*aLanguageResourceManager*/, std::vector<Bws<10>>& /*aLanguageList*/)
{
    aWriter.Write('{');
    aWriter.Write(Brn("\"default\":"));
    aWriter.Write(Brn("\""));
    Json::Escape(aWriter, iText.Default());
    aWriter.Write(Brn("\""));
    aWriter.Write(Brn(","));
    aWriter.Write(Brn("\"minlength\":"));
    Ascii::StreamWriteUint(aWriter, iText.MinLength());
    aWriter.Write(Brn(","));
    aWriter.Write(Brn("\"maxlength\":"));
    Ascii::StreamWriteUint(aWriter, iText.MaxLength());
    aWriter.Write(Brn(","));
    WriteMetaOptions(aWriter);
    aWriter.Write('}');
}

void ConfigUiValStartupSource::WriteMetaOption(IWriter& aWriter, SourceNameObserver& aObserver)
{
    JsonWriterString writerString(aWriter);

    aWriter.Write(Brn("{"));
    writerString.Write(Brn("id"));
    aWriter.Write(Brn(":"));
    aObserver.WriteSystemNameJson(aWriter);
    aWriter.Write(Brn(","));
    writerString.Write(Brn("value"));
    aWriter.Write(Brn(":"));
    aObserver.WriteNameJson(aWriter);
    aWriter.Write(Brn("}"));
}

void ConfigUiValStartupSource::WriteMetaOptions(IWriter& aWriter)
{
    aWriter.Write(Brn("\"options\":"));
    aWriter.Write(Brn("["));

    // Write out special "Last Used" value.
    Bws<64> lastUsed("{\"id\":\"");
    lastUsed.Append(ConfigStartupSource::kLastUsed);
    lastUsed.Append("\", \"value\":\"");
    lastUsed.Append(ConfigStartupSource::kLastUsed);
    lastUsed.Append("\"}");
    aWriter.Write(lastUsed);
    if (iObservers.size() > 0) {
        // More values to follow; add delimiter.
        aWriter.Write(Brn(","));
    }

    for (TUint i = 0; i < iObservers.size(); i++) {
        SourceNameObserver* obs = iObservers[i];
        WriteMetaOption(aWriter, *obs);
        if (i < iObservers.size() - 1) {
            aWriter.Write(Brn(","));
        }
    }

    aWriter.Write(Brn("]"));
}

void ConfigUiValStartupSource::Update(Configuration::ConfigText::KvpText& aKvp)
{
    AutoMutex a(iLock);

    // Check if aKvp.Val() is a valid value expected by the UI (i.e., last used or a valid source system name).
    TBool sourceFound = false;
    if (aKvp.Value() == ConfigStartupSource::kLastUsed) {
        sourceFound = true;
    }
    else {
        for (const SourceNameObserver* o: iObservers) {
            if (aKvp.Value() == o->SystemName()) {
                sourceFound = true;
                break;
            }
        }
    }

    if (sourceFound) {
        iVal.Replace(aKvp.Value());
    }
    else {
        // Garbage data in store. Default to reporting last used as startup source.
        iVal.Replace(ConfigStartupSource::kLastUsed);
    }

    // Update JSON string.
    iJsonValue.SetBytes(0);
    WriterBuffer writerBuf(iJsonValue);
    writerBuf.Write('\"');
    Json::Escape(writerBuf, iVal);
    writerBuf.Write('\"');

    ValueChanged(iJsonValue);
}

void ConfigUiValStartupSource::SourceNameChanged()
{
    // If source name has change, pretend that the startup source value has
    // changed so that WebUI updates its drop-down list of source names.
    AutoMutex a(iLock);
    ValueChanged(iJsonValue);
}


// ConfigUiValStartupSource::SourceNameObserver

ConfigUiValStartupSource::SourceNameObserver::SourceNameObserver(const Brx& aSystemName, Configuration::ConfigText& aConfigText, Functor aObserver)
    : iConfigText(aConfigText)
    , iFunctor(aObserver)
    , iSystemName(aSystemName)
    , iLock("CVSO")
{
    iSubscriberId = iConfigText.Subscribe(MakeFunctorConfigText(*this, &ConfigUiValStartupSource::SourceNameObserver::SourceNameChanged));
}

ConfigUiValStartupSource::SourceNameObserver::~SourceNameObserver()
{
    iConfigText.Unsubscribe(iSubscriberId);
}

const Brx& ConfigUiValStartupSource::SourceNameObserver::SystemName() const
{
    return iSystemName;
}

void ConfigUiValStartupSource::SourceNameObserver::WriteSystemNameJson(IWriter& aWriter) const
{
    JsonWriterString writerString(aWriter);
    writerString.Write(iSystemName);
}

void ConfigUiValStartupSource::SourceNameObserver::WriteNameJson(IWriter& aWriter) const
{
    AutoMutex a(iLock);
    JsonWriterString writerString(aWriter);
    writerString.Write(iName);
}

void ConfigUiValStartupSource::SourceNameObserver::SourceNameChanged(Configuration::KeyValuePair<const Brx&>& aKvp)
{
    {
        AutoMutex a(iLock);
        iName.Replace(aKvp.Value());
    }
    iFunctor();
}


// ConfigUiValStartupSourceDelayed

ConfigUiValStartupSourceDelayed::ConfigUiValStartupSourceDelayed(IConfigManager& aConfigManager, const std::vector<const Brx*>& aSources, const IWritable& aAdditionalJson)
    : iConfigManager(aConfigManager)
    , iAdditionalJson(aAdditionalJson)
    , iUiVal(nullptr)
    , iLock("CVSS")
{
    for (const Brx* systemName : aSources) {
        Bwh* name = new Bwh(*systemName);
        iSources.push_back(name);
    }
}

ConfigUiValStartupSourceDelayed::~ConfigUiValStartupSourceDelayed()
{
    if (iUiVal != nullptr) {
        delete iUiVal;
    }

    for (TUint i=0; i<iSources.size(); i++) {
        delete iSources[i];
    }
}

void ConfigUiValStartupSourceDelayed::WriteJson(IWriter& aWriter, IConfigUiUpdateWriter& aValWriter, ILanguageResourceManager& aLanguageResourceManager, std::vector<Bws<10>>& aLanguageList)
{
    AutoMutex a(iLock);
    ASSERT(iUiVal != nullptr);
    iUiVal->WriteJson(aWriter, aValWriter, aLanguageResourceManager, aLanguageList);
}

TUint ConfigUiValStartupSourceDelayed::AddObserver(IConfigUiValObserver& aObserver)
{
    AutoMutex a(iLock);
    if (iUiVal == nullptr) {
        ConfigText& text = iConfigManager.GetText(ConfigStartupSource::kKeySource);
        // This dynamic allocation at runtime only happens once.
        iUiVal = new ConfigUiValStartupSource(iConfigManager, text, iSources, iAdditionalJson);
    }
    return iUiVal->AddObserver(aObserver);
}

void ConfigUiValStartupSourceDelayed::RemoveObserver(TUint aObserverId)
{
    AutoMutex a(iLock);
    ASSERT(iUiVal != nullptr);
    iUiVal->RemoveObserver(aObserverId);
}


// ConfigAppBase

const Brn ConfigAppBase::kLangRoot("lang");
const Brn ConfigAppBase::kDefaultLanguage("en-gb");

ConfigAppBase::ConfigAppBase(IInfoAggregator& aInfoAggregator, IConfigManager& aConfigManager,
                             IConfigAppResourceHandlerFactory& aResourceHandlerFactory,
                             const Brx& aResourcePrefix, const Brx& aResourceDir, TUint aResourceHandlersCount,
                             TUint aMaxTabs, TUint aSendQueueSize, TUint aMsgBufCount, TUint aMsgBufBytes,
                             IRebootHandler& aRebootHandler)
    : iConfigManager(aConfigManager)
    , iRebootRequired(true)
    , iLangResourceDir(aResourceDir.Bytes()+1+kLangRoot.Bytes()+1)  // "<aResourceDir>/<kLangRoot>/"
    , iResourcePrefix(aResourcePrefix)
    , iLock("COAL")
{
    ASSERT(aResourceHandlersCount > 0);
    ASSERT(aMaxTabs > 0);

    Log::Print("ConfigAppBase::ConfigAppBase iResourcePrefix: %.*s\n", PBUF(iResourcePrefix));

    iLangResourceDir.Replace(aResourceDir);
    if (iLangResourceDir.Bytes() == 0 || iLangResourceDir[iLangResourceDir.Bytes()-1] != '/') {
        iLangResourceDir.Append('/');
    }
    iLangResourceDir.Append(kLangRoot);
    iLangResourceDir.Append('/');

    iMsgAllocator = new ConfigMessageAllocator(aInfoAggregator, aSendQueueSize, aMsgBufCount, aMsgBufBytes, *this);

    iResourceManager = new BlockingResourceManager(aResourceHandlerFactory, aResourceHandlersCount, aResourceDir);

    for (TUint i=0; i<aMaxTabs; i++) {
        iTabs.push_back(new ConfigTab(i, *iMsgAllocator, iConfigManager, aRebootHandler));
        iLanguageResourceHandlers.push_back(aResourceHandlerFactory.NewLanguageReader(iLangResourceDir));
    }

    iResourceMappings.insert(ResourcePair(Brn(""), Brn("index.html")));
}

ConfigAppBase::~ConfigAppBase()
{
    for (TUint i=0; i<iLanguageResourceHandlers.size(); i++) {
        delete iLanguageResourceHandlers[i];
    }

    for (TUint i=0; i<iTabs.size(); i++) {
        delete iTabs[i];
    }

    delete iResourceManager;

    for (auto val : iUiVals) {
        delete val;
    }

    delete iMsgAllocator;
}

// FIXME - is this really required? If so, app framework should call it when it is started
//void ConfigAppBase::Start()
//{
//    for (TUint i=0; i<iTabs.size(); i++) {
//        iTabs[i]->Start();
//    }
//}

ITab& ConfigAppBase::Create(ITabHandler& aHandler, const std::vector<Bws<10>>& aLanguageList)
{
    for (TUint i=0; i<iTabs.size(); i++) {
        if (!iTabs[i]->Allocated()) {
            // FIXME - won't be cleared until a new handler is set.
            // Shouldn't matter as only thing that can call tab handler is the
            // tab, which gets destroyed when it is no longer in use.
            iTabs[i]->SetHandler(aHandler, aLanguageList);
            return *iTabs[i];
        }
    }
    THROW(TabAllocatorFull);
}

const Brx& ConfigAppBase::ResourcePrefix() const
{
    return iResourcePrefix;
}

IResourceHandler* ConfigAppBase::CreateResourceHandler(const Brx& aResource)
{
    AutoMutex a(iLock);

    Brn resource(aResource);
    ResourceMap::iterator it = iResourceMappings.find(Brn(aResource));
    if (it != iResourceMappings.end()) {
        resource.Set(it->second);
    }

    // Blocks until an IResourceHandler is available.
    return iResourceManager->CreateResourceHandler(resource);
}

ILanguageResourceReader& ConfigAppBase::CreateLanguageResourceHandler(const Brx& aResourceUriTail, std::vector<Bws<10>>& aLanguageList)
{
    // If no desired language can be found, should default to English.
    // Developer error if English mappings don't exist.
    std::vector<Bws<10>> languages(aLanguageList);
    Bws<10> def(kDefaultLanguage);
    languages.push_back(def);

    AutoMutex a(iLock);
    for (TUint i=0; i<iLanguageResourceHandlers.size(); i++) {
        if (!iLanguageResourceHandlers[i]->Allocated()) {

            for (TUint j=0; j<languages.size(); j++) {
                Bws<Uri::kMaxUriBytes> resource(languages[j]);
                resource.Append("/");
                resource.Append(aResourceUriTail);
                try {
                    ILanguageResourceReader& handler = *iLanguageResourceHandlers[i];
                    handler.SetResource(resource);
                    return handler;
                }
                catch (LanguageResourceInvalid&) {
                    LOG(kHttp, "ConfigAppBase::CreateLanguageResourceHandler no mapping found for: %.*s\n", PBUF(resource));
                }
            }

            // No mapping found; should have been able to find kDefaultLanguage.
            // Useless to just assert here; print out all language resources
            // searched for.
            Log::Print("ConfigAppBase::CreateLanguageResourceHandler aResourceUriTail: %.*s\n", PBUF(aResourceUriTail));
            Log::Print("Languages:\n");
            for (TUint j=0; j<languages.size(); j++) {
                Log::Print("\t%.*s\n", PBUF(languages[j]));
            }
            ASSERTS();
        }
    }
    ASSERTS();  // No free handler available. // FIXME - throw exception instead?
    return *iLanguageResourceHandlers[0];   // unreachable
}

void ConfigAppBase::AddValue(IConfigUiVal* aValue)
{
    iUiVals.push_back(aValue);
    for (auto tab : iTabs) {
        tab->AddValue(*aValue);
    }
}

void ConfigAppBase::AddConfigNum(const Brx& aKey)
{
    auto& num = iConfigManager.GetNum(aKey);
    AddValue(new ConfigUiValNum(num, num.RebootRequired()? iRebootRequired : iRebootNotRequired));
}

void ConfigAppBase::AddConfigChoice(const Brx& aKey)
{
    auto& choice = iConfigManager.GetChoice(aKey);
    AddValue(new ConfigUiValChoice(choice, choice.RebootRequired() ? iRebootRequired : iRebootNotRequired));
}

void ConfigAppBase::AddConfigText(const Brx& aKey)
{
    auto& text = iConfigManager.GetText(aKey);
    AddValue(new ConfigUiValText(text, text.RebootRequired() ? iRebootRequired : iRebootNotRequired));
}

void ConfigAppBase::AddConfigNumConditional(const Brx& aKey)
{
    if (iConfigManager.HasNum(aKey) && iConfigManager.Access(aKey) == ConfigValAccess::Public) {
        AddConfigNum(aKey);
    }
}

void ConfigAppBase::AddConfigChoiceConditional(const Brx& aKey)
{
    if (iConfigManager.HasChoice(aKey) && iConfigManager.Access(aKey) == ConfigValAccess::Public) {
        AddConfigChoice(aKey);
    }
}

void ConfigAppBase::AddConfigTextConditional(const Brx& aKey)
{
    if (iConfigManager.HasText(aKey) && iConfigManager.Access(aKey) == ConfigValAccess::Public) {
        AddConfigText(aKey);
    }
}


// ConfigAppBasic

ConfigAppBasic::ConfigAppBasic(IInfoAggregator& aInfoAggregator, IConfigManager& aConfigManager,
                               IConfigAppResourceHandlerFactory& aResourceHandlerFactory,
                               const Brx& aResourcePrefix, const Brx& aResourceDir, TUint aResourceHandlersCount,
                               TUint aMaxTabs, TUint aSendQueueSize, TUint aMsgBufCount, TUint aMsgBufBytes,
                               IRebootHandler& aRebootHandler)
    : ConfigAppBase(aInfoAggregator, aConfigManager,
                    aResourceHandlerFactory, aResourcePrefix, aResourceDir, aResourceHandlersCount,
                    aMaxTabs, aSendQueueSize, aMsgBufCount, aMsgBufBytes, aRebootHandler)
{
    AddConfigText(Brn("Product.Name"));
    AddConfigText(Brn("Product.Room"));
}


// ConfigAppSources

ConfigAppSources::ConfigAppSources(IInfoAggregator& aInfoAggregator, IConfigManager& aConfigManager,
                                   IConfigAppResourceHandlerFactory& aResourceHandlerFactory,
                                   const std::vector<const Brx*>& aSources,
                                   const Brx& aResourcePrefix, const Brx& aResourceDir, TUint aResourceHandlersCount,
                                   TUint aMaxTabs, TUint aSendQueueSize, TUint aMsgBufCount, TUint aMsgBufBytes,
                                   IRebootHandler& aRebootHandler)
    : ConfigAppBasic(aInfoAggregator, aConfigManager,
                     aResourceHandlerFactory, aResourcePrefix, aResourceDir, aResourceHandlersCount,
                     aMaxTabs, aSendQueueSize, aMsgBufCount, aMsgBufBytes, aRebootHandler)
{
    // Get all product names.
    for (TUint i=0; i<aSources.size(); i++) {

        Bws<Av::Source::kKeySourceNameMaxBytes> key;
        Av::Source::GetSourceNameKey(*aSources[i], key);
        AddConfigText(key);

        Av::Source::GetSourceVisibleKey(*aSources[i], key);
        AddConfigChoice(key);
    }

    // Startup source value isn't added to ConfigManager until after ConfigApp is created.
    // Use special "delayed instantiation" ConfigUi value.
    AddValue(new ConfigUiValStartupSourceDelayed(iConfigManager, aSources, iRebootNotRequired));
}
