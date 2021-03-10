#include <OpenHome/Media/Utils/ProcessorAudioUtils.h>

using namespace OpenHome;
using namespace OpenHome::Media;

// ProcessorPcmBufTest

Brn ProcessorPcmBufTest::Buf() const
{
    return Brn(iBuf);
}

const TByte* ProcessorPcmBufTest::Ptr() const
{
    return iBuf.Ptr();
}

ProcessorPcmBufTest::ProcessorPcmBufTest()
    : iBuf(kBufferGranularity)
{
}

void ProcessorPcmBufTest::CheckSize(TUint aAdditionalBytes)
{
    while (iBuf.Bytes() + aAdditionalBytes > iBuf.MaxBytes()) {
        const TUint size = iBuf.MaxBytes() + kBufferGranularity;
        iBuf.Grow(size);
    }
}

void ProcessorPcmBufTest::ProcessFragment(const Brx& aData)
{
    CheckSize(aData.Bytes());
    iBuf.Append(aData);
}

void ProcessorPcmBufTest::BeginBlock()
{
    iBuf.SetBytes(0);
}

void ProcessorPcmBufTest::ProcessFragment(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes)
{
    ASSERT(aData.Bytes() % (aSubsampleBytes * aNumChannels) == 0);
    ProcessFragment(aData);
}

void ProcessorPcmBufTest::ProcessSilence(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes)
{
    ASSERT(aData.Bytes() % (aSubsampleBytes * aNumChannels) == 0);
    ProcessFragment(aData);
}

void ProcessorPcmBufTest::EndBlock()
{
}

void ProcessorPcmBufTest::Flush()
{
}


// ProcessorDsdBufTest

Brn ProcessorDsdBufTest::Buf() const
{
    return Brn(iBuf);
}

const TByte* ProcessorDsdBufTest::Ptr() const
{
    return iBuf.Ptr();
}

ProcessorDsdBufTest::ProcessorDsdBufTest()
    : iBuf(kBufferGranularity)
{
}

void ProcessorDsdBufTest::CheckSize(TUint aAdditionalBytes)
{
    while (iBuf.Bytes() + aAdditionalBytes > iBuf.MaxBytes()) {
        const TUint size = iBuf.MaxBytes() + kBufferGranularity;
        iBuf.Grow(size);
    }
}

void ProcessorDsdBufTest::BeginBlock()
{
    iBuf.SetBytes(0);
}

void ProcessorDsdBufTest::ProcessFragment(const Brx& aData, TUint /*aNumChannels*/, TUint /*aSampleBlockBits*/)
{
    CheckSize(aData.Bytes());
    iBuf.Append(aData);
}

void ProcessorDsdBufTest::EndBlock()
{
}

void ProcessorDsdBufTest::Flush()
{
}
