"""testBundler.py - utility to build and publish tarball required by UnitTestRunner
"""
import os
import sys
import tarfile
sys.path.append( os.path.abspath( '../../ohdevtools' ))
sys.path.append( os.path.abspath( '../ohdevtools' ))
sys.path.append( os.path.abspath( 'ohdevtools' ))
import aws


class TestBundler:

    def __init__(self):
        self.platform = os.environ['PLATFORM'] if 'PLATFORM' in os.environ else 'Unknown' 
        self.version = os.environ['BUILD_NUMBER'] if 'BUILD_NUMBER' in os.environ else '0.0.0'
        self.tarName = os.path.join(os.path.dirname(__file__), 'buildhudson', f'ohMediaPlayer_{self.version}_{self.platform}_TESTS.tar.gz')

    def Bundle(self):
        """Create tarball in build directory containing unit tests, manifests and any other
           required files for the UnitTestRunner to run unit tests"""
        print(f'Bundle unit tests into tarball for {self.platform} @ {self.version}')
        if os.path.exists(self.tarName):
            os.remove(self.tarName)        
        with tarfile.open(self.tarName, mode='x:gz', compresslevel=3) as t:
            for f in os.listdir(os.path.join(os.path.dirname(__file__))):
                if f.endswith('.test_manifest'):
                    print(f'    Adding {f}')
                    t.add(os.path.join(os.path.dirname(__file__), f), arcname=f)
            for f in os.listdir(os.path.join(os.path.dirname(__file__), 'buildhudson')):
                if f.startswith('Test'):
                    print(f'    Adding {f}')
                    t.add(os.path.join(os.path.dirname(__file__), 'buildhudson', f), arcname=f)
        print(f'Completed {self.tarName}\n')

    def Publish(self):
        """Publish unit tests tarball to AWS"""
        print(f'Publishing unit tests tarball for {self.platform} @ {self.version}')
        if os.path.exists(self.tarName):
            dest = f's3://linn-artifacts-private/dsUnitTests/ohMediaPlayer/{self.platform}/{os.path.basename(self.tarName)}'
            aws.cp(self.tarName, dest)
            print(f'    -> {dest}')


if __name__ == "__main__":

    bundler = TestBundler()
    bundler.Bundle()
    # bundler.Publish()        