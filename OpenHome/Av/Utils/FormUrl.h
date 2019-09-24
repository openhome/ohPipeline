#pragma once

#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Stream.h>

namespace OpenHome {
    class IWriter;
namespace Av {

class FormUrl
{
public:
    static void Encode(IWriter& aWriter, const Brx& aValue);
};

class WriterFormUrl : public IWriter
{
public:
    WriterFormUrl(IWriter& aWriter);
    void SetEnabled(TBool aEnabled);
private: // from IWriter
    void Write(TByte aValue) override;
    void Write(const Brx& aBuffer) override;
    void WriteFlush() override;
private:
    IWriter& iWriter;
    TBool iEnabled;
};

} // namespace Av
} // namespace OpenHome

