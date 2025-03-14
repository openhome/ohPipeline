#pragma once
#include <OpenHome/Optional.h>

#include <OpenHome/Optional.h>

namespace OpenHome {
namespace Media {
    class IMimeTypeList;
namespace Codec {

class ContainerBase;
class IMpegDRMProvider;

class ContainerFactory
{
public:
    static ContainerBase* NewId3v2();
    static ContainerBase* NewMpeg4(IMimeTypeList& aMimeTypeList, Optional<IMpegDRMProvider> aDRMProvider);
    static ContainerBase* NewMpegTs(IMimeTypeList& aMimeTypeList);
};

} // namespace Codec
} // namespace Media
} // namespace OpenHome
