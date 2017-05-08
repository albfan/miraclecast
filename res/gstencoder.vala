/*
 * MiracleCast - Wifi-Display/Miracast Implementation
 *
 * Copyright (c) 2013-2014 David Herrmann <dh.herrmann@gmail.com>
 *
 * MiracleCast is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * MiracleCast is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with MiracleCast; If not, see <http://www.gnu.org/licenses/>.
 */
public enum DispdEncoderConfig
{
	DISPLAY_TYPE,		/* string */
	DISPLAY_NAME,		/* string */
	MONITOR_NUM,		/* uint32 */
	X,					/* uint32 */
	Y,					/* uint32 */
	WIDTH,				/* uint32 */
	HEIGHT,				/* uint32 */
	WINDOW_ID,			/* uint32 */
	FRAMERATE,			/* uint32 */
	SCALE_WIDTH,		/* uint32 */
	SCALE_HEIGHT,		/* uint32 */
	AUDIO_TYPE,			/* string */
	AUDIO_DEV,			/* string */
	PEER_ADDRESS,		/* string */
	RTP_PORT0,			/* uint32 */
	RTP_PORT1,			/* uint32 */
	PEER_RTCP_PORT,		/* uint32 */
	LOCAL_ADDRESS,		/* uint32 */
	LOCAL_RTCP_PORT,	/* uint32 */
	H264_PROFILE,
	H264_LEVEL,
	DEBUG_LEVEL,
}

[DBus (name = "org.freedesktop.miracle.encoder.error")]
public errordomain DispdEncoderError
{
	CANT_RETURN_UNIQUE_NAME,
	UNEXPECTED_EOS,
	ENCODER_ERROR,
	INVALID_STATE,
}

[DBus(name = "org.freedesktop.miracle.encoder.state")]
public enum DispdEncoderState
{
	NULL,
	CONFIGURED,
	READY,
	STARTED,
	PAUSED,
}

[DBus(name = "org.freedesktop.miracle.encoder")]
public interface DispdEncoder : GLib.Object
{
	public const string OBJECT_PATH = "/org/freedesktop/miracle/encoder";

	public abstract DispdEncoderState state { get; protected set; }
	public abstract signal void error(string reason);

	public abstract void configure(HashTable<DispdEncoderConfig, Variant> configs) throws DispdEncoderError;
	public abstract void start() throws DispdEncoderError;
	public abstract void pause() throws DispdEncoderError;
	public abstract void stop() throws DispdEncoderError;
}

internal class GstEncoder : DispdEncoder, GLib.Object
{
	private DBusConnection conn;
	private HashTable<DispdEncoderConfig, Variant> configs;
	private Gst.Element pipeline;
	private Gst.State pipeline_state = Gst.State.NULL;
	private DispdEncoderState _state = DispdEncoderState.NULL;

	public DispdEncoderState state {
		get { return _state; }
		protected set {
			if(_state == value) {
				return;
			}

			_state = value;
			notify_state_changed();
		}
	}

	private void notify_state_changed()
	{
		var builder = new VariantBuilder(VariantType.ARRAY);
		var invalid_builder = new VariantBuilder(new VariantType ("as"));

		Variant s = state;
		builder.add ("{sv}", "State", s);

		try {
			conn.emit_signal(null, 
							"/org/freedesktop/miracle/encoder", 
							"org.freedesktop.DBus.Properties", 
							"PropertiesChanged", 
							new Variant ("(sa{sv}as)", 
											"org.freedesktop.miracle.encoder", 
											builder, 
											invalid_builder));
		}
		catch (Error e) {
			warning("failed to emit signal: %s", e.message);
		}
	}

	public void configure(HashTable<DispdEncoderConfig, Variant> configs) throws DispdEncoderError
	{
		//if(DispdEncoderState.NULL != state) {
			//throw new DispdEncoderError.INVALID_STATE("already configured");
		//}

		uint32 framerate = configs.contains(DispdEncoderConfig.FRAMERATE)
						? configs.get(DispdEncoderConfig.FRAMERATE).get_uint32()
						: 30;
		StringBuilder desc = new StringBuilder();
		desc.append_printf(
						"ximagesrc name=vsrc use-damage=false show-pointer=false " +
							"startx=%u starty=%u endx=%u endy=%u " +
						"! video/x-raw, framerate=%u/1 " +
						"! vaapipostproc " +
							"scale-method=2 " +		/* high quality scaling mode */
							/* "format=3 " + */			/* yv12" */
						"! video/x-raw, format=YV12, width=1920, height=1080 " +
						"! vaapih264enc " +
							"rate-control=1 " +
							"num-slices=1 " +		/* in WFD spec, one slice per frame */
							"max-bframes=0 " +		/* in H264 CHP, no bframe supporting */
							"cabac=true " +			/* in H264 CHP, CABAC entropy codeing is supported, but need more processing to decode */
							"dct8x8=true " +		/* in H264 CHP, DTC is supported */
							"cpb-length=50 " +		/* shortent buffer in order to decrease latency */
							"keyframe-period=30 " +
							/* "bitrate=62500 " + *//* the max bitrate of H264 level 4.2, crashing my dongle, let codec decide */
						"! h264parse " +
						"! video/x-h264, alignment=nal, stream-format=byte-stream " +
						"%s " +
						"! mpegtsmux name=muxer " +
						"! rtpmp2tpay " +
						"! .send_rtp_sink_0 rtpbin name=session do-retransmission=true " +
							"do-sync-event=true do-lost=true ntp-time-source=3 " +
							"buffer-mode=0 latency=20 max-misorder-time=30 " +
						"! application/x-rtp " +
						"! udpsink sync=false async=false host=\"%s\" port=%u ",
						configs.contains(DispdEncoderConfig.X)
							? configs.get(DispdEncoderConfig.X).get_uint32()
							: 0,
						configs.contains(DispdEncoderConfig.Y)
							? configs.get(DispdEncoderConfig.Y).get_uint32()
							: 0,
						configs.contains(DispdEncoderConfig.WIDTH)
							? configs.get(DispdEncoderConfig.WIDTH).get_uint32() - 1
							: 1919,
						configs.contains(DispdEncoderConfig.HEIGHT)
							? configs.get(DispdEncoderConfig.HEIGHT).get_uint32() - 1
							: 1079,
						framerate,
						configs.contains(DispdEncoderConfig.AUDIO_TYPE)
							? "! queue max-size-buffers=0 max-size-bytes=0"
							: "",
						configs.contains(DispdEncoderConfig.PEER_ADDRESS)
							? configs.get(DispdEncoderConfig.PEER_ADDRESS).get_string()
							: "",
						configs.contains(DispdEncoderConfig.RTP_PORT0)
							? configs.get(DispdEncoderConfig.RTP_PORT0).get_uint32()
							: 16384);
		if(configs.contains(DispdEncoderConfig.LOCAL_RTCP_PORT)) {
			desc.append_printf("""udpsrc address="%s" port=%u reuse=true
							! session.recv_rtcp_sink_0
							session.send_rtcp_src_0
							! udpsink host="%s" port=%u sync=false async=false """,
							configs.contains(DispdEncoderConfig.LOCAL_ADDRESS)
								? configs.get(DispdEncoderConfig.LOCAL_ADDRESS).get_string()
								: "",
							configs.contains(DispdEncoderConfig.LOCAL_RTCP_PORT)
								? configs.get(DispdEncoderConfig.LOCAL_RTCP_PORT).get_uint32()
								: 16385,
							configs.contains(DispdEncoderConfig.PEER_ADDRESS)
								? configs.get(DispdEncoderConfig.PEER_ADDRESS).get_string()
								: "",
							configs.contains(DispdEncoderConfig.PEER_RTCP_PORT)
								? configs.get(DispdEncoderConfig.PEER_RTCP_PORT).get_uint32()
								: 16385);
		}

		info("pipeline description: %s", desc.str);

		this.configs = configs;

		try {
			pipeline = Gst.parse_launch(desc.str);
		}
		catch(Error e) {
			throw new DispdEncoderError.ENCODER_ERROR("%s", e.message);
		}

		var bus = pipeline.get_bus();
		bus.add_signal_watch();
		bus.message.connect(on_pipeline_message);

		pipeline.set_state(Gst.State.READY);
	}

//	if(*os->audio_dev) {
//		for(tmp = pipeline_desc; *tmp; ++tmp);
//		*tmp ++ = "pulsesrc";
//		*tmp ++ = "do-timestamp=true";
//		*tmp ++ = "client-name=miraclecast";
//		*tmp ++ = "device=";
//		*tmp ++ = quote_str(os->audio_dev, audio_dev, sizeof(audio_dev));
//		*tmp ++ = "!";
//		*tmp ++ = "voaacenc";
//		*tmp ++ = "mark-granule=true";
//		*tmp ++ = "hard-resync=true";
//		*tmp ++ = "tolerance=40";
//		*tmp ++ = "!";
//		*tmp ++ = "audio/mpeg,";
//		*tmp ++ = "rate=48000,";
//		*tmp ++ = "channels=2,";
//		*tmp ++ = "stream-format=adts,";
//		*tmp ++ = "base-profile=lc";
//		*tmp ++ = "!";
//		*tmp ++ = "queue";
//		*tmp ++ = "max-size-buffers=0";
//		*tmp ++ = "max-size-bytes=0";
//		*tmp ++ = "max-size-time=0";
//		*tmp ++ = "!";
//		*tmp ++ = "muxer.";
//		*tmp ++ = NULL;
//	}
//
//	/* bad pratice, but since we are in the same process,
//	   I think this is the only way to do it */
//	if(WFD_DISPLAY_TYPE_X == os->display_type) {
//		r = setenv("XAUTHORITY", os->authority, 1);
//		if(0 > r) {
//			return r;
//		}
//
//		r = setenv("DISPLAY", os->display_name, 1);
//		if(0 > r) {
//			return r;
//		}
//
//		if(!os->display_param_name) {
//			snprintf(vsrc_param1, sizeof(vsrc_param1), "startx=%hu", os->x);
//			snprintf(vsrc_param2, sizeof(vsrc_param2), "starty=%hu", os->y);
//			snprintf(vsrc_param3, sizeof(vsrc_param3), "endx=%d", os->x + os->width - 1);
//			snprintf(vsrc_param4, sizeof(vsrc_param4), "endy=%d", os->y + os->height - 1);
//		}
//		else if(!strcmp("xid", os->display_param_name) ||
//						!strcmp("xname", os->display_param_name)) {
//			snprintf(vsrc_param1, sizeof(vsrc_param1),
//					"%s=\"%s\"",
//					os->display_param_name,
//					os->display_param_value);
//		}
//	}
//
//	pipeline = gst_parse_launchv(pipeline_desc, &error);
//	if(!pipeline) {
//		if(error) {
//			log_error("failed to create pipeline: %s", error->message);
//			g_error_free(error);
//		}
//		return -1;
//	}
//
//	vsrc = gst_bin_get_by_name(GST_BIN(pipeline), "vsrc");
//	gst_base_src_set_live(GST_BASE_SRC(vsrc), true);
//	g_object_unref(vsrc);
//	vsrc = NULL;
//
//	r = gst_element_set_state(pipeline, GST_STATE_PAUSED);
//	if(GST_STATE_CHANGE_FAILURE == r) {
//		g_object_unref(pipeline);
//		return -1;
//	}
//
//	bus = gst_element_get_bus(pipeline);
//	gst_bus_add_watch(bus, wfd_out_session_handle_gst_message, s);
//
//	os->pipeline = pipeline;
//	os->bus = bus;
//
//	return 0;
//}

	public void start() throws DispdEncoderError
	{
		check_configs();

		pipeline.set_state(Gst.State.PLAYING);
	}

	public void pause() throws DispdEncoderError
	{
		check_configs();

		pipeline.set_state(Gst.State.PAUSED);
	}

	public void stop() throws DispdEncoderError
	{
		if(null == pipeline) {
			return;
		}

		pipeline.set_state(Gst.State.NULL);
		state = DispdEncoderState.NULL;
		defered_terminate();
	}

	public async void prepare() throws Error
	{
		conn = yield Bus.get(BusType.SESSION);
		conn.register_object(DispdEncoder.OBJECT_PATH, this as DispdEncoder);

		string bus_info = "%s\n%s".printf(conn.unique_name,
						BusType.get_address_sync(BusType.SESSION));
		/* we are ready, tell parent how to communicate with us */
		ssize_t r = Posix.write(3, (void *) bus_info.data, bus_info.length);
		if(0 > r) {
			throw new DispdEncoderError.CANT_RETURN_UNIQUE_NAME("%s",
							Posix.strerror(Posix.errno));
		}
		Posix.fsync(3);
	}

	private void defered_terminate()
	{
		Timeout.add(100, () => {
			loop.quit();
			return false;
		});
	}

	private void check_configs() throws DispdEncoderError
	{
		if(null == configs || null == pipeline) {
			throw new DispdEncoderError.INVALID_STATE("not configure yet");
		}
	}

	private void on_pipeline_message(Gst.Message m)
	{
		Error e;
		string d;

		if(m.src != pipeline) {
			return;
		}

		switch(m.type) {
		case Gst.MessageType.EOS:
			error("unexpected EOS");
			defered_terminate();
			break;
		case Gst.MessageType.ERROR:
			m.parse_error(out e, out d);
			error("unexpected error: %s\n%s".printf(e.message, d));
			defered_terminate();
			break;
		case Gst.MessageType.STATE_CHANGED:
			Gst.State oldstate;
			m.parse_state_changed(out oldstate, out pipeline_state, null);
			info("pipeline state chagned from %s to %s",
					oldstate.to_string(),
					pipeline_state.to_string());
			switch(pipeline_state) {
			case Gst.State.READY:
				state = DispdEncoderState.CONFIGURED;
				break;
			case Gst.State.PLAYING:
				state = DispdEncoderState.STARTED;
				break;
			case Gst.State.PAUSED:
				if(Gst.State.PLAYING == oldstate) {
					state = DispdEncoderState.PAUSED;
				}
				break;
			}
			break;
		default:
			debug("unhandled message: %s", m.type.to_string());
			break;
		}
	}
}

private MainLoop loop;

int main(string[] argv)
{
	Gst.init(ref argv);

	var encoder = new GstEncoder();
	encoder.prepare.begin((o, r) => {
		try {
			encoder.prepare.end(r);
		}
		catch(Error e) {
			error("%s", e.message);
		}
	});

	loop = new MainLoop();
	loop.run();

	Posix.close(3);

	Gst.deinit();

	info("bye");

	return 0;
}
