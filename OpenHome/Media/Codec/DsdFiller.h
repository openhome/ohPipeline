#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>

namespace OpenHome {
namespace Media {

/* There are multiple points in the audio chain that we may want to do some operation
 * on DSD data resulting in a greater output size or a change to the sequence of the data.
 * These commonly include:
 * - Applying padding
 * - Interleaving supplied data
 * 
 * Clients implementing DsdFiller can specify an input and output block size, and DsdFiller
 * will fill its output buffer. It's guaranteed to call OutputDsd() with data which is exactly
 * divisible by the specified output block size.
 * 
 * Flush() will output any whole sample blocks held in the output buffer
 * 
 * Drain() constructs a full sample block from any pending data by padding the remainder with DSD silence
 * 
 * Operations on the data are deferred to the implementor via WriteChunkDsd(), where clients
 * can define the specific operation to be done. (See CodecDsdRaw for an example of this - 
 * data is padded and passed without interleaving)
 */

class DsdFiller
{
private:
    static const TByte kSilenceByteDsd = 0x69;
protected:
    DsdFiller(TUint aBlockBytesInput, TUint aBlockBytesOutput);
public:
    virtual ~DsdFiller() {}
protected:
    void Push(const Brx& aData);
    void Flush();
    void Drain();
    void Reset();
protected:
    virtual void WriteChunkDsd(const TByte*& aSrc, TByte*& aDest) = 0;
    virtual void OutputDsd(const Brx& aData) = 0;
private:
    void WriteBlocks(const Brx& aData);
private:
    const TUint iBlockBytesInput;
    const TUint iBlockBytesOutput;
    const TUint iChunksPerBlock;
    Bwh iOutputBuffer;
    Bwh iPending;
};

}
}