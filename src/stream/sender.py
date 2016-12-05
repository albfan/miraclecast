#!/usr/bin/python3

import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstBase', '1.0')
gi.require_version('GstPbutils', '1.0')
from gi.repository import GLib
from gi.repository import Gst, GstBase, GstPbutils

Gst.init(None)
GstPbutils.pb_utils_init()

pipeline = Gst.ElementFactory.make('pipeline')

ximagesrc = Gst.ElementFactory.make('ximagesrc')
ximagesrc.set_property('use-damage', False)
ximagesrc.set_property('show-pointer', False)
ximagesrc.set_property('do-timestamp', True)
ximagesrc.set_property('startx', 0)
ximagesrc.set_property('starty', 0)
ximagesrc.set_property('endx', 1919)
ximagesrc.set_property('endy', 1079)
pipeline.add(ximagesrc)

vscale = Gst.ElementFactory.make('videoscale')
pipeline.add(vscale)
ximagesrc.link(vscale)

vconvert = Gst.ElementFactory.make('videoconvert')
pipeline.add(vconvert)
vscale.link(vconvert)

filter = Gst.ElementFactory.make('capsfilter')
filter.set_property('caps', Gst.Caps.from_string('video/x-raw, format=I420, framerate=30/1, width=1920, height=1080'))
pipeline.add(filter)
vconvert.link(filter)

encoder = Gst.ElementFactory.make('encodebin')
#cont_profile = GstPbutils.EncodingContainerProfile.new('mpegts',
#                                                       None,
#                                                       Gst.Caps.from_string('video/mpegts, packetsize=188, systemstream=true'),
#                                                       None)
vencode_profile = GstPbutils.EncodingVideoProfile.new(Gst.Caps.from_string('video/x-h264'),
                                                      None,
                                                      Gst.Caps.new_any(),
                                                      1)
#cont_profile.add_profile(vencode_profile)
encoder.set_property('profile', vencode_profile)
pipeline.add(encoder)
filter.link(encoder)

mpegtsmux = Gst.ElementFactory.make('mpegtsmux')
mpegtsmux.set_property('alignment', 7)
pipeline.add(mpegtsmux)
encoder.link(mpegtsmux)

sink = Gst.ElementFactory.make('filesink')
sink.set_property('location', 'xxx')
pipeline.add(sink)
mpegtsmux.link(sink)

pipeline.set_state(Gst.State.PLAYING)

loop = GLib.MainLoop()
loop.run()

