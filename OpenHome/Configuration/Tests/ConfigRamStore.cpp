#include <OpenHome/Configuration/Tests/ConfigRamStore.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Printer.h>

using namespace OpenHome;
using namespace OpenHome::Configuration;


// ConfigRamStore

ConfigRamStore::ConfigRamStore()
    : iReadCount(0)
    , iWriteCount(0)
    , iLockData("RAMS")
    , iLockObservers("RAMO")
{
}

ConfigRamStore::~ConfigRamStore()
{
    ASSERT(iStoreObservers.size() == 0);
    ASSERT(iStoreEntryObservers.size() == 0);
    Clear();
}

void ConfigRamStore::AddStoreEntryObserver(IStoreEntryObserver& aStoreEntryObserver)
{
    AutoMutex _(iLockObservers);
    iStoreEntryObservers.push_back(aStoreEntryObserver);
}

void ConfigRamStore::RemoveStoreEntryObserver(IStoreEntryObserver& aStoreEntryObserver)
{
    AutoMutex _(iLockObservers);
    for (auto it=iStoreEntryObservers.begin(); it != iStoreEntryObservers.end(); ++it) {
        auto& o = (*it).get();
        if (&o == &aStoreEntryObserver) {
            iStoreEntryObservers.erase(it);
            return;
        }
    }
}

void ConfigRamStore::AddStoreObserver(IStoreObserver& aStoreObserver)
{
    AutoMutex _(iLockObservers);
    iStoreObservers.push_back(aStoreObserver);
}

void ConfigRamStore::RemoveStoreObserver(IStoreObserver& aStoreObserver)
{
    AutoMutex _(iLockObservers);
    for (auto it=iStoreObservers.begin(); it != iStoreObservers.end(); ++it) {
        auto& o = (*it).get();
        if (&o == &aStoreObserver) {
            iStoreObservers.erase(it);
            return;
        }
    }
}

TUint64 ConfigRamStore::ReadCount() const
{
    AutoMutex _(iLockData);
    return iReadCount;
}

TUint64 ConfigRamStore::WriteCount() const
{
    AutoMutex _(iLockData);
    return iWriteCount;
}

void ConfigRamStore::Read(const Brx& aKey, Bwx& aDest)
{
    Brn key(aKey);
    AutoMutex _(iLockData);

    iReadCount++;

    Map::iterator it = iMap.find(&key);
    if (it == iMap.end()) {
        THROW(StoreKeyNotFound);
    }

    if (it->second->Bytes() > aDest.MaxBytes()) {
        Log::Print("ConfigRamStore::Read StoreReadBufferUndersized aKey: %.*s, aDest.MaxBytes(): %u, it->second->Bytes(): %u, it->second: %.*s\n", PBUF(aKey), aDest.MaxBytes(), it->second->Bytes(), PBUF(*it->second));
        THROW(StoreReadBufferUndersized);
    }

    aDest.Replace(*(it->second));
}

void ConfigRamStore::Write(const Brx& aKey, const Brx& aSource)
{
    Brh* key = new Brh(aKey);
    Brh* val = new Brh(aSource);

    TBool entryAdded = false;
    TBool entryUpdated = false;
    {
        AutoMutex _(iLockData);

        iWriteCount++;

        // std::map doesn't insert a value if key exists, so first remove existing
        // key-value pair, if new value is different
        Map::iterator it = iMap.find(key);
        if (it != iMap.end()) {
            if (*(it->second) == aSource) {
                // new value is the same; free memory
                delete key;
                delete val;
            }
            else {
                // new value is different; remove old value
                delete it->first;
                delete it->second;
                iMap.erase(it);
                entryUpdated = true;
            }
        }
        else {
            entryAdded = true;
        }

        if (entryAdded || entryUpdated) {
            iMap.insert(std::pair<const Brx*, const Brx*>(key, val));
        }
    }

    {
        AutoMutex _(iLockObservers);
        if (entryAdded) {
            for (auto& o : iStoreEntryObservers) {
                o.get().StoreEntryAdded(aKey, aSource);
            }
            for (auto& o : iStoreObservers) {
                o.get().StoreChanged(*this);
            }
        }
        else if (entryUpdated) {
            for (auto& o : iStoreEntryObservers) {
                o.get().StoreEntryChanged(aKey, aSource);
            }
            for (auto& o : iStoreObservers) {
                o.get().StoreChanged(*this);
            }
        }
    }
}

void ConfigRamStore::Delete(const Brx& aKey)
{
    Brn key(aKey);
    {
        AutoMutex _(iLockData);
        Map::iterator it = iMap.find(&key);
        if (it == iMap.end()) {
            THROW(StoreKeyNotFound);
        }

        delete it->first;
        delete it->second;
        iMap.erase(it);
    }

    {
        AutoMutex _(iLockObservers);
        for (auto& o : iStoreEntryObservers) {
            o.get().StoreEntryDeleted(aKey);
        }
        for (auto& o : iStoreObservers) {
            o.get().StoreChanged(*this);
        }
    }
}

void ConfigRamStore::Accept(IStoreVisitor& aVisitor)
{
    AutoMutex _(iLockData);
    for (auto it = iMap.cbegin(); it != iMap.cend(); ++it) {
        aVisitor.Visit(*it->first, *it->second);
    }
}

void ConfigRamStore::Clear()
{
    AutoMutex _(iLockData);
    for (auto it = iMap.cbegin(); it != iMap.cend(); ++it) {
        delete (*it).first;
        delete (*it).second;
    }
    iMap.clear();
}


// StorePrinter

StorePrinter::StorePrinter(IStoreVisitable& aVisitable)
    : iVisitable(aVisitable)
{
}

void StorePrinter::Print()
{
    Log::Print("RamStore: [\n");
    iVisitable.Accept(*this);
    Log::Print("]\n");
}

void StorePrinter::Visit(const Brx& aKey, const Brx& aValue)
{
    Log::Print("   {%.*s, ", PBUF(aKey));
    // See if value is size of an int. If so, additionally print value as a
    // numeral, in case it is actually a numeric value.
    if (aValue.Bytes() == sizeof(TUint32)) {
        TUint32 val = Converter::BeUint32At(aValue, 0);
        Log::Print("%u/%.*s", val, PBUF(aValue));
    }
    else {
        Log::Print(aValue);
    }
    Log::Print("}\n");
}


// StoreFileReaderJson

const Brn StoreFileReaderJson::kKeyKey("key");
const Brn StoreFileReaderJson::kKeyValue("value");

StoreFileReaderJson::StoreFileReaderJson(const TChar* aFilePath)
    : iFilePath(aFilePath)
{
}

void StoreFileReaderJson::Read(IStoreReadWrite& aStore)
{
    IFile* file = nullptr;
    try {
        file = iFileSystem.Open(iFilePath, eFileReadOnly);
        const TUint bytes = file->Bytes();
        Bwh inputBuf(bytes);
        file->Read(inputBuf);

        auto jsonParserArray = JsonParserArray::Create(inputBuf);
        try {
            for (;;) {
                auto obj = jsonParserArray.NextObject();
                auto jsonParser = JsonParser();
                jsonParser.Parse(obj);
                if (jsonParser.HasKey(kKeyKey) && jsonParser.HasKey(kKeyValue)) {
                    auto key = jsonParser.String(kKeyKey);
                    Bwh value(jsonParser.String(kKeyValue));
                    Json::Unescape(value);
                    aStore.Write(key, value);
                }
            }
        }
        catch (JsonCorrupt&) {
            Log::Print("StoreFileReaderJson::Read Corrupt JSON in config file: %s\n", iFilePath);
            ASSERTS(); // Indeterminate amount of data read into store. Don't continue with store in unknown state.
        }
        catch (JsonInvalid&) {
            Log::Print("StoreFileReaderJson::Read Invalid JSON in config file: %s\n", iFilePath);
            ASSERTS();
        }
        catch (JsonUnsupported&) {
            Log::Print("StoreFileReaderJson::Read Unsupported JSON in config file: %s\n", iFilePath);
            ASSERTS();
        }
        catch (JsonArrayEnumerationComplete&) {
            // Parsed entire array. Nothing more to do.
        }
        delete file;
    }
    catch (FileOpenError&) {
        Log::Print("StoreFileReaderJson::Read Unable to open config file: %s. Assuming this is the first run, and a store file does not yet exist.\n", iFilePath);
    }
    catch (FileReadError&) {
        Log::Print("StoreFileReaderJson::Read Error reading config file: %s\n", iFilePath);
        delete file;
        ASSERTS();
    }
}


// StoreFileWriterJson

const Brn StoreFileWriterJson::kKeyKey("key");
const Brn StoreFileWriterJson::kKeyValue("value");

StoreFileWriterJson::StoreFileWriterJson(const TChar* aFilePath)
    : iFilePath(aFilePath)
    , iWriterJsonArray(nullptr)
{

}

void StoreFileWriterJson::StoreChanged(IStoreVisitable& aVisitable)
{
    try {
        iFileStream.OpenFile(iFilePath, eFileWriteOnly);
        iWriterJsonArray = new WriterJsonArray(iFileStream);
        aVisitable.Accept(*this);
        iWriterJsonArray->WriteEnd();
        delete iWriterJsonArray;
        iFileStream.CloseFile();
    }
    catch (FileOpenError&) {
        Log::Print("StoreFileWriterJson::StoreChanged Unable to open config file: %s\n", iFilePath);
        ASSERTS();
    }
    catch (FileWriteError&) {
        Log::Print("StoreFileWriterJson::StoreChanged Caught FileWriteError while writing to %s.\n", iFilePath);

        // Clean up.
        delete iWriterJsonArray;
        iFileStream.CloseFile();
        ASSERTS();
    }
    catch (WriterError&) {
        Log::Print("StoreFileWriterJson::StoreChanged Caught WriterError while writing to %s.\n", iFilePath);

        // Clean up.
        delete iWriterJsonArray;
        iFileStream.CloseFile();
        ASSERTS();
    }
}

void StoreFileWriterJson::Visit(const Brx& aKey, const Brx& aValue)
{
    ASSERT(aKey.Bytes() > 0);
    // Any exceptions thrown from here should be caught by ::StoreChanged().
    auto jsonObj = iWriterJsonArray->CreateObject();
    jsonObj.WriteString(kKeyKey, aKey);
    jsonObj.WriteString(kKeyValue, aValue);
    jsonObj.WriteEnd();
}


// StoreFileReaderBinary

StoreFileReaderBinary::StoreFileReaderBinary(const TChar* aFilePath)
    : iFilePath(aFilePath)
{
}

void StoreFileReaderBinary::Read(IStoreReadWrite& aStore)
{
    IFile* file = nullptr;
    try {
        file = iFileSystem.Open(iFilePath, eFileReadOnly);
        const TUint bytes = file->Bytes();
        Bwh inputBuf(bytes);
        file->Read(inputBuf);

        TUint offset = 0;
        TUint bytesRemaining = inputBuf.Bytes();
        while (bytesRemaining > 0) {

            TUint keyBytes = 0;
            Brn key;
            TUint valueBytes = 0;
            Brn value;

            // Try read key size.
            if (bytesRemaining >= kEntrySizeBytes) {
                Brn buf(inputBuf.Ptr()+offset, kEntrySizeBytes);
                keyBytes = Converter::BeUint32At(buf, 0);

                bytesRemaining -= buf.Bytes();
                offset += buf.Bytes();
            }
            else {
                ASSERTS();
            }

            // Try read key.
            if (bytesRemaining >= keyBytes) {
                key.Set(inputBuf.Ptr()+offset, keyBytes);

                bytesRemaining -= key.Bytes();
                offset += key.Bytes();
            }
            else {
                ASSERTS();
            }

            // Try read value size.
            if (bytesRemaining >= kEntrySizeBytes) {
                Brn buf(inputBuf.Ptr()+offset, kEntrySizeBytes);
                valueBytes = Converter::BeUint32At(buf, 0);

                bytesRemaining -= buf.Bytes();
                offset += buf.Bytes();
            }
            else {
                ASSERTS();
            }

            // Try read value.
            if (bytesRemaining >= valueBytes) {
                // Value may be empty (i.e., be 0 bytes in length).
                // However, this block of code handles that, as Brns can have
                // Set() called with a length of 0.
                value.Set(inputBuf.Ptr()+offset, valueBytes);

                bytesRemaining -= value.Bytes();
                offset += value.Bytes();
            }
            else {
                ASSERTS();
            }

            aStore.Write(key, value);
        }
        delete file;
    }
    catch (FileOpenError&) {
        Log::Print("StoreFileReaderBinary::Read Unable to open config file: %s. Assuming this is the first run, and a store file does not yet exist.\n", iFilePath);
    }
    catch (FileReadError&) {
        Log::Print("StoreFileReaderBinary::Read Error reading config file: %s\n", iFilePath);
        delete file;
        ASSERTS();
    }
}


// StoreFileWriterBinary

StoreFileWriterBinary::StoreFileWriterBinary(const TChar* aFilePath)
    : iFilePath(aFilePath)
{
}

void StoreFileWriterBinary::StoreChanged(IStoreVisitable& aVisitable)
{
    try {
        iFileStream.OpenFile(iFilePath, eFileWriteOnly);
        aVisitable.Accept(*this);
        iFileStream.CloseFile();
    }
    catch (FileOpenError&) {
        Log::Print("StoreFileWriterBinary::StoreChanged Unable to open config file: %s\n", iFilePath);
        ASSERTS();
    }
    catch (FileWriteError&) {
        Log::Print("StoreFileWriterBinary::StoreChanged Caught FileWriteError while writing to %s.\n", iFilePath);

        // Clean up.
        iFileStream.CloseFile();
        ASSERTS();
    }
    catch (WriterError&) {
        Log::Print("StoreFileWriterBinary::StoreChanged Caught WriterError while writing to %s.\n", iFilePath);

        // Clean up.
        iFileStream.CloseFile();
        ASSERTS();
    }
}

void StoreFileWriterBinary::Visit(const Brx& aKey, const Brx& aValue)
{
    ASSERT(aKey.Bytes() > 0);
    // Any exceptions thrown from here should be caught by ::StoreChanged().
    WriterBinary writerBinary(iFileStream);
    writerBinary.WriteUint32Be(aKey.Bytes());
    writerBinary.Write(aKey);
    writerBinary.WriteUint32Be(aValue.Bytes());
    if (aValue.Bytes() > 0) {
        // Can only write values of non-zero length to files.
        writerBinary.Write(aValue);
    }
}
