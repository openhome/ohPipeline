#include <OpenHome/Media/Protocol/ContentAudio.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Buffer.h>

using namespace OpenHome;
using namespace OpenHome::Media;

class WriterSupply : public IWriter
{
public:
    WriterSupply(ISupply& aSupply);
public: // IWriter
    virtual void Write(TByte aValue) override;
    virtual void Write(const Brx& aBuffer) override;
    virtual void WriteFlush() override;

private:
    ISupply& iSupply;
};


// WriterSupply
WriterSupply::WriterSupply(ISupply& aSupply)
    : iSupply(aSupply)
{ }

void WriterSupply::Write(TByte aValue)
{
    const Brn wrapped(&aValue, 1);
    Write(wrapped);
}

void WriterSupply::Write(const Brx& aBuffer)
{
    iSupply.OutputData(aBuffer);
}

void WriterSupply::WriteFlush()
{ }


// ContentAudio

ContentAudio::ContentAudio(MsgFactory& aMsgFactory, IPipelineElementDownstream& aDownstream)
{
    iSupply = new SupplyAggregatorBytes(aMsgFactory, aDownstream);
}

ContentAudio::~ContentAudio()
{
    delete iSupply;
}

TBool ContentAudio::Recognise(const Brx& /*aUri*/, const Brx& /*aMimeType*/, const Brx& /*aData*/)
{
    /* Assume that this processor will be offered content last.
       Content we don't support will be rejected.
       ...so we might as well have a go at treating everything as audio. */
    return true;
}

ProtocolStreamResult ContentAudio::Stream(IReader& aReader, TUint64 aTotalBytes)
{
    static const TUint kBlocksPerYield = 12; /* Pipeline threads will take priority over
                                                most other activities in a real-time system.
                                                This is necessary but can result in many
                                                seconds where evented updates are blocked
                                                when a high-res track starts.
                                                Mitigate the effects of this by yielding
                                                for a brief period every so often.  The
                                                value chosen is intended to allow ~5 yields
                                                per second for 192/24 stereo FLAC. */
    ProtocolStreamResult res = EProtocolStreamSuccess;
    TUint blocksUntilYield = kBlocksPerYield;
    try {
        for (;;) {
            Brn buf = aReader.Read(kMaxReadBytes);

            WriterSupply ws(*iSupply);

            ws.Write(buf);
            ws.WriteFlush();

            if (aTotalBytes > 0) {
                if (buf.Bytes() > aTotalBytes) { // aTotalBytes is inaccurate - ignore it
                    aTotalBytes = 0;
                }
                else {
                    aTotalBytes -= buf.Bytes();
                    if (aTotalBytes == 0) {
                        iSupply->Flush();
                        break;
                    }
                }
            }
            if (--blocksUntilYield == 0) {
                Thread::Sleep(5);
                blocksUntilYield = kBlocksPerYield;
            }
        }
    }
    catch (ReaderError&) {
        res = EProtocolStreamErrorRecoverable;
        iSupply->Flush();
    }
    return res;
}
