""" Jenkins Publisher for ohMediaPlayer repo

Publishes previously built ohMediaPlayer artifacts by copying these from the
specified oncommit build output (defaulting to 'latest') to the 'published'
locations

    oncommit artifacts in  s3://linn-artifacts-private/dsOncommit/ohMediaPlayer/<version>/<platform>
    published artifacts in s3://linn-artifacts-public/artifacts/ohMediaPlayer
"""
import os
import sys
sys.path.append(os.path.abspath('../ohdevtools'))
import aws
import re


class Publisher:

    def __init__(self):
        self.awsBucketPrivate = 's3://linn-artifacts-private'
        self.awsOncommitDir =   f'{self.awsBucketPrivate}/dsOncommit/ohMediaPlayer'
        self.awsLatestBuild =   f'{self.awsOncommitDir}/latest.txt'
        awsBucketPublic =       's3://linn-artifacts-public'
        awsPublishDir =         f'{awsBucketPublic}/artifacts/ohMediaPlayer' 

        releaseVer = os.environ['RELEASE_VERSION'] if 'RELEASE_VERSION' in os.environ else 'UNKNOWN'
        buildNum = self._GetBuildNum()
        platformDirs = self._GetPlatformDirs(buildNum)

        for platformDir in platformDirs:
            platform = platformDir.split('/')[-2]
            print(f'\n---- publish {platform} ----')
            files = aws.ls(platformDir)
            for f in files:
                variant = 'Release' if 'release' in f else 'Debug' if 'debug' in f else 'Unknown'
                print(f'copy {self.awsBucketPrivate}/{f} -> {awsPublishDir}/ohMediaPlayer-{releaseVer}-{platform}-{variant}.tar.gz')
                aws.copy(f'{self.awsBucketPrivate}/{f}', f'{awsPublishDir}/ohMediaPlayer-{releaseVer}-{platform}-{variant}.tar.gz')

    def _GetPlatformDirs(self, buildNum):
        platformDirs = []
        dirs = aws.ls(f'{self.awsOncommitDir}/{buildNum}')
        for dir in dirs:
            platformDirs.append(f'{self.awsBucketPrivate}/{dir}')
        return platformDirs

    def _GetBuildNum(self):
        buildNum = os.environ['BUILD_TO_PUBLISH'] if 'BUILD_TO_PUBLISH' in os.environ else 'latest'
        if buildNum == 'latest':
            tmpFile = 'latest.txt'
            try:
                aws.copy(self.awsLatestBuild, tmpFile)
            except:
                print(f'{self.awsLatestBuild} NOT found')
                sys.exit(1)
            with open(tmpFile, 'rt') as f:
                buildNum = f.read()
            os.unlink(tmpFile)
        return buildNum


if __name__ == '__main__':

    p = Publisher()
