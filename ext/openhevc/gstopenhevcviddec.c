/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <string.h>

#include "gstopenhevcviddec.h"
#include "gstopenhevc.h"

GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

#define MAX_TS_MASK 0xff

#define REQUIRED_POOL_MAX_BUFFERS       32
#define DEFAULT_STRIDE_ALIGN            31
#define DEFAULT_ALLOC_PARAM             { 0, DEFAULT_STRIDE_ALIGN, 0, 0, }

#define DEFAULT_MAX_THREADS             0
#define DEFAULT_TEMPORAL_LAYER_ID       0
#define DEFAULT_QUALITY_LAYER_ID        0

enum
{
  PROP_0,
  PROP_MAX_THREADS,
  PROP_TEMPORAL_LAYER_ID,
  PROP_QUALITY_LAYER_ID,
  PROP_LAST
};

G_DEFINE_TYPE (GstOpenHEVCVidDec, gst_openhevcviddec, GST_TYPE_VIDEO_DECODER);

static void gst_openhevcviddec_finalize (GObject * object);

static gboolean gst_openhevcviddec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_openhevcviddec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static gboolean gst_openhevcviddec_start (GstVideoDecoder * decoder);
static gboolean gst_openhevcviddec_stop (GstVideoDecoder * decoder);
static gboolean gst_openhevcviddec_flush (GstVideoDecoder * decoder);
static gboolean gst_openhevcviddec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query);
static gboolean gst_openhevcviddec_propose_allocation (GstVideoDecoder * decoder,
    GstQuery * query);

static void gst_openhevcviddec_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_openhevcviddec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_openhevcviddec_negotiate (GstOpenHEVCVidDec * openhevcdec);

static GstFlowReturn gst_openhevcviddec_finish (GstVideoDecoder * decoder);
static GstFlowReturn gst_openhevcviddec_drain (GstVideoDecoder * decoder);

#define GST_FFDEC_PARAMS_QDATA g_quark_from_static_string("openhevcdec-params")

static GstElementClass *parent_class = NULL;

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, "
        "stream-format=(string)byte-stream"//, "
//        "alignment=(string)au"
                     ));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw,format={ (string)I420, (string)I420_10LE }"));

static void
gst_openhevcviddec_class_init (GstOpenHEVCVidDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *viddec_class = GST_VIDEO_DECODER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_openhevcviddec_finalize;

  gobject_class->set_property = gst_openhevcviddec_set_property;
  gobject_class->get_property = gst_openhevcviddec_get_property;

  viddec_class->set_format = gst_openhevcviddec_set_format;
  viddec_class->handle_frame = gst_openhevcviddec_handle_frame;
  viddec_class->start = gst_openhevcviddec_start;
  viddec_class->stop = gst_openhevcviddec_stop;
  viddec_class->flush = gst_openhevcviddec_flush;
  viddec_class->finish = gst_openhevcviddec_finish;
  viddec_class->drain = gst_openhevcviddec_drain;
  viddec_class->decide_allocation = gst_openhevcviddec_decide_allocation;
  viddec_class->propose_allocation = gst_openhevcviddec_propose_allocation;

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_add_static_pad_template (element_class, &sink_template);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MAX_THREADS,
      g_param_spec_int ("max-threads", "Maximum decode threads",
          "Maximum number of worker threads to spawn. (0 = auto)",
          0, G_MAXINT, DEFAULT_MAX_THREADS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_QUALITY_LAYER_ID,
      g_param_spec_int ("quality-layer-id", "Quality Layer ID",
          "The HEVC quality layer to decode",
          0, G_MAXINT, DEFAULT_QUALITY_LAYER_ID,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TEMPORAL_LAYER_ID,
      g_param_spec_int ("temporal-layer-id", "Quality Layer ID",
          "The HEVC temporal layer to decode",
          0, G_MAXINT, DEFAULT_TEMPORAL_LAYER_ID,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_metadata (element_class, "OpenHEVC decoder",
      "Codec/Decoder/Video", "OpenHEVC decoder",
      "Matthew Waters <matthew@centricular.com>");

  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");
}

static void
gst_openhevcviddec_init (GstOpenHEVCVidDec * openhevcdec)
{
  /* some openhevc data */
  openhevcdec->opened = FALSE;

  gst_video_decoder_set_needs_format (GST_VIDEO_DECODER (openhevcdec), TRUE);
}

#ifndef GST_DISABLE_GST_DEBUG
static void
gst_openhevc_log_callback (void *ptr, int level, const char *fmt, va_list vl)
{
  GstDebugLevel gst_level;
  gint len = strlen (fmt);
  gchar *fmt2 = NULL;

  switch (level) {
    case OHEVC_LOG_PANIC:
    case OHEVC_LOG_FATAL:
    case OHEVC_LOG_ERROR:
      gst_level = GST_LEVEL_ERROR;
      break;
    case OHEVC_LOG_WARNING:
      gst_level = GST_LEVEL_WARNING;
      break;
    case OHEVC_LOG_INFO:
      gst_level = GST_LEVEL_INFO;
      break;
    case OHEVC_LOG_VERBOSE:
      gst_level = GST_LEVEL_DEBUG;
      break;
    case OHEVC_LOG_DEBUG:
      gst_level = GST_LEVEL_LOG;
      break;
    case OHEVC_LOG_TRACE:
      gst_level = GST_LEVEL_TRACE;
      break;
    default:
      gst_level = GST_LEVEL_INFO;
      break;
  }

  /* remove trailing newline as it gets already appended by the logger */
  if (fmt[len - 1] == '\n') {
    fmt2 = g_strdup (fmt);
    fmt2[len - 1] = '\0';
  }

  gst_debug_log_valist (GST_CAT_DEFAULT, gst_level, "", "", 0, NULL,
      fmt2 ? fmt2 : fmt, vl);

  g_free (fmt2);
}
#endif

static void
gst_openhevc_close_handle (GstOpenHEVCVidDec * openhevcdec)
{
  if (openhevcdec->hevc_handle != NULL) {
    oh_close (openhevcdec->hevc_handle);
    openhevcdec->hevc_handle = NULL;
  }
}

static void
gst_openhevc_open_handle (GstOpenHEVCVidDec * openhevcdec)
{
  g_return_if_fail (openhevcdec->hevc_handle == NULL);

  /* FIXME: set threads and thread type */
  openhevcdec->hevc_handle = oh_init (1, 1);
#ifndef GST_DISABLE_GST_DEBUG
  oh_set_log_level(openhevcdec->hevc_handle, OHEVC_LOG_VERBOSE);
  oh_set_log_callback (openhevcdec->hevc_handle, gst_openhevc_log_callback);
#endif
  oh_select_active_layer (openhevcdec->hevc_handle, openhevcdec->quality_layer_id);
  oh_select_view_layer (openhevcdec->hevc_handle, openhevcdec->quality_layer_id);
  oh_select_temporal_layer (openhevcdec->hevc_handle, openhevcdec->temporal_layer_id);
}

static void
gst_openhevcviddec_finalize (GObject * object)
{
  GstOpenHEVCVidDec *openhevcdec = (GstOpenHEVCVidDec *) object;

  gst_openhevc_close_handle (openhevcdec);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* with LOCK */
static gboolean
gst_openhevcviddec_close (GstOpenHEVCVidDec * openhevcdec, gboolean reset)
{
  GST_LOG_OBJECT (openhevcdec, "closing openhevc codec");

  gst_caps_replace (&openhevcdec->last_caps, NULL);

  gst_openhevc_close_handle (openhevcdec);
  openhevcdec->opened = FALSE;

  if (openhevcdec->extradata) {
    g_free (openhevcdec->extradata);
    openhevcdec->extradata = NULL;
  }

  return TRUE;
}

/* with LOCK */
static gboolean
gst_openhevcviddec_open (GstOpenHEVCVidDec * openhevcdec)
{
  gst_openhevc_open_handle (openhevcdec);
  if (!openhevcdec->hevc_handle)
    goto could_not_open;

  oh_start(openhevcdec->hevc_handle);
  openhevcdec->opened = TRUE;

  GST_LOG_OBJECT (openhevcdec, "Opened OpenHEVC codec");

  return TRUE;

  /* ERRORS */
could_not_open:
  {
    gst_openhevcviddec_close (openhevcdec, TRUE);
    GST_DEBUG_OBJECT (openhevcdec, "Failed to open OpenHEVC codec");
    return FALSE;
  }
}

static void
_reset_frame_info (OHFrameInfo * frame_info)
{
  frame_info->width = 0;
  frame_info->height = 0;
  frame_info->linesize_y = 0;
  frame_info->linesize_cb = 0;
  frame_info->linesize_cr = 0;
  frame_info->bitdepth = 0;
  frame_info->chromat_format = 0;
  frame_info->sample_aspect_ratio.num = 0;
  frame_info->sample_aspect_ratio.den = 1;
  frame_info->framerate.num = 0;
  frame_info->framerate.den = 1;
  frame_info->display_picture_number = 0;
  frame_info->flag = 0;
  frame_info->pts = 0;
}

static gboolean
_compare_frame_info (OHFrameInfo * src, OHFrameInfo * other)
{
  return src->width == other->width
      && src->height == other->height
      && src->bitdepth && other->bitdepth
      && src->chromat_format == other->chromat_format
      && src->sample_aspect_ratio.num == other->sample_aspect_ratio.num
      && src->sample_aspect_ratio.den == other->sample_aspect_ratio.den
      && src->framerate.num == other->framerate.num
      && src->framerate.den == other->framerate.den;
}

static gboolean
_update_frame_info (GstOpenHEVCVidDec * openhevcdec, OHFrameInfo * info)
{
  /* no change */
  if (_compare_frame_info (&openhevcdec->frame_info, info))
    return FALSE;

  memcpy (&openhevcdec->frame_info, info, sizeof (*info));

  GST_INFO_OBJECT (openhevcdec, "Updating video info to %ix%i depth %i chroma format %i",
      info->width, info->height, info->bitdepth, info->chromat_format);

  return TRUE;
}

static gboolean
gst_openhevcviddec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstOpenHEVCVidDec *openhevcdec;
  GstClockTime latency = GST_CLOCK_TIME_NONE;
  gboolean ret = FALSE;

  openhevcdec = (GstOpenHEVCVidDec *) decoder;

  if (openhevcdec->last_caps != NULL &&
      gst_caps_is_equal (openhevcdec->last_caps, state->caps)) {
    return TRUE;
  }

  GST_DEBUG_OBJECT (openhevcdec, "set_format called");

  GST_OBJECT_LOCK (openhevcdec);

  /* close old session */
  if (openhevcdec->opened) {
    GST_OBJECT_UNLOCK (openhevcdec);
    gst_openhevcviddec_finish (decoder);
    GST_OBJECT_LOCK (openhevcdec);
    if (!gst_openhevcviddec_close (openhevcdec, TRUE)) {
      GST_OBJECT_UNLOCK (openhevcdec);
      return FALSE;
    }
    _reset_frame_info (&openhevcdec->frame_info);
  }

  gst_caps_replace (&openhevcdec->last_caps, state->caps);

  if (!gst_openhevcviddec_open (openhevcdec))
    goto open_failed;

  /* get size and so */
  {
    GstStructure *s;
    const GValue *value, *fps, *par;
    GstBuffer *buf = NULL;

    s = gst_caps_get_structure (state->caps, 0);

    if ((value = gst_structure_get_value (s, "codec_data"))) {
      GstMapInfo map;

      buf = gst_value_get_buffer (value);
      gst_buffer_map (buf, &map, GST_MAP_READ);

      /* allocate with enough padding */
      GST_DEBUG ("copy codec data of size %" G_GSIZE_FORMAT, map.size);
      oh_extradata_cpy (openhevcdec->hevc_handle, map.data, map.size);

      gst_buffer_unmap (buf, &map);
    } else {
      GST_INFO_OBJECT (openhevcdec, "no codec data");
    }

    gst_structure_get_int (s, "width", &openhevcdec->frame_info.width);
    gst_structure_get_int (s, "height", &openhevcdec->frame_info.height);
    gst_structure_get_int (s, "bpp", &openhevcdec->frame_info.bitdepth);

    fps = gst_structure_get_value (s, "framerate");
    if (fps != NULL && GST_VALUE_HOLDS_FRACTION (fps)) {

      int num = gst_value_get_fraction_numerator (fps);
      int den = gst_value_get_fraction_denominator (fps);

      if (num > 0 && den > 0) {
        /* somehow these seem mixed up.. */
        /* they're fine, this is because it does period=1/frequency */
        openhevcdec->frame_info.framerate.den = gst_value_get_fraction_numerator (fps);
        openhevcdec->frame_info.framerate.num = gst_value_get_fraction_denominator (fps);

        GST_DEBUG ("setting framerate %d/%d = %lf",
            openhevcdec->frame_info.framerate.den, openhevcdec->frame_info.framerate.num,
            1. * openhevcdec->frame_info.framerate.den / openhevcdec->frame_info.framerate.num);
      } else {
        GST_INFO ("ignoring framerate %d/%d (probably variable framerate)",
            num, den);
      }
    }

    par = gst_structure_get_value (s, "pixel-aspect-ratio");
    if (par && GST_VALUE_HOLDS_FRACTION (par)) {

      int num = gst_value_get_fraction_numerator (par);
      int den = gst_value_get_fraction_denominator (par);

      if (num > 0 && den > 0) {
        openhevcdec->frame_info.sample_aspect_ratio.num = num;
        openhevcdec->frame_info.sample_aspect_ratio.den = den;

        GST_DEBUG ("setting pixel-aspect-ratio %d/%d = %lf",
            openhevcdec->frame_info.sample_aspect_ratio.num, openhevcdec->frame_info.sample_aspect_ratio.den,
            1. * openhevcdec->frame_info.sample_aspect_ratio.num /
            openhevcdec->frame_info.sample_aspect_ratio.den);
      } else {
        GST_WARNING ("ignoring insane pixel-aspect-ratio %d/%d",
            openhevcdec->frame_info.sample_aspect_ratio.num, openhevcdec->frame_info.sample_aspect_ratio.den);
      }
    }
  }

  GST_LOG_OBJECT (openhevcdec, "size after %dx%d", openhevcdec->frame_info.width,
      openhevcdec->frame_info.height);

  if (!openhevcdec->frame_info.framerate.den || !openhevcdec->frame_info.framerate.num) {
    GST_DEBUG_OBJECT (openhevcdec, "forcing 25/1 framerate");
    openhevcdec->frame_info.framerate.num = 1;
    openhevcdec->frame_info.framerate.den = 25;
  }

  {
#if 0
    GstQuery *query;
    gboolean is_live;

    /* FIXME: choose the number of threads and thread type based on
     * openhevcdec->max_threads; */

    query = gst_query_new_latency ();
    is_live = FALSE;
    /* Check if upstream is live. If it isn't we can enable frame based
     * threading, which is adding latency */
    if (gst_pad_peer_query (GST_VIDEO_DECODER_SINK_PAD (openhevcdec), query)) {
      gst_query_parse_latency (query, &is_live, NULL, NULL);
    }
    gst_query_unref (query);

    if (is_live)
      openhevcdec->context->thread_type = FF_THREAD_SLICE;
    else
      openhevcdec->context->thread_type = FF_THREAD_SLICE | FF_THREAD_FRAME;
#endif
  }
  /* open codec - we don't select an output pix_fmt yet,
   * simply because we don't know! We only get it
   * during playback... */

  if (openhevcdec->input_state)
    gst_video_codec_state_unref (openhevcdec->input_state);
  openhevcdec->input_state = gst_video_codec_state_ref (state);

  if (openhevcdec->input_state->info.fps_n) {
    GstVideoInfo *info = &openhevcdec->input_state->info;
    /* defualt to adding a frame worth of latancy for possible b frames */
    latency = gst_util_uint64_scale_ceil (
        (1) * GST_SECOND, info->fps_d,
        info->fps_n);
  }

  ret = TRUE;

done:
  GST_OBJECT_UNLOCK (openhevcdec);

  if (GST_CLOCK_TIME_IS_VALID (latency))
    gst_video_decoder_set_latency (decoder, latency, latency);

  return ret;

  /* ERRORS */
open_failed:
  {
    GST_DEBUG_OBJECT (openhevcdec, "Failed to open");
    goto done;
  }
}

static void
gst_openhevcviddec_update_par (GstOpenHEVCVidDec * openhevcdec,
    GstVideoInfo * in_info, GstVideoInfo * out_info)
{
  gboolean demuxer_par_set = FALSE;
  gboolean decoder_par_set = FALSE;
  gint demuxer_num = 1, demuxer_denom = 1;
  gint decoder_num = 1, decoder_denom = 1;

  if (in_info->par_n && in_info->par_d) {
    demuxer_num = in_info->par_n;
    demuxer_denom = in_info->par_d;
    demuxer_par_set = TRUE;
    GST_DEBUG_OBJECT (openhevcdec, "Demuxer PAR: %d:%d", demuxer_num,
        demuxer_denom);
  }

  if (openhevcdec->frame_info.sample_aspect_ratio.num && openhevcdec->frame_info.sample_aspect_ratio.den) {
    decoder_num = openhevcdec->frame_info.sample_aspect_ratio.num;
    decoder_denom = openhevcdec->frame_info.sample_aspect_ratio.den;
    decoder_par_set = TRUE;
    GST_DEBUG_OBJECT (openhevcdec, "Decoder PAR: %d:%d", decoder_num,
        decoder_denom);
  }

  if (!demuxer_par_set && !decoder_par_set)
    goto no_par;

  if (demuxer_par_set && !decoder_par_set)
    goto use_demuxer_par;

  if (decoder_par_set && !demuxer_par_set)
    goto use_decoder_par;

  /* Both the demuxer and the decoder provide a PAR. If one of
   * the two PARs is 1:1 and the other one is not, use the one
   * that is not 1:1. */
  if (demuxer_num == demuxer_denom && decoder_num != decoder_denom)
    goto use_decoder_par;

  if (decoder_num == decoder_denom && demuxer_num != demuxer_denom)
    goto use_demuxer_par;

  /* Both PARs are non-1:1, so use the PAR provided by the demuxer */
  goto use_demuxer_par;

use_decoder_par:
  {
    GST_DEBUG_OBJECT (openhevcdec,
        "Setting decoder provided pixel-aspect-ratio of %u:%u", decoder_num,
        decoder_denom);
    out_info->par_n = decoder_num;
    out_info->par_d = decoder_denom;
    return;
  }
use_demuxer_par:
  {
    GST_DEBUG_OBJECT (openhevcdec,
        "Setting demuxer provided pixel-aspect-ratio of %u:%u", demuxer_num,
        demuxer_denom);
    out_info->par_n = demuxer_num;
    out_info->par_d = demuxer_denom;
    return;
  }
no_par:
  {
    GST_DEBUG_OBJECT (openhevcdec,
        "Neither demuxer nor codec provide a pixel-aspect-ratio");
    out_info->par_n = 1;
    out_info->par_d = 1;
    return;
  }
}

static GstVideoFormat
video_format_from_chromat_format (int chroma_format, int bitdepth)
{
  switch (chroma_format) {
    case OH_YUV420:
      if (bitdepth == 8)
        return GST_VIDEO_FORMAT_I420;
      else if (bitdepth == 10)
        return GST_VIDEO_FORMAT_I420_10LE;
    case OH_YUV422:
      /* FIXME: untested */
      if (bitdepth == 8)
        return GST_VIDEO_FORMAT_Y42B;
    case OH_YUV444:
      /* FIXME: untested */
      if (bitdepth == 8)
        return GST_VIDEO_FORMAT_Y444;
      else if (bitdepth == 10)
        return GST_VIDEO_FORMAT_Y444_10LE;
    default:
      GST_WARNING ("Unknown chroma format / bitdepth combination %u %d", chroma_format, bitdepth);
      return GST_VIDEO_FORMAT_UNKNOWN;
  }
}

static gboolean
gst_openhevcviddec_negotiate (GstOpenHEVCVidDec * openhevcdec)
{
  GstVideoFormat fmt;
  GstVideoInfo *in_info, *out_info;
  GstVideoCodecState *output_state;
  gint fps_n, fps_d;
  GstClockTime latency;
  OHFrameInfo new;
//  GstStructure *in_s;

  oh_frameinfo_update (openhevcdec->hevc_handle, &new);

  /* no change in format */
  if (!_update_frame_info (openhevcdec, &new))
    return TRUE;

  fmt = video_format_from_chromat_format (openhevcdec->frame_info.chromat_format, openhevcdec->frame_info.bitdepth);

  output_state =
      gst_video_decoder_set_output_state (GST_VIDEO_DECODER (openhevcdec), fmt,
      openhevcdec->frame_info.width, openhevcdec->frame_info.height, openhevcdec->input_state);
  if (openhevcdec->output_state)
    gst_video_codec_state_unref (openhevcdec->output_state);
  openhevcdec->output_state = output_state;

  in_info = &openhevcdec->input_state->info;
  out_info = &openhevcdec->output_state->info;

  /* set the interlaced flag */
#if 0
  in_s = gst_caps_get_structure (openhevcdec->input_state->caps, 0);

  if (!gst_structure_has_field (in_s, "interlace-mode")) {
    if (openhevcdec->pic_interlaced) {
      if (openhevcdec->pic_field_order_changed ||
          (openhevcdec->pic_field_order & GST_VIDEO_BUFFER_FLAG_RFF)) {
        out_info->interlace_mode = GST_VIDEO_INTERLACE_MODE_MIXED;
      } else {
        out_info->interlace_mode = GST_VIDEO_INTERLACE_MODE_INTERLEAVED;
        if ((openhevcdec->pic_field_order & GST_VIDEO_BUFFER_FLAG_TFF))
          GST_VIDEO_INFO_FIELD_ORDER (out_info) =
              GST_VIDEO_FIELD_ORDER_TOP_FIELD_FIRST;
        else
          GST_VIDEO_INFO_FIELD_ORDER (out_info) =
              GST_VIDEO_FIELD_ORDER_BOTTOM_FIELD_FIRST;
      }
    } else {
      out_info->interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
    }
  }

  if (!gst_structure_has_field (in_s, "chroma-site")) {
    switch (context->chroma_sample_location) {
      case AVCHROMA_LOC_LEFT:
        out_info->chroma_site = GST_VIDEO_CHROMA_SITE_MPEG2;
        break;
      case AVCHROMA_LOC_CENTER:
        out_info->chroma_site = GST_VIDEO_CHROMA_SITE_JPEG;
        break;
      case AVCHROMA_LOC_TOPLEFT:
        out_info->chroma_site = GST_VIDEO_CHROMA_SITE_DV;
        break;
      case AVCHROMA_LOC_TOP:
        out_info->chroma_site = GST_VIDEO_CHROMA_SITE_V_COSITED;
        break;
      default:
        break;
    }
  }

  if (!gst_structure_has_field (in_s, "colorimetry")
      || in_info->colorimetry.primaries == GST_VIDEO_COLOR_PRIMARIES_UNKNOWN) {
    switch (context->color_primaries) {
      case AVCOL_PRI_BT709:
        out_info->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
        break;
      case AVCOL_PRI_BT470M:
        out_info->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT470M;
        break;
      case AVCOL_PRI_BT470BG:
        out_info->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT470BG;
        break;
      case AVCOL_PRI_SMPTE170M:
        out_info->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_SMPTE170M;
        break;
      case AVCOL_PRI_SMPTE240M:
        out_info->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_SMPTE240M;
        break;
      case AVCOL_PRI_FILM:
        out_info->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_FILM;
        break;
      case AVCOL_PRI_BT2020:
        out_info->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
        break;
      default:
        break;
    }
  }

  if (!gst_structure_has_field (in_s, "colorimetry")
      || in_info->colorimetry.transfer == GST_VIDEO_TRANSFER_UNKNOWN) {
    switch (context->color_trc) {
      case AVCOL_TRC_BT2020_10:
      case AVCOL_TRC_BT709:
      case AVCOL_TRC_SMPTE170M:
        out_info->colorimetry.transfer = GST_VIDEO_TRANSFER_BT709;
        break;
      case AVCOL_TRC_GAMMA22:
        out_info->colorimetry.transfer = GST_VIDEO_TRANSFER_GAMMA22;
        break;
      case AVCOL_TRC_GAMMA28:
        out_info->colorimetry.transfer = GST_VIDEO_TRANSFER_GAMMA28;
        break;
      case AVCOL_TRC_SMPTE240M:
        out_info->colorimetry.transfer = GST_VIDEO_TRANSFER_SMPTE240M;
        break;
      case AVCOL_TRC_LINEAR:
        out_info->colorimetry.transfer = GST_VIDEO_TRANSFER_GAMMA10;
        break;
      case AVCOL_TRC_LOG:
        out_info->colorimetry.transfer = GST_VIDEO_TRANSFER_LOG100;
        break;
      case AVCOL_TRC_LOG_SQRT:
        out_info->colorimetry.transfer = GST_VIDEO_TRANSFER_LOG316;
        break;
      case AVCOL_TRC_BT2020_12:
        out_info->colorimetry.transfer = GST_VIDEO_TRANSFER_BT2020_12;
        break;
      default:
        break;
    }
  }

  if (!gst_structure_has_field (in_s, "colorimetry")
      || in_info->colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_UNKNOWN) {
    switch (context->colorspace) {
      case AVCOL_SPC_RGB:
        out_info->colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
        break;
      case AVCOL_SPC_BT709:
        out_info->colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
        break;
      case AVCOL_SPC_FCC:
        out_info->colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_FCC;
        break;
      case AVCOL_SPC_BT470BG:
      case AVCOL_SPC_SMPTE170M:
        out_info->colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT601;
        break;
      case AVCOL_SPC_SMPTE240M:
        out_info->colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_SMPTE240M;
        break;
      case AVCOL_SPC_BT2020_NCL:
        out_info->colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
        break;
      default:
        break;
    }
  }

  if (!gst_structure_has_field (in_s, "colorimetry")
      || in_info->colorimetry.range == GST_VIDEO_COLOR_RANGE_UNKNOWN) {
    if (context->color_range == AVCOL_RANGE_JPEG) {
      out_info->colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;
    } else {
      out_info->colorimetry.range = GST_VIDEO_COLOR_RANGE_16_235;
    }
  }
#endif
  /* try to find a good framerate */
  if ((in_info->fps_d && in_info->fps_n) ||
      GST_VIDEO_INFO_FLAG_IS_SET (in_info, GST_VIDEO_FLAG_VARIABLE_FPS)) {
    /* take framerate from input when it was specified (#313970) */
    fps_n = in_info->fps_n;
    fps_d = in_info->fps_d;
  } else {
    fps_n = openhevcdec->frame_info.framerate.num;
    fps_d = openhevcdec->frame_info.framerate.den;

    if (!fps_d) {
      GST_LOG_OBJECT (openhevcdec, "invalid framerate: %d/0, -> %d/1", fps_n,
          fps_n);
      fps_d = 1;
    }
    if (gst_util_fraction_compare (fps_n, fps_d, 1000, 1) > 0) {
      GST_LOG_OBJECT (openhevcdec, "excessive framerate: %d/%d, -> 0/1", fps_n,
          fps_d);
      fps_n = 0;
      fps_d = 1;
    }
  }

  GST_LOG_OBJECT (openhevcdec, "setting framerate: %d/%d", fps_n, fps_d);
  out_info->fps_n = fps_n;
  out_info->fps_d = fps_d;

  /* calculate and update par now */
  gst_openhevcviddec_update_par (openhevcdec, in_info, out_info);
#if 0
  GST_VIDEO_INFO_MULTIVIEW_MODE (out_info) = openhevcdec->cur_multiview_mode;
  GST_VIDEO_INFO_MULTIVIEW_FLAGS (out_info) = openhevcdec->cur_multiview_flags;
#endif
  if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (openhevcdec)))
    goto negotiate_failed;

  /* The decoder is configured, we now know the true latency */
  if (fps_n) {
    latency =
        gst_util_uint64_scale_ceil (GST_SECOND, fps_d, fps_n);
    gst_video_decoder_set_latency (GST_VIDEO_DECODER (openhevcdec), latency,
        latency);
  }

  return TRUE;
#if 0
  /* ERRORS */
unknown_format:
  {
    GST_ERROR_OBJECT (openhevcdec,
        "decoder requires a video format unsupported by GStreamer");
    return FALSE;
  }
#endif
  negotiate_failed:
  {
    /* Reset so we try again next time even if force==FALSE */
    _reset_frame_info (&openhevcdec->frame_info);

    GST_ERROR_OBJECT (openhevcdec, "negotiation failed");
    return FALSE;
  }
}

static gboolean
copy_frame_to_codec_frame (GstOpenHEVCVidDec * openhevcdec, OHFrame * frame, GstVideoCodecFrame * out_frame)
{
  GstFlowReturn ret;
  GstVideoInfo dst_info;
  GstVideoFrame dst_frame;
  gboolean res = FALSE;
  gsize p;

  ret = gst_video_decoder_allocate_output_frame (GST_VIDEO_DECODER (openhevcdec), out_frame);
  if (ret != GST_FLOW_OK)
    goto error;

  if (!gst_video_info_set_format (&dst_info,
      video_format_from_chromat_format (frame->frame_par.chromat_format, frame->frame_par.bitdepth),
      frame->frame_par.width, frame->frame_par.height)) {
    GST_ERROR_OBJECT (openhevcdec, "Could not set destination video info");
    goto error;
  }

  if (!gst_video_frame_map (&dst_frame, &dst_info, out_frame->output_buffer, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (openhevcdec, "Failed to map destination video frame");
    goto error;
  }

  for (p = 0; p < GST_VIDEO_FRAME_N_PLANES (&dst_frame); p++) {
    /* plane 0 */
    gsize src_pos = 0, dst_pos = 0;
    guint8 * dst = dst_frame.data[p];
    gsize dst_stride = GST_VIDEO_FRAME_COMP_STRIDE (&dst_frame, p);
    guint8 * src;
    gsize src_stride;
    gsize l;

    if (p == 0) {
      src = (guint8 *) frame->data_y_p;
      src_stride = frame->frame_par.linesize_y;
    } else if (p == 1) {
      src = (guint8 *) frame->data_cb_p;
      src_stride = frame->frame_par.linesize_cb;
    } else if (p == 2) {
      src = (guint8 *) frame->data_cr_p;
      src_stride = frame->frame_par.linesize_cr;
    }

    for (l = 0; l < GST_VIDEO_FRAME_COMP_HEIGHT (&dst_frame, p); l++) {
      memcpy (&dst[dst_pos], &src[src_pos], GST_VIDEO_FRAME_COMP_STRIDE (&dst_frame, p));
      src_pos += src_stride;
      dst_pos += dst_stride;
    }
  }

  gst_video_frame_unmap (&dst_frame);

  GST_VIDEO_CODEC_FRAME_FLAG_UNSET (out_frame,
      GST_VIDEO_CODEC_FRAME_FLAG_DECODE_ONLY);

  res = TRUE;

done:
  return res;

error:
  res = FALSE;
  goto done;
}

/*
 * Returns: whether a frame was decoded
 */
static int
gst_openhevcviddec_video_frame (GstOpenHEVCVidDec * openhevcdec,
    GstVideoCodecFrame * frame, int got_picture, GstFlowReturn * ret)
{
  int got_frame = FALSE;
  GstVideoCodecFrame *out_frame = NULL;

  *ret = GST_FLOW_OK;

  got_frame = oh_output_update (openhevcdec->hevc_handle, got_picture, &openhevcdec->frame);
  if (got_frame == 0) {
    goto beach;
  } else if (got_frame < 0) {
    *ret = GST_FLOW_OK;
    GST_WARNING_OBJECT (openhevcdec, "Legitimate decoding error");
    goto beach;
  }

  {
    GList *l, *ol;
    GstVideoDecoder *dec = GST_VIDEO_DECODER (openhevcdec);

    GST_TRACE_OBJECT (openhevcdec, "Attempting to find frame with pts: %" G_GUINT64_FORMAT, openhevcdec->frame.frame_par.pts);

    ol = l = gst_video_decoder_get_frames (dec);
    while (l) {
      GstVideoCodecFrame *tmp = l->data;

    GST_TRACE_OBJECT (openhevcdec, "checking existing frame with pts: %" G_GUINT64_FORMAT, tmp->pts);
      if (openhevcdec->frame.frame_par.pts == tmp->pts) {
        out_frame = tmp;
      } else {
        gst_video_codec_frame_unref (tmp);
      }
      l = l->next;
    }
    g_list_free (ol);
  }
  if (!out_frame) {
    got_frame = 0;
    goto beach;
  }

  /* Extract auxilliary info not stored in the main AVframe */
  {
#if 0
    GstVideoInfo *in_info = &openhevcdec->input_state->info;
    /* Take multiview mode from upstream if present */
    openhevcdec->picture_multiview_mode = GST_VIDEO_INFO_MULTIVIEW_MODE (in_info);
    openhevcdec->picture_multiview_flags =
        GST_VIDEO_INFO_MULTIVIEW_FLAGS (in_info);

    /* Otherwise, see if there's info in the frame */
    if (openhevcdec->picture_multiview_mode == GST_VIDEO_MULTIVIEW_MODE_NONE) {
      AVFrameSideData *side_data =
          av_frame_get_side_data (openhevcdec->picture, AV_FRAME_DATA_STEREO3D);
      if (side_data) {
        AVStereo3D *stereo = (AVStereo3D *) side_data->data;
        openhevcdec->picture_multiview_mode = stereo_av_to_gst (stereo->type);
        if (stereo->flags & AV_STEREO3D_FLAG_INVERT) {
          openhevcdec->picture_multiview_flags =
              GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_VIEW_FIRST;
        } else {
          openhevcdec->picture_multiview_flags = GST_VIDEO_MULTIVIEW_FLAGS_NONE;
        }
      }
    }
#endif
  }

  GST_DEBUG_OBJECT (openhevcdec,
      "pts %" G_GUINT64_FORMAT " duration %" G_GUINT64_FORMAT,
      out_frame->pts, out_frame->duration);
  GST_DEBUG_OBJECT (openhevcdec, "picture: pts %" G_GUINT64_FORMAT,
      (guint64) openhevcdec->frame.frame_par.pts);

  if (!gst_openhevcviddec_negotiate (openhevcdec))
    goto negotiation_error;

  gst_buffer_replace (&out_frame->output_buffer, NULL);
  if (!copy_frame_to_codec_frame (openhevcdec, &openhevcdec->frame, out_frame))
    goto no_output;
#if 0
  if (openhevcdec->pic_interlaced) {
    /* set interlaced flags */
    if (openhevcdec->picture->repeat_pict)
      GST_BUFFER_FLAG_SET (out_frame->output_buffer, GST_VIDEO_BUFFER_FLAG_RFF);
    if (openhevcdec->picture->top_field_first)
      GST_BUFFER_FLAG_SET (out_frame->output_buffer, GST_VIDEO_BUFFER_FLAG_TFF);
    if (openhevcdec->picture->interlaced_frame)
      GST_BUFFER_FLAG_SET (out_frame->output_buffer,
          GST_VIDEO_BUFFER_FLAG_INTERLACED);
  }
#endif
  /* cleaning time */
  /* so we decoded this frame, frames preceding it in decoding order
   * that still do not have a buffer allocated seem rather useless,
   * and can be discarded, due to e.g. misparsed bogus frame
   * or non-keyframe in skipped decoding, ...
   * In any case, not likely to be seen again, so discard those,
   * before they pile up and/or mess with timestamping */
  {
    GList *l, *ol;
    GstVideoDecoder *dec = GST_VIDEO_DECODER (openhevcdec);
    gboolean old = TRUE;

    ol = l = gst_video_decoder_get_frames (dec);
    while (l) {
      GstVideoCodecFrame *tmp = l->data;

      if (tmp == out_frame)
        old = FALSE;

      if (old && GST_VIDEO_CODEC_FRAME_IS_DECODE_ONLY (tmp)) {
        GST_LOG_OBJECT (dec,
            "discarding ghost frame %p (#%d) PTS:%" GST_TIME_FORMAT " DTS:%"
            GST_TIME_FORMAT, tmp, tmp->system_frame_number,
            GST_TIME_ARGS (tmp->pts), GST_TIME_ARGS (tmp->dts));
        /* drop extra ref and remove from frame list */
        gst_video_decoder_release_frame (dec, tmp);
      } else {
        /* drop extra ref we got */
        gst_video_codec_frame_unref (tmp);
      }
      l = l->next;
    }
    g_list_free (ol);
  }

  *ret = gst_video_decoder_finish_frame (GST_VIDEO_DECODER (openhevcdec), out_frame);

beach:
  GST_DEBUG_OBJECT (openhevcdec, "return flow %s, got frame: %d",
      gst_flow_get_name (*ret), got_frame);
  return got_frame;

  /* special cases */
no_output:
  {
    GST_DEBUG_OBJECT (openhevcdec, "no output buffer");
    gst_video_decoder_drop_frame (GST_VIDEO_DECODER (openhevcdec), out_frame);
    goto beach;
  }

negotiation_error:
  {
    if (GST_PAD_IS_FLUSHING (GST_VIDEO_DECODER_SRC_PAD (openhevcdec))) {
      *ret = GST_FLOW_FLUSHING;
      goto beach;
    }
    GST_WARNING_OBJECT (openhevcdec, "Error negotiating format");
    *ret = GST_FLOW_NOT_NEGOTIATED;
    goto beach;
  }
}


 /* Returns: Whether a frame was decoded */
static gboolean
gst_openhevcviddec_frame (GstOpenHEVCVidDec * openhevcdec, GstVideoCodecFrame * frame,
    int got_picture, GstFlowReturn * ret)
{
  int got_frame = 0;

  if (G_UNLIKELY (openhevcdec->hevc_handle == NULL))
    goto no_codec;

  *ret = GST_FLOW_OK;

  got_frame = gst_openhevcviddec_video_frame (openhevcdec, frame, got_picture, ret);

  return got_frame > 0;

  /* ERRORS */
no_codec:
  {
    GST_ERROR_OBJECT (openhevcdec, "no codec context");
    *ret = GST_FLOW_NOT_NEGOTIATED;
    return -1;
  }
}

static GstFlowReturn
gst_openhevcviddec_drain (GstVideoDecoder * decoder)
{
  GstOpenHEVCVidDec *openhevcdec = (GstOpenHEVCVidDec *) decoder;

  if (!openhevcdec->opened)
    return GST_FLOW_OK;

  if (TRUE) {
    GstFlowReturn ret;
    gboolean got_frame = FALSE;

    GST_LOG_OBJECT (openhevcdec,
        "codec has delay capabilities, calling until openhevc has drained everything");

    do {
      got_frame = gst_openhevcviddec_frame (openhevcdec, NULL, -1, &ret);
    } while (got_frame && ret == GST_FLOW_OK);
    oh_flush (openhevcdec->hevc_handle);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_openhevcviddec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstOpenHEVCVidDec *openhevcdec = (GstOpenHEVCVidDec *) decoder;
  guint8 *data;
  gint size;
  int got_picture, got_decode;
  GstMapInfo minfo;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (openhevcdec,
      "Received new data of size %" G_GSIZE_FORMAT ", dts %" GST_TIME_FORMAT
      ", pts:%" GST_TIME_FORMAT ", dur:%" GST_TIME_FORMAT,
      gst_buffer_get_size (frame->input_buffer), GST_TIME_ARGS (frame->dts),
      GST_TIME_ARGS (frame->pts), GST_TIME_ARGS (frame->duration));

  if (!gst_buffer_map (frame->input_buffer, &minfo, GST_MAP_READ)) {
    GST_ELEMENT_ERROR (openhevcdec, STREAM, DECODE, ("Decoding problem"),
        ("Failed to map buffer for reading"));
    return GST_FLOW_ERROR;
  }

  /* treat frame as void until a buffer is requested for it */
  GST_VIDEO_CODEC_FRAME_FLAG_SET (frame,
      GST_VIDEO_CODEC_FRAME_FLAG_DECODE_ONLY);

  data = minfo.data;
  size = minfo.size;
#if 0
  if (size > 0 && (!GST_MEMORY_IS_ZERO_PADDED (minfo.memory)
          || (minfo.maxsize - minfo.size) < AV_INPUT_BUFFER_PADDING_SIZE)) {
    /* add padding */
    if (openhevcdec->padded_size < size + AV_INPUT_BUFFER_PADDING_SIZE) {
      openhevcdec->padded_size = size + AV_INPUT_BUFFER_PADDING_SIZE;
      openhevcdec->padded = g_realloc (openhevcdec->padded, openhevcdec->padded_size);
      GST_LOG_OBJECT (openhevcdec, "resized padding buffer to %" G_GSIZE_FORMAT,
          openhevcdec->padded_size);
    }
    GST_CAT_TRACE_OBJECT (GST_CAT_PERFORMANCE, openhevcdec,
        "Copy input to add padding");
    memcpy (openhevcdec->padded, data, size);
    memset (openhevcdec->padded + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    data = openhevcdec->padded;
  }
#endif

  /* no way of associating data with the input we pass to OpenHevc so we rely
   * on the pts */
  got_decode = oh_decode (openhevcdec->hevc_handle, data, size, frame->pts);

  if (got_decode < 0)
    goto decode_error;

  if (!((1 << openhevcdec->quality_layer_id) & got_decode))
    goto done;

  do {
    /* decode a frame of audio/video now */
    got_picture = gst_openhevcviddec_frame (openhevcdec, frame, got_decode, &ret);

    if (ret != GST_FLOW_OK) {
      GST_LOG_OBJECT (openhevcdec, "breaking because of flow ret %s",
          gst_flow_get_name (ret));
      break;
    }
  } while (got_picture);

done:
  gst_buffer_unmap (frame->input_buffer, &minfo);
  gst_video_codec_frame_unref (frame);

  return ret;

decode_error:
  {
    GST_WARNING_OBJECT (openhevcdec, "Failed to send data for decoding");
    goto done;
  }
}

static gboolean
gst_openhevcviddec_start (GstVideoDecoder * decoder)
{
  GstOpenHEVCVidDec *openhevcdec = (GstOpenHEVCVidDec *) decoder;

  GST_OBJECT_LOCK (openhevcdec);
  gst_openhevcviddec_close (openhevcdec, FALSE);
  GST_OBJECT_UNLOCK (openhevcdec);

  return TRUE;
}

static gboolean
gst_openhevcviddec_stop (GstVideoDecoder * decoder)
{
  GstOpenHEVCVidDec *openhevcdec = (GstOpenHEVCVidDec *) decoder;

  GST_OBJECT_LOCK (openhevcdec);
  gst_openhevcviddec_close (openhevcdec, FALSE);
  GST_OBJECT_UNLOCK (openhevcdec);
  if (openhevcdec->input_state)
    gst_video_codec_state_unref (openhevcdec->input_state);
  openhevcdec->input_state = NULL;
  if (openhevcdec->output_state)
    gst_video_codec_state_unref (openhevcdec->output_state);
  openhevcdec->output_state = NULL;

  return TRUE;
}

static GstFlowReturn
gst_openhevcviddec_finish (GstVideoDecoder * decoder)
{
  gst_openhevcviddec_drain (decoder);
  /* note that finish can and should clean up more drastically,
   * but drain is also invoked on e.g. packet loss in GAP handling */
  gst_openhevcviddec_flush (decoder);

  return GST_FLOW_OK;
}

static gboolean
gst_openhevcviddec_flush (GstVideoDecoder * decoder)
{
  GstOpenHEVCVidDec *openhevcdec = (GstOpenHEVCVidDec *) decoder;

  if (openhevcdec->opened) {
    GST_LOG_OBJECT (decoder, "flushing buffers");
    oh_flush (openhevcdec->hevc_handle);
  }

  return TRUE;
}

static gboolean
gst_openhevcviddec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstVideoCodecState *state;
  GstBufferPool *pool;
  guint size, min, max;
  GstStructure *config;
  gboolean have_pool, have_videometa, have_alignment, update_pool = FALSE;
  GstAllocator *allocator = NULL;
  GstAllocationParams params = DEFAULT_ALLOC_PARAM;

  have_pool = (gst_query_get_n_allocation_pools (query) != 0);

  if (!GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation (decoder,
          query))
    return FALSE;

  state = gst_video_decoder_get_output_state (decoder);

  if (gst_query_get_n_allocation_params (query) > 0) {
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    params.align = MAX (params.align, DEFAULT_STRIDE_ALIGN);
  } else {
    gst_query_add_allocation_param (query, allocator, &params);
  }

  gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, state->caps, size, min, max);
  gst_buffer_pool_config_set_allocator (config, allocator, &params);

  have_videometa =
      gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  if (have_videometa)
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

  have_alignment =
      gst_buffer_pool_has_option (pool, GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

  /* configure */
  if (!gst_buffer_pool_set_config (pool, config)) {
    gboolean working_pool = FALSE;
    config = gst_buffer_pool_get_config (pool);

    if (gst_buffer_pool_config_validate_params (config, state->caps, size, min,
            max)) {
      working_pool = gst_buffer_pool_set_config (pool, config);
    } else {
      gst_structure_free (config);
    }

    if (!working_pool) {
      gst_object_unref (pool);
      pool = gst_video_buffer_pool_new ();
      config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_set_params (config, state->caps, size, min, max);
      gst_buffer_pool_config_set_allocator (config, NULL, &params);
      gst_buffer_pool_set_config (pool, config);
      update_pool = TRUE;
    }
  }

  /* and store */
  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);

  gst_object_unref (pool);
  if (allocator)
    gst_object_unref (allocator);
  gst_video_codec_state_unref (state);

  return TRUE;
}

static gboolean
gst_openhevcviddec_propose_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstAllocationParams params;

  gst_allocation_params_init (&params);
  params.flags = GST_MEMORY_FLAG_ZERO_PADDED;
  params.align = DEFAULT_STRIDE_ALIGN;
//  params.padding = AV_INPUT_BUFFER_PADDING_SIZE;
  /* we would like to have some padding so that we don't have to
   * memcpy. We don't suggest an allocator. */
  gst_query_add_allocation_param (query, NULL, &params);

  return GST_VIDEO_DECODER_CLASS (parent_class)->propose_allocation (decoder,
      query);
}

static void
gst_openhevcviddec_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstOpenHEVCVidDec *openhevcdec = (GstOpenHEVCVidDec *) object;

  switch (prop_id) {
    case PROP_MAX_THREADS:
      openhevcdec->max_threads = g_value_get_int (value);
      break;
    case PROP_TEMPORAL_LAYER_ID:
      openhevcdec->temporal_layer_id = g_value_get_int (value);
      break;
    case PROP_QUALITY_LAYER_ID:
      openhevcdec->quality_layer_id = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_openhevcviddec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstOpenHEVCVidDec *openhevcdec = (GstOpenHEVCVidDec *) object;

  switch (prop_id) {
    case PROP_MAX_THREADS:
      g_value_set_int (value, openhevcdec->max_threads);
      break;
    case PROP_TEMPORAL_LAYER_ID:
      g_value_set_int (value, openhevcdec->temporal_layer_id);
      break;
    case PROP_QUALITY_LAYER_ID:
      g_value_set_int (value, openhevcdec->quality_layer_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_openhevcviddec_register (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "openhevcdec", GST_RANK_MARGINAL, gst_openhevcviddec_get_type())) {
    g_warning ("Failed to register openhevcdec");
    return FALSE;
  }

  return TRUE;
}
