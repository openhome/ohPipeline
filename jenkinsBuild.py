""" Jenkins Builder for ohMediaPlayer repo

Performs build of ohMediaPlayer repo and stores output artifacts, resources
and tests for use by future publish and test jobs. Note that these artifacts
are stored in time-limited locations where aged artifacts are regularily
cleared out

    Build output artifacts and resources for use by future 'publish' jobs
        -> s3://linn-artifacts-private/dsOncommit/ohMediaPlayer/<platform>
    Test artifacts for use by future 'test' jobs
        -> s3://linn-artifacts-private/dsUnitTests/ohMediaPlayer/<platform>
"""

import os
import sys
sys.path.append(os.path.abspath('../ohdevtools'))
sys.path.append(os.path.abspath('ohdevtools'))
import JenkinsBuildUtils as build
import shutil
import testBundler


class Builder():

    def __init__(self):
        print(f'Starting ohMediaPlayer build\n')
        try:
            self.platform = os.environ['PLATFORM']
        except:
            print('No PLATFORM defined .... exiting')
            sys.exit(1)
        build.setupEnv(self.platform)
        shutil.rmtree( 'install', True )
        build.fetch('--clean')
        build.fetch('--all', f'--platform={self.platform}')
        build.waf('configure', '--dest-platform', self.platform, '--release')
        build.waf('clean')
        build.waf('build')
        build.waf('install')
        build.waf('bundle')
        self.PublishBuildOutput()
        self.PublishFilesForTest()

    def PublishBuildOutput(self): 
        buildNum = os.environ['TEST_TARBALL_ID'] if 'TEST_TARBALL_ID' in os.environ else os.environ['BUILD_NUMBER']
        buildDir = os.path.abspath(os.path.join(os.path.dirname('__file__'), 'build'))                                   
        awsRoot = f's3://linn-artifacts-private/dsOncommit/ohMediaPlayer/{buildNum}/{self.platform}'
        print(f'awsCopy {os.path.join(buildDir, "ohMediaPlayer.tar.gz")} -> {awsRoot}/ohMediaPlayer.tar.gz')
        build.awsCopy(os.path.join(buildDir, 'ohMediaPlayer.tar.gz'), f'{awsRoot}/ohMediaPlayer.tar.gz')
        with open(os.path.join(buildDir, 'latest.txt'), 'wt') as f:
            f.write(buildNum)
        print(f'awsCopy {os.path.join(buildDir, "latest.txt")} -> s3://linn-artifacts-private/dsOncommit/ohMediaPlayer/latest.txt')
        build.awsCopy(os.path.join(buildDir, 'latest.txt'), 's3://linn-artifacts-private/dsOncommit/ohMediaPlayer/latest.txt')

    def PublishFilesForTest(self):
        os.environ['PLATFORM'] = self.platform  # initial env-var getting changed by VS setup on Windows ????
        bundler = testBundler.TestBundler()
        bundler.Bundle()
        bundler.Publish()                


if __name__ == '__main__':

    b = Builder()
