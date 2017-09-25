#include <OpenHome/Av/Tidal/Tidal.h>
#include <OpenHome/Av/Tidal/TidalMetadata.h>
#include <OpenHome/Av/Tidal/TidalPins.h>
#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Types.h>
#include <OpenHome/SocketSsl.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Av/Utils/FormUrl.h>
#include <OpenHome/Json.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Net;
using namespace OpenHome::Configuration;

TidalPins::TidalPins(Tidal& aTidal, DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, CpStack& aCpStack)
    : iLock("TPIN")
    , iTidal(aTidal)
    , iJsonResponse(kJsonResponseChunks)
    , iTrackFactory(aTrackFactory)
    , iCpStack(aCpStack)
{
    CpDeviceDv* cpDevice = CpDeviceDv::New(iCpStack, aDevice);
    iCpPlaylist = new CpProxyAvOpenhomeOrgPlaylist1(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another
}

TidalPins::~TidalPins()
{
    delete iCpPlaylist;
}

TBool TidalPins::IsNumber(const Brx& aRequest) {
    for (TUint i = 0; i<aRequest.Bytes(); i++) {
        if (!Ascii::IsDigit(aRequest[0])) {
            return false;
        }
    }
    return true;
}

TBool TidalPins::LoadTracksByArtistId(const Brx& aArtistId, TUint aMaxTracks)
{
    AutoMutex _(iLock);
    TidalMetadata tm(iTrackFactory);
    TUint offset = 0;
    TUint total = aMaxTracks;
    TUint newId = 0;
    TUint currId = 0;
    TBool initPlay = false;
    JsonParser parser;
    iCpPlaylist->SyncDeleteAll();
    Bwh inputBuf(20);

    if (aArtistId.Bytes() == 0) {
        inputBuf.ReplaceThrow(Brn("3648083")); // working debug value: (Amy MacDonald, 207 tracks as of 25/09/2017)
    }
    else if (!IsNumber(aArtistId)) {
        iJsonResponse.Reset();
        TBool success = iTidal.TryGetArtistId(iJsonResponse, aArtistId);
        if (!success) {
            return false;
        }
        inputBuf.ReplaceThrow(tm.FirstIdFromJson(iJsonResponse.Buffer()));
        if (inputBuf.Bytes() == 0) {
            return false;
        }
    }
    else {
        inputBuf.ReplaceThrow(aArtistId);
    }

    while (offset < total) {
        try {
            iJsonResponse.Reset();
            TBool success = iTidal.TryGetTracksByArtistId(iJsonResponse, inputBuf, kTrackLimitPerRequest, offset);
            if (!success) {
                return false;
            }
            offset += kTrackLimitPerRequest;

            parser.Reset();
            parser.Parse(iJsonResponse.Buffer());
            TUint tracks = parser.Num(Brn("totalNumberOfItems"));
            if (tracks < total) {
                total = tracks;
            }
            auto parserItems = JsonParserArray::Create(parser.String(Brn("items")));

            try {
                for (;;) {
                    JsonParser parserItem;
                    auto* track = tm.TrackFromJson(parserItems.NextObject());
                    iCpPlaylist->SyncInsert(currId, (*track).Uri(), (*track).MetaData(), newId);
                    track->RemoveRef();
                    currId = newId;
                }
            }
            catch (JsonArrayEnumerationComplete&) {}
            if (!initPlay) {
                initPlay = true;
                iCpPlaylist->SyncPlay();
            }
        }
        catch (Exception&) {
            return false;
        }
        
    }
    return true;
}

TBool TidalPins::Test(const Brx& aType, const Brx& aInput, IWriterAscii& aWriter)
{
    if (aType == Brn("help")) {
        aWriter.Write(Brn("tidalpin_artist (input: Artist numeric ID or search string)"));
        aWriter.Write(Brn(" "));
        aWriter.WriteNewline(); // can't get this to work
        return true;
    }
    else if (aType == Brn("tidalpin_artist")) {
        LoadTracksByArtistId(aInput);
        aWriter.Write(Brn("Complete"));
        return true;
    }
    return false;
}