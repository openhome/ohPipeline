""" Jenkins Ready-To-Publish for ohMediaPlayer repo

Writes 'latest' pipeline job numvber to AWS which will be picked up by the
Jenkins publish job to select most recent build available for publishing.

By executing this as final stage in Jenkins on-commit pipeline this 
ensures that publish is not available until after all build and test jobs
in the pipeline have completed
"""
import os
import sys
sys.path.append(os.path.abspath('../ohdevtools'))
sys.path.append(os.path.abspath('ohdevtools'))
import JenkinsBuildUtils as build

buildNum = os.environ['PIPELINE_JOB_NUMBER'] if 'PIPELINE_JOB_NUMBER' in os.environ else '0'
buildDir = os.path.abspath(os.path.dirname('__file__'))                                   
with open(os.path.join(buildDir, 'latest.txt'), 'wt') as f:
    f.write(buildNum)
print(f'awsCopy {os.path.join(buildDir, "latest.txt")} -> s3://linn-artifacts-private/dsOncommit/ohMediaPlayer/latest.txt')
build.awsCopy(os.path.join(buildDir, 'latest.txt'), f's3://linn-artifacts-private/dsOncommit/ohMediaPlayer/latest.txt')
