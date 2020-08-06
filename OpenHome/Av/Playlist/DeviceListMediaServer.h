#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Uri.h>

#include <map>
#include <vector>

EXCEPTION(MediaServerNotFound);
EXCEPTION(PropertyServerNotFound);

namespace OpenHome {
    class Environment;
    namespace Net {
        class CpStack;
        class CpDevice;
        class CpDeviceList;
    }
namespace Av {

class DeviceListMediaServer
{
    static const Brn kDomainUpnp;
    static const Brn kServiceContentDirectory;
public:
    DeviceListMediaServer(Environment& aEnv, Net::CpStack& aCpStack);
    ~DeviceListMediaServer();
    void GetServerRef(const Brx& aUdn, Net::CpDevice*& aDevice, TUint aTimeoutMs);
    void GetPropertyServerUri(const Brx& aUdn, Bwx& aPsUri, TUint aTimeoutMs);
    void Cancel();
private:
    void DeviceAdded(Net::CpDevice& aDevice);
    void DeviceRemoved(Net::CpDevice& aDevice);
private:
    Environment & iEnv;
    Mutex iLock;
    Semaphore iSemAdded;
    Net::CpDeviceList* iDeviceList;
    std::map<Brn, Net::CpDevice*, BufferCmp> iMap;
    Uri iUri; // only used in GetPropertyServerUri but too large for the stack
    TBool iCancelled;
};

}
}
