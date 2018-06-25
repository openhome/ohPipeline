#!/usr/bin/env python
"""TestPins - test device-side Pins operation

Parameters:
    arg#1 - Device Under Test (local for SoftPlayer)
    arg#2 - Qobuz username
    arg#3 - Qobuz password
    arg#4 - Tidal username
    arg#5 - Tidal password
    # arg#6 - Cloud ID
    # arg#7 - Cloud secret
    # arg#8 - Cloud token
"""
import _Paths   # NOQA
import CommonPins as BASE
import sys


class TestPins( BASE.CommonPins ):
    def __init__( self ):
        BASE.CommonPins.__init__( self )
        self.doc = __doc__


if __name__ == '__main__':
    BASE.Run( sys.argv )
