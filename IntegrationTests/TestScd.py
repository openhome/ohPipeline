#!/usr/bin/env python
"""TestScd - test device-side Songcast Direct operation

Parameters:
    arg#1 - Device Under Test

http://wiki.openhome.org/wiki/Av:Developer:Scd
"""
import _Paths   # NOQA
import CommonScd as BASE
import sys


class TestScd( BASE.CommonScd ):
    def __init__( self ):
        BASE.CommonScd.__init__( self )
        self.doc = __doc__


if __name__ == '__main__':
    BASE.Run( sys.argv )
