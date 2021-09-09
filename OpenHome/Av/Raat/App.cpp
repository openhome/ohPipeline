#include <OpenHome/Av/Raat/App.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Functor.h>

#include <raat_device.h> 
#include <rc_guid.h>
#include <raat_base.h>

using namespace OpenHome;
using namespace OpenHome::Av;

RattApp::RattApp()
    : iThread("Raat", MakeFunctor(*this, &RattApp::RaatThread))
{
}

RattApp::~RattApp()
{
}

static void Raat_Log(RAAT__LogEntry* entry, void* /*userdata*/) {
    Log::Print("[%07d] %lld %s\n", entry->seq, entry->time, entry->message);
}

void RattApp::RaatThread()
{
    RAAT__static_init();
    RAAT__Log    *log;
    auto status = RAAT__log_new(RC__ALLOCATOR_DEFAULT, RAAT__LOG_DEFAULT_SIZE, &log);
    ASSERT(RC__STATUS_IS_SUCCESS(status));
    RAAT__log_add_callback(log, Raat_Log, NULL); // FIXME - redirect as per Log::Print

    RAAT__Device *device;
    status = RAAT__device_new(RC__ALLOCATOR_DEFAULT, log, &device);
    ASSERT(RC__STATUS_IS_SUCCESS(status));

    RAAT__Info   *info;
    info = RAAT__device_get_info(device);

    // FIXME - following info is all stubbed out
    status = RAAT__info_set(info, RAAT__INFO_KEY_UNIQUE_ID, "unique_id");
    ASSERT(RC__STATUS_IS_SUCCESS(status));
    status = RAAT__info_set(info, RAAT__INFO_KEY_VENDOR, "vendor");
    ASSERT(RC__STATUS_IS_SUCCESS(status));
    status = RAAT__info_set(info, RAAT__INFO_KEY_VENDOR_MODEL, "vendor_model");
    ASSERT(RC__STATUS_IS_SUCCESS(status));
    status = RAAT__info_set(info, RAAT__INFO_KEY_OUTPUT_NAME, "output_name");
    ASSERT(RC__STATUS_IS_SUCCESS(status));
    status = RAAT__info_set(info, RAAT__INFO_KEY_SERIAL, "serial_number");
    ASSERT(RC__STATUS_IS_SUCCESS(status));
    status = RAAT__info_set(info, RAAT__INFO_KEY_AUTO_NAME, "auto_name");
    ASSERT(RC__STATUS_IS_SUCCESS(status));
    status = RAAT__info_set(info, RAAT__INFO_KEY_MODEL, "model");
    ASSERT(RC__STATUS_IS_SUCCESS(status));
    status = RAAT__info_set(info, RAAT__INFO_KEY_VERSION, "version");
    ASSERT(RC__STATUS_IS_SUCCESS(status));

    status = RAAT__device_run(device);
    if (!RC__STATUS_IS_SUCCESS(status)) {
        Log::Print("RAAT server exited with error\n");
    }

}
