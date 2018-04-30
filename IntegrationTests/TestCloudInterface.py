#!/usr/bin/env python
"""TestCloudInterface - test interface with Linn Cloud services

Parameters:
    arg#1 - Device Under Test
    arg#2 - App ID for Cloud
    arg#3 - App Secret for Cloud
    arg#4 - Renew token for App/User with Cloud

Tests device interface with 'cloud' features
    - cloud service
    - device side association/disassociation
    - device integration
        - https://github.com/linn/mylinn/wiki/Device-Integration
"""
import _Paths   # NOQA
import CommonCloudInterface as BASE
import sys


class TestCloudInterface( BASE.CommonCloudInterface ):
    def __init__( self ):
        BASE.CommonCloudInterface.__init__( self )
        self.doc = __doc__


if __name__ == '__main__':
    BASE.Run( sys.argv )
