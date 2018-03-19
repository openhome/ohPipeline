#!/usr/bin/env python
"""TestCalmRadioPlayChannels - test Playing of Calm Radio serverd channels

Parameters:
    arg#1 - Sender (local for SoftPlayer)
    arg#2 - Receiver (None if not required, local for SoftPlayer)
    arg#3 - Time to play before skipping to next (None = play indefinitely)
    arg#4 - Number of channels to test with (use 0 for fixed list of 20 hard-coded channels)
    arg#5 - Calm Radio User
    arg#6 - Calm Radio Password

Test test which plays Calm Radio served channels from a channel list sequentially. The
channels may be played for any specified length of time (or indefinitely)
"""
import _Paths   # NOQA
import CommonCalmRadioPlayChannels as BASE
import sys


class TestCalmRadioPlayChannels( BASE.CommonCalmRadioPlayChannels ):

    def __init__( self ):
        BASE.CommonCalmRadioPlayChannels.__init__( self )
        self.doc = __doc__


if __name__ == '__main__':

    BASE.Run( sys.argv )
