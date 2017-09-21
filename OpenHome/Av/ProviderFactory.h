#pragma once

#include <OpenHome/Types.h>

namespace OpenHome {
namespace Configuration {
    class IConfigManager;
    class IConfigObservable;
    class IStoreReadWrite;
}
namespace Net {
    class DvDevice;
}
namespace Av {
    class Product;
    class IRebootHandler;

/**
 * DvProvider (the base class of all providers) does not have a publicly
 * accessible destructor, so the ProviderFactory cannot return pointers to that
 * type, as they cannot be deleted.
 *
 * This interface provides a virtual destructor for provider classes, allowing
 * this anonymous IProvider type to be returned from the ProviderFactory but,
 * crucially, also provides a public (virtual) destructor.
 */
class IProvider
{
public:
    virtual ~IProvider() {}
};

class ProviderFactory
{
public:
    static IProvider* NewConfiguration(Net::DvDevice& aDevice,
                                       Configuration::IConfigManager& aConfigManager);
    static IProvider* NewConfigApp(Net::DvDevice& aDevice,
                                   Configuration::IConfigManager& aConfigManager,
                                   Configuration::IConfigObservable& aConfigObservable,
                                   Configuration::IStoreReadWrite& aStore,
                                   IRebootHandler& aRebootHandler);
};

} // namespace Av
} // namespaceOpenHome

