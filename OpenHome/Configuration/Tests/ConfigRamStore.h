#pragma once

#include <OpenHome/Configuration/BufferPtrCmp.h>
#include <OpenHome/Configuration/IStore.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/File.h>
#include <OpenHome/Json.h>

#include <functional>
#include <list>
#include <map>

namespace OpenHome {
namespace Configuration {

/**
 * Interface to be called when visiting a single store entry.
 */
class IStoreVisitor
{
public:
    virtual void Visit(const Brx& aKey, const Brx& aValue) = 0;
    virtual ~IStoreVisitor() {}
};

/**
 * Interface to allow visiting an entire store object.
 *
 * When Accept() is called, the implementor should traverse every entry in the
 * store, calling Visit() on the visitor for each entry.
 */
class IStoreVisitable
{
public:
    virtual void Accept(IStoreVisitor& aVisitor) = 0;
    virtual ~IStoreVisitable() {}
};

/**
 * Interface for observing whenever an entry within the store changes.
 */
class IStoreEntryObserver
{
public:
    virtual void StoreEntryAdded(const Brx& aKey, const Brx& aValue) = 0;
    virtual void StoreEntryChanged(const Brx& aKey, const Brx& aValue) = 0;
    virtual void StoreEntryDeleted(const Brx& aKey) = 0;
    virtual ~IStoreEntryObserver() {}
};

/**
 * Interface for observing changes to global state of store.
 */
class IStoreObserver
{
public:
    virtual void StoreChanged(IStoreVisitable& aVisitable) = 0;
    virtual ~IStoreObserver() {}
};


/*
 * Class providing a basic implementation of a read/write store for storing
 * configuration in memory (no file writing, so no persistence between runs).
 */
class ConfigRamStore : public IStoreReadWrite, public IStoreVisitable
{
public:
    ConfigRamStore();
    ~ConfigRamStore();

    void AddStoreEntryObserver(IStoreEntryObserver& aStoreEntryObserver);
    void RemoveStoreEntryObserver(IStoreEntryObserver& aStoreEntryObserver);
    void AddStoreObserver(IStoreObserver& aStoreObserver);
    void RemoveStoreObserver(IStoreObserver& aStoreObserver);

    TUint64 ReadCount() const;
    TUint64 WriteCount() const;
public: // from IStoreReadWrite
    void Read(const Brx& aKey, Bwx& aDest) override;
    void Write(const Brx& aKey, const Brx& aSource) override;
    void Delete(const Brx& aKey) override;
public: // from IStoreVisitable
    void Accept(IStoreVisitor& aVisitor) override;
private:
    void Clear();
private:
    typedef std::map<const Brx*, const Brx*, BufferPtrCmp> Map;
    Map iMap;
    TUint64 iReadCount;
    TUint64 iWriteCount;
    mutable Mutex iLockData;
    std::list<std::reference_wrapper<IStoreEntryObserver>> iStoreEntryObservers;
    std::list<std::reference_wrapper<IStoreObserver>> iStoreObservers;
    Mutex iLockObservers;
};

class StorePrinter : public IStoreVisitor
{
public:
    StorePrinter(IStoreVisitable& aVisitable);
    void Print();
public: // from IStoreVisitor
    void Visit(const Brx& aKey, const Brx& aValue) override;
private:
    IStoreVisitable& iVisitable;
};

class StoreFileReaderJson
{
private:
    static const Brn kKeyKey;
    static const Brn kKeyValue;
public:
    StoreFileReaderJson(const TChar* aFilePath);
    void Read(IStoreReadWrite& aStore);
private:
    const TChar* iFilePath;
    FileSystemAnsii iFileSystem;
};

class StoreFileWriterJson : public IStoreObserver, public IStoreVisitor
{
private:
    static const Brn kKeyKey;
    static const Brn kKeyValue;
public:
    StoreFileWriterJson(const TChar* aFilePath);
public: // from IStoreObserver
    void StoreChanged(IStoreVisitable& aVisitable) override;
public: // from IStoreVisitor
    void Visit(const Brx& aKey, const Brx& aValue) override;
private:
    const TChar* iFilePath;
    FileStream iFileStream;
    WriterJsonArray* iWriterJsonArray;
};

class StoreFileReaderBinary
{
private:
    static const TUint kEntrySizeBytes = 4;
public:
    StoreFileReaderBinary(const TChar* aFilePath);
    void Read(IStoreReadWrite& aStore);
private:
    const TChar* iFilePath;
    FileSystemAnsii iFileSystem;
};

class StoreFileWriterBinary : public IStoreObserver, public IStoreVisitor
{
public:
    StoreFileWriterBinary(const TChar* aFilePath);
public: // from IStoreObserver
    void StoreChanged(IStoreVisitable& aVisitable) override;
public: // from IStoreVisitor
    void Visit(const Brx& aKey, const Brx& aValue) override;
private:
    const TChar* iFilePath;
    FileStream iFileStream;
};

} // namespace Configuration
} // namespace OpenHome
