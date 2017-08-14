#!/usr/bin/env python
"""TestCalmRadioDropout - test for dropout on Calm Radio playback

Parameters:
    arg#1 - Sender (local for SoftPlayer)
    arg#2 - Receiver/Repeater (None if not required, local for SoftPlayer)
    arg#3 - Receiver/Slave (None if not required, local for SoftPlayer)
    arg#4 - Test duration (secs) or 'forever'
    arg#5 - songcast sender mode (unicast/multicast/none)
    arg#6 - Number of channels to test with (use 0 for fixed list of 20 hard-coded channels)
    arg#7 - Calm Radio User
    arg#8 - Calm Radio Password

Verifies Calm Radio served audio played by the DUT does not suffer from audio dropout.
Additionally checks for dropout on downstream songcast receiver(s)
"""
import _Paths
import CommonCalmRadioDropout as BASE
import sys


class TestCalmRadioDropout( BASE.CommonCalmRadioDropout ):

    def __init__( self ):
        BASE.CommonCalmRadioDropout.__init__( self )
        self.doc = __doc__


if __name__ == '__main__':

    BASE.Run( sys.argv )
