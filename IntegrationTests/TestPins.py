#!/usr/bin/env python
"""TestPins - test device-side Pins operation

Parameters:
    arg#1  - Device Under Test
    arg#2  - CalmRadio username
    arg#3  - CalmRadio password
    arg#4  - Qobuz username
    arg#5  - Qobuz password
    arg#6  - Tidal username
    arg#7  - Tidal password
    arg#8  - Tunein partner ID
    arg#9  - Tunein user
    arg#10 - Cloud ID
    arg#11 - Cloud secret
    arg#12 - Cloud token
"""
import _Paths   # NOQA
import CommonPins as BASE
import sys


class TestPins( BASE.CommonPins ):
    def __init__( self ):
        BASE.CommonPins.__init__( self )
        self.doc = __doc__

    def _SetupCloud( self, aId, aSecret, aToken ):
        # no cloud support in OHMP
        pass

    def _CheckAccount( self ):
        # no cloud support in OHMP
        pass


if __name__ == '__main__':
    BASE.Run( sys.argv )
