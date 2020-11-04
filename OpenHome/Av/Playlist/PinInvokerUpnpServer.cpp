#include <OpenHome/Av/Playlist/PinInvokerUpnpServer.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Av/Pins/Pins.h>
#include <OpenHome/Av/Playlist/DeviceListMediaServer.h>
#include <OpenHome/Av/Playlist/TrackDatabase.h>
#include <OpenHome/Net/Core/CpDevice.h>
#include <OpenHome/Net/Core/FunctorCpDevice.h>
#include <Generated/CpUpnpOrgContentDirectory1.h>
#include <Generated/CpAvOpenhomeOrgPlaylist1.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>
#include <OpenHome/Net/Private/XmlParser.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Media/Debug.h>

#include <algorithm>
#include <atomic>
#include <vector>

using namespace OpenHome;
using namespace OpenHome::Net;
using namespace OpenHome::Av;

// PinInvokerUpnpServer

const TChar* PinInvokerUpnpServer::kMode = "upnp.cd";
const Brn PinInvokerUpnpServer::kModeBuf(kMode);
const TChar* PinInvokerUpnpServer::kQueryContainer = "id";
const TChar* PinInvokerUpnpServer::kQueryTrack = "trackId";
const Brn PinInvokerUpnpServer::kBrowseFilterAll("*");

PinInvokerUpnpServer::PinInvokerUpnpServer(CpStack& aCpStack,
                                           Net::DvDevice& aDevice,
                                           IThreadPool& aThreadPool,
                                           ITrackDatabase& aTrackDatabase,
                                           DeviceListMediaServer& aDeviceList)
    : iTrackDatabase(aTrackDatabase)
    , iDeviceList(aDeviceList)
    , iProxyContentDirectory(nullptr)
    , iSemDeviceFound("PiKS", 0)
    , iShuffle(false)
    , iCancel(false)
    , iTrackId(nullptr)
{
    iTphContainer = aThreadPool.CreateHandle(MakeFunctor(*this, &PinInvokerUpnpServer::ReadContainer),
                                             "PinInvokerUpnpServer-Container", ThreadPoolPriority::Medium);
    iTphTrack = aThreadPool.CreateHandle(MakeFunctor(*this, &PinInvokerUpnpServer::ReadTrack),
                                         "PinInvokerUpnpServer-Track", ThreadPoolPriority::Medium);
    auto cpDeviceSelf = CpDeviceDv::New(aCpStack, aDevice);
    iProxyPlaylist = new CpProxyAvOpenhomeOrgPlaylist1(*cpDeviceSelf);
    cpDeviceSelf->RemoveRef(); // iProxyPlaylist will have claimed a ref above
}

PinInvokerUpnpServer::~PinInvokerUpnpServer()
{
    delete iProxyPlaylist;
    delete iProxyContentDirectory; // from most recent pin invocation (if any)
    iTphTrack->Destroy();
    iTphContainer->Destroy();
}

void PinInvokerUpnpServer::BeginInvoke(const IPin& aPin, Functor aCompleted)
{
    if (aPin.Mode() != Brn(kMode)) {
        return;
    }

    delete iProxyContentDirectory; // from most recent pin invocation (if any)
    iProxyContentDirectory = nullptr;
    AutoPinComplete completion(aCompleted);
    iPinUri.Replace(aPin.Uri());
    iShuffle = aPin.Shuffle();
    Brn query(iPinUri.Query());
    query.Set(query.Split(1)); // queries being with '?' - we'd rather just deal with the query body
    Parser parser(query);
    iQueryKvps.clear();
    TBool complete = false;
    do {
        Brn key = parser.Next('=');
        Brn val = parser.Next('&');
        if (val.Bytes() == 0) {
            val.Set(parser.Remaining());
            complete = true;
        }
        iQueryKvps.push_back(std::pair<Brn, Brn>(key, val));

    } while (!complete);

    auto udn = FromQuery("udn");
    CpDevice* server = nullptr;
    iDeviceList.GetServerRef(udn, server, 5000);
    iProxyContentDirectory = new CpProxyUpnpOrgContentDirectory1(*server);
    server->RemoveRef();
    iPlaying = false;
    iTrackIdInsertAfter = ITrackDatabase::kTrackIdNone;
    IThreadPoolHandle* tph = nullptr;
    try {
        auto container = FromQuery(kQueryContainer);
        iContainers.push_back(new Brh(container));
        iContainersIndex = 0;
        tph = iTphContainer;
    }
    catch (PinUriError&) {
        auto track = FromQuery(kQueryTrack);
        iTrackId = new Brh(track);
        tph = iTphTrack;
    }
    completion.Cancel();
    iCompleted = aCompleted;
    (void)tph->TrySchedule();
}

void PinInvokerUpnpServer::Cancel()
{
    iCancel = true;
}

const TChar* PinInvokerUpnpServer::Mode() const
{
    return kMode;
}

TBool PinInvokerUpnpServer::SupportsVersion(TUint version) const
{
    return version >= kMinSupportedVersion && version <= kMaxSupportedVersion;
}


Brn PinInvokerUpnpServer::FromQuery(const TChar* aKey) const
{
    Brn key(aKey);
    Brn val;
    for (const auto& kvp : iQueryKvps) {
        if (kvp.first == key) {
            val.Set(kvp.second);
            break;
        }
    }
    if (val.Bytes() == 0) {
        LOG_ERROR(kPipeline, "PinInvokerUpnpServer - no %s in query - %.*s\n", aKey, PBUF(iPinUri.Query()));
        THROW(PinUriError);
    }
    return val;
}

TBool PinInvokerUpnpServer::IsCancelled()
{
    if (!iCancel) {
        return false;
    }
    Complete();
    iCancel = false;
    return true;
}

void PinInvokerUpnpServer::Complete()
{
    Log::Print("PinInvokerUpnpServer::Complete cancel=%u\n", iCancel.load());
    // would ideally delete iProxyContentDirectory here but can't because
    // this is sometimes called from action completion callback
    for (auto p : iContainers) {
        delete p;
    }
    iContainers.clear();
    delete iTrackId;
    iTrackId = nullptr;
    if (iCompleted) {
        iCompleted();
    }
}

void PinInvokerUpnpServer::ReadContainer()
{
    if (IsCancelled()) {
        return;
    }
    Brn container(*iContainers[iContainersIndex++]);
    //Log::Print("Container: %.*s, total=%u, index=%u\n", PBUF(container), iContainers.size(), iContainersIndex - 1);
    static const Brn kBrowseFlag("BrowseDirectChildren");
    auto callback = MakeFunctorAsync(*this, &PinInvokerUpnpServer::BrowseContainerCallback);
    iProxyContentDirectory->BeginBrowse(container, kBrowseFlag, kBrowseFilterAll, 0, 0, Brx::Empty(), callback);
}

void PinInvokerUpnpServer::ReadTrack()
{
    if (IsCancelled()) {
        return;
    }
    static const Brn kBrowseFlag("BrowseMetadata");
    auto callback = MakeFunctorAsync(*this, &PinInvokerUpnpServer::BrowseTrackCallback);
    iProxyContentDirectory->BeginBrowse(*iTrackId, kBrowseFlag, kBrowseFilterAll, 0, 0, Brx::Empty(), callback);
}

void PinInvokerUpnpServer::BrowseContainerCallback(IAsync& aAsync)
{
    if (IsCancelled()) {
        return;
    }

    Brh xml;
    TUint numberReturned;
    TUint totalMatches;
    TUint updateId;
    iProxyContentDirectory->EndBrowse(aAsync, xml, numberReturned, totalMatches, updateId);

    auto didl = XmlParserBasic::Find("DIDL-Lite", xml);
    auto items = didl;
    auto newContainers = false;
    try {
        for (;;) {
            auto container = XmlParserBasic::Element("container", didl, didl);
            auto child = XmlParserBasic::FindAttribute("container", "id", container);
            iContainers.push_back(new Brh(child));
            newContainers = true;
        }
    }
    catch (XmlError&) {
        Log::Print("BrowseContainerCallback - XmlError parsing %.*s\n", PBUF(didl));
    }

    TBool playlistFull = false;
    try {
        for (; !playlistFull;) {
            auto item = XmlParserBasic::Find("item", items, items);
            playlistFull = !TryAddItem(item);
        }
    }
    catch (XmlError&) {}

    if (newContainers && iShuffle) {
        std::random_shuffle(iContainers.begin() + iContainersIndex, iContainers.end());
    }

    if (iContainers.size() == iContainersIndex || playlistFull) {
        Complete();
    }
    else {
        (void)iTphContainer->TrySchedule();
    }
}

void PinInvokerUpnpServer::BrowseTrackCallback(IAsync& aAsync)
{
    if (IsCancelled()) {
        return;
    }
    AutoPinComplete _(iCompleted);

    Brh xml;
    TUint numberReturned;
    TUint totalMatches;
    TUint updateId;
    iProxyContentDirectory->EndBrowse(aAsync, xml, numberReturned, totalMatches, updateId);

    try {
        auto didl = XmlParserBasic::Find("DIDL-Lite", xml);
        auto item = XmlParserBasic::Find("item", didl, didl);
        (void)TryAddItem(item);
    }
    catch (XmlError&) {
        Log::Print("PinInvokerUpnpServer - XmlError parsing %.*s\n", PBUF(xml));
    }
}

TBool PinInvokerUpnpServer::TryAddItem(const Brx& aItemDidl)
{
    // clear previous playlist once we know we've found at least one track for this pin
    if (iTrackIdInsertAfter == ITrackDatabase::kTrackIdNone) {
        iTrackDatabase.DeleteAll();
    }

    static const Brn kDidlStart("<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\"><item>");
    static const Brn kDidlEnd("</item></DIDL-Lite>");
    auto trackUri = XmlParserBasic::Find("res", aItemDidl);
    iTrackMetadata.ReplaceThrow(kDidlStart);
    TryAddTag(aItemDidl, "title", Ns::Dc);
    TryAddTag(aItemDidl, "class", Ns::Upnp);
    TryAddTag(aItemDidl, "albumArtURI", Ns::Upnp);
    TryAddTag(aItemDidl, "album", Ns::Upnp);
    TryAddArtistTags(aItemDidl);
    TryAddTag(aItemDidl, "genre", Ns::Upnp);
    TryAddTag(aItemDidl, "date", Ns::Dc);
    try {
        auto res = XmlParserBasic::Element("res", aItemDidl);
        iTrackMetadata.AppendThrow(res);
    }
    catch (XmlError&) {}
    iTrackMetadata.AppendThrow(kDidlEnd);
    try {
        iTrackDatabase.Insert(iTrackIdInsertAfter, trackUri, iTrackMetadata, iTrackIdInsertAfter);
        if (!iPlaying) {
            FunctorAsync empty;
            iProxyPlaylist->BeginSetShuffle(iShuffle, empty);
            iProxyPlaylist->BeginPlay(empty);
            iPlaying = true;
        }
    }
    catch (TrackDbFull&) {
        return false;
    }
    return true;
}

void PinInvokerUpnpServer::TryAddTag(const Brx& aItemDidl, const TChar* aTag, Ns aNs)
{
    try {
        auto val = XmlParserBasic::Find(aTag, aItemDidl);
        TryAddTag(aTag, val, aNs, Brx::Empty());
    }
    catch (XmlError&) {}
}

void PinInvokerUpnpServer::TryAddArtistTags(const Brx& aItemDidl)
{
    Brn doc(aItemDidl);
    try {
        for (;;) {
            auto elem = XmlParserBasic::Element("artist", doc, doc);
            auto val = XmlParserBasic::Find("artist", elem);
            auto role = XmlParserBasic::FindAttribute("artist", "role", elem);
            TryAddTag("artist", val, Ns::Upnp, role);
        }
    }
    catch (XmlError&) {}
}

void PinInvokerUpnpServer::TryAddTag(const TChar* aTag, const Brx& aVal, Ns aNs, const Brx& aRole)
{
    static const Brn kTagDc("dc=\"http://purl.org/dc/elements/1.1/\"");
    static const Brn kTagUpnp("upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\"");
    iTrackMetadata.AppendThrow("<");
    if (aNs == Ns::Dc) {
        iTrackMetadata.AppendThrow("dc:");
    }
    else {
        iTrackMetadata.AppendThrow("upnp:");
    }
    iTrackMetadata.AppendThrow(aTag);
    iTrackMetadata.AppendThrow(" xmlns:");
    if (aNs == Ns::Dc) {
        iTrackMetadata.AppendThrow(kTagDc);
    }
    else {
        iTrackMetadata.AppendThrow(kTagUpnp);
    }
    if (aRole.Bytes() > 0) {
        iTrackMetadata.AppendThrow(" role=\"");
        iTrackMetadata.AppendThrow(aRole);
        iTrackMetadata.AppendThrow("\"");
    }
    iTrackMetadata.AppendThrow(">");
    iTrackMetadata.AppendThrow(aVal);
    iTrackMetadata.AppendThrow("</");
    if (aNs == Ns::Dc) {
        iTrackMetadata.AppendThrow("dc:");
    }
    else {
        iTrackMetadata.AppendThrow("upnp:");
    }
    iTrackMetadata.AppendThrow(aTag);
    iTrackMetadata.AppendThrow(">");
}
