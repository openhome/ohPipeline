#include <OpenHome/Av/Playlist/DeviceListMediaServer.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Json.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Av/Pins/Pins.h>
#include <OpenHome/Av/OhMetadata.h>
#include <OpenHome/Av/Playlist/TrackDatabase.h>
#include <OpenHome/Net/Core/CpDevice.h>
#include <OpenHome/Net/Core/CpDeviceUpnp.h>
#include <OpenHome/Net/Core/FunctorCpDevice.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>
#include <Generated/CpAvOpenhomeOrgPlaylist1.h>
#include <OpenHome/Net/Private/XmlParser.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Media/Debug.h>

#include <map>
#include <vector>
#include <utility>

using namespace OpenHome;
using namespace OpenHome::Net;
using namespace OpenHome::Av;

// DeviceListMediaServer

const Brn DeviceListMediaServer::kDomainUpnp("upnp.org");
const Brn DeviceListMediaServer::kServiceContentDirectory("ContentDirectory");

DeviceListMediaServer::DeviceListMediaServer(Environment& aEnv, CpStack& aCpStack)
    : iEnv(aEnv)
    , iLock("DLKS")
    , iSemAdded("DLKS", 0)
    , iCancelled(false)
{
    auto added = MakeFunctorCpDevice(*this, &DeviceListMediaServer::DeviceAdded);
    auto removed = MakeFunctorCpDevice(*this, &DeviceListMediaServer::DeviceRemoved);
    iDeviceList = new CpDeviceListUpnpServiceType(aCpStack, kDomainUpnp, kServiceContentDirectory,
                                                  1, added, removed);
}

DeviceListMediaServer::~DeviceListMediaServer()
{
    delete iDeviceList;
    for (auto kvp : iMap) {
        kvp.second->RemoveRef();
    }
}

void DeviceListMediaServer::GetServerRef(const Brx& aUdn, CpDevice*& aDevice, TUint aTimeoutMs)
{
    auto timeout = Time::Now(iEnv) + aTimeoutMs;
    AutoMutex _(iLock);
    iSemAdded.Clear();
    Brn udn(aUdn);
    auto it = iMap.find(udn);
    while (it == iMap.end()) {
        auto waitTime = timeout - Time::Now(iEnv);
        if (waitTime == 0 || waitTime > timeout) {
            break;
        }
        iDeviceList->Refresh();
        iLock.Signal();
        TBool found = false;
        try {
            iSemAdded.Wait(waitTime);
            found = true;
        }
        catch (Timeout&) {}
        iLock.Wait();
        if (!found || iCancelled) {
            THROW(MediaServerNotFound);
        }
        it = iMap.find(udn);
    }
    if (it == iMap.end()) {
        THROW(MediaServerNotFound);
    }
    aDevice = it->second;
    aDevice->AddRef();
}

void DeviceListMediaServer::GetPropertyServerUri(const Brx& aUdn, Bwx& aPsUri, TUint aTimeoutMs)
{
    CpDevice* cpDevice = nullptr;
    GetServerRef(aUdn, cpDevice, aTimeoutMs);
    AutoRefCpDevice _(*cpDevice);
    Brh xml;
    if (!cpDevice->GetAttribute("Upnp.DeviceXml", xml)) {
        THROW(PropertyServerNotFound);
    }
    try {
        /* Note that the following would not work against all UPnP devices.
           The Media Endpoint API is complex and lightly documented so we assume that no-one
           but Linn will ever implement it ...and that Linn's implementation will continue to
           use ohNet, which formats its device XML in predictable ways.*/
        Brn root = XmlParserBasic::Find("root", xml);
        Brn device = XmlParserBasic::Find("device", root);
        Brn presUrl = XmlParserBasic::Find("presentationURL", root);
        Brn psRoot = XmlParserBasic::Find("X_PATH", device);

        iUri.Replace(presUrl);
        aPsUri.ReplaceThrow(iUri.Scheme());
        aPsUri.AppendThrow("://");
        aPsUri.AppendThrow(iUri.Host());
        aPsUri.Append(':');
        Ascii::AppendDec(aPsUri, iUri.Port());
        aPsUri.AppendThrow(psRoot);
    }
    catch (XmlError&) {
        THROW(PropertyServerNotFound);
    }
}

void DeviceListMediaServer::Cancel()
{
    AutoMutex _(iLock);
    iCancelled = true;
    iSemAdded.Signal();
}

void DeviceListMediaServer::DeviceAdded(CpDevice& aDevice)
{
    AutoMutex _(iLock);
    aDevice.AddRef();
    Brn udn(aDevice.Udn());
    iMap.insert(std::pair<Brn, CpDevice*>(udn, &aDevice));
    iSemAdded.Signal();
}

void DeviceListMediaServer::DeviceRemoved(CpDevice& aDevice)
{
    AutoMutex _(iLock);
    Brn udn(aDevice.Udn());
    auto it = iMap.find(udn);
    if (it != iMap.end()) {
        it->second->RemoveRef();
        iMap.erase(it);
    }
}
