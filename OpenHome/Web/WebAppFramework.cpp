#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Web/WebAppFramework.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Fifo.h>
#include <OpenHome/Private/File.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/MimeTypes.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Net/Core/OhNet.h>
#include <OpenHome/Private/NetworkAdapterList.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/ThreadPool.h>

#include <functional>
#include <limits>

using namespace OpenHome;
using namespace OpenHome::Configuration;
using namespace OpenHome::Web;


// FrameworkTabHandler

FrameworkTabHandler::FrameworkTabHandler(IFrameworkSemaphore& aSemRead, IFrameworkSemaphore& aSemWrite, IFrameworkTimer& aTimer, TUint aSendQueueSize, TUint aSendTimeoutMs)
    : iSendTimeoutMs(aSendTimeoutMs)
    , iFifo(aSendQueueSize)
    , iEnabled(false)
    , iPolling(false)
    , iLock("FTHL")
    , iSemRead(aSemRead)
    , iSemWrite(aSemWrite)
    , iTimer(aTimer)
{
    iSemRead.Clear();
    iSemWrite.Clear();
    // Allow msgs to be queued via Send().
    for (TUint i=0; i<aSendQueueSize; i++) {
        iSemWrite.Signal();
    }
}

FrameworkTabHandler::~FrameworkTabHandler()
{
    AutoMutex a(iLock);
    ASSERT(iFifo.SlotsUsed() == 0);
}

void FrameworkTabHandler::LongPoll(IWriter& aWriter)
{
    // This routine has 3 paths:
    // - There are >= 1 msgs in FIFO. If so, output and return.
    // - There are no msgs in FIFO. Block until a msg arrives via Send(), output it, and return.
    // - There are no msgs in FIFO. Block until timer triggers a timeout and return.
    {
        // Don't accept any long polls if in an interrupted state.
        // Don't accept long polls if already polling (i.e., misbehaving client is making overlapping long polls).
        AutoMutex a(iLock);
        if (!iEnabled || iPolling) {
            return;
        }
        iPolling = true;
    }

    // Start timer.
    iTimer.Start(iSendTimeoutMs, *this);
    iSemRead.Wait();
    iTimer.Cancel();    // Cancel timer here, in case it wasn't timer that signalled iSemRead.

    // Check if ::Disable() was called.
    AutoMutex a(iLock);
    // Code below may throw exception, so clear state here while lock is held.
    iPolling = false;
    iSemRead.Clear();

    // Output messages, if any (there will be none if timer callback signalled iSemRead (i.e., timeout) or if ::Disable() was called).
    WriteMessagesLocked(aWriter);   // May throw WriterError.
}

void FrameworkTabHandler::Disable()
{
    AutoMutex a(iLock);

    while (iFifo.SlotsUsed() > 0) {
        ITabMessage* msg = iFifo.Read();
        msg->Destroy();
        iSemWrite.Signal(); // Unblock any Send() calls.
    }

    iEnabled = false;

    if (iPolling) {
        // Safe to do this here, as long as iLock is held. ::LongPoll() call
        // can't progress beyond its iSemRead.Wait() call until this method
        // releases iLock.
        iSemRead.Signal();
    }
    else {
        iSemRead.Clear();
    }
}

void FrameworkTabHandler::Enable()
{
    AutoMutex a(iLock);
    iEnabled = true;
}

void FrameworkTabHandler::Send(ITabMessage& aMessage)
{
    // Blocks until message can be sent.

    iSemWrite.Wait();
    AutoMutex a(iLock);
    if (!iEnabled) {
        aMessage.Destroy();
        iSemWrite.Signal(); // Dropped message instead of putting in FIFO, so can just resignal.
    }
    else {
        iFifo.Write(&aMessage);
        // Only need to signal first message going into queue.
        if (iFifo.SlotsUsed() == 1) {
            iSemRead.Signal();
        }
    }
}

void FrameworkTabHandler::Complete()
{
    iSemRead.Signal();
}

void FrameworkTabHandler::WriteMessagesLocked(IWriter& aWriter)
{
    // This writes nothing if there are no messages to be sent.
    TBool msgOutput = false;
    while (iFifo.SlotsUsed() > 0) {
        ITabMessage* msg = iFifo.Read();
        try {
            if (!msgOutput) {
                aWriter.Write(Brn("["));
                msgOutput = true;
            }

            msg->Send(aWriter); // May throw WriterError.
            msg->Destroy();
            iSemWrite.Signal();

            // All but last msg should be followed by "," in a JSON array.
            if (iFifo.SlotsUsed() > 0) {
                aWriter.Write(Brn(","));
            }
        }
        catch (const WriterError&) {
            // Destroy msg and rethrow so that higher level can take
            // appropriate action.
            msg->Destroy();
            iSemWrite.Signal();

            // Empty remaining messages from FIFO.
            while (iFifo.SlotsUsed() > 0) {
                ITabMessage* msgDiscard = iFifo.Read();
                msgDiscard->Destroy();
                iSemWrite.Signal(); // Unblock any Send() calls.
            }

            throw;
        }
    }

    // Doesn't matter if this throws WriterError here, as FIFO has been emptied so nothing to clean up.
    if (msgOutput) {
        aWriter.Write(Brn("]"));
    }
}


// FrameworkTimer

FrameworkTimer::FrameworkTimer(Environment& aEnv, const TChar* aStringId, TUint aNumericId)
    : iStringId(aStringId)
    , iNumericId(aNumericId)
    , iTimer(aEnv, MakeFunctor(*this, &FrameworkTimer::Complete), "WebUiTimer")
    , iHandler(nullptr)
    , iLock("FRTL")
{
    // Make use of iStringId and iNumericId to prevent compilers complaining about unused members.
    ASSERT(strlen(iStringId) > 0);
    ASSERT(iNumericId >= 0);
}

FrameworkTimer::~FrameworkTimer()
{
    iTimer.Cancel();
}

void FrameworkTimer::Start(TUint aDurationMs, IFrameworkTimerHandler& aHandler)
{
    //LOG(kHttp, "FrameworkTimer::Start iStringId: %s, iNumericId: %u, aDurationMs: %u\n", iStringId, iNumericId, aDurationMs);
    AutoMutex a(iLock);
    ASSERT(iHandler == nullptr);
    iHandler = &aHandler;
    iTimer.FireIn(aDurationMs);
}

void FrameworkTimer::Cancel()
{
    //LOG(kHttp, "FrameworkTimer::Cancel iStringId: %s, iNumericId: %u\n", iStringId, iNumericId);
    TBool nullHandler;
    {
        AutoMutex a(iLock);
        nullHandler = (iHandler == nullptr);
    } //Mutex must be split over two blocks, because iTimer has callback mutex

    if (!nullHandler) {
        iTimer.Cancel();
    }

    {
        AutoMutex b(iLock);
        iHandler = nullptr;
    }
}

void FrameworkTimer::Complete()
{
    //LOG(kHttp, "FrameworkTimer::Complete: iStringId: %s, iNumericId: %u\n", iStringId, iNumericId);
    IFrameworkTimerHandler* handler = nullptr;
    {
        AutoMutex a(iLock);
        ASSERT(iHandler != nullptr);
        handler = iHandler;
        iHandler = nullptr;
    }
    handler->Complete();    // Avoid issues with attempted recursive locks on mutex if client calls back into Start()/Cancel() during callback.
}


// FrameworkSemaphore

FrameworkSemaphore::FrameworkSemaphore(const TChar* aName, TUint aCount)
    : iSem(aName, aCount)
{
}

void FrameworkSemaphore::Wait()
{
    iSem.Wait();
}

TBool FrameworkSemaphore::Clear()
{
    return iSem.Clear();
}

void FrameworkSemaphore::Signal()
{
    iSem.Signal();
}


// TaskTimedCallback

TaskTimedCallback::TaskTimedCallback(TUint aTimeoutMs, ITimerFactory& aTimerFactory, IThreadPool& aThreadPool, ITabTimeoutObserver& aTabTimeoutObserver)
    : iTimeoutMs(aTimeoutMs)
    , iIdInvalid(IFrameworkTab::kInvalidTabId)
    , iId(iIdInvalid)
    , iTabTimeoutObserver(aTabTimeoutObserver)
{
    iThreadPoolHandle = aThreadPool.CreateHandle(MakeFunctor(*this, &TaskTimedCallback::TaskCallback), "TaskTimedCallback", ThreadPoolPriority::Low);
    iTimer = aTimerFactory.CreateTimer(MakeFunctor(*this, &TaskTimedCallback::TimerCallback), "TaskTimedCallback");
}

TaskTimedCallback::~TaskTimedCallback()
{
    delete iTimer;
    iThreadPoolHandle->Destroy();
}

void TaskTimedCallback::Start(TUint aId)
{
    if (iId != iIdInvalid) {
        ASSERTS();
    }
    //ASSERT(iId == iIdInvalid);  // ::Cancel() must have been called, or callback task must have run.
    iId = aId;
    iTimer->FireIn(iTimeoutMs);
}

void TaskTimedCallback::Cancel()
{
    iTimer->Cancel();
    iThreadPoolHandle->Cancel();    // Task has either run or been cancelled by time this returns.
    iId = iIdInvalid;
}

void TaskTimedCallback::TimerCallback()
{
    (void)iThreadPoolHandle->TrySchedule();
}

void TaskTimedCallback::TaskCallback()
{
    const TUint id = iId;
    if (id != iIdInvalid) {
        iTabTimeoutObserver.TabTimedOut(id);    // No exceptions thrown by this method.
    }
    iId = iIdInvalid;   // Only clear iId when task has completed, so that Start() can't be successfully called mid-task.
}


// FrameworkTab

FrameworkTab::FrameworkTab(TUint aTabId, IFrameworkTabHandler& aTabHandler)
    : iTabId(aTabId)
    , iHandler(aTabHandler)
    , iSessionId(kInvalidTabId)
    , iDestroyHandler(nullptr)
    , iTab(nullptr)
    , iPollActive(false)
    , iLock("FRTL")
    , iRefCount(0)
{
}

FrameworkTab::~FrameworkTab()
{
    ASSERT(iRefCount == 0);
}

TUint FrameworkTab::SessionId() const
{
    AutoMutex a(iLock);
    return iSessionId;
}

void FrameworkTab::Initialise(TUint aSessionId, ITabCreator& aTabCreator, ITabDestroyHandler& aDestroyHandler, const std::vector<char*>& aLanguages)
{
    LOG(kHttp, "FrameworkTab::CreateTab iSessionId: %u, iTabId: %u\n", iSessionId, iTabId);
    ASSERT(aSessionId != kInvalidTabId);
    // AutoMutex a(iLock);
    ASSERT(iTab == nullptr);
    iSessionId = aSessionId;
    iDestroyHandler = &aDestroyHandler;
    iHandler.Enable();          // Ensure TabHandler is ready to receive messages (and not drop them).
    iLanguages.clear();
    for (auto it=aLanguages.begin(); it!=aLanguages.end(); ++it) {
        Bws<10> lang(*it);
        for (TUint i=0; i<lang.Bytes(); i++) {
            lang[i] = Ascii::ToLowerCase(lang[i]);
        }
        iLanguages.push_back(lang);
    }
    try {
        iTab = &aTabCreator.Create(iHandler, iLanguages);
        ASSERT(iTab != nullptr);
        // iTimer.Start(iPollTimeoutMs, *this);
    }
    catch (TabAllocatorFull&) {
        iHandler.Disable();
        iSessionId = kInvalidTabId;
        iDestroyHandler = nullptr;
        iLanguages.clear();
        throw;
    }
    iRefCount++;
}

void FrameworkTab::AddRef()
{
    iRefCount++;
}

void FrameworkTab::RemoveRef()
{
    ASSERT(iRefCount != 0);
    const TBool clear = (--iRefCount == 0);
    if (clear) {
        Clear();
        iDestroyHandler->Destroy(this);
    }
}

void FrameworkTab::LongPoll(IWriter& aWriter)
{
    {
        AutoMutex a(iLock);
        ASSERT(iTab != nullptr);

        if (iPollActive) {
            THROW(WebAppLongPollInProgress);
        }
        iPollActive = true;
    }
    iHandler.LongPoll(aWriter);

    // Will only reach here if blocking send isn't terminated (i.e., tab is still active).
    AutoMutex a(iLock);
    iPollActive = false;
}

void FrameworkTab::Interrupt()
{
    // Can't be uninterrupted; destroying and creating new tab clears interrupted state.
    iHandler.Disable();
}

void FrameworkTab::Receive(const Brx& aMessage)
{
    AutoMutex a(iLock);
    ASSERT(iTab != nullptr);
    iTab->Receive(aMessage);
}

void FrameworkTab::Send(ITabMessage& aMessage)
{
    {
        AutoMutex a(iLock);
        ASSERT(iTab != nullptr);
    }

    // Can't lock here. If message queue is full, could cause deadlock.
    iHandler.Send(aMessage);
}

void FrameworkTab::Clear()
{
    // This should only be called from RemoveRef() call when ref count reaches 0, so safe to not hold mutex here.
    // ASSERT(iTab != nullptr);
    if (iTab == nullptr) {
        ASSERTS();
    }
    iHandler.Disable();   // Should reject/drop any further calls to Send() from ITab.
    iSessionId = kInvalidTabId;
    iTab->Destroy();
    iTab = nullptr;
    iLanguages.clear();
}


// FrameworkTabFull

FrameworkTabFull::FrameworkTabFull(Environment& aEnv, TUint aTabId, TUint aSendQueueSize, TUint aSendTimeoutMs)
    : iSemRead("FTSR", 0)
    , iSemWrite("FTSW", aSendQueueSize)
    , iTabHandlerTimer(aEnv, "TabHandlerTimer", aTabId)
    , iTabHandler(iSemRead, iSemWrite, iTabHandlerTimer, aSendQueueSize, aSendTimeoutMs)
    , iTab(aTabId, iTabHandler)
    , iDestroyHandler(nullptr)
{
}

TUint FrameworkTabFull::SessionId() const
{
    return iTab.SessionId();
}

void FrameworkTabFull::Initialise(TUint aSessionId, ITabCreator& aTabCreator, ITabDestroyHandler& aDestroyHandler, const std::vector<char*>& aLanguages)
{
    iDestroyHandler = &aDestroyHandler;
    iTab.Initialise(aSessionId, aTabCreator, *this, aLanguages);
}

void FrameworkTabFull::AddRef()
{
    iTab.AddRef();
}

void FrameworkTabFull::RemoveRef()
{
    iTab.RemoveRef();
}

void FrameworkTabFull::Receive(const Brx& aMessage)
{
    iTab.Receive(aMessage);
}

void FrameworkTabFull::LongPoll(IWriter& aWriter)
{
    iTab.LongPoll(aWriter);
}

void FrameworkTabFull::Interrupt()
{
    iTab.Interrupt();
}

void FrameworkTabFull::Destroy(IFrameworkTab* aTab)
{
    ASSERT(aTab == &iTab);
    // This owns aTab. Do nothing more with it here.
    ASSERT(iDestroyHandler != nullptr);
    iDestroyHandler->Destroy(this);
    iDestroyHandler = nullptr;
}


// TabManager

TabManager::TabManager(const std::vector<IFrameworkTab*>& aTabs)
    : iTabsInactive(aTabs.size())
    , iNextSessionId(IFrameworkTab::kInvalidTabId+1)
    , iEnabled(true)
    , iLock("TBML")
{
    iTabsActive.reserve(aTabs.size());
    for (auto* t : aTabs) {
        iTabsInactive.Write(t);
    }
}

TabManager::~TabManager()
{
    AutoMutex amx(iLock);
    ASSERT(!iEnabled);                  // Disable() must have been called.
    ASSERT(iTabsActive.size() == 0);    // All tabs must have been made inactive by this point.
    while (iTabsInactive.SlotsUsed() > 0) {
        auto* t = iTabsInactive.Read();
        delete t;
    }
}

void TabManager::Disable()
{
    std::vector<IFrameworkTab*> tabs;
    {
        AutoMutex amx(iLock);
        iEnabled = false;   // Invalidate all future calls to TabManager.

        tabs = iTabsActive;
        iTabsActive.clear();
    }
    for (auto t : tabs) {
        t->Interrupt();
        t->RemoveRef();
    }
}

TUint TabManager::CreateTab(ITabCreator& aTabCreator, const std::vector<char*>& aLanguageList)
{
    AutoMutex amx(iLock);
    if (!iEnabled) {
        THROW(TabManagerFull);
    }

    if (iTabsInactive.SlotsUsed() == 0) {
        THROW(TabManagerFull);
    }
    auto* t = iTabsInactive.Read();
    try {
        const TUint sessionId = iNextSessionId;
        // Adds ref.
        t->Initialise(sessionId, aTabCreator, *this, aLanguageList); // Takes ownership of (buffers in) language list.
        iTabsActive.push_back(t);

        // Tab successfully initialised. Increment iNextSessionId.
        iNextSessionId++;
        return sessionId;
    }
    catch (const TabAllocatorFull&) {
        // Problem while calling IFrameworkTab::Initialise().
        iTabsInactive.Write(t);
        throw;
    }
}

void TabManager::LongPoll(TUint aId, IWriter& aWriter)
{
    if (aId == IFrameworkTab::kInvalidTabId) {
        LOG(kHttp, "TabManager::LongPoll kInvalidTabId\n");
        THROW(InvalidTabId);
    }

    LOG(kHttp, "TabManager::LongPoll aId: %u\n", aId);
    IFrameworkTab* tab = nullptr;
    {
        AutoMutex amx(iLock);
        if (!iEnabled) {
            THROW(InvalidTabId);
        }

        for (auto* t : iTabsActive) {
            if (t->SessionId() == aId) {
                t->AddRef();
                tab = t;
                break;
            }
        }
    }
    if (tab == nullptr) {
        THROW(InvalidTabId);
    }
    try {
        tab->LongPoll(aWriter);
    }
    catch (const Exception&) {
        tab->RemoveRef();
        throw;
    }
    tab->RemoveRef();   // Note: iLock not held, so tab must do internal locking.
}

void TabManager::Receive(TUint aId, const Brx& aMessage)
{
    if (aId == IFrameworkTab::kInvalidTabId) {
        LOG(kHttp, "TabManager::Receive kInvalidTabId, aMessage: %.*s\n", PBUF(aMessage));
        THROW(InvalidTabId);
    }

    LOG(kHttp, "TabManager::Receive aId: %u\n", aId);
    IFrameworkTab* tab = nullptr;
    {
        AutoMutex amx(iLock);
        if (!iEnabled) {
            THROW(InvalidTabId);
        }

        for (auto* t : iTabsActive) {
            if (t->SessionId() == aId) {
                t->AddRef();
                tab = t;
                break;
            }
        }
    }
    if (tab == nullptr) {
        THROW(InvalidTabId);
    }
    tab->Receive(aMessage);
    tab->RemoveRef();
}

void TabManager::DestroyTab(TUint aId)
{
    LOG(kHttp, "TabManager::DestroyTab aId: %u\n", aId);
    if (aId == IFrameworkTab::kInvalidTabId) {
        THROW(InvalidTabId);
    }

    IFrameworkTab* tab = nullptr;
    {
        AutoMutex amx(iLock);
        if (!iEnabled) {
            THROW(InvalidTabId);
        }

        for (auto it = iTabsActive.begin(); it != iTabsActive.end(); ++it) {
            if ((*it)->SessionId() == aId) {
                // No need to add ref here; going to remove ref that was added in ::CreateTab() call.
                tab = *it;
                iTabsActive.erase(it);
                break;
            }
        }
    }
    if (tab == nullptr) {
        THROW(InvalidTabId);
    }
    tab->Interrupt();
    tab->RemoveRef(); // Ref added in ::CreateTab() call.
}

void TabManager::Destroy(IFrameworkTab* aTab)
{
    AutoMutex amx(iLock);
    iTabsInactive.Write(aTab);
}


// TabManagerTimed::Timeout

const TUint TabManagerTimed::Timeout::kIdInvalid;

TabManagerTimed::Timeout::Timeout(TUint aPollTimeoutMs, ITimerFactory& aTimerFactory, IThreadPool& aThreadPool, ITabTimeoutObserver& aTabTimeoutObserver)
    : iId(kIdInvalid)
    , iTimer(aPollTimeoutMs, aTimerFactory, aThreadPool, aTabTimeoutObserver)
{
}

TUint TabManagerTimed::Timeout::Id() const
{
    return iId;
}

TaskTimedCallback& TabManagerTimed::Timeout::Timer()
{
    return iTimer;
}

void TabManagerTimed::Timeout::SetId(TUint aId)
{
    iId = aId;
}


// TabManagerTimed

TabManagerTimed::TabManagerTimed(const std::vector<IFrameworkTab*>& aTabs, TUint aPollTimeoutMs, ITimerFactory& aTimerFactory, IThreadPool& aThreadPool)
    : iTabManager(aTabs)
    , iTimeoutsInactive(aTabs.size())
    , iLock("TMTL")
{
    iTimeoutsActive.reserve(aTabs.size());
    for (TUint i = 0; i < aTabs.size(); i++) {
        iTimeoutsInactive.Write(new Timeout(aPollTimeoutMs, aTimerFactory, aThreadPool, *this));
    }
}

TabManagerTimed::~TabManagerTimed()
{
    ASSERT(iTimeoutsActive.size() == 0);
    while (iTimeoutsInactive.SlotsUsed() > 0) {
        delete iTimeoutsInactive.Read();
    }
}

void TabManagerTimed::Disable()
{
    AutoMutex amx(iLock);
    for (auto* t : iTimeoutsActive) {
        t->Timer().Cancel();
        t->SetId(Timeout::kIdInvalid);
        iTimeoutsInactive.Write(t);
    }
    iTimeoutsActive.clear();
    iTabManager.Disable();
}

TUint TabManagerTimed::CreateTab(ITabCreator& aTabCreator, const std::vector<char*>& aLanguageList)
{
    AutoMutex amx(iLock);
    const auto tabId = iTabManager.CreateTab(aTabCreator, aLanguageList); // May throw exception (so will not progress to below code).
    ASSERT(iTimeoutsInactive.SlotsUsed() > 0);
    ASSERT(tabId != Timeout::kIdInvalid);

    auto* timeout = iTimeoutsInactive.Read();
    timeout->SetId(tabId);
    iTimeoutsActive.push_back(timeout);
    timeout->Timer().Start(tabId);
    return tabId;
}

void TabManagerTimed::LongPoll(TUint aId, IWriter& aWriter)
{
    {
        AutoMutex amx(iLock);
        for (auto* timeout : iTimeoutsActive) {
            if (timeout->Id() == aId) {
                timeout->Timer().Cancel();
                break;
            }
        }
    }
    iTabManager.LongPoll(aId, aWriter); // May throw exception (so will not attempt to start timer below).

    // Tab may have been deallocated between cancelling timer above and here, so need to check if timeout still exists.
    {
        AutoMutex amx(iLock);
        for (auto* timeout : iTimeoutsActive) {
            if (timeout->Id() == aId) {
                timeout->Timer().Start(aId);
                break;
            }
        }
    }
}

void TabManagerTimed::Receive(TUint aId, const Brx& aMessage)
{
    iTabManager.Receive(aId, aMessage);
}

void TabManagerTimed::DestroyTab(TUint aId)
{
    DestroyTab(aId, true);
}

void TabManagerTimed::TabTimedOut(TUint aId)
{
    // Callback from timeout, so don't manipulate timeout methods that would acquire a lock within timeout here.
    DestroyTab(aId, false);
}

void TabManagerTimed::DestroyTab(TUint aId, TBool aCancelTimeout)
{
    {
        AutoMutex amx(iLock);
        for (auto it = iTimeoutsActive.begin(); it != iTimeoutsActive.end(); ++it) {
            auto* timeout = (*it);
            if (timeout->Id() == aId) {
                if (aCancelTimeout) {
                    timeout->Timer().Cancel(); // Safe to do, as long as this call didn't originate in timeout callback (otherwise deadlock will occur).
                }
                timeout->SetId(Timeout::kIdInvalid);
                iTimeoutsActive.erase(it);
                iTimeoutsInactive.Write(timeout);
                break;
            }
        }
        // Couldn't find timeout with given aId. Fall through to TabManager::DestroyTab() call below.
    }
    iTabManager.DestroyTab(aId);
}


// WebAppFramework::BrxPtrCmp

TBool WebAppFramework::BrxPtrCmp::operator()(const Brx* aStr1, const Brx* aStr2) const
{
    return BufferCmp()(*aStr1, *aStr2);
}


// WebAppInternal

WebAppInternal::WebAppInternal(IWebApp* aWebApp, FunctorPresentationUrl aFunctor)
    : iWebApp(aWebApp)
    , iFunctor(aFunctor)
{
}

WebAppInternal::~WebAppInternal()
{
    delete iWebApp;
}

void WebAppInternal::SetPresentationUrl(const Brx& aPresentationUrl)
{
    iFunctor(aPresentationUrl);
}

IResourceHandler* WebAppInternal::CreateResourceHandler(const Brx& aResource)
{
    return iWebApp->CreateResourceHandler(aResource);
}

ITab& WebAppInternal::Create(ITabHandler& aHandler, const std::vector<Bws<10>>& aLanguageList)
{
    return iWebApp->Create(aHandler, aLanguageList);
}

const Brx& WebAppInternal::ResourcePrefix() const
{
    return iWebApp->ResourcePrefix();
}


// WebAppFrameworkInitParams

WebAppFrameworkInitParams::WebAppFrameworkInitParams()
    : iPort(kDefaultPort)
    , iThreadResourcesCount(kDefaultMinServerThreadsResources)
    , iThreadLongPollCount(kDefaultMaxServerThreadsLongPoll)
    , iSendQueueSize(kDefaultSendQueueSize)
    , iSendTimeoutMs(kDefaultSendTimeoutMs)
    , iLongPollTimeoutMs(kDefaultLongPollTimeoutMs)
{
}

void WebAppFrameworkInitParams::SetServerPort(TUint aPort)
{
    iPort = aPort;
}

void WebAppFrameworkInitParams::SetMinServerThreadsResources(TUint aThreadResourcesCount)
{
    iThreadResourcesCount = aThreadResourcesCount;
}

void WebAppFrameworkInitParams::SetMaxServerThreadsLongPoll(TUint aThreadLongPollCount)
{
    iThreadLongPollCount = aThreadLongPollCount;
}

void WebAppFrameworkInitParams::SetSendQueueSize(TUint aSendQueueSize)
{
    iSendQueueSize = aSendQueueSize;
}

void WebAppFrameworkInitParams::SetSendTimeoutMs(TUint aSendTimeoutMs)
{
    iSendTimeoutMs = aSendTimeoutMs;
}

void WebAppFrameworkInitParams::SetLongPollTimeoutMs(TUint aLongPollTimeoutMs)
{
    iLongPollTimeoutMs = aLongPollTimeoutMs;
}

TUint WebAppFrameworkInitParams::Port() const
{
    return iPort;
}

TUint WebAppFrameworkInitParams::MinServerThreadsResources() const
{
    return iThreadResourcesCount;
}

TUint WebAppFrameworkInitParams::MaxServerThreadsLongPoll() const
{
    return iThreadLongPollCount;
}

TUint WebAppFrameworkInitParams::SendQueueSize() const
{
    return iSendQueueSize;
}

TUint WebAppFrameworkInitParams::SendTimeoutMs() const
{
    return iSendTimeoutMs;
}

TUint WebAppFrameworkInitParams::LongPollTimeoutMs() const
{
    return iLongPollTimeoutMs;
}


// WebAppFramework

const TChar* WebAppFramework::kName("WebUiServer");
const TChar* WebAppFramework::kAdapterCookie("WebAppFramework");
const Brn WebAppFramework::kSessionPrefix("WebUiSession");

WebAppFramework::WebAppFramework(Environment& aEnv, WebAppFrameworkInitParams* aInitParams, IThreadPool& aThreadPool)
    : iEnv(aEnv)
    , iInitParams(aInitParams)
    , iServer(nullptr)
    , iDefaultApp(nullptr)
    , iStarted(false)
    , iCurrentAdapter(nullptr)
    , iMutex("webapp")
{
    ASSERT(iInitParams != nullptr);

    // A server isn't much use without any serving threads.
    ASSERT(iInitParams->MinServerThreadsResources() > 0);
    ASSERT(iInitParams->MaxServerThreadsLongPoll() > 0);

    // Create iMaxLongPollServerThreads tabs. From now on in, the TabManager
    // will enforce the limitations by refusing to create new tabs when its tab
    // limit is exhausted.
    // (Similarly, if a request comes in for a tab that isn't in the TabManager
    // it will be immediately rejected, therefore not blocking any thread.)
    std::vector<IFrameworkTab*> tabs;

    for (TUint i=0; i<iInitParams->MaxServerThreadsLongPoll(); i++) {
        tabs.push_back(new FrameworkTabFull(aEnv, i, iInitParams->SendQueueSize(), iInitParams->SendTimeoutMs()));
    }
    //iTabManager = new TabManager(tabs); // Takes ownership.
    TimerFactory timerFactory(aEnv);
    iTabManager = new TabManagerTimed(tabs, iInitParams->LongPollTimeoutMs(), timerFactory, aThreadPool); // Takes ownership of tabs.

    Functor functor = MakeFunctor(*this, &WebAppFramework::CurrentAdapterChanged);
    NetworkAdapterList& nifList = iEnv.NetworkAdapterList();
    iAdapterListenerId = nifList.AddCurrentChangeListener(functor, "WebAppFramework", false);

    CurrentAdapterChanged();    // Force to set iCurrentAdapter, as not called at point of subscription.
    // no need to call AddSessions() - this happens inside CurrentAdapterChanged()
}

WebAppFramework::~WebAppFramework()
{
    NetworkAdapterList& nifList = iEnv.NetworkAdapterList();
    nifList.RemoveCurrentChangeListener(iAdapterListenerId);

    if (iCurrentAdapter != nullptr) {
        iCurrentAdapter->RemoveRef(kAdapterCookie);
    }

    // Terminate any blocking LongPoll() calls that server may have open and prevent further access/creation of tabs.
    iTabManager->Disable();

    // Don't allow any more web requests.
    delete iServer;

    // Delete TabManager before WebApps to allow it to free up any WebApp tabs that it may hold reference for.
    delete iTabManager;

    WebAppMap::iterator it;
    for (it=iWebApps.begin(); it!=iWebApps.end(); ++it) {
        // First elem is pointer to ref.
        delete it->second;
    }

    delete iInitParams;
}

void WebAppFramework::Start()
{
    AutoMutex amx(iMutex);
    ASSERT(iWebApps.size() > 0);
    ASSERT(!iStarted);
    iStarted = true;
    for (HttpSession& s : iSessions) {
        s.StartSession();
    }
}

TUint WebAppFramework::Port() const
{
    AutoMutex amx(iMutex);
    return iServer->Port();
}

TIpAddress WebAppFramework::Interface() const
{
    AutoMutex amx(iMutex);
    return iServer->Interface();
}

void WebAppFramework::SetDefaultApp(const Brx& aResourcePrefix)
{
    AutoMutex amx(iMutex);
    ASSERT(!iStarted);
    ASSERT(iDefaultApp == nullptr); // Don't want clashes in setting default app.
    WebAppMap::const_iterator it = iWebApps.find(&aResourcePrefix);
    if (it == iWebApps.cend()) {
        THROW(InvalidAppPrefix);
    }
    iDefaultApp = it->second;
}

void WebAppFramework::Add(IWebApp* aWebApp, FunctorPresentationUrl aFunctor)
{
    AutoMutex amx(iMutex);
    ASSERT(!iStarted);

    WebAppMap::const_iterator it = iWebApps.find(&aWebApp->ResourcePrefix());
    if (it != iWebApps.cend()) {
        ASSERTS(); // app with given resource prefix already exists
    }

    // Dynamic allocation here is acceptable as Start() hasn't been called and
    // class will persist for lifetime of WebAppFramework.
    WebAppInternal* webAppInternal = new WebAppInternal(aWebApp, aFunctor);
    iWebApps.insert(WebAppPair(&webAppInternal->ResourcePrefix(), webAppInternal));

    TIpAddress addr = iServer->Interface();
    if (addr == 0) {
        if (iCurrentAdapter != nullptr) {
            addr = iCurrentAdapter->Address();
        }
    }

    Bws<Uri::kMaxUriBytes> uri;
    uri.Append(":");
    Ascii::AppendDec(uri, iServer->Port());
    uri.Append("/");
    uri.Append(aWebApp->ResourcePrefix());
    uri.Append("/");
    //uri.Append(aWebApp->HomePage());
    uri.Append("index.html");   // FIXME - hard-coded info about webapp-specific resource!
                                // webapps are resource handlers (which may know how to redirect '/')
                                // so should probably be able to provide access to their own homepage
    webAppInternal->SetPresentationUrl(uri);
}

IWebApp& WebAppFramework::GetApp(const Brx& aResourcePrefix)
{
    AutoMutex amx(iMutex);
    ASSERT(iStarted);

    if (aResourcePrefix.Bytes() == 0 && iDefaultApp != nullptr) {
        return *iDefaultApp;
    }

    WebAppMap::const_iterator it = iWebApps.find(&aResourcePrefix);
    if (it == iWebApps.cend()) {
        THROW(InvalidAppPrefix);
    }

    return *(it->second);
}

IResourceHandler* WebAppFramework::CreateResourceHandler(const Brx& aResource)
{
    AutoMutex amx(iMutex);
    ASSERT(iStarted);
    Parser p(aResource);
    p.Next('/');    // skip leading '/'
    Brn prefix = p.Next('/');
    Brn tail = p.Next('?'); // Read up to query string (if any).

    if (prefix.Bytes() == 0) {
        if (iDefaultApp != nullptr) {
            return iDefaultApp->CreateResourceHandler(tail);
        }
        else {
            THROW(ResourceInvalid);
        }
    }

    WebAppMap::const_iterator it = iWebApps.find(&prefix);
    if (it == iWebApps.cend()) {
        // Didn't find an app with the given prefix.
        // Maybe it wasn't a prefix and was actually a URI tail for the default app.
        // Need to re-parse aResource in case there were multiple '/' in it.
        p.Set(aResource);
        p.Next('/');    // skip leading '/'
        tail.Set(p.Next('?')); // Read up to query string (if any).
        if (iDefaultApp != nullptr) {
            return iDefaultApp->CreateResourceHandler(tail);
        }
        THROW(ResourceInvalid);
    }

    IWebApp& app = *(it->second);
    return app.CreateResourceHandler(tail);
}

void WebAppFramework::AddSessions()
{
    for (TUint i=0; i<iInitParams->MinServerThreadsResources()+iInitParams->MaxServerThreadsLongPoll(); i++) {
        Bws<kMaxSessionNameBytes> name(kSessionPrefix);
        Ascii::AppendDec(name, i+1);
        auto* session = new HttpSession(iEnv, *this, *iTabManager, *this);
        iSessions.push_back(*session);
        iServer->Add(name.PtrZ(), session);
    }
}

void WebAppFramework::CurrentAdapterChanged()
{
    NetworkAdapterList& nifList = iEnv.NetworkAdapterList();
    NetworkAdapter* current = iEnv.NetworkAdapterList().CurrentAdapter(kAdapterCookie);
    // If no current adapter, choose first (if any) from subnet list.
    if (current == nullptr) {
        std::vector<NetworkAdapter*>* subnetList = nifList.CreateSubnetList();
        if (subnetList->size() > 0) {
            current = (*subnetList)[0];
            current->AddRef(kAdapterCookie);    // Add ref before destroying subnet list.
        }
        NetworkAdapterList::DestroySubnetList(subnetList);
    }

    // If "current" is not the same as "iCurrentAdapter" update iCurrentAdapter.
    AutoMutex amx(iMutex);
    if (iCurrentAdapter != current) {
        // Shouldn't need to clear any active tabs here.
        // Could potentially resume session. In the worst case, they will time out
        // and be recreated, which is what would otherwise be done here anyway.
        if (current != nullptr) {
            try {
                delete iServer;
                iServer = nullptr;
                iSessions.clear();
                const TUint port = iInitParams->Port();
                Endpoint::EndpointBuf epBuf;
                const Endpoint ep(port, current->Address());
                ep.AppendEndpoint(epBuf);
                Log::Print("WebAppFramework::CurrentAdapterChanged %.*s\n", PBUF(epBuf));
        
                iServer = new SocketTcpServer(iEnv, kName, port, current->Address());
                AddSessions();
                if (iStarted) {
                    for (HttpSession& s : iSessions) {
                        s.StartSession();
                    }
                }
            }
            catch (Exception& aExc) {
                Log::Print("WebAppFramework::CurrentAdapterChanged caught exception %s:%u %s\n", aExc.File(), aExc.Line(), aExc.Message());
                // Don't rethrow. Capture this exception and do nothing further, allowing any subsequent adapter change callbacks to successfully run.
            }
        }
        if (iCurrentAdapter != nullptr) {
            iCurrentAdapter->RemoveRef(kAdapterCookie);
        }
        iCurrentAdapter = current;  // Ref already added to current above; ref passes to iCurrentAdapter (if current != nullptr).
    }
    else {
        if (current != nullptr) {
            current->RemoveRef(kAdapterCookie); // current is not different from iCurrentAdapter. Remove reference from current.
            current = nullptr;
        }
    }
}


// WriterHttpResponseContentLengthUnknown

WriterHttpResponseContentLengthUnknown::WriterHttpResponseContentLengthUnknown(IWriter& aWriter)
    : iWriter(aWriter)
    , iWriterResponse(iWriter)
    , iWriterChunked(iWriter)
{
    // Default to HTTP/1.1, so set up for chunked output.
    iWriterChunked.SetChunked(true);
}

void WriterHttpResponseContentLengthUnknown::WriteHeader(Http::EVersion aVersion, HttpStatus aStatus, const Brx& aContentType)
{
    /*
     * In HTTP/1.0, if content length is not known, it appears to be valid to merely omit the content-length header in a response, and use the fact that the connection must be closed at the end of the response to identify end-of-response.
     *
     * In HTTP/1.1, chunking must be used if content-length is not known in advance.
     */
    ASSERT_VA(aVersion == Http::eHttp10 || aVersion == Http::eHttp11, "WriterHttpResponseContentLengthUnknown::WriteHeader aVersion: %u\n", aVersion);

    iWriterResponse.WriteStatus(aStatus, aVersion);

    if (aContentType.Bytes() > 0) {
        //iWriterResponse.WriteHeader(Http::kHeaderContentType, aContentType);
        IWriterAscii& writer = iWriterResponse.WriteHeaderField(Http::kHeaderContentType);
        writer.Write(aContentType);
        writer.WriteFlush();
    }

    if (aVersion == Http::eHttp11) {
        iWriterResponse.WriteHeader(Http::kHeaderTransferEncoding, Http::kTransferEncodingChunked);
        iWriterChunked.SetChunked(true);
    }
    else {
        iWriterChunked.SetChunked(false);
    }

    // Always going to close connection, regardless of HTTP/1.0 or HTTP/1.1.
    iWriterResponse.WriteHeader(Http::kHeaderConnection, Http::kConnectionClose);
    iWriterResponse.WriteFlush();
}

void WriterHttpResponseContentLengthUnknown::Write(TByte aValue)
{
    iWriterChunked.Write(aValue);
}

void WriterHttpResponseContentLengthUnknown::Write(const Brx& aBuffer)
{
    iWriterChunked.Write(aBuffer);
}

void WriterHttpResponseContentLengthUnknown::WriteFlush()
{
    iWriterChunked.WriteFlush();
}


// WriterLongPollResponse

WriterLongPollResponse::WriterLongPollResponse(WriterHttpResponseContentLengthUnknown& aWriter)
    : iWriter(aWriter)
{
}

void WriterLongPollResponse::WriteHeader(Http::EVersion aVersion)
{
    const Brn contentType("text/plain; charset=\"utf-8\"");
    iWriter.WriteHeader(aVersion, HttpStatus::kOk, contentType);
}

void WriterLongPollResponse::Write(TByte aValue)
{
    iWriter.Write(aValue);
}

void WriterLongPollResponse::Write(const Brx& aBuffer)
{
    iWriter.Write(aBuffer);
}

void WriterLongPollResponse::WriteFlush()
{
    iWriter.WriteFlush();
}


// WriterLongPollDelayed

WriterLongPollDelayed::WriterLongPollDelayed(WriterLongPollResponse& aWriter, Http::EVersion aVersion)
    : iWriter(aWriter)
    , iVersion(aVersion)
    , iStarted(false)
{
}

void WriterLongPollDelayed::Write(TByte aValue)
{
    WriteHeaderIfNotWritten();
    iWriter.Write(aValue);
}

void WriterLongPollDelayed::Write(const Brx& aBuffer)
{
    WriteHeaderIfNotWritten();
    iWriter.Write(aBuffer);
}

void WriterLongPollDelayed::WriteFlush()
{
    WriteHeaderIfNotWritten();
    iWriter.WriteFlush();
}

void WriterLongPollDelayed::WriteHeader()
{
    iWriter.WriteHeader(iVersion);
    iWriter.Write(Brn("lp\r\n"));
}

void WriterLongPollDelayed::WriteHeaderIfNotWritten()
{
    if (!iStarted) {
        iStarted = true;
        WriteHeader();
    }
}


// HttpSession

HttpSession::HttpSession(Environment& aEnv, IWebAppManager& aAppManager, ITabManager& aTabManager, IResourceManager& aResourceManager)
    : iAppManager(aAppManager)
    , iTabManager(aTabManager)
    , iResourceManager(aResourceManager)
    , iResponseStarted(false)
    , iResponseEnded(false)
    , iResourceWriterHeadersOnly(false)
    , iStarted(false)
    , iLock("HTSL")
{
    iReadBuffer = new Srs<kMaxRequestBytes>(*this);
    iReaderUntilPreChunker = new ReaderUntilS<kMaxRequestBytes>(*iReadBuffer);
    iReaderRequest = new ReaderHttpRequest(aEnv, *iReaderUntilPreChunker);
    iReaderChunked = new ReaderHttpChunked(*iReaderUntilPreChunker);
    iReaderUntil = new ReaderUntilS<kMaxRequestBytes>(*iReaderChunked);
    iWriterBuffer = new Sws<kMaxResponseBytes>(*this);
    iWriterResponse = new WriterHttpResponseContentLengthUnknown(*iWriterBuffer);
    iWriterResponseLongPoll = new WriterLongPollResponse(*iWriterResponse);

    iReaderRequest->AddMethod(Http::kMethodGet);
    iReaderRequest->AddMethod(Http::kMethodPost);
    iReaderRequest->AddMethod(Http::kMethodHead);

    iReaderRequest->AddHeader(iHeaderHost);
    iReaderRequest->AddHeader(iHeaderTransferEncoding);
    iReaderRequest->AddHeader(iHeaderConnection);
    iReaderRequest->AddHeader(iHeaderAcceptLanguage);
}

HttpSession::~HttpSession()
{
    delete iWriterResponseLongPoll;
    delete iWriterResponse;
    delete iWriterBuffer;
    delete iReaderUntil;
    delete iReaderChunked;
    delete iReaderRequest;
    delete iReaderUntilPreChunker;
    delete iReadBuffer;
}

void HttpSession::StartSession()
{
    AutoMutex a(iLock);
    iStarted = true;
}

void HttpSession::Run()
{
    // Try limit hammering of server from misbehaving clients or other bad actors.
    Thread::Sleep(kModerationTimeMs);

    iErrorStatus = &HttpStatus::kOk;
    iReaderRequest->Flush();
    iResourceWriterHeadersOnly = false;

    Http::EVersion version = Http::eHttp11; // Default to HTTP/1.1.

    // check headers
    try {
        try {
            iReaderRequest->Read(kReadTimeoutMs);
        }
        catch (HttpError&) {
            Error(HttpStatus::kBadRequest);
        }

        version = iReaderRequest->Version();
        if (version != Http::eHttp10 && version != Http::eHttp11) {
            LOG(kHttp, "HttpSession::Run Unsupported version: %u\n", version);
            // version is unknown, so set it to a version supported by this server so that iWriterResponse->WriteHeader() doesn't assert (as it only sends HTTP/1.0 and HTTP/1.1 responses).
            version = Http::eHttp11;
            Error(HttpStatus::kHttpVersionNotSupported);
        }

        /*
         * See:
         *
         * https://tools.ietf.org/html/rfc7230#section-5.4
         *
         * "A client MUST send a Host header field in all HTTP/1.1 request messages...A server MUST respond with a 400 (Bad Request) status code to any HTTP/1.1 request message that lacks a Host header field..."
         */
        if (version == Http::eHttp11) {
            if (!iHeaderHost.Received()) {
                Error(HttpStatus::kBadRequest);
            }
        }

        if (iReaderRequest->MethodNotAllowed()) {
            Error(HttpStatus::kMethodNotAllowed);
        }
        const Brx& method = iReaderRequest->Method();
        iReaderRequest->UnescapeUri();

        const Brx& uri = iReaderRequest->Uri();
        LOG(kHttp, "HttpSession::Run Method: %.*s, URI: %.*s\n", PBUF(method), PBUF(uri));

        iResponseStarted = false;
        iResponseEnded = false;

        iReaderChunked->SetChunked(iHeaderTransferEncoding.IsChunked());

        {
            /* TCP server is already active (and can't be temporarily deactivated),
            * even if !iStarted.
            * So, it is possible that, e.g., an existing web session, attempts to
            * access a web app before a new device with the same IP address is
            * fully initialised.
            * Report a 503 (Service Unavailable) in that case.
            */
            AutoMutex a(iLock);
            if (!iStarted) {
                THROW(WebAppServiceUnavailable);
            }
        }

        if (method == Http::kMethodGet) {
            Get();
        }
        else if (method == Http::kMethodHead) {
            iResourceWriterHeadersOnly = true;
            Get();
        }
        else if (method == Http::kMethodPost) {
            Post();
        }
    }
    catch (ResourceInvalid&) {
        if (iErrorStatus == &HttpStatus::kOk) {
            iErrorStatus = &HttpStatus::kNotFound;
        }
    }
    catch (HttpError&) {
        if (iErrorStatus == &HttpStatus::kOk) {
            iErrorStatus = &HttpStatus::kBadRequest;
        }
    }
    catch (ReaderError&) {
        if (iErrorStatus == &HttpStatus::kOk) {
            iErrorStatus = &HttpStatus::kBadRequest;
        }
    }
    catch (WebAppServiceUnavailable&) {
        iErrorStatus = &HttpStatus::kServiceUnavailable;
    }
    catch (WriterError&) {
    }

    try {
        if (!iResponseStarted) {
            if (iErrorStatus == &HttpStatus::kOk) {
                iErrorStatus = &HttpStatus::kNotFound;
            }
            iWriterResponse->WriteHeader(version, *iErrorStatus, Brx::Empty());
            iWriterResponse->WriteFlush();
            // FIXME - serve up some kind of error page in case browser does not display its own?
        }
        else if (!iResponseEnded) {
            iWriterResponse->WriteFlush();
        }
    }
    catch (WriterError&) {}
}

void HttpSession::Error(const HttpStatus& aStatus)
{
    iErrorStatus = &aStatus;
    THROW(HttpError);
}

void HttpSession::Get()
{
    // Try access requested resource.
    const Brx& uri = iReaderRequest->Uri();
    IResourceHandler* resourceHandler = iResourceManager.CreateResourceHandler(uri);    // throws ResourceInvalid

    try {
        Brn mimeType = MimeUtils::MimeTypeFromUri(uri);
        LOG(kHttp, "HttpSession::Get URI: %.*s  Content-Type: %.*s\n", PBUF(uri), PBUF(mimeType));

        // Write response headers.
        iResponseStarted = true;
        iWriterResponse->WriteHeader(iReaderRequest->Version(), HttpStatus::kOk, mimeType);

        // Write content.
        resourceHandler->Write(*iWriterResponse);
        iWriterResponse->WriteFlush(); // FIXME - move into iResourceWriter.Write()?
        resourceHandler->Destroy();
        iResponseEnded = true;
    }
    catch (Exception&) {
        // If ANY exception occurs need to free up resources.
        resourceHandler->Destroy();
        throw;
    }
}

void HttpSession::Post()
{
    const Brx& uri = iReaderRequest->Uri();
    Parser uriParser(uri);
    uriParser.Next('/');    // skip leading '/'
    Brn uriPrefix = uriParser.Next('/');
    Brn uriTail = uriParser.Next('?'); // Read up to query string (if any).
    const auto version = iReaderRequest->Version();

    // Try retrieve IWebApp using assumed prefix, in case it was actually the
    // URI tail and there is no prefix as the assumed app is the default app.
    try {
        (void)iAppManager.GetApp(uriPrefix);
    }
    catch (InvalidAppPrefix&) {
        // There was no app with the given uriPrefix, so maybe it's the default
        // app and the uriPrefix is actually uriTail.
        (void)iAppManager.GetApp(Brx::Empty());    // See if default app set.
        uriTail.Set(uriPrefix);         // Default app set, so assume uriPrefix is actually uriTail.
        uriPrefix.Set(Brx::Empty());    // Default app.
    }

    if (uriTail == Brn("lpcreate")) {
        try {
            IWebApp& app = iAppManager.GetApp(uriPrefix);
            const TUint id = iTabManager.CreateTab(app, iHeaderAcceptLanguage.LanguageList());
            iResponseStarted = true;
            iWriterResponseLongPoll->WriteHeader(version);
            Bws<Ascii::kMaxUintStringBytes> idBuf;
            Ascii::AppendDec(idBuf, id);
            iWriterResponseLongPoll->Write(Brn("lpcreate\r\n"));
            iWriterResponseLongPoll->Write(Brn("session-id: "));
            iWriterResponseLongPoll->Write(idBuf);
            iWriterResponseLongPoll->Write(Brn("\r\n"));
            iWriterResponseLongPoll->WriteFlush();
            iResponseEnded = true;
        }
        catch (InvalidAppPrefix&) {
            // FIXME - just respond with error instead of asserting? Yes - report a 404.

            // Programmer error/misuse by client.
            // Long-polling can only be initiated from a page served up by this framework (which implies that it must have a valid app prefix!).
            ASSERTS();
        }
        catch (TabAllocatorFull&) { // Thrown when an IWebApp fails to Create() a new tab, due to resource exhaustion in that particular app.
            Error(HttpStatus::kServiceUnavailable);
        }
        catch (TabManagerFull&) {   // Thrown when TabManager fails to Create() a new tab, due to long poll server thread exhaustion.
            Error(HttpStatus::kServiceUnavailable);
        }
        catch (InvalidTabId&) {
            ASSERTS();  // shouldn't happen - we've just created the tab
        }
    }
    else if (uriTail == Brn("lp")) {
        // Parse session-id and retrieve tab.
        Brn buf;
        // Don't rely on a content-length header (may be chunked encoding, or may be poor client implementation missing the header), as we know format of expected data.
        try {
            buf = Ascii::Trim(iReaderUntil->ReadUntil(Ascii::kLf));
        }
        catch (ReaderError&) {
            Error(HttpStatus::kBadRequest);
        }
        Parser p(buf);
        Brn sessionBuf = p.Next();
        if (sessionBuf == Brn("session-id:")) {
            sessionBuf = p.Next();
            TUint sessionId = 0;
            try {
                sessionId = Ascii::Uint(sessionBuf);
            }
            catch (AsciiError&) {
                // respond with bad request?
                Error(HttpStatus::kNotFound);
            }
            try {
                iResponseStarted = true;

                // Want to give 200 response iff tab is valid. Use special LongPollResponseWriter helper for that purpose.
                // If InvalidTabId is thrown (which should be done before any attempt to start writing data), WriterLongPollDelayed will not write any 200 response, and error handling code here can provide correct response code.

                // Long-polling holds connection open for 5s before returning. When using (HTTP/1.1) chunked encoding, if HTTP header is written immediately, and (message body and) last-chunk identifier is written after those 5, there is no problem. When using (HTTP/1.0) response with no content-length (because we do not know content-length in advance) and HTTP header is written immediately, many clients appear to close connection shortly after that, instead of waiting the 5s until this (returns an optional message body and) closes the socket. As the 5s delay is intended to be a way of rate-limiting polling calls, that is violated, and client will send next request immediately, resulting in what is essentially a denial-of-service attack.
                // So, use WriterLongPollDelayed to delay writing of HTTP header until there is a message body to send, or 5s timeout is reached.
                WriterLongPollDelayed writer(*iWriterResponseLongPoll, version);
                iTabManager.LongPoll(sessionId, writer);    // May write no data and can throw WriterError.
                // Tab was valid. Call LongPollResponseWriter::WriteFlush() to ensure data is output (and that headers are written!).
                writer.WriteFlush();
                iResponseEnded = true;
            }
            catch (const InvalidTabId&) {
                Error(HttpStatus::kNotFound);
            }
            catch (const WriterError&) {
                try {
                    iTabManager.DestroyTab(sessionId);
                }
                catch (InvalidTabId&) {
                    // Don't set error state to kNotFound.
                    // Just fall through to WriterError;
                    //Error(HttpStatus::kNotFound);
                }
                THROW(WriterError);
            }
            catch (const WebAppLongPollInProgress&) {
                // Long poll already in progress for given tab.

                // Do nothing for now.
                //Error(HttpStatus::kBadRequest);
            }
        }
        else {
            // No session request made.
            Error(HttpStatus::kBadRequest);
        }
    }
    else if (uriTail == Brn("lpterminate")) {
        // Parse session-id and retrieve tab.
        Brn buf;
        try {
            buf = Ascii::Trim(iReaderUntil->ReadUntil(Ascii::kLf));
        }
        catch (ReaderError&) {
            Error(HttpStatus::kBadRequest);
        }
        Parser p(buf);
        Brn sessionBuf = p.Next();
        if (sessionBuf == Brn("session-id:")) {
            sessionBuf = p.Next();
            TUint sessionId = 0;
            try {
                sessionId = Ascii::Uint(sessionBuf);
            }
            catch (AsciiError&) {
                Error(HttpStatus::kNotFound);
            }
            try {
                iTabManager.DestroyTab(sessionId);
                iResponseStarted = true;
                iWriterResponseLongPoll->WriteHeader(version);
                iWriterResponseLongPoll->WriteFlush();
                iResponseEnded = true;
            }
            catch (InvalidTabId&) {
                Error(HttpStatus::kNotFound);
            }
        }
        else {
            // No session request made.
            Error(HttpStatus::kBadRequest);
        }
    }
    else if (uriTail == Brn("update")) {
        // Parse session-id and retrieve tab.
        Brn buf;
        try {
            buf = Ascii::Trim(iReaderUntil->ReadUntil(Ascii::kLf));
        }
        catch (ReaderError&) {
            Error(HttpStatus::kBadRequest);
        }
        Parser p(buf);
        Brn sessionLine = p.NextLine();
        Parser sessionParser(sessionLine);
        Brn sessionTag = sessionParser.Next();
        if (sessionTag == Brn("session-id:")) {
            Brn sessionIdBuf = sessionParser.Next();
            TUint sessionId = 0;
            try {
                sessionId = Ascii::Uint(sessionIdBuf);
            }
            catch (AsciiError&) {
                Error(HttpStatus::kNotFound);
            }

            Brn update;
            // Read in rest of update request. Should be a single ConfigVal per request (so should fit in read buffer).
            try {
                update = Ascii::Trim(iReaderUntil->ReadUntil(Ascii::kLf));
            }
            catch (ReaderError&) {
                Error(HttpStatus::kBadRequest);
            }

            try {
                iTabManager.Receive(sessionId, update);
                iResponseStarted = true;
                iWriterResponse->WriteHeader(version, HttpStatus::kOk, Brx::Empty());
                iWriterResponse->WriteFlush();
                iResponseEnded = true;
            }
            catch (InvalidTabId&) {
                Error(HttpStatus::kNotFound);
            }
        }
        else {
            // No session request made.
            Error(HttpStatus::kBadRequest);
        }
    }
    else {
        Error(HttpStatus::kNotFound);
    }
}


// MimeUtils

const Brn MimeUtils::kExtCss("css");
const Brn MimeUtils::kExtJs("js");
const Brn MimeUtils::kExtXml("xml");
const Brn MimeUtils::kExtBmp("bmp");
const Brn MimeUtils::kExtGif("gif");
const Brn MimeUtils::kExtJpeg("jpeg");
const Brn MimeUtils::kExtPng("png");

Brn MimeUtils::MimeTypeFromUri(const Brx& aUri)
{ // static
    Parser p(aUri);
    Brn buf;
    while (!p.Finished()) {
        buf = p.Next('.');
    }

    if (Ascii::CaseInsensitiveEquals(buf, kExtCss)) {
        return Brn(kOhNetMimeTypeCss);
    }
    else if (Ascii::CaseInsensitiveEquals(buf, kExtJs)) {
        return Brn(kOhNetMimeTypeJs);
    }
    else if (Ascii::CaseInsensitiveEquals(buf, kExtXml)) {
        return Brn(kOhNetMimeTypeXml);
    }
    else if (Ascii::CaseInsensitiveEquals(buf, kExtBmp)) {
        return Brn(kOhNetMimeTypeBmp);
    }
    else if (Ascii::CaseInsensitiveEquals(buf, kExtGif)) {
        return Brn(kOhNetMimeTypeGif);
    }
    else if (Ascii::CaseInsensitiveEquals(buf, kExtJpeg)) {
        return Brn(kOhNetMimeTypeJpeg);
    }
    else if (Ascii::CaseInsensitiveEquals(buf, kExtPng)) {
        return Brn(kOhNetMimeTypePng);
    }
    // default to "text/html"
    return Brn(kOhNetMimeTypeHtml);
}
