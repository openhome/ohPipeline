#!/usr/bin/python

import sys
import os
import shutil


from waflib.Node import Node

from wafmodules.filetasks import (
    find_resource_or_fail)

import os.path, sys
sys.path[0:0] = [os.path.join('dependencies', 'AnyPlatform', 'ohWafHelpers')]

from filetasks import gather_files, build_tree, copy_task, find_dir_or_fail, create_copy_task
from utilfuncs import invoke_test, guess_dest_platform, configure_toolchain, guess_ohnet_location, guess_location, guess_ssl_location, guess_raat_location, guess_libplatform_location, guess_libosa_location, is_core_platform

def options(opt):
    opt.load('msvs')
    opt.load('msvc')
    opt.load('compiler_cxx')
    opt.load('compiler_c')
    opt.add_option('--ohnet-include-dir', action='store', default=None)
    opt.add_option('--ohnet-lib-dir', action='store', default=None)
    opt.add_option('--testharness-dir', action='store', default=os.path.join('dependencies', 'AnyPlatform', 'testharness'))
    opt.add_option('--ohnet', action='store', default=None)
    opt.add_option('--ssl', action='store', default=None)
    opt.add_option('--libplatform', action='store', default=None)
    opt.add_option('--libosa', action='store', default=None)
    opt.add_option('--raat', action='store', default=None)
    opt.add_option('--debug', action='store_const', dest="debugmode", const="Debug", default="Release")
    opt.add_option('--release', action='store_const', dest="debugmode",  const="Release", default="Release")
    opt.add_option('--dest-platform', action='store', default=None)
    opt.add_option('--cross', action='store', default=None)
    opt.add_option('--with-default-fpm', action='store_true', default=False)

def configure(conf):

    def set_env(conf, varname, value):
        conf.msg(
                'Setting %s to' % varname,
                "True" if value is True else
                "False" if value is False else
                value)
        setattr(conf.env, varname, value)
        return value

    conf.msg("debugmode:", conf.options.debugmode)
    if conf.options.dest_platform is None:
        try:
            conf.options.dest_platform = guess_dest_platform()
        except KeyError:
            conf.fatal('Specify --dest-platform')

    if is_core_platform(conf):
        guess_libosa_location(conf)
        guess_libplatform_location(conf)

    configure_toolchain(conf)
    guess_ohnet_location(conf)
    guess_ssl_location(conf)
    guess_raat_location(conf)

    conf.env.dest_platform = conf.options.dest_platform
    conf.env.testharness_dir = os.path.abspath(conf.options.testharness_dir)

    if conf.options.dest_platform.startswith('Windows'):
        conf.env.LIB_OHNET=['ws2_32', 'iphlpapi', 'dbghelp', 'psapi', 'userenv']
    conf.env.STLIB_OHNET=['TestFramework', 'ohNetCore']

    if is_core_platform(conf):
        conf.env.prepend_value('STLIB_OHNET', ['target', 'platform'])
        conf.env.append_value('DEFINES', ['DEFINE_TRACE', 'NETWORK_NTOHL_LOCAL', 'NOTERMIOS']) # Tell FLAC to use local ntohl implementation

    conf.env.INCLUDES = [
        '.',
        conf.path.find_node('.').abspath()
        ]

    # Setup Ogg lib options
    # Using https://git.xiph.org/?p=ogg.git
    # 1344d4ed60e26f6426c782b705ec0c9c5fddfe43
    # (Fri, 8 May 2015 21:30:14 +0100 (13:30 -0700))
    conf.env.INCLUDES_OGG = [
        'thirdparty/libogg/include',
        ]

    # Setup FLAC lib options
    conf.env.DEFINES_FLAC = ['VERSION=\"1.2.1\"', 'FLAC__NO_DLL', 'FLAC__HAS_OGG']
    conf.env.INCLUDES_FLAC = [
        'thirdparty/flac-1.2.1/src/libFLAC/include',
        'thirdparty/flac-1.2.1/include',
        ]

    # Setup Apple ALAC
    # Using http://svn.macosforge.org/repository/alac/trunk
    # Revision: 4
    # (2012-12-12 22:09:07 +0000 (Wed, 12 Dec 2012))
    #
    # thirdparty/apple_alac/codec/EndianPortable.c attempts to define TARGET_RT_LITTLE_ENDIAN for little endian platforms (leaving it unset implies big endian).
    if conf.options.dest_platform in ['Windows-x86', 'Windows-x64']:
        conf.env.DEFINES_ALAC_APPLE = ['TARGET_RT_LITTLE_ENDIAN']       # Could define TARGET_OS_WIN32, but that is ultimately used by EndianPortable.c to set TARGET_RT_LITTLE_ENDIAN.
    elif conf.options.dest_platform in ['Linux-armhf', 'Linux-rpi']:
        conf.env.DEFINES_ALAC_APPLE = ['TARGET_RT_LITTLE_ENDIAN']       # EndianPortable.c does not handle '__arm__', so define TARGET_RT_LITTLE_ENDIAN here.
    conf.env.INCLUDES_ALAC_APPLE = [
        'thirdparty/apple_alac/codec/',
        ]

    # Setup FDK AAC lib.
    # FDK AAC is maintained as part of the Android Open Source Project (AOSP): https://android.googlesource.com/platform/external/aac/+/master/
    # However, we are using a stand-alone version with PowerPC optimisations maintained here: https://github.com/mstorsjo/fdk-aac
    # We are also using an older version, from before FDKv2, as FDKv2 causes memory corruption when attempting to decode any AAC file on Core-ppc32 platform, resulting in a crash (but works fine on all other platforms).
    # a30bfced6b6d6d976c728552d247cb30dd86e238
    # (Tue Mar 6 12:22:48 2018 +0200)
    conf.env.DEFINES_AAC_FDK = ['FDK_ASSERT_ENABLE']
    if conf.options.dest_platform not in ['Core-ppc32']:
        conf.env.append_value('DEFINES', ['FDK_LITTLE_ENDIAN']) # Not setting FDK_LITTLE_ENDIAN assumes big endian.
    conf.env.INCLUDES_AAC_FDK = [
        'thirdparty/fdk-aac/libAACdec/include',
        'thirdparty/fdk-aac/libFDK/include',
        'thirdparty/fdk-aac/libMpegTPDec/include',
        'thirdparty/fdk-aac/libPCMutils/include',
        'thirdparty/fdk-aac/libSBRdec/include',
        'thirdparty/fdk-aac/libSYS/include'
        ]

    # Setup Mad (mp3) lib options
    fixed_point_model = 'FPM_INTEL'
    if conf.options.with_default_fpm:
        fixed_point_model = 'FPM_DEFAULT'
    elif conf.options.dest_platform in ['Linux-ARM', 'Linux-armhf', 'Linux-arm64', 'Linux-rpi', 'Core-armv5', 'Core-armv6']:
        fixed_point_model = 'FPM_DEFAULT' # FIXME: was FPM_ARM, but failing to build on gcc-linaro-5.3.1
    elif conf.options.dest_platform in ['Linux-ppc32', 'Core-ppc32']:
        fixed_point_model = 'FPM_PPC'
    elif conf.options.dest_platform in ['Linux-x64', 'Windows-x64', 'Mac-x64']:
        fixed_point_model = 'FPM_64BIT'
    elif conf.options.dest_platform == 'Linux-mipsel':
        fixed_point_model = 'FPM_MIPS'
    conf.env.DEFINES_MAD = [fixed_point_model, 'OPT_ACCURACY']
    conf.env.INCLUDES_MAD = ['thirdparty/libmad-0.15.1b']
    if conf.options.dest_platform in ['Windows-x86', 'Windows-x64']:
        conf.env.DEFINES_MAD.append('HAVE_CONFIG_H')
        conf.env.INCLUDES_MAD.append('thirdparty/libmad-0.15.1b/msvc++')

    # Setup Vorbis lib options
    # Using https://git.xiph.org/?p=tremor.git
    # b56ffce0c0773ec5ca04c466bc00b1bbcaf65aef
    # (Sun, 4 Jan 2015 20:11:49 +0100 (19:11 +0000))
    if conf.options.dest_platform in ['Core-ppc32']:
        conf.env.DEFINES_VORBIS = ['BIG_ENDIAN', 'BYTE_ORDER=BIG_ENDIAN']
    conf.env.INCLUDES_VORBIS = [
        'thirdparty/Tremor',
        ]

class GeneratedFile(object):
    def __init__(self, xml, domain, type, version, target):
        self.xml = xml
        self.domain = domain
        self.type = type
        self.version = version
        self.target = target

upnp_services = [
        GeneratedFile('OpenHome/Av/ServiceXml/Upnp/AVTransport1.xml',       'upnp.org',        'AVTransport',       '1', 'UpnpOrgAVTransport1'),
        GeneratedFile('OpenHome/Av/ServiceXml/Upnp/ConnectionManager1.xml', 'upnp.org',        'ConnectionManager', '1', 'UpnpOrgConnectionManager1'),
        GeneratedFile('OpenHome/Av/ServiceXml/Upnp/RenderingControl1.xml',  'upnp.org',        'RenderingControl',  '1', 'UpnpOrgRenderingControl1'),
        GeneratedFile('OpenHome/Av/ServiceXml/Upnp/ContentDirectory1.xml',  'upnp.org',        'ContentDirectory',  '1', 'UpnpOrgContentDirectory1'),
        GeneratedFile('OpenHome/Av/ServiceXml/OpenHome/Product4.xml',       'av.openhome.org', 'Product',           '4', 'AvOpenhomeOrgProduct4'),
        GeneratedFile('OpenHome/Av/ServiceXml/OpenHome/Radio2.xml',         'av.openhome.org', 'Radio',             '2', 'AvOpenhomeOrgRadio2'),
        GeneratedFile('OpenHome/Av/ServiceXml/OpenHome/Sender2.xml',        'av.openhome.org', 'Sender',            '2', 'AvOpenhomeOrgSender2'),
        GeneratedFile('OpenHome/Av/ServiceXml/OpenHome/Playlist1.xml',      'av.openhome.org', 'Playlist',          '1', 'AvOpenhomeOrgPlaylist1'),
        GeneratedFile('OpenHome/Av/ServiceXml/OpenHome/Receiver1.xml',      'av.openhome.org', 'Receiver',          '1', 'AvOpenhomeOrgReceiver1'),
        GeneratedFile('OpenHome/Av/ServiceXml/OpenHome/Time1.xml',          'av.openhome.org', 'Time',              '1', 'AvOpenhomeOrgTime1'),
        GeneratedFile('OpenHome/Av/ServiceXml/OpenHome/Info1.xml',          'av.openhome.org', 'Info',              '1', 'AvOpenhomeOrgInfo1'),
        GeneratedFile('OpenHome/Av/ServiceXml/OpenHome/Volume4.xml',        'av.openhome.org', 'Volume',            '4', 'AvOpenhomeOrgVolume4'),
        GeneratedFile('OpenHome/Av/ServiceXml/OpenHome/Config2.xml',        'av.openhome.org', 'Config',            '2', 'AvOpenhomeOrgConfig2'),
        GeneratedFile('OpenHome/Av/ServiceXml/OpenHome/ConfigApp1.xml',     'av.openhome.org', 'ConfigApp',         '1', 'AvOpenhomeOrgConfigApp1'),
        GeneratedFile('OpenHome/Av/ServiceXml/OpenHome/Credentials1.xml',   'av.openhome.org', 'Credentials',       '1', 'AvOpenhomeOrgCredentials1'),
        GeneratedFile('OpenHome/Av/ServiceXml/OpenHome/Debug2.xml',         'av.openhome.org', 'Debug',             '2', 'AvOpenhomeOrgDebug2'),
        GeneratedFile('OpenHome/Av/ServiceXml/OpenHome/Transport1.xml',     'av.openhome.org', 'Transport',         '1', 'AvOpenhomeOrgTransport1'),
        GeneratedFile('OpenHome/Av/ServiceXml/OpenHome/Pins1.xml',          'av.openhome.org', 'Pins',              '1', 'AvOpenhomeOrgPins1'),
        GeneratedFile('OpenHome/Av/ServiceXml/OpenHome/OAuth1.xml',         'av.openhome.org', 'OAuth',             '1', 'AvOpenhomeOrgOAuth1'),
    ]

def build(bld):

    # Generated provider base classes
    service_gen_path = os.path.join(bld.env['SERVICE_GEN_DIR'], 'ServiceGen.py')
    output_dir = 'Generated'
    for service in upnp_services:
        for template_file, prefix, ext in [
            ('DvUpnpCppCoreHeader.py', 'Dv', '.h'),
            ('DvUpnpCppCoreSource.py', 'Dv', '.cpp'),
            ('CpUpnpCppBufferHeader.py', 'Cp', '.h'),
            ('CpUpnpCppBufferSource.py', 'Cp', '.cpp')
        ]:
            output_file = bld.path.find_or_declare(os.path.join(output_dir, prefix + service.target + ext))
            bld( rule='python ' + service_gen_path +
                      ' -t ' + template_file +
                      ' -o ' + output_dir +
                      ' -x ' + os.path.abspath(service.xml) +
                      ' -d ' + service.domain +
                      ' -y ' + service.type +
                      ' -v ' + service.version,
                 source=service.xml,
                 target=output_file)
    bld.add_group()

    # Copy ConfigUi resources to 'build' and 'install/bin'.
    confui_node = find_dir_or_fail(bld, bld.path, os.path.join('OpenHome', 'Web', 'ConfigUi', 'res'))
    confui_files = confui_node.ant_glob('**/*')
    cwd = confui_node.path_from(bld.path)
    create_copy_task(bld, confui_files, Node, 'res', cwd, True, 'WebUiStatic')
    # Also copy to install/bin/
    install_path = os.path.join('..', 'install', 'bin', 'res')
    create_copy_task(bld, confui_files, Node, install_path, cwd, True, None)

    # rebuild if ohNet libraries, but not headers, are updated
    # skip libplatform binaries (which are munged into STLIB_OHNET to avoid
    # making USE platform-specific for all executables)
    for lib in bld.env['STLIB_OHNET']:
        if lib not in ['target', 'platform']:
            bld.read_stlib(lib, paths=[bld.env['STLIBPATH_OHNET']])

    # Library
    bld.stlib(
            source=[
                'OpenHome/Media/Pipeline/VolumeRamper.cpp',
                'OpenHome/Media/Pipeline/AudioDumper.cpp',
                'OpenHome/Media/Pipeline/AudioReservoir.cpp',
                'OpenHome/Media/Pipeline/DecodedAudioAggregator.cpp',
                'OpenHome/Media/Pipeline/DecodedAudioReservoir.cpp',
                'OpenHome/Media/Pipeline/DecodedAudioValidator.cpp',
                'OpenHome/Media/Pipeline/Drainer.cpp',
                'OpenHome/Media/Pipeline/EncodedAudioReservoir.cpp',
                'OpenHome/Media/Pipeline/Flusher.cpp',
                'OpenHome/Media/Pipeline/Logger.cpp',
                'OpenHome/Media/Pipeline/Msg.cpp',
                'OpenHome/Media/Pipeline/Muter.cpp',
                'OpenHome/Media/Pipeline/MuterVolume.cpp',
                'OpenHome/Media/Pipeline/PreDriver.cpp',
                'OpenHome/Media/Pipeline/Attenuator.cpp',
                'OpenHome/Media/Pipeline/Ramper.cpp',
                'OpenHome/Media/Pipeline/Reporter.cpp',
                'OpenHome/Media/Pipeline/AsyncTrackReporter.cpp',
                'OpenHome/Media/Pipeline/AirplayReporter.cpp',
                'OpenHome/Media/Pipeline/SpotifyReporter.cpp',
                'OpenHome/Media/Pipeline/RampValidator.cpp',
                'OpenHome/Media/Pipeline/Rewinder.cpp',
                'OpenHome/Media/Pipeline/Router.cpp',
                'OpenHome/Media/Pipeline/StreamValidator.cpp',
                'OpenHome/Media/Pipeline/Seeker.cpp',
                'OpenHome/Media/Pipeline/PhaseAdjuster.cpp',
                'OpenHome/Media/Pipeline/Skipper.cpp',
                'OpenHome/Media/Pipeline/StarterTimed.cpp',
                'OpenHome/Media/Pipeline/StarvationRamper.cpp',
                'OpenHome/Media/Pipeline/Stopper.cpp',
                'OpenHome/Media/Pipeline/TrackInspector.cpp',
                'OpenHome/Media/Pipeline/VariableDelay.cpp',
                'OpenHome/Media/Pipeline/Waiter.cpp',
                'OpenHome/Media/Pipeline/Pipeline.cpp',
                'OpenHome/Media/Pipeline/ElementObserver.cpp',
                'OpenHome/Media/ClockPuller.cpp',
                'OpenHome/Media/IdManager.cpp',
                'OpenHome/Media/Filler.cpp',
                'OpenHome/Media/Supply.cpp',
                'OpenHome/Media/SupplyAggregator.cpp',
                'OpenHome/Media/Utils/AnimatorBasic.cpp',
                'OpenHome/Media/Utils/ProcessorAudioUtils.cpp',
                'OpenHome/Media/Utils/ClockPullerManual.cpp',
                'OpenHome/Media/Codec/Mpeg4.cpp',
                'OpenHome/Media/Codec/Container.cpp',
                'OpenHome/Media/Codec/Id3v2.cpp',
                'OpenHome/Media/Codec/MpegTs.cpp',
                'OpenHome/Media/Codec/CodecController.cpp',
                'OpenHome/Media/Codec/DsdFiller.cpp',
                'OpenHome/Media/Protocol/Protocol.cpp',
                'OpenHome/Media/Protocol/ProtocolHls.cpp',
                'OpenHome/Media/Protocol/ProtocolHttp.cpp',
                'OpenHome/Media/Protocol/ProtocolFile.cpp',
                'OpenHome/Media/Protocol/ProtocolTone.cpp',
                'OpenHome/Media/Protocol/Icy.cpp',
                'OpenHome/Media/Protocol/Rtsp.cpp',
                'OpenHome/Media/Protocol/ProtocolRtsp.cpp',
                'OpenHome/Media/Protocol/ContentAudio.cpp',
                'OpenHome/Media/Protocol/ContentMpd.cpp',
                'OpenHome/Media/UriProviderRepeater.cpp',
                'OpenHome/Media/UriProviderSingleTrack.cpp',
                'OpenHome/Media/PipelineManager.cpp',
                'OpenHome/Media/PipelineObserver.cpp',
                'OpenHome/Media/MuteManager.cpp',
                'OpenHome/Media/FlywheelRamper.cpp',
                'OpenHome/Media/MimeTypeList.cpp',
                'OpenHome/Media/Utils/AllocatorInfoLogger.cpp', # needed here by MediaPlayer.  Should move back to tests lib
                'OpenHome/Configuration/ConfigManager.cpp',
                'OpenHome/Media/Utils/Silencer.cpp',
                'OpenHome/SocketHttp.cpp',
                'OpenHome/SocketSsl.cpp',
                'OpenHome/Av/OhMetadata.cpp',
            ],
            use=['SSL', 'ohNetCore', 'OHNET'],
            target='ohPipeline')

    # Library
    bld.stlib(
            source=[
                'OpenHome/Av/Utils/FaultCode.cpp',
                'OpenHome/Av/KvpStore.cpp',
                'OpenHome/Av/ProviderUtils.cpp',
                'OpenHome/Av/Product.cpp',
                'Generated/DvAvOpenhomeOrgProduct4.cpp',
                'Generated/CpAvOpenhomeOrgProduct4.cpp',
                'OpenHome/Av/ProviderProduct.cpp',
                'Generated/DvAvOpenhomeOrgTime1.cpp',
                'OpenHome/Av/ProviderTime.cpp',
                'Generated/DvAvOpenhomeOrgInfo1.cpp',
                'OpenHome/Av/ProviderInfo.cpp',
                'Generated/DvAvOpenhomeOrgTransport1.cpp',
                'Generated/CpAvOpenhomeOrgTransport1.cpp',
                'OpenHome/Av/TransportControl.cpp',
                'OpenHome/Av/ProviderTransport.cpp',
                'OpenHome/Av/Pins/TransportPins.cpp',
                'OpenHome/Av/Radio/TuneInPins.cpp',
                'OpenHome/Av/Radio/RadioPins.cpp',
                'OpenHome/Av/Pins/UrlPins.cpp',
                'OpenHome/Av/CalmRadio/CalmRadioPins.cpp',
                'Generated/CpAvOpenhomeOrgRadio2.cpp',
                'Generated/DvAvOpenhomeOrgVolume4.cpp',
                'OpenHome/Av/ProviderVolume.cpp',
                'OpenHome/Av/Source.cpp',
                'OpenHome/Av/MediaPlayer.cpp',
                'OpenHome/Av/Logger.cpp',
                'OpenHome/Av/DeviceAnnouncerMdns.cpp',
                'Generated/DvAvOpenhomeOrgConfig2.cpp',
                'OpenHome/AESHelpers.cpp',
                'OpenHome/Json.cpp',
                'OpenHome/OAuth.cpp',
                'Generated/DvAvOpenhomeOrgOAuth1.cpp',
                'OpenHome/Av/Utils/FormUrl.cpp',
                'OpenHome/NtpClient.cpp',
                'OpenHome/UnixTimestamp.cpp',
                'OpenHome/Configuration/ProviderConfig.cpp',
                'Generated/DvAvOpenhomeOrgConfigApp1.cpp',
                'OpenHome/Configuration/ProviderConfigApp.cpp',
                'OpenHome/PowerManager.cpp',
                'OpenHome/ThreadPool.cpp',
                'OpenHome/FsFlushPeriodic.cpp',
                'OpenHome/Av/Credentials.cpp',
                'Generated/DvAvOpenhomeOrgCredentials1.cpp',
                'OpenHome/Av/ProviderCredentials.cpp',
                'OpenHome/Av/ProviderOAuth.cpp',
                'OpenHome/Av/VolumeManager.cpp',
                'OpenHome/Av/FriendlyNameAdapter.cpp',
                'Generated/DvAvOpenhomeOrgDebug2.cpp',
                'OpenHome/Av/ProviderDebug.cpp',
                'OpenHome/Av/Pins/Pins.cpp',
                'Generated/DvAvOpenhomeOrgPins1.cpp',
                'OpenHome/Av/Pins/ProviderPins.cpp',
            ],
            use=['OHNET', 'SSL', 'ohPipeline'],
            target='ohMediaPlayer')

    bld.stlib(
            source=[
                'OpenHome/Net/Odp/Odp.cpp',
                'OpenHome/Net/Odp/DviOdp.cpp',
                'OpenHome/Net/Odp/DviProtocolOdp.cpp',
                'OpenHome/Net/Odp/DviServerOdp.cpp',
                'OpenHome/Net/Odp/CpiOdp.cpp',
                'OpenHome/Net/Odp/CpiDeviceOdp.cpp',
                'OpenHome/Net/Odp/CpDeviceOdp.cpp',
            ],
            use=['OHNET'],
            target='Odp')


    # Library
    bld.stlib(
            source=[
                'Generated/DvAvOpenhomeOrgPlaylist1.cpp',
                'Generated/CpAvOpenhomeOrgPlaylist1.cpp',
                'OpenHome/Av/Playlist/ProviderPlaylist.cpp',
                'OpenHome/Av/Playlist/SourcePlaylist.cpp',
                'OpenHome/Av/Playlist/TrackDatabase.cpp',
                'OpenHome/Av/Playlist/UriProviderPlaylist.cpp',
                'OpenHome/Av/Tidal/Tidal.cpp',
                'OpenHome/Av/Tidal/TidalMetadata.cpp',
                'OpenHome/Av/Tidal/TidalPins.cpp',
                'OpenHome/Av/Tidal/ProtocolTidal.cpp',
                'OpenHome/Av/Qobuz/Qobuz.cpp',
                'OpenHome/Av/Qobuz/QobuzMetadata.cpp',
                'OpenHome/Av/Qobuz/QobuzPins.cpp',
                'OpenHome/Av/Qobuz/ProtocolQobuz.cpp',
                'Generated/CpAvOpenhomeOrgTransport1.cpp',
                'OpenHome/Av/Playlist/PinInvokerPlaylist.cpp',
                'OpenHome/Av/Playlist/DeviceListMediaServer.cpp',
                'OpenHome/Av/Playlist/PinInvokerKazooServer.cpp',
                'OpenHome/Av/Playlist/PinInvokerUpnpServer.cpp',
                'Generated/CpUpnpOrgContentDirectory1.cpp',
            ],
            use=['OHNET', 'ohMediaPlayer', 'Podcast', 'SSL'],
            target='SourcePlaylist')

    # Library
    bld.stlib(
            source=[
                'OpenHome/Av/Radio/SourceRadio.cpp',
                'OpenHome/Av/Radio/PresetDatabase.cpp',
                'OpenHome/Av/Radio/UriProviderRadio.cpp',
                'OpenHome/Av/Radio/Presets.cpp',
                'OpenHome/Av/Radio/TuneIn.cpp',
                'OpenHome/Av/CalmRadio/CalmRadio.cpp',
                'OpenHome/Av/CalmRadio/ProtocolCalmRadio.cpp',
                'OpenHome/Av/Radio/ContentAsx.cpp',
                'OpenHome/Av/Radio/ContentM3u.cpp',
                'OpenHome/Av/Radio/ContentM3uX.cpp',
                'OpenHome/Av/Radio/ContentOpml.cpp',
                'OpenHome/Av/Radio/ContentPls.cpp',
                'Generated/DvAvOpenhomeOrgRadio2.cpp',
                'OpenHome/Av/Radio/ProviderRadio.cpp',
            ],
            use=['OHNET', 'ohMediaPlayer', 'Podcast'],
            target='SourceRadio')

    # Library
    bld.stlib(
            source=[
                'Generated/DvAvOpenhomeOrgSender2.cpp',
                'OpenHome/Av/Songcast/Ohm.cpp',
                'OpenHome/Av/Songcast/OhmMsg.cpp',
                'OpenHome/Av/Songcast/OhmSender.cpp',
                'OpenHome/Av/Songcast/OhmSocket.cpp',
                'OpenHome/Av/Songcast/ProtocolOhBase.cpp',
                'OpenHome/Av/Songcast/ProtocolOhu.cpp',
                'OpenHome/Av/Songcast/ProtocolOhm.cpp',
                'Generated/DvAvOpenhomeOrgReceiver1.cpp',
                'OpenHome/Av/Songcast/ProviderReceiver.cpp',
                'OpenHome/Av/Songcast/ZoneHandler.cpp',
                'OpenHome/Av/Songcast/SourceReceiver.cpp',
                'OpenHome/Av/Songcast/Splitter.cpp',
                'OpenHome/Av/Songcast/Sender.cpp',
                'OpenHome/Av/Songcast/SenderThread.cpp',
                'OpenHome/Av/Utils/DriverSongcastSender.cpp',
            ],
            use=['OHNET', 'ohMediaPlayer'],
            target='SourceSongcast')

    # Library
    bld.stlib(
            source=[
                'OpenHome/Av/Scd/ScdMsg.cpp',
                'OpenHome/Av/Scd/Receiver/ProtocolScd.cpp',
                'OpenHome/Av/Scd/Receiver/SupplyScd.cpp',
                'OpenHome/Av/Scd/Receiver/UriProviderScd.cpp',
                'OpenHome/Av/Scd/Receiver/SourceScd.cpp'
            ],
            use=['OHNET', 'ohMediaPlayer'],
            target='SourceScd')

    # Library
    bld.stlib(
            source=[
                'OpenHome/Av/Raop/Raop.cpp',
                'OpenHome/Av/Raop/SourceRaop.cpp',
                'OpenHome/Av/Raop/ProtocolRaop.cpp',
                'OpenHome/Av/Raop/UdpServer.cpp',
                'OpenHome/Av/Raop/CodecRaopApple.cpp',
            ],
            use=['OHNET', 'SSL', 'ohMediaPlayer', 'CodecAlacAppleBase'],
            target='SourceRaop')

    # Library
    bld.stlib(
            source=[
                'Generated/DvUpnpOrgAVTransport1.cpp',
                'OpenHome/Av/UpnpAv/ProviderAvTransport.cpp',
                'Generated/DvUpnpOrgConnectionManager1.cpp',
                'OpenHome/Av/UpnpAv/ProviderConnectionManager.cpp',
                'Generated/DvUpnpOrgRenderingControl1.cpp',
                'OpenHome/Av/UpnpAv/ProviderRenderingControl.cpp',
                'OpenHome/Av/UpnpAv/UpnpAv.cpp',
                'OpenHome/Av/UpnpAv/FriendlyNameUpnpAv.cpp'
            ],
            use=['OHNET', 'ohMediaPlayer'],
            target='SourceUpnpAv')

    # RAAT
    if 'RAAT_ENABLE' in bld.env.DEFINES:
        bld.stlib(
                source=[
                    'OpenHome/Av/Raat/Artwork.cpp',
                    'OpenHome/Av/Raat/App.cpp',
                    'OpenHome/Av/Raat/Metadata.cpp',
                    'OpenHome/Av/Raat/Output.cpp',
                    'OpenHome/Av/Raat/Volume.cpp',
                    'OpenHome/Av/Raat/SourceSelection.cpp',
                    'OpenHome/Av/Raat/Transport.cpp',
                    'OpenHome/Av/Raat/Plugin.cpp',
                    'OpenHome/Av/Raat/ProtocolRaat.cpp',
                    'OpenHome/Av/Raat/SourceRaat.cpp'
                ],
                use=['OHNET', 'ohMediaPlayer', 'RAAT'],
                target='SourceRaat')

    # Podcast
    bld.stlib(
            source=[
                'OpenHome/Av/Pins/PodcastPins.cpp',
                'OpenHome/Av/Pins/PodcastPinsITunes.cpp',
                'OpenHome/Av/Pins/PodcastPinsTuneIn.cpp'
            ],
            use=['OHNET', 'ohMediaPlayer'],
            target='Podcast')

    # Wav
    bld.stlib(
            source=['OpenHome/Media/Codec/Wav.cpp'],
            use=['OHNET'],
            target='CodecWav')

    # PCM
    bld.stlib(
            source=['OpenHome/Media/Codec/Pcm.cpp'],
            use=['OHNET'],
            target='CodecPcm')

    # DSD
    bld.stlib(
            source=['OpenHome/Media/Codec/DsdDsf.cpp'],
            use=['OHNET'],
            target='CodecDsdDsf')

    # DSDDFF
    bld.stlib(
            source=['OpenHome/Media/Codec/DsdDff.cpp'],
            use=['OHNET'],
            target='CodecDsdDff')

    # DSDDFF
    bld.stlib(
            source=['OpenHome/Media/Codec/DsdRaw.cpp'],
            use=['OHNET', 'ohPipeline'],
            target='CodecDsdRaw')

    # AiffBase
    bld.stlib(
            source=['OpenHome/Media/Codec/AiffBase.cpp'],
            use=['OHNET'],
            target='CodecAiffBase')

    # AIFC
    bld.stlib(
            source=['OpenHome/Media/Codec/Aifc.cpp'],
            use=['CodecAiffBase', 'OHNET'],
            target='CodecAifc')

    # AIFF
    bld.stlib(
            source=['OpenHome/Media/Codec/Aiff.cpp'],
            use=['CodecAiffBase', 'OHNET'],
            target='CodecAiff')

    # Ogg
    bld.stlib(
            source=[
                'thirdparty/libogg/src/bitwise.c',
                'thirdparty/libogg/src/framing.c'
            ],
            use=['OGG'],
            target='libOgg')

    # Flac
    bld.stlib(
            source=[
                'OpenHome/Media/Codec/Flac.cpp',
                'thirdparty/flac-1.2.1/src/libFLAC/bitreader.c',
                'thirdparty/flac-1.2.1/src/libFLAC/bitmath.c',
                'thirdparty/flac-1.2.1/src/libFLAC/cpu.c',
                'thirdparty/flac-1.2.1/src/libFLAC/crc.c',
                'thirdparty/flac-1.2.1/src/libFLAC/fixed.c',
                'thirdparty/flac-1.2.1/src/libFLAC/format.c',
                'thirdparty/flac-1.2.1/src/libFLAC/lpc.c',
                'thirdparty/flac-1.2.1/src/libFLAC/md5.c',
                'thirdparty/flac-1.2.1/src/libFLAC/memory.c',
                'thirdparty/flac-1.2.1/src/libFLAC/stream_decoder.c',
                'thirdparty/flac-1.2.1/src/libFLAC/ogg_decoder_aspect.c',
                'thirdparty/flac-1.2.1/src/libFLAC/ogg_mapping.c',
            ],
            use=['FLAC', 'OGG', 'libOgg', 'OHNET'],
            shlib=['m'],
            target='CodecFlac')

    # AlacAppleBase
    bld.stlib(
            source=[
                'OpenHome/Media/Codec/AlacAppleBase.cpp',
                'thirdparty/apple_alac/codec/ag_dec.c',
                'thirdparty/apple_alac/codec/ALACDecoder.cpp',
                'thirdparty/apple_alac/codec/ALACBitUtilities.c',
                'thirdparty/apple_alac/codec/dp_dec.c',
                'thirdparty/apple_alac/codec/EndianPortable.c',
                'thirdparty/apple_alac/codec/matrix_dec.c',
            ],
            use=['ALAC_APPLE', 'OHNET', 'ohMediaPlayer'],
            target='CodecAlacAppleBase')

    # AlacApple
    bld.stlib(
            source=[
                'OpenHome/Media/Codec/AlacApple.cpp',
            ],
            use=['CodecAlacAppleBase', 'OHNET'],
            target='CodecAlacApple')

    # AacFdk (raw decoder only; no codec wrapper).
    aac_fdk = bld.stlib(
            source=[
                'thirdparty/fdk-aac/libAACdec/src/aacdec_drc.cpp',
                'thirdparty/fdk-aac/libAACdec/src/aacdec_hcr_bit.cpp',
                'thirdparty/fdk-aac/libAACdec/src/aacdec_hcr.cpp',
                'thirdparty/fdk-aac/libAACdec/src/aacdec_hcrs.cpp',
                'thirdparty/fdk-aac/libAACdec/src/aacdecoder.cpp',
                'thirdparty/fdk-aac/libAACdec/src/aacdecoder_lib.cpp',
                'thirdparty/fdk-aac/libAACdec/src/aacdec_pns.cpp',
                'thirdparty/fdk-aac/libAACdec/src/aacdec_tns.cpp',
                'thirdparty/fdk-aac/libAACdec/src/aac_ram.cpp',
                'thirdparty/fdk-aac/libAACdec/src/aac_rom.cpp',
                'thirdparty/fdk-aac/libAACdec/src/block.cpp',
                'thirdparty/fdk-aac/libAACdec/src/channel.cpp',
                'thirdparty/fdk-aac/libAACdec/src/channelinfo.cpp',
                'thirdparty/fdk-aac/libAACdec/src/conceal.cpp',
                'thirdparty/fdk-aac/libAACdec/src/ldfiltbank.cpp',
                'thirdparty/fdk-aac/libAACdec/src/pulsedata.cpp',
                'thirdparty/fdk-aac/libAACdec/src/rvlcbit.cpp',
                'thirdparty/fdk-aac/libAACdec/src/rvlcconceal.cpp',
                'thirdparty/fdk-aac/libAACdec/src/rvlc.cpp',
                'thirdparty/fdk-aac/libAACdec/src/stereo.cpp',

                'thirdparty/fdk-aac/libFDK/src/autocorr2nd.cpp',
                'thirdparty/fdk-aac/libFDK/src/dct.cpp',
                'thirdparty/fdk-aac/libFDK/src/FDK_bitbuffer.cpp',
                'thirdparty/fdk-aac/libFDK/src/FDK_core.cpp',
                'thirdparty/fdk-aac/libFDK/src/FDK_crc.cpp',
                'thirdparty/fdk-aac/libFDK/src/FDK_hybrid.cpp',
                'thirdparty/fdk-aac/libFDK/src/FDK_tools_rom.cpp',
                'thirdparty/fdk-aac/libFDK/src/FDK_trigFcts.cpp',
                'thirdparty/fdk-aac/libFDK/src/fft.cpp',
                'thirdparty/fdk-aac/libFDK/src/fft_rad2.cpp',
                'thirdparty/fdk-aac/libFDK/src/fixpoint_math.cpp',
                'thirdparty/fdk-aac/libFDK/src/mdct.cpp',
                'thirdparty/fdk-aac/libFDK/src/qmf.cpp',
                'thirdparty/fdk-aac/libFDK/src/scale.cpp',

                'thirdparty/fdk-aac/libMpegTPDec/src/tpdec_adif.cpp',
                'thirdparty/fdk-aac/libMpegTPDec/src/tpdec_adts.cpp',
                'thirdparty/fdk-aac/libMpegTPDec/src/tpdec_asc.cpp',
                'thirdparty/fdk-aac/libMpegTPDec/src/tpdec_drm.cpp',
                'thirdparty/fdk-aac/libMpegTPDec/src/tpdec_latm.cpp',
                'thirdparty/fdk-aac/libMpegTPDec/src/tpdec_lib.cpp',

                'thirdparty/fdk-aac/libPCMutils/src/limiter.cpp',
                'thirdparty/fdk-aac/libPCMutils/src/pcmutils_lib.cpp',

                'thirdparty/fdk-aac/libSBRdec/src/env_calc.cpp',
                'thirdparty/fdk-aac/libSBRdec/src/env_dec.cpp',
                'thirdparty/fdk-aac/libSBRdec/src/env_extr.cpp',
                'thirdparty/fdk-aac/libSBRdec/src/huff_dec.cpp',
                'thirdparty/fdk-aac/libSBRdec/src/lpp_tran.cpp',
                'thirdparty/fdk-aac/libSBRdec/src/psbitdec.cpp',
                'thirdparty/fdk-aac/libSBRdec/src/psdec.cpp',
                'thirdparty/fdk-aac/libSBRdec/src/psdec_hybrid.cpp',
                'thirdparty/fdk-aac/libSBRdec/src/sbr_crc.cpp',
                'thirdparty/fdk-aac/libSBRdec/src/sbr_deb.cpp',
                'thirdparty/fdk-aac/libSBRdec/src/sbr_dec.cpp',
                'thirdparty/fdk-aac/libSBRdec/src/sbrdec_drc.cpp',
                'thirdparty/fdk-aac/libSBRdec/src/sbrdec_freq_sca.cpp',
                'thirdparty/fdk-aac/libSBRdec/src/sbrdecoder.cpp',
                'thirdparty/fdk-aac/libSBRdec/src/sbr_ram.cpp',
                'thirdparty/fdk-aac/libSBRdec/src/sbr_rom.cpp',

                'thirdparty/fdk-aac/libSYS/src/cmdl_parser.cpp',
                'thirdparty/fdk-aac/libSYS/src/conv_string.cpp',
                'thirdparty/fdk-aac/libSYS/src/genericStds.cpp',
                'thirdparty/fdk-aac/libSYS/src/wav_file.cpp'
            ],
            use=['AAC_FDK', 'OHNET'],
            target='CodecAacFdk')
    # Ignore all warnings for third-party libfdk-aac decoder.
    if bld.env.CXX_NAME == 'msvc':
        aac_fdk.cxxflags=['/w']
    elif bld.env.CXX_NAME == 'g++' or bld.env.CXX_NAME == 'gcc':
        aac_fdk.cxxflags=['-w']
    elif bld.env.CXX_NAME == 'clang++' or bld.env.CXX_NAME == 'clang':
        aac_fdk.cxxflags=['-w']

    # AacFdkBase
    bld.stlib(
            source=[
                'OpenHome/Media/Codec/AacFdkBase.cpp'
            ],
            use=['CodecAacFdk', 'OHNET'],
            target='CodecAacFdkBase')

    # AacFdkMp4
    bld.stlib(
            source=[
                 'OpenHome/Media/Codec/AacFdkMp4.cpp',
            ],
            use=['CodecAacFdkBase', 'OHNET'],
            target='CodecAacFdkMp4')

    # AacFdkAdts
    bld.stlib(
            source=[
                 'OpenHome/Media/Codec/AacFdkAdts.cpp',
            ],
            use=['CodecAacFdkBase', 'OHNET'],
            target='CodecAacFdkAdts')

    # MP3
    bld.stlib(
            source=[
                'OpenHome/Media/Codec/Mp3.cpp',
                'thirdparty/libmad-0.15.1b/version.c',
                'thirdparty/libmad-0.15.1b/fixed.c',
                'thirdparty/libmad-0.15.1b/bit.c',
                'thirdparty/libmad-0.15.1b/timer.c',
                'thirdparty/libmad-0.15.1b/stream.c',
                'thirdparty/libmad-0.15.1b/frame.c',
                'thirdparty/libmad-0.15.1b/synth.c',
                'thirdparty/libmad-0.15.1b/decoder.c',
                'thirdparty/libmad-0.15.1b/layer12.c',
                'thirdparty/libmad-0.15.1b/layer3.c',
                'thirdparty/libmad-0.15.1b/huffman.c',
            ],
            use=['MAD', 'OHMEDIAPLAYER', 'OHNET'],
            target='CodecMp3')

    # Vorbis
    vorbis = bld.stlib(
            source=[
                'OpenHome/Media/Codec/Vorbis.cpp',
                'thirdparty/Tremor/block.c',
                'thirdparty/Tremor/codebook.c',
                'thirdparty/Tremor/floor0.c',
                'thirdparty/Tremor/floor1.c',
                'thirdparty/Tremor/info.c',
                'thirdparty/Tremor/mapping0.c',
                'thirdparty/Tremor/mdct.c',
                'thirdparty/Tremor/registry.c',
                'thirdparty/Tremor/res012.c',
                'thirdparty/Tremor/sharedbook.c',
                'thirdparty/Tremor/synthesis.c',
                'thirdparty/Tremor/vorbisfile.c',
                'thirdparty/Tremor/window.c',
            ],
            use=['VORBIS', 'OGG', 'libOgg', 'OHNET'],
            target='CodecVorbis')
    # Vorbis decoder reports warnings under msvc compiler. Ignore these as it is third-party code.
    if bld.env.CC_NAME == 'msvc':
        vorbis.cflags=['/w']

    # WebAppFramework
    bld.stlib(
        source=[
            'OpenHome/Web/ResourceHandler.cpp',
            'OpenHome/Web/WebAppFramework.cpp',
        ],
        use=['ohNetCore', 'OHNET', 'OHMEDIAPLAYER', 'PLATFORM', 'WebUiStatic'],
        target='WebAppFramework')

    # WebAppFramework tests
    bld.stlib(
        source=[
            'OpenHome/Web/Tests/TestWebAppFramework.cpp',
        ],
        use=['WebAppFramework', 'OHMEDIAPLAYER', 'OHNET', 'PLATFORM'],
        target='WebAppFrameworkTestUtils')

    # ConfigUi
    bld.stlib(
        source=[
            'OpenHome/Web/ConfigUi/ConfigUi.cpp',
            'OpenHome/Web/ConfigUi/FileResourceHandler.cpp',
            'OpenHome/Web/ConfigUi/ConfigUiMediaPlayer.cpp',
        ],
        use=['WebAppFramework', 'OHMEDIAPLAYER', 'OHNET', 'PLATFORM'],
        target='ConfigUi')

    # ConfigUi tests
    bld.stlib(
        source=[
            'OpenHome/Web/ConfigUi/Tests/TestConfigUi.cpp'
        ],
        use=['ConfigUi', 'WebAppFramework', 'OHMEDIAPLAYER', 'OHNET', 'PLATFORM', 'SSL'],
        target='ConfigUiTestUtils')

    # Tests
    bld.stlib(
            source=[
                'OpenHome/Av/Tests/TestStore.cpp',
                'OpenHome/Av/Tests/RamStore.cpp',
                'OpenHome/Media/Tests/TestMsg.cpp',
                'OpenHome/Media/Tests/TestStarvationRamper.cpp',
                'OpenHome/Media/Tests/TestStreamValidator.cpp',
                'OpenHome/Media/Tests/TestSeeker.cpp',
                'OpenHome/Media/Tests/TestSkipper.cpp',
                'OpenHome/Media/Tests/TestStopper.cpp',
                'OpenHome/Media/Tests/TestWaiter.cpp',
                'OpenHome/Media/Tests/TestSupply.cpp',
                'OpenHome/Media/Tests/TestSupplyAggregator.cpp',
                'OpenHome/Media/Tests/TestAudioReservoir.cpp',
                'OpenHome/Media/Tests/TestVariableDelay.cpp',
                'OpenHome/Media/Tests/TestTrackInspector.cpp',
                'OpenHome/Media/Tests/TestRamper.cpp',
                'OpenHome/Media/Tests/TestFlywheelRamper.cpp',
                'OpenHome/Media/Tests/TestReporter.cpp',
                'OpenHome/Media/Tests/TestSpotifyReporter.cpp',
                'OpenHome/Media/Tests/TestPreDriver.cpp',
                'OpenHome/Media/Tests/TestVolumeRamper.cpp',
                'OpenHome/Media/Tests/TestMuter.cpp',
                'OpenHome/Media/Tests/TestMuterVolume.cpp',
                'OpenHome/Media/Tests/TestDrainer.cpp',
                'OpenHome/Media/Tests/TestStarterTimed.cpp',
                'OpenHome/Av/Tests/TestContentProcessor.cpp',
                'OpenHome/Media/Tests/TestPipeline.cpp',
                'OpenHome/Media/Tests/TestPipelineConfig.cpp',
                'OpenHome/Media/Tests/TestProtocolHls.cpp',
                'OpenHome/Media/Tests/TestProtocolHttp.cpp',
                'OpenHome/Media/Tests/TestCodec.cpp',
                'OpenHome/Media/Tests/TestCodecInit.cpp',
                'OpenHome/Media/Tests/TestCodecController.cpp',
                'OpenHome/Media/Tests/TestDecodedAudioAggregator.cpp',
                'OpenHome/Media/Tests/TestContainer.cpp',
                'OpenHome/Media/Tests/TestSilencer.cpp',
                'OpenHome/Media/Tests/TestIdProvider.cpp',
                'OpenHome/Media/Tests/TestFiller.cpp',
                'OpenHome/Media/Tests/TestToneGenerator.cpp',
                'OpenHome/Media/Tests/TestMuteManager.cpp',
                'OpenHome/Media/Tests/TestRewinder.cpp',
                'OpenHome/Media/Tests/TestShell.cpp',
                'OpenHome/Media/Tests/TestPhaseAdjuster.cpp',
                'OpenHome/Media/Tests/TestUriProviderRepeater.cpp',
                'OpenHome/Av/Tests/TestFriendlyNameManager.cpp',
                'OpenHome/Av/Tests/TestUdpServer.cpp',
                'OpenHome/Av/Tests/TestUpnpErrors.cpp',
                'Generated/CpUpnpOrgAVTransport1.cpp',
                'Generated/CpUpnpOrgConnectionManager1.cpp',
                'Generated/CpUpnpOrgRenderingControl1.cpp',
                'OpenHome/Av/Tests/TestTrackDatabase.cpp',
                #'OpenHome/Av/Tests/TestPlaylist.cpp',
                'OpenHome/Av/Tests/TestMediaPlayer.cpp',
                'OpenHome/Av/Tests/TestMediaPlayerOptions.cpp',
                'OpenHome/Configuration/Tests/ConfigRamStore.cpp',
                'OpenHome/Configuration/Tests/TestConfigManager.cpp',
                'OpenHome/Tests/TestPipe.cpp',
                'OpenHome/Tests/Mock.cpp',
                'OpenHome/Tests/TestPowerManager.cpp',
                'OpenHome/Tests/TestSsl.cpp',
                'OpenHome/Tests/TestSocket.cpp',
                'OpenHome/Av/Tests/TestCredentials.cpp',
                'Generated/CpAvOpenhomeOrgCredentials1.cpp',
                'OpenHome/Tests/TestJson.cpp',
                'OpenHome/Tests/TestObservable.cpp',
                'OpenHome/Tests/TestAESHelpers.cpp',
                'OpenHome/Tests/TestThreadPool.cpp',
                'OpenHome/Av/Tests/TestRaop.cpp',
                'OpenHome/Av/Tests/TestVolumeManager.cpp',
                'OpenHome/Av/Tests/TestPins.cpp',
                'OpenHome/Av/Tests/TestOhMetadata.cpp',
                'OpenHome/Av/Tests/TestSenderQueue.cpp',
                'OpenHome/Net/Odp/Tests/TestDvOdp.cpp',
                'OpenHome/Tests/TestOAuth.cpp',
                'OpenHome/Media/Tests/TestContentMpd.cpp',
            ],
            use=['ConfigUi', 'WebAppFramework', 'ohMediaPlayer', 'WebAppFramework', 'CodecFlac', 'CodecWav', 'CodecPcm', 'CodecDsdDsf', 'CodecDsdDff', 'CodecDsdRaw',  'CodecAlac', 'CodecAlacApple', 'CodecAifc', 'CodecAiff', 'CodecAacFdkAdts', 'CodecAacFdkMp4', 'CodecMp3', 'CodecVorbis', 'Odp', 'TestFramework', 'OHNET', 'SSL'],
            target='ohMediaPlayerTestUtils')

    bld.program(
            source='OpenHome/Media/Tests/TestShellMain.cpp',
            use=['OHNET', 'SSL', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'WebAppFrameworkTestUtils', 'SourcePlaylist', 'SourceRadio', 'SourceRaop', 'SourceSongcast', 'SourceUpnpAv', 'Odp'],
            target='TestShell',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestMsgMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestMsg',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestStarvationRamperMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestStarvationRamper',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestStreamValidatorMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestStreamValidator',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestSeekerMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestSeeker',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestSkipperMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestSkipper',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestStopperMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestStopper',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestWaiterMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestWaiter',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestSupplyMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestSupply',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestSupplyAggregatorMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestSupplyAggregator',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestAudioReservoirMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestAudioReservoir',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestVariableDelayMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestVariableDelay',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestTrackInspectorMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestTrackInspector',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestRamperMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestRamper',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestFlywheelRamperManualMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestFlywheelRamperManual',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestFlywheelRamperMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestFlywheelRamper',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestReporterMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestReporter',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestSpotifyReporterMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestSpotifyReporter',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestPreDriverMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestPreDriver',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestVolumeRamperMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestVolumeRamper',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestMuterMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestMuter',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestMuterVolumeMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestMuterVolume',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestDrainerMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestDrainer',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestStarterTimedMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestStarterTimed',
            install_path=None)
    bld.program(
            source='OpenHome/Av/Tests/TestContentProcessorMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'SourceRadio', 'SSL'],
            target='TestContentProcessor',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestPipelineMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestPipeline',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestPipelineConfigMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestPipelineConfig',
            install_path=None)
    bld.program(
            source='OpenHome/Av/Tests/TestStoreMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestStore',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestProtocolHlsMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'SSL'],
            target='TestProtocolHls',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestProtocolHttpMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'SSL'],
            target='TestProtocolHttp',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestCodecMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'SSL'],
            target='TestCodec',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestCodecInteractiveMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestCodecInteractive',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestCodecControllerMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestCodecController',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestDecodedAudioAggregatorMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestDecodedAudioAggregator',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestContainerMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestContainer',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestSilencerMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestSilencer',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestIdProviderMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestIdProvider',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestFillerMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestFiller',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestToneGeneratorMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'SSL'],
            target='TestToneGenerator',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestMuteManagerMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestMuteManager',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestRewinderMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestRewinder',
            install_path=None)
    bld.program(
            source='OpenHome/Av/Tests/TestUdpServerMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'SourceRaop'],
            target='TestUdpServer',
            install_path=None)
    bld.program(
            source='OpenHome/Av/Tests/TestUpnpErrorsMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'SourceUpnpAv'],
            target='TestUpnpErrors',
            install_path=None)
    bld.program(
            source='OpenHome/Av/Tests/TestTrackDatabaseMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'SourcePlaylist'],
            target='TestTrackDatabase',
            install_path=None)
    #bld.program(
    #        source='OpenHome/Av/Tests/TestPlaylistMain.cpp',
    #        use=['OHNET', 'SSL', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'SourcePlaylist'],
    #        target='TestPlaylist',
    #        install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestPhaseAdjusterMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestPhaseAdjuster',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestUriProviderRepeaterMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'SourceUpnpAv'],
            target='TestUriProviderRepeater',
            install_path=None)
    bld.program(
            source='OpenHome/Av/Tests/TestMediaPlayerMain.cpp',
            use=[
                'OHNET',
                'SSL',
                'ohMediaPlayer',
                'ohMediaPlayerTestUtils',
                'SourcePlaylist',
                'SourceRadio',
                'SourceSongcast',
                'SourceRaop',
                'SourceUpnpAv',
                'RAAT',
                'SourceRaat',
                'SourceScd',
                'WebAppFramework',
                'ConfigUi'
                ],
            target='TestMediaPlayer',
            install_path=os.path.join(bld.path.abspath(), 'install', 'bin'))
    bld.program(
            source='OpenHome/Configuration/Tests/TestConfigManagerMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestConfigManager',
            install_path=None)
    bld.program(
            source='OpenHome/Tests/TestPowerManagerMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestPowerManager',
            install_path=None)
    bld.program(
            source='OpenHome/Tests/TestSslMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'SSL'],
            target='TestSsl',
            install_path=None)
    bld.program(
            source='OpenHome/Tests/TestSocketMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestSocket',
            install_path=None)
    bld.program(
            source='OpenHome/Av/Tests/TestCredentialsMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'SSL'],
            target='TestCredentials',
            install_path=None)
    bld.program(
            source='OpenHome/Tests/TestOAuthMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestOAuth',
            install_path=None)
    bld.program(
            source='OpenHome/Media/Tests/TestContentMpdMain.cpp',
            use=['OHNET', 'ohPipeline', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'SSL'],
            target="TestContentMpd",
            install_path=None)
    bld.program(
            source='OpenHome/Tests/TestHttps.cpp',
            use=['OHNET', 'ohMediaPlayer', 'SSL'],
            target='TestHttps',
            install_path=None)
    bld.program(
            source='OpenHome/Av/Tests/TestFriendlyNameManagerMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestFriendlyNameManager',
            install_path=None)
    #bld.program(
    #        source='OpenHome/Tests/TestKey.cpp',
    #        use=['OHNET', 'ohMediaPlayer', 'SSL'],
    #        target='TestKey',
    #        install_path=None)
    #bld.program(
    #        source='OpenHome/Tests/TestHttpsBsd.cpp',
    #        use=['OHNET', 'ohMediaPlayer', 'SSL'],
    #        target='TestHttpsBsd',
    #        install_path=None)
    bld.program(
            source='OpenHome/Tests/TestObservableMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestObservable',
            install_path=None)
    bld.program(
            source='OpenHome/Tests/TestJsonMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestJson',
            install_path=None)
    bld.program(
            source='OpenHome/Tests/TestAESHelpersMain.cpp',
            use=['OHNET', 'SSL', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestAESHelpers',
            install_path=None)
    bld.program(
            source='OpenHome/Tests/TestThreadPoolMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestThreadPool',
            install_path=None)
    bld.program(
            source='OpenHome/Av/Qobuz/TestQobuz.cpp',
            use=['OHNET', 'SSL', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'SourcePlaylist'],
            target='TestQobuz',
            install_path=None)
    bld.program(
            source='OpenHome/Tests/TestNtpClient.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'SourcePlaylist'],
            target='TestNtpClient',
            install_path=None)
    bld.program(
            source=['OpenHome/Web/Tests/TestWebAppFrameworkMain.cpp'],
            use=['OHNET', 'PLATFORM', 'WebAppFrameworkTestUtils', 'WebAppFramework', 'ohMediaPlayer'],
            target='TestWebAppFramework',
            install_path=None)
    bld.program(
            source=['OpenHome/Web/ConfigUi/Tests/TestConfigUiMain.cpp'],
            use=[
                'OHNET',
                'PLATFORM',
                'SSL',
                'ConfigUiTestUtils',
                'WebAppFrameworkTestUtils',
                'ConfigUi',
                'WebAppFramework',
                'ohMediaPlayerTestUtils',
                'SourcePlaylist',
                'SourceRadio',
                'SourceSongcast',
                'SourceRaop',
                'RAAT',
                'SourceRaat',
                'SourceScd',
                'SourceUpnpAv',
                'ohMediaPlayer'
                ],
            target='TestConfigUi',
            install_path=None)
    bld.program(
            source='OpenHome/Av/Tests/TestRaopMain.cpp',
            use=['OHNET', 'SSL', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'SourceRaop'],
            target='TestRaop',
            install_path=None)
    bld.program(
            source='OpenHome/Av/Tests/TestVolumeManagerMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'SSL'],
            target='TestVolumeManager',
            install_path=None)
    bld.program(
            source='OpenHome/Av/Tests/TestPinsMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils'],
            target='TestPins',
            install_path=None)
    bld.program(
            source='OpenHome/Av/Tests/TestOhMetadataMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'ohPipline'],
            target='TestOhMetadata',
            install_path=None)
    bld.program(
            source='OpenHome/Av/Tests/TestSenderQueueMain.cpp',
            use=['OHNET', 'ohMediaPlayer', 'ohMediaPlayerTestUtils', 'SourceSongcast'],
            target='TestSenderQueue',
            install_path=None)
    bld.program(
            source='OpenHome/Net/Odp/Tests/TestDvOdpMain.cpp',
            use=['OHNET', 'Odp', 'ohMediaPlayerTestUtils'],
            target='TestDvOdp',
            install_path=None)
    bld.program(
            source='OpenHome/Net/Odp/Tests/TestCpDeviceListOdp.cpp',
            use=['OHNET', 'Odp', 'ohMediaPlayerTestUtils'],
            target='TestCpDeviceListOdp',
            install_path=None)

    bld.stlib(
            source=[
                'OpenHome/Av/Scd/ScdMsg.cpp',
                'OpenHome/Av/Scd/Sender/ScdSupply.cpp',
                'OpenHome/Av/Scd/Sender/ScdServer.cpp'
            ],
            use=['OHNET', 'ohMediaPlayer'],
            target='ScdSender')
    if bld.env.dest_platform == 'Windows-x86':
        bld.program(
                source=[
                    'OpenHome/Av/Scd/Sender/Demo/WavSender.cpp',
                    'OpenHome/Av/Scd/Sender/Demo/DirScanner.cpp',
                    'OpenHome/Av/Scd/Sender/Demo/WavSenderMain.cpp'
                    ],
                use=['OHNET', 'ScdSender', 'Odp', 'ohMediaPlayer'],
                target='WavSender',
                install_path=None)

# Bundles
def bundle(ctx):
    print('bundle binaries')
    header_files = gather_files(ctx, '{top}', ['OpenHome/**/*.h', 'OpenHome/**/*.inl', 'thirdparty/fdk-aac/**/*.h'])
    lib_names = ['ohPipeline',
                 'ohMediaPlayer',
                 'ohMediaPlayerTestUtils',
                 'SourcePlaylist',
                 'SourceRadio',
                 'SourceSongcast',
                 'SourceRaop',
                 'SourceScd',
                 'SourceUpnpAv',
                 'CodecAacFdk',
                 'CodecAacFdkAdts',
                 'CodecAacFdkBase',
                 'CodecAacFdkMp4',
                 'CodecAifc',
                 'CodecAiff',
                 'CodecAiffBase',
                 'CodecAlacAppleBase',
                 'CodecAlacApple',
                 'CodecDsdDsf',
                 'CodecDsdDff',
                 'CodecDsdRaw',
                 'CodecFlac',
                 'CodecMp3',
                 'CodecVorbis',
                 'CodecWav',
                 'CodecPcm',
                 'libOgg',
                 'WebAppFramework',
                 'ConfigUi',
                 'ConfigUiTestUtils',
                 'Odp',
                 'Podcast'
                ]
    if 'RAAT_ENABLE' in ctx.env.DEFINES:
        lib_names.append('SourceRaat')

    lib_files = gather_files(ctx, '{bld}', (ctx.env.cxxstlib_PATTERN % x for x in lib_names))
    res_files = gather_files(ctx, '{top}/OpenHome/Web/ConfigUi/res', ['**/*'])
    dep_file = gather_files(ctx, '{top}/projectdata', ['dependencies.json'])
    bundle_dev_files = build_tree({
        'ohMediaPlayer/lib' : lib_files,
        'ohMediaPlayer/include' : header_files,
        'ohMediaPlayer/res' : res_files,
        'ohMediaPlayer' : dep_file
        })
    bundle_dev_files.create_tgz_task(ctx, 'ohMediaPlayer.tar.gz')

# == Command for invoking unit tests ==

def test(tst):
    if not hasattr(tst, 'test_manifest'):
        tst.test_manifest = 'oncommit.test'
    if tst.env.dest_platform in ['Windows-x86', 'Windows-x64']:
        tst.executable_dep = 'TestShell.exe'
    else:
        tst.executable_dep = 'TestShell'
    print('Testing using manifest:', tst.test_manifest)
    rule = 'python {test} -m {manifest} -p {platform} -b {build_dir} -t {tool_dir}'.format(
        test        = os.path.join(tst.env.testharness_dir, 'Test'),
        manifest    = '${SRC}',
        platform    =  tst.env.dest_platform,
        build_dir   = '.',
        tool_dir    = os.path.join('..', 'dependencies', 'AnyPlatform'))
    tst(rule=rule, source=[tst.test_manifest, os.path.join('projectdata', 'dependencies.json'), tst.executable_dep])

def test_full(tst):
    tst.test_manifest = 'nightly.test'
    test(tst)

# == Contexts to make 'waf test' work ==

from waflib.Build import BuildContext

class TestContext(BuildContext):
    cmd = 'test'
    fun = 'test'

class TestContext(BuildContext):
    cmd = 'test_full'
    fun = 'test_full'

class BundleContext(BuildContext):
    cmd = 'bundle'
    fun = 'bundle'

# vim: set filetype=python softtabstop=4 expandtab shiftwidth=4 tabstop=4:
