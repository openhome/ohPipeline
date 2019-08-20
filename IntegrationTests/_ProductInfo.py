"""_ProductInfo - product specific information (for OHMP)
"""


class ProductInfo:

    def __init__( self, aDut ):
        pass

    #
    # 'Features' which are only available on certain devices
    #

    @property
    def upnpAv( self ):
        return True

    @property
    def exaktOnly( self ):
        return False

    @property
    def noPreOut( self ):
        return True

    @property
    def digitalAmps( self ):
        """Products with digital amps"""
        return False

    @property
    def inputLevelAdjust( self ):
        """Products with adjustable input level (on analog inputs)"""
        return False

    @property
    def dsdPlayback( self ):
        return True

    @property
    def core4( self ):
        return False

    @property
    def mono( self ):
        return False
