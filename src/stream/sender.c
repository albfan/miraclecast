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
#include <stdarg.h>
#include <glib.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <glib/gprintf.h>
#include <gdk/gdk.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/pbutils/encoding-profile.h>
#include "sender-iface.h"
#include "sender.h"

struct SenderImpl
{
	Sender *skeleton;

	GMainLoop *loop;

	GstElement *pipeline;

	struct CmdChannel *channel;

	guint bus_owner_id;

	guint bus_obj_id;

	GDBusMethodInvocation *method_invoke;
};

static gchar *arg_host = NULL;

static guint arg_port = 1991;

static guint arg_width = 0;

static guint arg_height = 0;

static gint arg_screen = -1;

static gchar *arg_acodec = NULL;

static gboolean arg_audio_only = FALSE;

static guint arg_refresh_rate = 30;

static gboolean arg_interleave = FALSE;

static char * arg_h264_profile = NULL;

static const char *vpipeline_desc =
				"ximagesrc name=vsrc use-damage=false show-pointer=false do-timestamp=true starty=%d startx=%d endy=%d endx=%d "
				"capsfilter name=caps_framerate caps=\"video/x-raw, framerate=%d/1\" "
				"videoscale name=vscale "
				"capsfilter name=caps_scale caps=\"video/x-raw, width=%d, height=%d\" "
				"autovideoconvert name=vconv "
				"capsfilter name=caps_format caps=\"video/x-raw, format=I420\" "
				"encodebin name=vencoder "
				"queue name=vqueue max-size-buffers=0 max-size-bytes=0 "
				"mpegtsmux name=muxer alignment=7 "
				"capsfilter name=caps_muxer caps=\"video/mpegts, packetsize=188, systemstream=true\" "
				"rtpmp2tpay name=rtppay "
				"udpsink name=sink host=\"%s\" port=%d ";

static const char *apipeline_desc =
				//"pulsesrc name=asrc device=\"%s\" "
				"audiotestsrc name=asrc "
				"audioconvert name=aconv "
				"audioresample name=aresample "
				"encodebin name=aencoder "
				"queue name=aqueue ";

static gboolean sender_impl_prepare(struct SenderImpl *self,
				GDBusMethodInvocation *invocation,
				const gchar *host,
				guint16 port,
				const gchar *display,
				guint16 width,
				guint16 height,
				guint16 refresh_rate,
				gboolean interleave);

static gboolean sender_impl_play(struct SenderImpl *sender, GDBusMethodInvocation *invocation);

static gboolean sender_impl_pause(struct SenderImpl *sender, GDBusMethodInvocation *invocation);

static gboolean sender_impl_stop(struct SenderImpl *sender, GDBusMethodInvocation *invocation);

#define SENDER_IMPL_ERROR_DOMAIN_NAME "miracle-sender-impl"

static const GDBusErrorEntry sender_dbus_error_entries[] = {
	{ MIRACLE_SENDER_ERROR_UNKNOWN, "org.freedesktop.miracle.Sender.Error.Unknown" },
	{ MIRACLE_SENDER_ERROR_NOT_PREPARED, "org.freedesktop.miracle.Sender.Error.NoPrepared" },
};

static GQuark sender_impl_error_quark()
{
	static gsize registered = 0;
	g_dbus_error_register_error_domain("miracle-sender",
					&registered,
					sender_dbus_error_entries,
					G_N_ELEMENTS(sender_dbus_error_entries));

	return g_quark_from_static_string("miracle-sender-impl");
}

static struct SenderImpl * sender_impl_new()
{
	struct SenderImpl *self = g_slice_new0(struct SenderImpl);

	return self;
}

static void sender_impl_free(struct SenderImpl *self)
{
	if(self->pipeline) {
		gst_element_set_state(self->pipeline, GST_STATE_NULL);
		g_object_unref(G_OBJECT(self->pipeline));
	}

	if(self->loop) {
		g_main_loop_unref(self->loop);
	}

	if(self->bus_owner_id) {
		g_bus_unown_name(self->bus_owner_id);
	}

	g_slice_free(struct SenderImpl, self);
}

static void get_screen_dimension(gint *top,
				gint *left,
				gint *bottom,
				gint *right)
{
	GdkRectangle rect;

#if GDK_VERSION_MIN_REQUIRED > GDK_VERSION_3_20
	GdkDisplay *display = gdk_display_get_default();
	GdkMonitor *monitor;

	if(arg_screen < 0 || arg_screen >= gdk_display_get_n_monitors(display)) {
		monitor = gdk_display_get_primary_monitor(display);
	}
	else {
		monitor = gdk_display_get_monitor(display, arg_screen);
	}
	gdk_monitor_get_geometry(monitor, &rect);
#else
	GdkScreen *screen = gdk_screen_get_default();
	gint monitor;
	if(arg_screen < 0 || arg_screen >= gdk_screen_get_n_monitors(screen)) {
		monitor = gdk_screen_get_primary_monitor(screen);
	}
	else {
		monitor = arg_screen;
	}
	gdk_screen_get_monitor_geometry(screen, monitor, &rect);
#endif

	*top = rect.y;
	*left = rect.x;
	*bottom = rect.y + rect.height - 1;
	*right = rect.x + rect.width - 1;
}

static int link_elements(GstBin *bin, const char *name, ...)
{
	va_list argv;
	const char *name1, *name2;

	va_start(argv, name);
	name1 = name;
	while((name2 = va_arg(argv, const char *))) {
		GstElement *e1 = gst_bin_get_by_name(bin, name1);
		GstElement *e2 = gst_bin_get_by_name(bin, name2);
		gboolean linked = gst_element_link(e1, e2);
		g_object_unref(G_OBJECT(e2));
		g_object_unref(G_OBJECT(e1));
		
		if(!linked) {
			g_warning("failed to link %s to %s", name1, name2);
			errno = EINVAL;
			return -1;
		}

		name1 = name2;
	}
	va_end(argv);

	return 0;
}

void on_gst_message_state_changed(struct SenderImpl *self,
				GstMessage *message)
{
	GstState old, curr, pending;
	gst_message_parse_state_changed(message, &old, &curr, &pending);
	const char *src = GST_MESSAGE_SRC_NAME(message);
	if(strncmp("pipeline", src, 8) || !self->skeleton) {
		return;
	}

	g_info("%s(%s) state changed: %s => %s",
					GST_MESSAGE_SRC_NAME(message),
					g_type_name(G_OBJECT_TYPE(GST_MESSAGE_SRC(message))),
					gst_element_state_get_name(old),
					gst_element_state_get_name(curr));
	
	switch(curr) {
		case GST_STATE_PLAYING:
			g_object_set(self->skeleton, "state", "playing", NULL);
			if(self->method_invoke) {
				sender_complete_play(SENDER(self->skeleton),
								self->method_invoke);
				self->method_invoke = NULL;
			}
			break;
		case GST_STATE_PAUSED:
			g_object_set(self->skeleton, "state", "paused", NULL);
			if(self->method_invoke) {
				sender_complete_pause(SENDER(self->skeleton),
								self->method_invoke);
				self->method_invoke = NULL;
			}
			break;
		default:
			break;
	}
}

void on_gst_message_error(struct SenderImpl *self,
				GstMessage *message)
{
	GError *error = NULL;
	char *dbg = NULL;
	gst_message_parse_error(message, &error, &dbg);
	if(self->method_invoke) {
		g_dbus_method_invocation_return_gerror(self->method_invoke, error);
		self->method_invoke = NULL;
	}
	g_warning("%s", dbg);
	g_error_free(error);
	g_free(dbg);
}

void on_gst_message(struct SenderImpl *self,
				GstMessage *message,
				GstBus *bus)
{
	g_debug("Gstreamer message: %s", GST_MESSAGE_TYPE_NAME(message));
	switch(GST_MESSAGE_TYPE(message)) {
		case GST_MESSAGE_ERROR:
			on_gst_message_error(self, message);
			break;
		case GST_MESSAGE_STATE_CHANGED:
			on_gst_message_state_changed(self, message);
			break;
		case GST_MESSAGE_LATENCY: {
				GstClockTime latency = gst_pipeline_get_latency(GST_PIPELINE(self->pipeline));
				g_info("New latency is: %lu", latency);
			}
			break;
		default:
			break;
	}
}

static int prepare_pipeline(struct SenderImpl *self)
{
	GError *error = NULL;
	gint result;
	gint screen_top, screen_left, screen_bottom, screen_right;
	GString * desc;

	desc = g_string_new(NULL);
	if(!desc) {
		result = -1;
		goto end;
	}

	get_screen_dimension(&screen_top, &screen_left, &screen_bottom, &screen_right);
	g_string_append_printf(desc,
					vpipeline_desc,
					screen_top,
					screen_left,
					screen_bottom,
					screen_right,
					arg_refresh_rate,
					arg_width ? arg_width : screen_right - screen_left + 1,
					arg_height ? arg_height : screen_bottom - screen_top + 1,
					arg_host,
					arg_port);

	if(arg_acodec) {
		g_string_append_printf(desc,
					apipeline_desc,
					"alsa_output.pci-0000_00_1b.0.analog-stereo.monitor");
	}

	self->pipeline = gst_parse_launch(desc->str, &error);
	if(error) {
		g_error("%s", error->message);
		goto free_desc;
	}

	gst_element_set_name(GST_ELEMENT(self->pipeline), "pipeline");

	GstEncodingVideoProfile *vencode_profile = gst_encoding_video_profile_new(
					gst_caps_from_string("video/x-h264, profile=high"),
					NULL,
					gst_caps_new_any(),
					0);
	GstElement *vencoder = gst_bin_get_by_name(GST_BIN(self->pipeline), "vencoder");
	g_object_set(G_OBJECT(vencoder), "tune", 0x4, NULL);
	g_object_set(G_OBJECT(vencoder), "profile", vencode_profile, NULL);
	g_object_unref(G_OBJECT(vencoder));

	if(arg_acodec) {
		const char *format;
		if(!strncmp("aac", arg_acodec, 3)) {
			format = "audio/mpeg, framed=true, mpegversion=4, stream-format=adts";
		}
		else if(!strncmp("ac3", arg_acodec, 3)) {
			format = "audio/x-ac3, framed=true";
		}
		else if(!strncmp("pcm", arg_acodec, 3)) {
			format = "audio/x-lpcm";
		}
		GstEncodingAudioProfile *aencode_profile = gst_encoding_audio_profile_new(
						gst_caps_from_string(format),
						NULL,
						gst_caps_new_any(),
						0);
		GstElement *aencoder = gst_bin_get_by_name(GST_BIN(self->pipeline), "aencoder");
		g_object_set(G_OBJECT(aencoder), "profile", aencode_profile, NULL);
		g_object_unref(G_OBJECT(aencoder));
	}

	result = link_elements(GST_BIN(self->pipeline),
					"vsrc",
					"caps_framerate",
					"vscale",
					"caps_scale",
					"vconv",
					"caps_format",
					"vencoder",
					"vqueue",
					"muxer",
					"caps_muxer",
					"rtppay",
					"sink",
					NULL);
	if(result < 0) {
		goto error;
	}

	if(arg_acodec) {
		link_elements(GST_BIN(self->pipeline),
						"asrc",
						"aconv",
						"aresample",
						"aencoder",
						"aqueue",
						"muxer",
						NULL);
	}

	GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(self->pipeline));
	g_signal_connect_swapped(bus, "message", G_CALLBACK(on_gst_message), self);
	gst_bus_add_signal_watch(bus);

	gst_element_set_state(self->pipeline, GST_STATE_PAUSED);

	goto free_desc;

error:
	if(self->pipeline) {
		g_object_unref(self->pipeline);
		self->pipeline = NULL;
	}
free_desc:
	g_string_free(desc, TRUE);
end:
	return result;
}

static void sender_on_name_acquired(GDBusConnection *conn,
				const char *name,
				gpointer user_data)
{
	struct SenderImpl *self = user_data;
	GError *error = NULL;
	gboolean result;

	self->skeleton = sender_skeleton_new();
	g_signal_connect_swapped(self->skeleton,
					"handle-prepare",
					G_CALLBACK(sender_impl_prepare),
					self);
	g_signal_connect_swapped(self->skeleton,
					"handle-play",
					G_CALLBACK(sender_impl_play),
					self);
	g_signal_connect_swapped(self->skeleton,
					"handle-pause",
					G_CALLBACK(sender_impl_pause),
					self);
	g_signal_connect_swapped(self->skeleton,
					"handle-stop",
					G_CALLBACK(sender_impl_stop),
					self);
	g_object_set(self->skeleton, "state", "stop", NULL);

	result = g_dbus_interface_skeleton_export(
					G_DBUS_INTERFACE_SKELETON(self->skeleton),
					conn,
					"/org/freedesktop/miracle/Sender/0",
					&error);
	if(!result) {
		g_error("failed to expose object");
	}
}

static void sender_on_name_lost(GDBusConnection *conn,
				const char *name,
				gpointer user_data)
{
	struct SenderImpl *self = user_data;
	if(self->skeleton) {
		g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON(self->skeleton));
		g_signal_handlers_disconnect_by_data(self->skeleton, self);
		g_object_unref(self->skeleton);
		self->skeleton = NULL;
	}
}

static gint sender_impl_init(struct SenderImpl *self)
{
	gint result;
	self->loop = g_main_loop_new(NULL, FALSE);
	if(!self->loop) {
		result = -1;
		goto end;
	}

	self->bus_owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
				"org.freedesktop.miracle.Sender",
				G_BUS_NAME_OWNER_FLAGS_NONE,
				NULL,
				sender_on_name_acquired,
				NULL,
				self,
				NULL);

end:
	return result;
}

static gboolean sender_impl_prepare(struct SenderImpl *self,
				GDBusMethodInvocation *invocation,
				const gchar *host,
				guint16 port,
				const gchar *display,
				guint16 width,
				guint16 height,
				guint16 refresh_rate,
				gboolean interleave)
{
	if(self->pipeline) {
		sender_complete_prepare(SENDER(self->skeleton), invocation);
		return TRUE;
	}

	if(self->method_invoke) {
		g_dbus_method_invocation_return_error(invocation,
						MIRACLE_SENDER_ERROR,
						MIRACLE_SENDER_ERROR_AGAIN,
						"request handling in progress");
		return TRUE;
	}

	self->method_invoke = invocation;

	if(arg_host) {
		g_free(arg_host);
	}
	arg_host = g_strdup(host);
	arg_port = port;
	g_setenv("DISPLAY", display ? display : ":0", TRUE);
	arg_width = width;
	arg_height = height;

	prepare_pipeline(self);

	return TRUE;
}

static gboolean sender_impl_play(struct SenderImpl *self,
				GDBusMethodInvocation *invocation)
{
	if(!self->pipeline) {
		g_dbus_method_invocation_return_error(invocation,
						MIRACLE_SENDER_ERROR,
						MIRACLE_SENDER_ERROR_NOT_PREPARED,
						"sender not prepared");
		return TRUE;
	}

	if(self->method_invoke) {
		g_dbus_method_invocation_return_error(invocation,
						MIRACLE_SENDER_ERROR,
						MIRACLE_SENDER_ERROR_AGAIN,
						"request handling in progress");
		return TRUE;
	}

	self->method_invoke = invocation;

	gst_element_set_state(self->pipeline, GST_STATE_PLAYING);

	return TRUE;
}

static gboolean sender_impl_pause(struct SenderImpl *self,
				GDBusMethodInvocation *invocation)
{
	if(!self->pipeline) {
		g_dbus_method_invocation_return_error(invocation,
						MIRACLE_SENDER_ERROR,
						MIRACLE_SENDER_ERROR_NOT_PREPARED,
						"sender not prepared");
		return TRUE;
	}

	if(self->method_invoke) {
		g_dbus_method_invocation_return_error(invocation,
						MIRACLE_SENDER_ERROR,
						MIRACLE_SENDER_ERROR_AGAIN,
						"request handling in progress");
		return TRUE;
	}

	self->method_invoke = invocation;

	gst_element_set_state(self->pipeline, GST_STATE_PAUSED);

	return TRUE;
}

static gboolean sender_impl_stop(struct SenderImpl *self,
				GDBusMethodInvocation *invocation)
{
	if(!self->pipeline) {
		goto end;
	}

	if(self->method_invoke) {
		g_dbus_method_invocation_return_error(invocation,
						MIRACLE_SENDER_ERROR,
						MIRACLE_SENDER_ERROR_AGAIN,
						"request handling in progress");
		return TRUE;
	}

	g_object_set(self->skeleton, "state", "stop", NULL);

	gst_element_set_state(self->pipeline, GST_STATE_NULL);
	g_object_unref(self->pipeline);
	self->pipeline = NULL;

end:
	sender_complete_stop(SENDER(self->skeleton), invocation);

	return TRUE;
}

static gboolean sender_impl_run(struct SenderImpl *self)
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
	{ "acodec", 'a', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &arg_acodec, "codec to encode audio", "" },
	{ "audio-only", 'o', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &arg_audio_only, "no video, audio stream only", "" },
	{ 0 }
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

static void gst_rerank(const char *name, ...)
{
	va_list names;
	GstRegistry *reg = gst_registry_get();

	va_start(names, name);
	while(name) {
		GstPluginFeature *plugin = gst_registry_lookup_feature(reg, name);
		if(!plugin) {
			goto next;
		}

		g_info("raising rank of plugin %s from %u to %u",
						name,
						gst_plugin_feature_get_rank(plugin),
						GST_RANK_PRIMARY + 1);
		gst_plugin_feature_set_rank(plugin, GST_RANK_PRIMARY + 1);
		gst_object_unref(plugin);

next:
		name = va_arg(names, const char *);
	}

	va_end(names);
}

int main(int argc, char *args[])
{
	struct SenderImpl *sender;

	arg_parse(&argc, &args);

	gdk_init(&argc, &args);
	gst_init(&argc, &args);
	gst_pb_utils_init();
	//gst_rerank("vaapih264enc", "vaapienc_h264", "glcolorconvert", NULL);

	sender = sender_impl_new();
	if(!sender) {
		g_error("%s", strerror(errno));
	}

	if(!sender_impl_init(sender)) {
		g_error("%s", strerror(errno));
	}

	sender_impl_run(sender);

	g_object_unref(sender);

	return 0;
}
