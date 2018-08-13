#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Thread.h>

#include <map>
#include <vector>

EXCEPTION(PinError)
EXCEPTION(PinInvokeError);
EXCEPTION(PinIndexOutOfRange)
EXCEPTION(PinIdNotFound)
EXCEPTION(PinModeNotSupported);
EXCEPTION(PinTypeNotSupported);
EXCEPTION(PinSmartTypeNotSupported);
EXCEPTION(PinUriError);
EXCEPTION(PinNothingToPlay);
EXCEPTION(PinUriMissingRequiredParameter);

namespace OpenHome {
    class WriterJsonObject;
    class Uri;
    namespace Configuration {
        class IStoreReadWrite;
    }
namespace Av {

class IPin
{
public:
    static const TUint kMaxModeBytes = 16;
    static const TUint kMaxTypeBytes = 32;
    static const TUint kMaxUriBytes = 512;
    static const TUint kMaxTitleBytes = 128;
    static const TUint kMaxDescBytes = 512;
public:
    virtual ~IPin() {}
    virtual TUint Id() const = 0;
    virtual const Brx& Mode() const = 0;
    virtual const Brx& Type() const = 0;
    virtual const Brx& Uri() const = 0;
    virtual const Brx& Title() const = 0;
    virtual const Brx& Description() const = 0;
    virtual const Brx& ArtworkUri() const = 0;
    virtual TBool Shuffle() const = 0;
};

class IPinIdProvider
{
public:
    static const TUint kIdEmpty = 0;
public:
    virtual ~IPinIdProvider() {}
    virtual TUint NextId() = 0;
};

class Pin : public IPin, private INonCopyable
{
public:
    Pin(IPinIdProvider& aIdProvider);
    TBool TryUpdate(const Brx& aMode, const Brx& aType, const Brx& aUri,
                    const Brx& aTitle, const Brx& aDescription, const Brx& aArtworkUri,
                    TBool aShuffle);
    TBool Clear();
    void Internalise(const Brx& aBuf);
    void Externalise(IWriter& aWriter) const;
    void Write(WriterJsonObject& aWriter) const;
    void Copy(const Pin& aPin);
private:
    TBool Set(const Brx& aMode, const Brx& aType, const Brx& aUri,
              const Brx& aTitle, const Brx& aDescription, const Brx& aArtworkUri,
              TBool aShuffle);
    void ReadBuf(ReaderBinary& aReader, TUint aLenBytes, Bwx& aBuf);
    Pin(const Pin& aPin);
    const Pin& operator=(const Pin& aPin);
public: // from IPin
    TUint Id() const override;
    const Brx& Mode() const override;
    const Brx& Type() const override;
    const Brx& Uri() const override;
    const Brx& Title() const override;
    const Brx& Description() const override;
    const Brx& ArtworkUri() const override;
    TBool Shuffle() const override;
private:
    IPinIdProvider& iIdProvider;
    TUint iId;
    Bws<kMaxModeBytes> iMode;
    Bws<kMaxTypeBytes> iType;
    Bws<kMaxUriBytes> iUri;
    Bws<kMaxTitleBytes> iTitle;
    Bws<kMaxDescBytes> iDescription;
    Bws<kMaxUriBytes> iArtworkUri;
    TBool iShuffle;
};

class PinIdProvider : public IPinIdProvider, private INonCopyable
{
public:
    PinIdProvider();
public: // from IPinIdProvider
    TUint NextId() override;
private:
    Mutex iLock;
    TUint iNextId;
};

class PinSet
{
    friend class SuitePinSet;
    friend class SuitePinsManager;
public:
    PinSet(TUint aCount, IPinIdProvider& aIdProvider, Configuration::IStoreReadWrite& aStore, const TChar* aName);
    ~PinSet();
    void SetCount(TUint aCount);
    TUint Count() const;
    TBool Set(TUint aIndex, const Brx& aMode, const Brx& aType, const Brx& aUri,
             const Brx& aTitle, const Brx& aDescription, const Brx& aArtworkUri,
             TBool aShuffle);
    TBool Clear(TUint aId);
    TBool Swap(TUint aId1, TUint aId2);
    TBool Contains(TUint aId) const;
    TBool IsEmpty() const;
    const Pin& PinFromId(TUint aId) const;
    const Pin& PinFromIndex(TUint aIndex) const;
    const std::vector<TUint>& IdArray() const;
    TUint IndexFromId(TUint aId) const;
private:
    void WriteToStore(TUint aIndex);
    void GetStoreKey(TUint aIndex, Bwx& aKey);
private:
    IPinIdProvider& iIdProvider;
    Configuration::IStoreReadWrite& iStore;
    Brn iName;
    WriterBwh iStoreBuf;
    std::vector<Pin*> iPins;
    std::vector<TUint> iIds;
};

class IPinsAccountObserver
{
public:
    virtual ~IPinsAccountObserver() {}
    virtual void NotifySettable(TBool aSettable) = 0;
    virtual void NotifyAccountPin(TUint aIndex, const Brx& aMode, const Brx& aType,
                                  const Brx& aUri, const Brx& aTitle, const Brx& aDescription,
                                  const Brx& aArtworkUri, TBool aShuffle) = 0;
};

class IPinsAccount
{
public:
    virtual ~IPinsAccount() {}
    virtual void Set(TUint aIndex, const Brx& aMode, const Brx& aType,
                     const Brx& aUri, const Brx& aTitle, const Brx& aDescription,
                     const Brx& aArtworkUri, TBool aShuffle) = 0;
    virtual void Swap(TUint aIndex1, TUint aIndex2) = 0;
    virtual void SetObserver(IPinsAccountObserver& aObserver) = 0;
};

class IPinsObserver
{
public:
    virtual void NotifyDevicePinsMax(TUint aMax) = 0;
    virtual void NotifyAccountPinsMax(TUint aMax) = 0;
    virtual void NotifyModeAdded(const Brx& aMode) = 0;
    virtual void NotifyCloudConnected(TBool aConnected) = 0;
    virtual void NotifyUpdatesDevice(const std::vector<TUint>& aIdArray) = 0;
    virtual void NotifyUpdatesAccount(const std::vector<TUint>& aIdArray) = 0;
    virtual ~IPinsObserver() {}
};

class IPinsManager
{
public:
    virtual ~IPinsManager() {}
    virtual void SetObserver(IPinsObserver& aObserver) = 0;
    virtual void Set(TUint aIndex, const Brx& aMode, const Brx& aType, const Brx& aUri,
                     const Brx& aTitle, const Brx& aDescription, const Brx& aArtworkUri,
                     TBool aShuffle) = 0;
    virtual void Clear(TUint aId) = 0;
    virtual void Swap(TUint aIndex1, TUint aIndex2) = 0;
    virtual void WriteJson(IWriter& aWriter, const std::vector<TUint>& aIds) = 0;
    virtual void InvokeId(TUint aId) = 0;
    virtual void InvokeIndex(TUint aIndex) = 0;
    virtual void InvokeUri(const Brx& aMode, const Brx& aType, const Brx& aUri, TBool aShuffle) = 0;
};

class IPinInvoker
{
public:
    virtual ~IPinInvoker() {}
    virtual void BeginInvoke(const IPin& aPin, Functor aCompleted) = 0;
    virtual void Cancel() = 0; // will only be called on an in-progress invocation
                               // (BeginInvoke returned but its Completed callback not yet called or returned)
    virtual const TChar* Mode() const = 0;
};

class IPinsInvocable
{
public:
    virtual ~IPinsInvocable() {}
    virtual void Add(IPinInvoker* aInvoker) = 0; // transfers ownership
};

class IPinsAccountStore
{
public:
    virtual ~IPinsAccountStore() {}
    virtual void SetAccount(IPinsAccount& aAccount, TUint aCount) = 0;
};

class PinsManager : public IPinsManager
                  , public IPinsAccountStore
                  , public IPinsInvocable
                  , private IPinsAccountObserver
{
    friend class SuitePinsManager;
public:
    PinsManager(Configuration::IStoreReadWrite& aStore, TUint aMaxDevice);
    ~PinsManager();
public: // from IPinsAccountStore
    void SetAccount(IPinsAccount& aAccount, TUint aCount) override;
public: // from IPinsInvocable
    void Add(IPinInvoker* aInvoker) override;
private: // from IPinsManager
    void SetObserver(IPinsObserver& aObserver) override;
    void Set(TUint aIndex, const Brx& aMode, const Brx& aType, const Brx& aUri,
             const Brx& aTitle, const Brx& aDescription, const Brx& aArtworkUri,
             TBool aShuffle) override;
    void Clear(TUint aId) override;
    void Swap(TUint aId1, TUint aId2) override;
    void WriteJson(IWriter& aWriter, const std::vector<TUint>& aIds) override;
    void InvokeId(TUint aId) override;
    void InvokeIndex(TUint aIndex) override;
    void InvokeUri(const Brx& aMode, const Brx& aType, const Brx& aUri, TBool aShuffle) override;
private: // from IPinAccountObserver
    void NotifySettable(TBool aSettable) override;
    void NotifyAccountPin(TUint aIndex, const Brx& aMode, const Brx& aType,
                          const Brx& aUri, const Brx& aTitle, const Brx& aDescription,
                          const Brx& aArtworkUri, TBool aShuffle) override;
private:
    TBool IsAccountId(TUint aId) const;
    TBool IsAccountIndex(TUint aIndex) const;
    TUint AccountFromCombinedIndex(TUint aCombinedIndex) const;
    const Pin& PinFromId(TUint aId) const;
    inline IPinsAccount& AccountSetter();
    void BeginInvoke();
    void NotifyInvocationCompleted();
private:
    Mutex iLock;
    Mutex iLockInvoke;
    Mutex iLockInvoker;
    Semaphore iSemInvokerComplete;
    PinIdProvider iIdProvider;
    PinSet iPinsDevice;
    PinSet iPinsAccount;
    IPinsObserver* iObserver;
    IPinsAccount* iAccountSetter;
    std::map<Brn, IPinInvoker*, BufferCmp> iInvokers;
    Pin iInvoke;
    IPinInvoker* iCurrent;
};

class AutoPinComplete
{
public:
    AutoPinComplete(Functor aFunctor);
    ~AutoPinComplete();
    void Cancel();
private:
    Functor iFunctor;
};

class PinUri
{
public:
    PinUri(const IPin& aPin);
    ~PinUri();
    const Brx& Mode() const;
    const Brx& Type() const ;
    TBool TryGetValue(const TChar* aKey, Brn& aValue) const;
    TBool TryGetValue(const Brx& aKey, Brn& aValue) const;
    TBool TryGetValue(const TChar* aKey, Bwx& aValue) const;
    TBool TryGetValue(const Brx& aKey, Bwx& aValue) const;
private:
    Bwh iMode;
    Bwh iType;
    OpenHome::Uri* iUri;
    std::vector<std::pair<Brn, Brn>> iQueryKvps;
};

class PinMetadata
{
public:
    static void GetDidlLite(const IPin& aPin, Bwx& aDidlLite);
private:
    static void TryAppend(Bwx& aDidlLite, const TChar* aStr);
    static void TryAppend(Bwx& aDidlLite, const Brx& aBuf);
    static void TryAddTag(Bwx& aDidlLite, const Brx& aDidlTag, const Brx& aNs, const Brx& aRole, const Brx& aValue);
    static void TryAddAttribute(Bwx& aDidlLite, const TChar* aValue, const TChar* aDidlAttr);
};

} // namespace Av
} // namespace OpenHome