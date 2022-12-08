#pragma once

#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Media/SupplyAggregator.h>

namespace OpenHome {
namespace Media {

class MsgFactory;
class IPipelineElementDownstream;

class IDRMProvider
{
public:
    virtual ~IDRMProvider() { }

    // Returns 'true' if this provider should handle the data. It's assumed that most DRM providers
    // will need to have been setup & configured by upstream components or content processors and so
    // by the time this value is queried, we should know if we are protected content or not.
    virtual TBool IsActive() = 0;

    // Converts the data in the 'incoming' buffer to the 'outgoing' writer. It is expected that providers
    // implement any buffer space they may require for processing the incoming data. 'Writer' has been chosen
    // to give as much flexibility to implementers for handling memory and/or streamed output
    virtual TBool TryGetAudioFrom(const Brx& aIncoming, IWriter& aOutgoing) = 0;
};

class ContentAudio : public ContentProcessor
{
private:
    static const TUint kMaxReadBytes = EncodedAudio::kMaxBytes;
public:
    ContentAudio(MsgFactory& aMsgFactory, IPipelineElementDownstream& aDownstream);
    ~ContentAudio();
public:
    void Add(IDRMProvider* aProvider); // Ownership is taken
private: // from ContentProcessor
    TBool Recognise(const Brx& aUri, const Brx& aMimeType, const Brx& aData);
    ProtocolStreamResult Stream(IReader& aReader, TUint64 aTotalBytes);
private:
    SupplyAggregator* iSupply;
    std::vector<IDRMProvider*> iDRMProviders;
};

} // namespace Media
} // namespace OpenHome

