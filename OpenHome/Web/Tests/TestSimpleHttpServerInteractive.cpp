#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Tests/Cdecl.h>
#include <OpenHome/Media/Tests/GetCh.h>
#include <OpenHome/Web/SimpleHttpServer.h>
#include <OpenHome/Web/ConfigUi/FileResourceHandler.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Private/OptionParser.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Configuration/ProviderConfig.h>
#include <OpenHome/Configuration/Tests/ConfigRamStore.h>

#include <stdlib.h>


using namespace OpenHome;
using namespace OpenHome::Net;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Web;


int CDECL main(int aArgc, char* aArgv[])
{
#ifdef _WIN32
    char* noErrDlgs = getenv("NO_ERROR_DIALOGS");
    if (noErrDlgs != nullptr && strcmp(noErrDlgs, "1") == 0) {
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    }
#endif // _WIN32

    // Parse command line args.
    OptionParser parser;
    OptionString optionDir("-d", "--root-dir", Brn("../OpenHome/Web/Tests/res/"), "Root directory for serving static files");
    parser.AddOption(&optionDir);

    if (!parser.Parse(aArgc, aArgv)) {
        return 1;
    }

    // Initialise ohNet.
    InitialisationParams* initParams = InitialisationParams::Create();
    Library* lib = new Library(initParams);
    DvStack* dvStack = lib->StartDv();

    // Set up the server.
    Debug::SetLevel(Debug::kHttp);

    static const TUint kPort = 0;  // bind to OS-assigned port
    static const TUint kMinResourceThreads = 1;
    FileResourceHandlerFactory factory;
    BlockingResourceManager* resourceManager = new BlockingResourceManager(factory, kMinResourceThreads, optionDir.Value());
    SimpleHttpServer* server = new SimpleHttpServer(*dvStack, *resourceManager, kPort);
    server->Start();

    Log::Print("\nTest Simple Http server\n");
    Log::Print("Root dir for static resources: ");
    Log::Print(optionDir.Value());
    Log::Print("\n");

    Log::Print("Press <q> followed by <enter> to quit:\n");
    Log::Print("\n");
    while (getchar() != 'q') {
        ;
    }

    // Shutdown.
    delete resourceManager;
    delete server;
    delete lib;

    return 0;
}
