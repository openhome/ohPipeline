#include <OpenHome/Av/Pins/PodcastPins.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Net;

Brn PodcastPins::GetFirstXmlAttribute(const Brx& aXml, const Brx& aAttribute)
{
    Parser parser;
    parser.Set(aXml);

    Brn buf;
    parser.Next(' ');
    while (!parser.Finished()) {
        Brn att = parser.Next('=');
        parser.Next('"');
        Brn val = parser.NextNoTrim('"');
        if (att == aAttribute) {
            return val;
        } 
    }
    THROW(ReaderError);
}

Brn PodcastPins::GetNextXmlValueByTag(Parser& aParser, const Brx& aTag)
{
    Brn remaining = aParser.Remaining();
    TInt indexOffset = aParser.Index();

    Brn buf;
    TInt start = -1;
    TInt end = -1;
    TBool startFound = false;
    TBool endFound = false;
    while (!aParser.Finished()) {
        aParser.Next('<');
        start = aParser.Index();
        buf.Set(aParser.Next('>'));
        if (buf.BeginsWith(aTag)) {
            if (aParser.At(-2) == '/') {
                // tag with no true value, but info stored as attribute instead
                end = aParser.Index()-2;
                return remaining.Split(start-indexOffset, end-start);
            }
            else {
                start = aParser.Index();
                startFound = true;
                break;
            }
        }
    }
    if (startFound) {
        while (!aParser.Finished()) {
            aParser.Next('<');
            end = aParser.Index() - 1;
            buf.Set(aParser.Next('>'));
            Bwh endTag(aTag.Bytes()+1, aTag.Bytes()+1);
            endTag.ReplaceThrow(Brn("/"));
            endTag.TryAppend(aTag);
            if (buf.BeginsWith(endTag)) {
                endFound = true;
                break;
            }
        }

        if (endFound) {
            return remaining.Split(start-indexOffset, end-start);
        }
    }
    THROW(ReaderError);
}

// ListenedDatePooled

ListenedDatePooled::ListenedDatePooled()
    : iId(Brx::Empty())
    , iDate(Brx::Empty())
    , iPriority(0)
{
}

void ListenedDatePooled::Set(const Brx& aId, const Brx& aDate, TUint aPriority)
{
    iId.Replace(aId);
    iDate.Replace(aDate);
    iPriority = aPriority;
}

const Brx& ListenedDatePooled::Id() const
{
    return iId;
}

const Brx& ListenedDatePooled::Date() const
{
    return iDate;
}

const TUint ListenedDatePooled::Priority() const
{
    return iPriority;
}

void ListenedDatePooled::DecPriority()
{
    if (iPriority > 0) {
        iPriority--;
    }
}

TBool ListenedDatePooled::Compare(const ListenedDatePooled* aFirst, const ListenedDatePooled* aSecond)
{
    if (aFirst->Priority() == aSecond->Priority() &&
        aFirst->Date() == aSecond->Date() &&
        aFirst->Id() == aSecond->Id()) {
        return false;
    }
    return (aFirst->Priority() >= aSecond->Priority());
}