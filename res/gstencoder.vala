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

	string gen_scaler_n_converter_desc(uint32 width, uint32 height)
	{
		if(null != Gst.ElementFactory.find("vaapih264enc")) {
			return ("! vaapipostproc " +
							"scale-method=2 " +
							"format=3 " +
							"force-aspect-ratio=true " +
					"! video/x-raw, " +
							"format=YV12, " +
							"width=%u, " +
							"height=%u ").printf(width, height);
		}

		info("since vaapih264enc is not available, vaapipostproc can't be " +
						"trusted, use videoscale+videoconvert instead");

		return ("! videoscale method=0 add-borders=true " +
			"! video/x-raw, width=%u, height=%u " +
			"! videoconvert " +
			"! video/x-raw, format=YV12 ").printf(width, height);
	}

	string gen_encoder_desc(uint32 framerate)
	{
		if(null != Gst.ElementFactory.find("vaapih264enc")) {
				return ("! vaapih264enc " +
							"rate-control=1 " +
							"num-slices=1 " +      /* in WFD spec, one slice per frame */
							"max-bframes=0 " +     /* in H264 CHP, no bframe supporting */
							"cabac=true " +        /* in H264 CHP, CABAC entropy codeing is supported, but need more processing to decode */
							"dct8x8=true " +       /* in H264 CHP, DTC is supported */
							"cpb-length=1000 " +   /* shortent buffer in order to decrease latency */
							"keyframe-period=%u ").printf(framerate);
		}

		info("vaapih264enc not available, use x264enc instead");

		return ("! x264enc pass=4 b-adapt=false key-int-max=%u " +
						"speed-preset=4 tune=4 ").printf(framerate);
	}

	public void configure(HashTable<DispdEncoderConfig, Variant> configs) throws DispdEncoderError
	{
		uint32 framerate = configs.contains(DispdEncoderConfig.FRAMERATE)
						? configs.get(DispdEncoderConfig.FRAMERATE).get_uint32()
						: 30;
		uint32 width = configs.contains(DispdEncoderConfig.WIDTH)
			? configs.get(DispdEncoderConfig.WIDTH).get_uint32()
			: 1920;
		uint32 height = configs.contains(DispdEncoderConfig.HEIGHT)
			? configs.get(DispdEncoderConfig.HEIGHT).get_uint32()
			: 1080;
		StringBuilder desc = new StringBuilder();
		desc.append_printf(
						"ximagesrc " +
							"name=vsrc " +
							"use-damage=false " +
							"show-pointer=false " +
							"startx=%u starty=%u endx=%u endy=%u " +
						"! video/x-raw, " +
							"framerate=%u/1 " +
						"%s" +						/* scaling & color space convertion */
						"%s" +						/* encoding */
						"! h264parse " +
						"! video/x-h264, " +
							"alignment=nal, " +
							"stream-format=byte-stream " +
						"%s " +						/* add queue if audio enabled */
						"! mpegtsmux " +
							"name=muxer " +
						"! rtpmp2tpay " +
						"! .send_rtp_sink_0 " +
						"rtpbin " +
							"name=session " +
							"rtp-profile=1 " + 			/* avp */
							"do-retransmission=true " +
							"do-sync-event=true " +
							"do-lost=true " +
							"ntp-time-source=3 " + 		/* pipeline clock time */
							"buffer-mode=0 " +
							"latency=40 " +
							"max-misorder-time=50 " +
						"! application/x-rtp " +
						"! udpsink " +
							"sync=false " +
							"async=false " +
							"host=\"%s\" " +
							"port=%u ",
						configs.contains(DispdEncoderConfig.X)
							? configs.get(DispdEncoderConfig.X).get_uint32()
							: 0,
						configs.contains(DispdEncoderConfig.Y)
							? configs.get(DispdEncoderConfig.Y).get_uint32()
							: 0,
						configs.get(DispdEncoderConfig.X).get_uint32() + width - 1,
						configs.get(DispdEncoderConfig.Y).get_uint32() + height - 1,
						framerate,
						gen_scaler_n_converter_desc(width, height),
						gen_encoder_desc(framerate),
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
			desc.append_printf("udpsrc " +
								"address=\"%s\" " +
								"port=%u " +
								"reuse=true " +
							"! session.recv_rtcp_sink_0 " +
							"session.send_rtcp_src_0 " +
							"! udpsink " +
								"host=\"%s\" " +
								"port=%u " +
								"sync=false " +
								"async=false ",
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

		if(configs.contains(DispdEncoderConfig.AUDIO_TYPE)) {
			desc.append_printf("pulsesrc " +
								"do-timestamp=true " +
								"client-name=miraclecast " +
								"device=\"%s\" " +
							"! avenc_aac " +
							"! audio/mpeg, " +
								"channels=2, " +
								"rate=48000 " +
//								"base-profile=lc " +
							"! queue " +
								"max-size-buffers=0 " +
								"max-size-bytes=0 " +
								"max-size-time=0 " +
							"! muxer. ",
							configs.contains(DispdEncoderConfig.AUDIO_DEV)
								? configs.get(DispdEncoderConfig.AUDIO_DEV).get_string()
								: "");
		}

		info("final pipeline description: %s", desc.str);

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

#if VALA_0_54
                string bus_info = "%s\n%s".printf(conn.unique_name,
                                               BusType.SESSION.get_address_sync ());
#else
		string bus_info = "%s\n%s".printf(conn.unique_name,
						BusType.get_address_sync(BusType.SESSION));
#endif
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
