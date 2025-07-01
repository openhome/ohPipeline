#pragma once

namespace OpenHome {
namespace Media {
    class ContentProcessor;
    class IMimeTypeList;
} // namespace Media
namespace Av {

class ContentProcessorFactory
{
public:
    static Media::ContentProcessor* NewM3u(Media::IMimeTypeList& aMimeTypeList);
    static Media::ContentProcessor* NewM3uX();
    static Media::ContentProcessor* NewPls(Media::IMimeTypeList& aMimeTypeList);
    static Media::ContentProcessor* NewOpml(Media::IMimeTypeList& aMimeTypeList);
    static Media::ContentProcessor* NewAsx();
    static Media::ContentProcessor* NewMPD();
};

} // namespace Av
} // namespace OpenHome

