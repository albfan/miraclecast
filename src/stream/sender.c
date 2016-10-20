/*
 * =====================================================================================
 *
 *       Filename:  sender.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  2016年10月17日 18時11分16秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <stdio.h>
#include <glib-object.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/pbutils/encoding-profile.h>

int main(int argc, char *args[])
{
	GMainLoop *loop;
	GstElement *pipeline;
	GError *error = NULL;

	gst_init(&argc, &args);
	gst_pb_utils_init();

	GString *desc = g_string_new("");
	g_string_append_printf(desc,
				"ximagesrc "
				"! videoscale "
				"! video/x-raw, width=%d, height=%d, framerate=%d/1 "
				"! videoconvert name=vconverter "
				"! video/x-raw, format=NV12 "
				"! encodebin name=encoder "
				"! rtpmp2tpay "
				"! udpsink host=%s port=%d ",
				1280, 720, 30,
				"127.0.0.1",
				1991);

	pipeline = gst_parse_launch(desc->str, &error);
	if(GST_PARSE_ERROR_LINK != error->code) {
		g_error("%s", error->message);
		goto error;
	}

	GstCaps *caps = gst_caps_from_string("video/mpegts, systemstream=true, packetsize=188");
	GstEncodingContainerProfile *container = gst_encoding_container_profile_new("mpeg-ts-profile",
				NULL,
				caps,
				NULL);
	gst_caps_unref(caps);

	caps = gst_caps_from_string("video/x-h264, format=YV12, width=1280, height=720");
	GstEncodingVideoProfile *vencoder = gst_encoding_video_profile_new(caps, NULL, NULL, 0);
	gst_caps_unref(caps);

	gst_encoding_container_profile_add_profile(container, GST_ENCODING_PROFILE(vencoder));
	/*g_object_unref(vencoder);*/

	GstElement *encoder = gst_bin_get_by_name(GST_BIN(pipeline), "encoder");
	g_object_set(G_OBJECT(encoder), "profile", container, NULL);
	/*g_object_unref(container);*/

	GstElement *vconverter = gst_bin_get_by_name(GST_BIN(pipeline), "vconverter");
	if(!gst_element_link(vconverter, encoder)) {
		printf("failed to link vconverter to encoder\n");
	}

	gst_element_set_state(pipeline, GST_STATE_PLAYING);

	loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(loop);

error:
	g_object_unref(pipeline);
end:
	return 0;
}
