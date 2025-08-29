#include "webrtc_stats.h"

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc_fwd.h>
#include <stdint.h>
#undef GST_USE_UNSTABLE_API

#define ENUM_TO_STR(r) \
    case r:            \
        return #r

const gchar *webrtc_stats_type_string(const GstWebRTCStatsType type) {
    switch (type) {
        ENUM_TO_STR(GST_WEBRTC_STATS_CODEC);
        ENUM_TO_STR(GST_WEBRTC_STATS_INBOUND_RTP);
        ENUM_TO_STR(GST_WEBRTC_STATS_OUTBOUND_RTP);
        ENUM_TO_STR(GST_WEBRTC_STATS_REMOTE_INBOUND_RTP);
        ENUM_TO_STR(GST_WEBRTC_STATS_REMOTE_OUTBOUND_RTP);
        ENUM_TO_STR(GST_WEBRTC_STATS_CSRC);
        ENUM_TO_STR(GST_WEBRTC_STATS_PEER_CONNECTION);
        ENUM_TO_STR(GST_WEBRTC_STATS_DATA_CHANNEL);
        ENUM_TO_STR(GST_WEBRTC_STATS_STREAM);
        ENUM_TO_STR(GST_WEBRTC_STATS_TRANSPORT);
        ENUM_TO_STR(GST_WEBRTC_STATS_CANDIDATE_PAIR);
        ENUM_TO_STR(GST_WEBRTC_STATS_LOCAL_CANDIDATE);
        ENUM_TO_STR(GST_WEBRTC_STATS_REMOTE_CANDIDATE);
        ENUM_TO_STR(GST_WEBRTC_STATS_CERTIFICATE);
        default:
            return "UNKNOWN RESULT";
    }
}

typedef struct {
    GString *str;
    gint fields_to_print;
    uint32_t indent;
} PrintInfo;

static gboolean print_field(const GQuark field_id, const GValue *value, const gpointer user_data) {
    const gchar *name = g_quark_to_string(field_id);

    const GType type = g_type_fundamental(G_VALUE_TYPE(value));

    PrintInfo *info = (PrintInfo *)user_data;
    info->fields_to_print--;

    for (uint32_t i = 0; i < info->indent; i++) {
        g_string_append(info->str, "\t");
    }

    // clang-format off
	switch (type) {
		case G_TYPE_STRING:
			g_string_append_printf(info->str, "\"%s\": \"%s\"",
			                       name, g_value_get_string(value));
			break;
		case G_TYPE_INT:
			g_string_append_printf(info->str, "\"%s\": %d",
			                       name, g_value_get_int(value));
			break;
		case G_TYPE_DOUBLE:
			g_string_append_printf(info->str, "\"%s\": %f",
			                       name, g_value_get_double(value));
			break;
		case G_TYPE_ENUM:
			g_string_append_printf(info->str, "\"%s\": \"%s\"", name,
			                       webrtc_stats_type_string(g_value_get_enum(value)));
			break;
		case G_TYPE_UINT:
			g_string_append_printf(info->str, "\"%s\": %d",
			                       name, g_value_get_uint(value));
			break;
		case G_TYPE_UINT64:
			g_string_append_printf(info->str, "\"%s\": %ld",
			                       name, g_value_get_uint64(value));
			break;
		case G_TYPE_INT64:
			g_string_append_printf(info->str, "\"%s\": %ld",
			                       name, g_value_get_int64(value));
			break;
		case G_TYPE_BOOLEAN:
			g_string_append_printf(info->str, "\"%s\": %s", name,
			                       g_value_get_boolean(value) ? "true" : "false");
			break;
		case G_TYPE_BOXED: {
			GstStructure *sub_structre = g_value_get_boxed(value);
			PrintInfo sub_info = {
				.str = info->str,
				.fields_to_print = gst_structure_n_fields(sub_structre),
				.indent = info->indent + 1,
			};
			g_string_append_printf(info->str, "\"%s\": {\n", name);
			gst_structure_foreach(sub_structre, print_field, &sub_info);
			for (uint32_t i = 0; i < info->indent; i++) {
				g_string_append(info->str, "\t");
			}
			g_string_append(info->str, "}");
			break;
		}
		default:
			g_printerr("%s: Unhandled type %s (%ld)\n", name, g_type_name(type), type);
	}
    // clang-format on

    if (info->fields_to_print != 0) {
        g_string_append(info->str, ",");
    }
    g_string_append(info->str, "\n");

    return TRUE;
}

static gboolean stats_foreach(GQuark field_id, const GValue *value, const gpointer user_data) {
    if (!GST_VALUE_HOLDS_STRUCTURE(value)) {
        g_printerr("Structure does not hold a value.");
        return FALSE;
    }

    PrintInfo *outer_info = (PrintInfo *)user_data;
    outer_info->fields_to_print--;

    const GstStructure *s = gst_value_get_structure(value);
    PrintInfo info = {
        .str = outer_info->str,
        .fields_to_print = gst_structure_n_fields(s),
        .indent = 2,
    };

    g_string_append(outer_info->str, "\t{\n");
    gst_structure_foreach(s, print_field, &info);
    g_string_append(outer_info->str, "\t}");

    if (outer_info->fields_to_print != 0) {
        g_string_append(outer_info->str, ",");
    }

    g_string_append(outer_info->str, "\n");

    return TRUE;
}

GString *webrtc_stats_get_json(const GstStructure *stats) {
    GString *json_str = g_string_new("");

    PrintInfo info = {
        .str = json_str,
        .fields_to_print = gst_structure_n_fields(stats),
        .indent = 1,
    };

    g_string_append(json_str, "[\n");
    gst_structure_foreach(stats, (GstStructureForeachFunc)stats_foreach, &info);
    g_string_append(json_str, "]\n");

    return json_str;
}

// static void on_webrtcbin_stats(GstPromise *promise, GstElement *user_data) {
//     const GstStructure *reply = gst_promise_get_reply(promise);
//
//     GString *json = webrtc_stats_get_json(reply);
//     // ALOGD("webrtcbin stats: %s", json->str);
//     g_string_free(json, TRUE);
// }
//
// static gboolean print_webrtc_stats() {
//     if (!recv_state.webrtcbin) {
//         return G_SOURCE_CONTINUE;
//     }
//
//     GstPromise *promise = gst_promise_new_with_change_func((GstPromiseChangeFunc)on_webrtcbin_stats, NULL, NULL);
//     g_signal_emit_by_name(recv_state.webrtcbin, "get-stats", NULL, promise);
//
//     // Show FEC stats
//     if (0) {
//         for (int i = 0; i < 2; i++) {
//             gchar *name;
//             if (i == 0) {
//                 name = "rtpulpfecdec0";
//             } else {
//                 name = "rtpulpfecdec1";
//             }
//
//             GstElement *rtpulpfecdec = find_element_by_name(GST_BIN(recv_state.webrtcbin), name);
//
//             if (rtpulpfecdec) {
//                 GValue pt = G_VALUE_INIT;
//                 GValue recovered = G_VALUE_INIT;
//                 GValue unrecovered = G_VALUE_INIT;
//
//                 g_object_get_property(G_OBJECT(rtpulpfecdec), "pt", &pt);
//                 g_object_get_property(G_OBJECT(rtpulpfecdec), "recovered", &recovered);
//                 g_object_get_property(G_OBJECT(rtpulpfecdec), "unrecovered", &unrecovered);
//
//                 g_print("FEC stats: pt %u, recovered %u, unrecovered %u\n",
//                         g_value_get_uint(&pt),
//                         g_value_get_uint(&recovered),
//                         g_value_get_uint(&unrecovered));
//
//                 g_value_unset(&pt);
//                 g_value_unset(&recovered);
//                 g_value_unset(&unrecovered);
//             }
//         }
//     }
//
//     return G_SOURCE_CONTINUE;
// }
//
// static gboolean check_pipeline_dot_data() {
//     if (!recv_state.pipeline) {
//         return G_SOURCE_CONTINUE;
//     }
//
//     gchar *dot_data = gst_debug_bin_to_dot_data(GST_BIN(recv_state.pipeline), GST_DEBUG_GRAPH_SHOW_ALL);
//     g_free(dot_data);
//
//     return G_SOURCE_CONTINUE;
// }