public enum GstEncoderConfig
{
    DISPLAY_SYSTEM, /* string */
    DISPLAY_NAME,   /* string */
    MONITOR,        /* uint32 */
    TOP,            /* uint32 */
    LEFT,           /* uint32 */
    WIDTH,          /* uint32 */
    HEIGHT,         /* uint32 */
    WINDOW_ID,      /* uint32 */
    FRAMERATE,      /* uint32 */
    SCALE_WIDTH,    /* uint32 */
    SCALE_HEIGHT,   /* uint32 */
    RTP_PORT1,      /* uint32 */
    RTP_PORT2,      /* uint32 */
    RTCP_PORT,      /* uint32 */
    H264_PROFILE,
    H264_LEVEL,
    DEBUG_LEVEL,
}

[DBus (name = "org.freedesktop.miracle.encoder.error")]
public errordomain GstEncoderError
{
    UNEXPECTED_EOS,
    ENCODER_ERROR,
    INVALID_STATE,
}

[DBus(name = "org.freedesktop.miracle.encoder")]
public interface GstEncoder : GLib.Object
{
    public enum State
    {
        NULL,
        CONFIGURED,
        READY,
        STARTED,
        PAUSED,
    }

    public const string OBJECT_PATH = "/org/freedesktop/miracle/encoder";

    public abstract State state { get; protected set; default = State.NULL; }

    public abstract void configure(HashTable<GstEncoderConfig, Variant> configs) throws GstEncoderError;
    public abstract void start() throws GstEncoderError;
    public abstract void pause() throws GstEncoderError;
    public abstract void stop() throws GstEncoderError;
}

internal class EncoderImpl : GstEncoder, GLib.Object
{
    private DBusConnection conn;
    private HashTable<GstEncoderConfig, Variant> configs;
    private Gst.Element pipeline;
    private Gst.State pipeline_state = Gst.State.NULL;

    public GstEncoder.State state { get; private set; }

    public void configure(HashTable<GstEncoderConfig, Variant> configs) throws GstEncoderError
    {
        if(GstEncoder.State.NULL != state) {
            throw new GstEncoderError.INVALID_STATE("already configured");
        }

        try {
            pipeline = Gst.parse_launch("""videotestsrc ! autovideosink""");
        }
        catch(Error e) {
            throw new GstEncoderError.ENCODER_ERROR("%s", e.message);
        }

        pipeline.set_state(Gst.State.READY);
        var bus = pipeline.get_bus();
        bus.add_signal_watch();
        bus.message.connect(on_pipeline_message);

        this.configs = configs;
        state = GstEncoder.State.CONFIGURED;
    }

    public void start() throws GstEncoderError
    {
        check_configs();

        pipeline.set_state(Gst.State.PLAYING);
    }

    public void pause() throws GstEncoderError
    {
        check_configs();

        pipeline.set_state(Gst.State.PAUSED);
    }

    public void stop() throws GstEncoderError
    {
        if(null == pipeline) {
            return;
        }

        pipeline.set_state(Gst.State.NULL);
        pipeline = null;
    }

    public async void prepare() throws Error
    {
        conn = yield Bus.get(BusType.SESSION);
        conn.register_object(GstEncoder.OBJECT_PATH, this as GstEncoder);

        /* we are ready, tell parent how to communicate with us */
        stderr.printf("\nunique-name: %s\n", conn.unique_name);
        stderr.flush();
    }

    private void check_configs() throws GstEncoderError
    {
        if(null == configs || null == pipeline) {
            throw new GstEncoderError.INVALID_STATE("not configure yet");
        }
    }

    private void on_pipeline_message(Gst.Message m)
    {
        if(m.src != pipeline) {
            return;
        }

        switch(m.type) {
        case Gst.MessageType.EOS:
            break;
        case Gst.MessageType.STATE_CHANGED:
            Gst.State oldstate;
            m.parse_state_changed(out oldstate, out pipeline_state, null);
            debug("state chagned from %s to %s",
                    oldstate.to_string(),
                    pipeline_state.to_string());
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

    var encoder = new EncoderImpl();
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

    Gst.deinit();

    return 0;
}
