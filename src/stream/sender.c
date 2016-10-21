/*
 * MiracleCast - Wifi-Display/Miracast Implementation
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
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <glib/gprintf.h>
#include <gdk/gdk.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/pbutils/encoding-profile.h>

struct CmdChannel
{
	gint in;
	gint out;

	guint in_source;

	gboolean controllee;
};

static gboolean on_req_in(gint fd, GIOCondition cond, struct CmdChannel *self)
{

}

static struct CmdChannel * cmd_channel_new()
{
	int fds[2];
	GError *error = NULL;
	struct CmdChannel *self = g_slice_new(struct CmdChannel);
	if(!self) {
		goto end;
	}

	if(!g_unix_open_pipe(fds, 0, &error)) {
		g_warning("%s", error->message);
		goto free_self;
	}

	self->in = fds[0];
	self->out = fds[1];

	self->in_source = g_unix_fd_add(self->in,
				G_IO_IN | G_IO_ERR | G_IO_HUP,
				(GUnixFDSourceFunc) on_req_in,
				self);

	goto end;

free_self:
	g_slice_free(struct CmdChannel, self);
	self = NULL;
end:
	return self;
}

static void cmd_channel_free(struct CmdChannel *self)
{
	g_source_remove(self->in_source);
	g_close(self->in, NULL);
	g_close(self->out, NULL);
	g_slice_free(struct CmdChannel, self);
}

struct Sender
{
	GMainLoop *loop;

	GstElement *pipeline;

	struct CmdChannel *channel;
};

static gchar *arg_host = NULL;

static guint arg_port = 1991;

static guint arg_width = 0;

static guint arg_height = 0;

static gint arg_screen = -1;

static gchar *arg_acodec = NULL;

static struct Sender *sender_new()
{
	struct Sender *self = g_slice_new(struct Sender);
	if(!self) {
		goto end;
	}

end:
	return self;
}

static void sender_free(struct Sender *self)
{
	if(self->pipeline) {
		gst_element_set_state(self->pipeline, GST_STATE_NULL);
		g_object_unref(G_OBJECT(self->pipeline));
	}

	if(self->loop) {
		g_main_loop_unref(self->loop);
	}

	g_slice_free(struct Sender, self);
}

static int sender_prepare(struct Sender *self)
{
	GError *error = NULL;
	GString *desc = g_string_new("ximagesrc ");
	GstElement *encoder;
	GstElement *vconv;
	gint screen_no;

	if(arg_screen == -1) {
		GdkScreen *screen = gdk_screen_get_default();
		screen_no = gdk_screen_get_number(screen);
	}
	else {
		screen_no = arg_screen;
	}
	g_string_append_printf(desc,
				"screen-num=%d ! video/x-raw, framerate=30/1 ",
				screen_no);

	if(arg_width || arg_height) {
		g_string_append(desc, "! videoscale ! video/x-raw, ");
	}
	if(arg_width) {
		g_string_append_printf(desc, "width=%d ", arg_width);
	}
	if(arg_height) {
		g_string_append_printf(desc, "height=%d ", arg_height);
	}

	g_string_append_printf(desc,
				"! videoconvert name=vconv "
				"! video/x-raw, format=NV12 "
				"! encodebin name=encoder "
				"! rtpmp2tpay "
				"! udpsink host=\"%s\" port=%d ",
				arg_host,
				arg_port);

	if(arg_acodec) {
		g_string_append_printf(desc, "pulsesrc device=\"%s\" ! encoder.",
					"alsa_output.pci-0000_00_1b.0.analog-stereo");
	}

	g_info("final pipeline: %s", desc->str);

	self->pipeline = gst_parse_launch(desc->str, &error);
	if(GST_PARSE_ERROR_LINK != error->code) {
		g_error("%s", error->message);
		goto error;
	}

	GstCaps *caps = gst_caps_from_string("video/mpegts, systemstream=true, packetsize=188");
	GstEncodingContainerProfile *cprofile = gst_encoding_container_profile_new("mpeg-ts-profile",
				NULL,
				caps,
				NULL);
	gst_caps_unref(caps);

	caps = gst_caps_from_string("video/x-h264, format=YV12, width=1280, height=720");
	GstEncodingVideoProfile *vprofile = gst_encoding_video_profile_new(caps, NULL, NULL, 0);
	gst_encoding_container_profile_add_profile(cprofile, GST_ENCODING_PROFILE(vprofile));
	//gst_caps_unref(caps);
	/*g_object_unref(vprofile);*/

	if(arg_acodec) {
		if(!strcmp("aac", arg_acodec)) {
			caps = gst_caps_from_string("audio/aac");
		}
		GstEncodingAudioProfile *aprofile = gst_encoding_audio_profile_new(caps, NULL, NULL, 0);
		//gst_caps_unref(caps);

		gst_encoding_container_profile_add_profile(cprofile, GST_ENCODING_PROFILE(aprofile));
		/*g_object_unref(aprofile);*/
	}

	encoder = gst_bin_get_by_name(GST_BIN(self->pipeline), "encoder");
	g_object_set(G_OBJECT(encoder), "profile", cprofile, NULL);
	/*g_object_unref(cprofile);*/

	vconv = gst_bin_get_by_name(GST_BIN(self->pipeline), "vconv");
	if(!gst_element_link(vconv, encoder)) {
		printf("failed to link vconv to encoder\n");
	}

	self->loop = g_main_loop_new(NULL, FALSE);

	goto end;

error:
	if(self->pipeline) {
		g_object_unref(self->pipeline);
		self->pipeline = NULL;
	}
end:
	g_string_free(desc, TRUE);

	return 0;
}

static void sender_start(struct Sender *self)
{
	gst_element_set_state(self->pipeline, GST_STATE_PLAYING);
}

static void sender_pause(struct Sender *self)
{
	gst_element_set_state(self->pipeline, GST_STATE_PAUSED);
}

static void sender_stop(struct Sender *self)
{
	gst_element_set_state(self->pipeline, GST_STATE_NULL);
}

static void sender_run(struct Sender *self)
{
	g_main_loop_run(self->loop);
}

static void arg_enable_audio();

static GOptionEntry entries[] = {
	{ "host", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &arg_host, "the hostname of sink", "" },
	{ "port", 'p', G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &arg_port, "the port which sink is waiting for RTP string", "" },
	{ "width", 'w', G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &arg_width, "", "" },
	{ "height", 'h', G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &arg_height, "", "" },
	{ "screen-num", 's', G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &arg_screen, "screen number to cast to", "" },
	{ "acodec", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &arg_acodec, "codec to encode audio", "" },
};

static void arg_parse(int *argc, char ***args)
{
	GOptionContext *opt_context;
	GError *error = NULL;

	opt_context = g_option_context_new("");
	if(!opt_context) {
		g_error("%s", strerror(errno));
	}

	g_option_context_add_main_entries(opt_context, entries, NULL);
	if(!g_option_context_parse(opt_context, argc, args, &error)) {
		g_fprintf(stderr, "%s\n", error->message);
		exit(1);
	}

	if(0 >= arg_port || arg_port > 65535) {
		g_fprintf(stderr, "Invalid port number: %i\n", arg_port);
		exit(1);
	}

	g_option_context_free(opt_context);
}

int main(int argc, char *args[])
{
	struct Sender *sender;

	arg_parse(&argc, &args);

	gdk_init(&argc, &args);
	gst_init(&argc, &args);
	gst_pb_utils_init();

	sender = sender_new();
	if(!sender) {
		g_error("%s", strerror(errno));
	}

	if(sender_prepare(sender) < 0) {
		g_error("%s", strerror(errno));
	}

	sender_start(sender);

	sender_run(sender);

	sender_free(sender);

	return 0;
}
