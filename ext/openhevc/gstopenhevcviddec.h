/* GStreamer
 * Copyright (C) 2018 Matthew Waters <matthew@centricular.com>
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
#ifndef __GST_OPENHEVCVIDDEC_H__
#define __GST_OPENHEVCVIDDEC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <libopenhevc/openhevc.h>

G_BEGIN_DECLS

GType gst_openhevcviddec_get_type (void);

typedef struct _GstOpenHEVCVidDec GstOpenHEVCVidDec;
struct _GstOpenHEVCVidDec
{
  GstVideoDecoder parent;

  GstVideoCodecState *input_state;
  GstVideoCodecState *output_state;

  /* decoding */
  OHHandle hevc_handle;
  gboolean opened;

  /* current output pictures */
  OHFrameInfo frame_info;
  OHFrame frame;

  /* current context */
  gint ctx_ticks;
  gint ctx_time_d;
  gint ctx_time_n;
  GstBuffer *palette;

  int max_threads;
  int temporal_layer_id;
  int quality_layer_id;

  unsigned char *extradata;

  unsigned char *padded;
  gsize padded_size;

  GstCaps *last_caps;
};

typedef struct _GstOpenHEVCVidDecClass GstOpenHEVCVidDecClass;

struct _GstOpenHEVCVidDecClass
{
  GstVideoDecoderClass parent_class;
};

gboolean gst_openhevcviddec_register (GstPlugin * plugin);

G_END_DECLS

#endif
