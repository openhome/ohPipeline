#ifndef HEADER_CPDEVICEODP
#define HEADER_CPDEVICEODP

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Core/CpDevice.h>
#include <OpenHome/Net/Core/FunctorCpDevice.h>

namespace OpenHome {
namespace Net {

class CpStack;

/**
 * CpDevice::GetAttribute supports the following keys for devices created by
 * one of the lists below:
 *  Location     - host portion of uri to the device
 *  FriendlyName - user displayable name for the device
 *  Type         - Mdns service type
 *  UglyName     - unique device name
 *
 * All key names are case sensitive
 */

/**
 * List of all Odp devices on the current subnet
 *
 * @see CpDeviceList
 * @ingroup ControlPoint
 */
class DllExportClass CpDeviceListOdpAll : public CpDeviceList
{
public:
    DllExport CpDeviceListOdpAll(CpStack& aCpStack, FunctorCpDevice aAdded, FunctorCpDevice aRemoved);
};

} // namespace Net
} // namespace OpenHome

#endif // HEADER_CPDEVICEODP
