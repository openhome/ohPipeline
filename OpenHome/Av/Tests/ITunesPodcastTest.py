#!/usr/bin/python -tt

import argparse
import ast
import re
import pprint
import sys
import os
import urllib2
import json
import datetime
import operator
import time
import tempfile
import shutil
import string
from urlparse import urlparse
from xml.dom import minidom
from dateutil import parser as dateparser
from datetime import datetime
import pytz
import xml

kErrorCollectionHttp            = []
kErrorCollectionUrl             = []
kErrorCollectionFeedSize0       = []
kErrorCollectionXml             = []
kErrorCollectionNoFeedUrlTag    = []
kErrorCollectionNoLatestUrl     = []
kErrorCollectionNoEpisodes      = []
kErrorCollectionOrderAsc        = []
kErrorCollectionOrderUnknown    = []
kErrorCollectionOrderMixedAsc   = []
kErrorCollectionOrderMixedDesc  = []
kErrorCollectionUnexpected      = []

kErrorEpisodeNoDurationTag      = []
kErrorEpisodeNoEnclosureTag     = []
kErrorEpisodeNoTitleTag         = []
kErrorEpisodeNoPubDateTag       = []
kErrorEpisodeEmptyTitleTag      = []
kErrorEpisodeHttp               = []
kErrorEpisodeUrl                = []
kErrorEpisodeUnexpected         = []

def GetJsonFromUrl( aUrl ):
    print("Download example iTunes podcast info... %s" % aUrl )
    req = urllib2.Request( aUrl )
    data = urllib2.urlopen( req ).read()
    jsonData = json.loads( data ) # decode from string
    return jsonData

def Info( aMsg ):
    print( "%s" % aMsg )

def GetEpisodesFromFeed( aFeedData ):
    xmldoc = minidom.parseString( aFeedData )
    return xmldoc.getElementsByTagName('item')

def GetEpisodeSizes( aEpisodes ):
    minSize = 0
    maxSize = 0
    for episode in aEpisodes:
        size = len( episode.toxml() )
        if size > maxSize:
            maxSize = size
        if size < minSize or minSize == 0:
            minSize = size
    return minSize, maxSize

def ValidateEpisodeOrder( aEpisodes, aCollectionId ):
    prevDate = None
    order = "Unknown"
    latestEpisodeUrl = None
    for episode in aEpisodes:
        url = None
        try: 
            if len(episode.getElementsByTagName("pubDate")) > 0 and episode.getElementsByTagName("pubDate")[0].firstChild != None:
                if len(episode.getElementsByTagName("enclosure")) > 0:
                    url = episode.getElementsByTagName("enclosure")[0].getAttribute("url").replace("https", "http")

                if url != None: # skip episodes with no URL
                    date = dateparser.parse( episode.getElementsByTagName("pubDate")[0].firstChild.data )
                    if date.tzinfo == None:
                        date = date.replace( tzinfo=pytz.utc )

                    if prevDate == None:
                        prevDate = date
                        latestEpisodeUrl = url
                        order = "Single"
                    elif prevDate < date:
                        if order == "Single":
                            order = "Ascending"
                            latestEpisodeUrl = url
                        elif order != "Ascending":
                            order = "Mixed (Desc)" # order was descending until now (latest url will be first in list)
                            break
                        else:
                            latestEpisodeUrl = url
                    else:
                        if order == "Single":
                            order = "Descending"
                        elif order != "Descending":
                            order = "Mixed (Asc)" # order was ascending until now (latest url will be last in list up to now)
                            
                            break
                    prevDate = date
        except ValueError as e:
            print( "    --> Skipping episode in order test as pubDate is invalid: %s" % episode.getElementsByTagName("pubDate")[0].firstChild.data )
    if order == "Descending" or order == "Single":
        pass
    elif order == "Ascending":
        kErrorCollectionOrderAsc.append( aCollectionId )
    elif order == "Mixed (Desc)":
        kErrorCollectionOrderMixedDesc.append( aCollectionId )
    elif order == "Mixed (Asc)":
        kErrorCollectionOrderMixedAsc.append( aCollectionId )
    else:
        kErrorCollectionOrderUnknown.append( aCollectionId )
    return order, latestEpisodeUrl

def ValidateEpisodeData( aEpisodes, aCollectionId ):
    metadata = True
    episodeUrl = None
    for episode in aEpisodes:
        title = "none"
        if len(episode.getElementsByTagName("title")) > 0:
            if episode.getElementsByTagName("title")[0].firstChild != None:
                title = episode.getElementsByTagName("title")[0].firstChild.data
            else:
                print( "    --> Episode Metadata Error : Empty title tag" )
                kErrorEpisodeEmptyTitleTag.append( aCollectionId )
                metadata = False
        else:
            print( "    --> Episode Metadata Error : No title tag" )
            kErrorEpisodeNoTitleTag.append( aCollectionId )
            metadata = False
        if len(episode.getElementsByTagName("enclosure")) > 0:
            episodeUrl = episode.getElementsByTagName("enclosure")[0].getAttribute("url").replace("https", "http")
            if episodeUrl == None:
                print( "    --> Episode Metadata Error : No url attribute inside enclosure tag (%s)" % unicode(title).encode('latin-1', 'replace') )
                kErrorEpisodeNoEnclosureTag.append( aCollectionId )
                metadata = False
        else:
            print( "    --> Episode Metadata Error : No enclosure tag (%s)" % unicode(title).encode('latin-1', 'replace') )
            kErrorEpisodeNoEnclosureTag.append( aCollectionId )
            metadata = False
        if len(episode.getElementsByTagName("itunes:duration")) > 0 and episode.getElementsByTagName("itunes:duration")[0].firstChild != None:
            duration = episode.getElementsByTagName("itunes:duration")[0].firstChild.data
        else:
            print( "    --> Episode Metadata Error : No itunes:duration tag (%s)" % unicode(title).encode('latin-1', 'replace') )
            kErrorEpisodeNoDurationTag.append( aCollectionId )
            metadata = False
        if len(episode.getElementsByTagName("pubDate")) > 0 and episode.getElementsByTagName("pubDate")[0].firstChild != None:
            try:
                date = dateparser.parse( episode.getElementsByTagName("pubDate")[0].firstChild.data )
            except ValueError as e:
                print( "    --> Episode Metadata Error : pubDate invalid: %s" % episode.getElementsByTagName("pubDate")[0].firstChild.data )
                kErrorEpisodeNoPubDateTag.append( aCollectionId )
                metadata = False
        else:
            print( "    --> Episode Metadata Error : No pubDate tag (%s)" % unicode(title).encode('latin-1', 'replace') )
            kErrorEpisodeNoPubDateTag.append( aCollectionId )
            metadata = False
        #print( "    Podcast Episode %s (%s, %s): %s" % (unicode(title).encode('latin-1', 'replace'), date, duration, unicode(episodeUrl).encode('latin-1', 'replace')) )
    return metadata

def ValidateEpisodeUrl( aEpisodeUrl, aCollectionId ):
    valid = True
    playable = True
    if aEpisodeUrl != None and len( aEpisodeUrl ) > 0:
        try:
            req = urllib2.Request( aEpisodeUrl )
            reqUrl = urllib2.urlopen( req )
        except urllib2.HTTPError, e:
            print( "    --> Episode HTTP Error %d :  %s" % ( e.code, aEpisodeUrl  ) )
            kErrorEpisodeHttp.append( aCollectionId )
            playable = False
        except urllib2.URLError, e:
            print( "    --> Episode URL Error %s :  %s" % ( e.args, aEpisodeUrl  ) )
            kErrorEpisodeUrl.append( aCollectionId )
            valid = False
            playable = False
        except Exception as e:
            print( "    --> Episode Unexpected Error %s :  %s" % ( e.args, aEpisodeUrl  ) )
            kErrorEpisodeUnexpected.append( aCollectionId )
            valid = False
            playable = False
    else:
        print( "    --> Episode URL Error : no valid latest episode URL could be found" )
        kErrorCollectionNoLatestUrl.append( aCollectionId )
        valid = False
        playable = False
    return valid, playable
    
def TestOrderCmd( args ):
    kPodcastSearchList = [
        "http://itunes.apple.com/search?media=podcast&entity=podcast&term=music&limit=%d" % args.UserPodcastLimit,
        "http://itunes.apple.com/search?media=podcast&entity=podcast&term=comedy&limit=%d" % args.UserPodcastLimit,
        "http://itunes.apple.com/search?media=podcast&entity=podcast&term=sport&limit=%d" % args.UserPodcastLimit,
        "http://itunes.apple.com/search?media=podcast&entity=podcast&term=politic&limit=%d" % args.UserPodcastLimit,
        "http://itunes.apple.com/search?media=podcast&entity=podcast&term=news&limit=%d" % args.UserPodcastLimit,
        "http://itunes.apple.com/search?media=podcast&entity=podcast&term=bbc&limit=%d" % args.UserPodcastLimit,
        "http://itunes.apple.com/search?media=podcast&entity=podcast&term=classical&limit=%d" % args.UserPodcastLimit,
        "http://itunes.apple.com/search?media=podcast&entity=podcast&term=jazz&limit=%d" % args.UserPodcastLimit,
    ]
    if args.UserPodcastId > 0:
        kPodcastSearchList = [ "https://itunes.apple.com/lookup?id=%d" % args.UserPodcastId ]

    total = 0
    totalPod = 0
    podcasts = {}
    podcastIdList = []
    podcastFeedUrlList = []

    # create high-level podcast collection dict --> { id : (name, feedUrl) }
    for searchUrl in kPodcastSearchList:
        json = GetJsonFromUrl( searchUrl )
        total += json['resultCount']
        for podcast in json['results']:
            collId = 0
            feed = None
            name = ""
            if podcast.has_key('kind') and podcast['kind'] == 'podcast':
                totalPod += 1
                if podcast.has_key('collectionId') and podcast['collectionId'] > 0:
                    collId = podcast['collectionId']
                    podcastIdList.append( collId )
                if podcast.has_key('feedUrl') and len(podcast['feedUrl']) > 0:
                    feed = podcast['feedUrl'].replace("https", "http")
                    podcastFeedUrlList.append( feed )
                else:
                    kErrorCollectionNoFeedUrlTag.append( collId )
                if podcast.has_key('collectionName') and len(podcast['collectionName']) > 0:
                    name = podcast['collectionName']
                if collId > 0 and feed != None:
                    podcasts[collId] = ( name, feed )
    
    # test podcast collections one by one
    validFeedList = []
    feedSizeList = []
    episodeSizeList = []
    episodeCountList = []
    episodeOrderType = { "Descending": 0, "Ascending": 0, "Single": 0, "Mixed (Asc)": 0, "Mixed (Desc)": 0, "Unknown": 0 }
    validMetadataCount = 0
    validLatestUrlCount = 0
    playableLatestUrlCount = 0
    for collId, (collName, feedUrl) in podcasts.iteritems():
        print( "Podcast ID %s (%s), Feed: %s" % (unicode(collId).encode('latin-1', 'replace'), unicode(collName).encode('latin-1', 'replace'), unicode(feedUrl).encode('latin-1', 'replace')) )
        try:
            req = urllib2.Request( feedUrl )
            url = urllib2.urlopen( req )
            data = url.read()
            if len(data) > 0:
                feedSizeList.append( len(data) )
            else:
                kErrorCollectionFeedSize0.append( collId )
                continue
            episodes = GetEpisodesFromFeed( data )
            if len(episodes) > 0:
                validFeedList.append( feedUrl )
                episodeCountList.append( len(episodes) )
                metadata = ValidateEpisodeData( episodes, collId )
                validMetadataCount += metadata
                order, latestUrl = ValidateEpisodeOrder( episodes, collId )
                episodeOrderType[order] = episodeOrderType.get(order, 0) + 1
                urlVaild, urlPlayable = ValidateEpisodeUrl( latestUrl, collId )
                validLatestUrlCount += urlVaild
                playableLatestUrlCount += urlPlayable
                minEpSize, maxEpSize = GetEpisodeSizes( episodes )
                episodeSizeList.append( minEpSize )
                episodeSizeList.append( maxEpSize )
            else:
                print( "--> Feed Data Error : No episodes (items) found" )
                kErrorCollectionNoEpisodes.append( collId )
        except urllib2.HTTPError, e:
            print( "--> Feed HTTP Error %d :  %s" % ( e.code, feedUrl  ) )
            kErrorCollectionHttp.append( collId )
        except urllib2.URLError, e:
            print( "--> Feed URL Error %s :  %s" % ( e.args, feedUrl  ) )
            kErrorCollectionUrl.append( collId )
        except xml.parsers.expat.ExpatError, e:
            print( "--> Feed XML Error %s :  %s" % ( e.args, feedUrl  ) )
            kErrorCollectionXml.append( collId )
        except xml.parsers.expat.ExpatError, e:
            print( "--> Feed Unexpected Error %s :  %s" % ( e.args, feedUrl  ) )
            kErrorCollectionUnexpected.append( collId )

    print
    print("=============================================================")
    print("iTunes podcast API test")
    print("=============================================================")
    print
    Info( "Total items found in search                              %s" % total )
    Info( "Total podcasts found in search                           %s" % totalPod )
    Info( "Total podcasts with collectionId                         %d" % len( podcastIdList ) )
    Info( "Total podcasts with feedUrl                              %d" % len( podcastFeedUrlList ) )
    Info( "Total podcasts with working feed with episode(s)         %d" % len( validFeedList ) )
    Info( "Total podcasts with episodes in descending order         %d" % (episodeOrderType["Descending"] + episodeOrderType["Single"] + episodeOrderType["Mixed (Desc)"] + episodeOrderType["Unknown"]) ) # Unknown can only happen if pubDates are missing or invalid, so we assume the most common order
    Info( "Total podcasts with episodes in ascending order          %d" % (episodeOrderType["Ascending"] + episodeOrderType["Mixed (Asc)"]) )
    Info( "Total podcasts with all episode metadata present         %d" % validMetadataCount )
    Info( "Total podcasts with latest episode URL valid             %d" % validLatestUrlCount )
    Info( "Total podcasts with latest episode URL playable          %d" % playableLatestUrlCount )
    if len(feedSizeList) > 0:
        print
        Info( "Feed size in bytes (average)                             %d" % (sum(feedSizeList) / (len(feedSizeList))) )
        Info( "Feed size in bytes (min)                                 %d" % min(feedSizeList) )
        Info( "Feed size in bytes (max)                                 %d" % max(feedSizeList) )
    if len(episodeCountList) > 0:
        print
        Info( "Episode count per feed (average)                         %d" % (sum(episodeCountList) / (len(episodeCountList))) )
        Info( "Episode count per feed (min)                             %d" % min(episodeCountList) )
        Info( "Episode count per feed (max)                             %d" % max(episodeCountList) )
    if len(episodeSizeList) > 0:
        print
        Info( "Episode size in bytes (average)                          %d" % (sum(episodeSizeList) / (len(episodeSizeList))) )
        Info( "Episode size in bytes (min)                              %d" % min(episodeSizeList) )
        Info( "Episode size in bytes (max)                              %d" % max(episodeSizeList) )
    print
    print("=============================================================")
    print("Error info")
    print("=============================================================")
    print
    Info( "Podcast collection with HTTP error           %4d:    %s" % (len(list(set(kErrorCollectionHttp))), list(set(kErrorCollectionHttp))) )
    Info( "Podcast collection with URL error            %4d:    %s" % (len(list(set(kErrorCollectionUrl))), list(set(kErrorCollectionUrl))) )
    Info( "Podcast collection with feed size 0          %4d:    %s" % (len(list(set(kErrorCollectionFeedSize0))), list(set(kErrorCollectionFeedSize0))) )
    Info( "Podcast collection with XML error            %4d:    %s" % (len(list(set(kErrorCollectionXml))), list(set(kErrorCollectionXml))) )
    Info( "Podcast collection with no feedUrl tag       %4d:    %s" % (len(list(set(kErrorCollectionNoFeedUrlTag))), list(set(kErrorCollectionNoFeedUrlTag))) )
    Info( "Podcast collection with no valid latest url  %4d:    %s" % (len(list(set(kErrorCollectionNoLatestUrl))), list(set(kErrorCollectionNoLatestUrl))) )
    Info( "Podcast collection with no episodes (items)  %4d:    %s" % (len(list(set(kErrorCollectionNoEpisodes))), list(set(kErrorCollectionNoEpisodes))) )
    Info( "Podcast collection with asc order            %4d:    %s" % (len(list(set(kErrorCollectionOrderAsc))), list(set(kErrorCollectionOrderAsc))) )
    Info( "Podcast collection with unknown order        %4d:    %s" % (len(list(set(kErrorCollectionOrderUnknown))), list(set(kErrorCollectionOrderUnknown))) )
    Info( "Podcast collection with mixed asc order      %4d:    %s" % (len(list(set(kErrorCollectionOrderMixedAsc))), list(set(kErrorCollectionOrderMixedAsc))) )
    Info( "Podcast collection with mixed desc order     %4d:    %s" % (len(list(set(kErrorCollectionOrderMixedDesc))), list(set(kErrorCollectionOrderMixedDesc))) )
    Info( "Podcast collection with unexpected error     %4d:    %s" % (len(list(set(kErrorCollectionUnexpected))), list(set(kErrorCollectionUnexpected))) )
    print
    Info( "Podcast episode with no duration tag         %4d:    %s" % (len(list(set(kErrorEpisodeNoDurationTag))), list(set(kErrorEpisodeNoDurationTag))) )
    Info( "Podcast episode with no enclosure tag        %4d:    %s" % (len(list(set(kErrorEpisodeNoEnclosureTag))), list(set(kErrorEpisodeNoEnclosureTag))) )
    Info( "Podcast episode with no title tag            %4d:    %s" % (len(list(set(kErrorEpisodeNoTitleTag))), list(set(kErrorEpisodeNoTitleTag))) )
    Info( "Podcast episode with no pubDate tag          %4d:    %s" % (len(list(set(kErrorEpisodeNoPubDateTag))), list(set(kErrorEpisodeNoPubDateTag))) )
    Info( "Podcast episode with empty title             %4d:    %s" % (len(list(set(kErrorEpisodeEmptyTitleTag))), list(set(kErrorEpisodeEmptyTitleTag))) )
    Info( "Podcast episode with HTTP error              %4d:    %s" % (len(list(set(kErrorEpisodeHttp))), list(set(kErrorEpisodeHttp))) )
    Info( "Podcast episode with URL error               %4d:    %s" % (len(list(set(kErrorEpisodeUrl))), list(set(kErrorEpisodeUrl))) )
    Info( "Podcast episode with unexpected error        %4d:    %s" % (len(list(set(kErrorEpisodeUnexpected))), list(set(kErrorEpisodeUnexpected))) )
    print
   

if __name__ == "__main__":
    parser = argparse.ArgumentParser( description='Test to validate iTunes podcast API assumptions' )
    subparsers = parser.add_subparsers()
    
    parser_order = subparsers.add_parser( 'order', help='validate podcast order is always newest to oldest' )
    parser_order.add_argument('--dry-run', action="store_true", dest='DryRun', default=False, help='non destructive test run')
    parser_order.add_argument('-p', '--podcast-id', metavar='ID', action="store", dest='UserPodcastId', default=0, help='target specific podcast collection by ID', type=int)
    parser_order.add_argument('-l', '--limit', metavar='LIMIT', action="store", dest='UserPodcastLimit', default=200, help='specify podcast limit for 8 x search (max/default = 200)', type=int)
    
    parser_order.set_defaults( func=TestOrderCmd )
    args = parser.parse_args()
    args.func( args ) # will call the correct function here



