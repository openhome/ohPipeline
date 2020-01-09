#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/CpiService.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Thread.h>

#include <atomic>

namespace OpenHome {
    class IWriter;
    class WriterJsonObject;
    class WriterJsonArray;
    class JsonParser;
namespace Net {
    class CpiSubscription;

class ICpiOdpResponse
{
public:
    virtual void HandleOdpResponse(const JsonParser& aJsonParser) = 0;
    virtual void HandleError() = 0;
    virtual ~ICpiOdpResponse() {}
};

class ICpiOdpDevice
{
public:
    virtual IWriter& WriteLock() = 0;
    virtual void WriteUnlock() = 0;
    virtual void WriteEnd(IWriter& aWriter) = 0;
    virtual TUint RegisterResponseHandler(ICpiOdpResponse& aResponseHandler) = 0; // returns corralation id
    virtual const Brx& Udn() const = 0;
    virtual const Brx& Alias() const = 0;
    virtual ~ICpiOdpDevice() {}
};

class CpiOdpResponseHandler : public ICpiOdpResponse
{
protected:
    CpiOdpResponseHandler(ICpiOdpDevice& aDevice);
    void WriteCorrelationId(WriterJsonObject& aWriterRequest);
    void WaitForResponse();
public: // from ICpiOdpResponse
    void HandleOdpResponse(const JsonParser& aJsonParser) override;
    void HandleError() override;
private:
    virtual void DoHandleResponse(const JsonParser& aJsonParser) = 0;
protected:
    ICpiOdpDevice & iDevice;
private:
    Semaphore iSem;
};

class CpiOdpInvocable : public CpiOdpResponseHandler
                      , public IInvocable
                      , private INonCopyable
{
public:
    CpiOdpInvocable(ICpiOdpDevice& aDevice);
public: // from IInvocable
    void InvokeAction(Invocation& aInvocation) override;
private: // from CpiOdpResponseHandler
    void DoHandleResponse(const JsonParser& aJsonParser) override;
private:
    Invocation* iInvocation;
    IWriter* iWriter;
};

class CpiOdpWriterArgs : private IInputArgumentProcessor
                       , private INonCopyable
{
public:
    CpiOdpWriterArgs(WriterJsonArray& aWriter);
    void Process(Argument& aArg);
private: // from IInputArgumentProcessor
    void ProcessString(const Brx& aVal) override;
    void ProcessInt(TInt aVal) override;
    void ProcessUint(TUint aVal) override;
    void ProcessBool(TBool aVal) override;
    void ProcessBinary(const Brx& aVal) override;
private:
    void WriteString(const Brx& aVal);
private:
    WriterJsonArray& iWriter;
    Argument* iArg;
};

class CpiOdpOutputProcessor : public IOutputProcessor
{
private: // from IOutputProcessor
    void ProcessString(const Brx& aBuffer, Brhz& aVal) override;
    void ProcessInt(const Brx& aBuffer, TInt& aVal) override;
    void ProcessUint(const Brx& aBuffer, TUint& aVal) override;
    void ProcessBool(const Brx& aBuffer, TBool& aVal) override;
    void ProcessBinary(const Brx& aBuffer, Brh& aVal) override;
};

class CpiOdpSubscriber : public CpiOdpResponseHandler
                       , private INonCopyable
{
public:
    CpiOdpSubscriber(ICpiOdpDevice& aDevice);
    void Subscribe(CpiSubscription& aSubscription);
private: // from CpiOdpResponseHandler
    void DoHandleResponse(const JsonParser& aJsonParser) override;
private:
    CpiSubscription* iSubscription;
};

class CpiOdpUnsubscriber : public CpiOdpResponseHandler
                         , private INonCopyable
{
public:
    CpiOdpUnsubscriber(ICpiOdpDevice& aDevice);
    void Unsubscribe(const Brx& aSid);
private: // from CpiOdpResponseHandler
    void DoHandleResponse(const JsonParser& aJsonParser) override;
};

class CpiOdpWriterService
{
public:
    static void Write(WriterJsonObject& aWriter, const ServiceType& aServiceType);
};

// takes write locked session, unlocks on destruction
class AutoOdpDevice : private INonCopyable
{
public:
    AutoOdpDevice(ICpiOdpDevice& aDevice);
    ~AutoOdpDevice();
private:
    ICpiOdpDevice& iDevice;
};

/*
 * Class that inserts itself into a queue when it receives a response callback.
 */
class CpiOdpInvocableQueueItem : public IInvocable
{
public:
    CpiOdpInvocableQueueItem(ICpiOdpDevice& aDevice, Fifo<IInvocable*>& aQueue);
public: // from IInvocable
    void InvokeAction(Invocation& aInvocation) override;
private:
    Fifo<IInvocable*>& iQueue;
    CpiOdpInvocable iInvocable;
};

} // namespace Net
} // namespace OpenHome
