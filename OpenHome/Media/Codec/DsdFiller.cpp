#include <OpenHome/Media/Codec/DsdFiller.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Media;

// DsdFiller

DsdFiller::DsdFiller(TUint aBlockBytesInput, TUint aBlockBytesOutput)
    : iBlockBytesInput(aBlockBytesInput)
    , iBlockBytesOutput(aBlockBytesOutput)
    , iChunksPerBlock(aBlockBytesInput / 4)
    , iOutputBuffer(AudioData::kMaxBytes - (AudioData::kMaxBytes % aBlockBytesOutput))
    , iPending(aBlockBytesInput)
{
    ASSERT(iBlockBytesInput % 4 == 0);
}

void DsdFiller::Push(const Brx& aData)
{
    if (iPending.Bytes() + aData.Bytes() < iBlockBytesInput) {
        iPending.Append(aData);
        return;
    }

    Brn data(aData);
    if (iPending.Bytes() > 0) {
        const TUint partialBlockBytes = iBlockBytesInput - iPending.Bytes();
        iPending.Append(data.Split(0, partialBlockBytes));
        data.Set(data.Ptr() + partialBlockBytes, data.Bytes() - partialBlockBytes);
        WriteBlocks(iPending);
        iPending.SetBytes(0);
    }

    const TUint blocks = data.Bytes() / iBlockBytesInput;
    const TUint bytes = blocks * iBlockBytesInput;
    iPending.Replace(data.Split(bytes));
    data.Set(data.Ptr(), bytes);
    WriteBlocks(data);
}

void DsdFiller::Flush()
{
    if (iOutputBuffer.Bytes() == 0) {
        return;
    }
    OutputDsd(iOutputBuffer);
    iOutputBuffer.SetBytes(0);
}

void DsdFiller::Drain()
{
    if (iPending.Bytes() != 0) {
        // iPending.MaxBytes() will be exactly iBlockBytesInput, so can safely fill
        // the remainder with silence to construct a full input sample block
        iPending.SetBytes(iPending.MaxBytes());
        for (TUint i = iPending.Bytes(); i < iPending.MaxBytes(); i++) {
            iPending[i] = kSilenceByteDsd;
        }
        WriteBlocks(iPending);
        iPending.SetBytes(0);
    }
    Flush();
}

void DsdFiller::WriteBlocks(const Brx& aData)
{
    ASSERT(aData.Bytes() % iBlockBytesInput == 0); // Expect whole sample blocks at this point

    const TByte* src = aData.Ptr();
    TUint inputBlocks = aData.Bytes() / iBlockBytesInput;

    while (inputBlocks > 0) {
        TByte* dest = const_cast<TByte*>(iOutputBuffer.Ptr() + iOutputBuffer.Bytes());
        TUint outputBlocks = iOutputBuffer.BytesRemaining() / iBlockBytesOutput;
        const TUint blocks = std::min(inputBlocks, outputBlocks);
        const TUint bytes = blocks * iBlockBytesOutput;

        for (TUint i = 0; i < blocks; i++) {
            for (TUint j = 0; j < iChunksPerBlock; j++) {
                WriteChunkDsd(src, dest);
            }
        }
        iOutputBuffer.SetBytes(iOutputBuffer.Bytes() + bytes);
        inputBlocks -= blocks;
        outputBlocks -= blocks;

        if (outputBlocks == 0) {
            Flush();
        }
    }
}
