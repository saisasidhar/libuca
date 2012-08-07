/* Copyright (C) 2011, 2012 Matthias Vogelgesang <matthias.vogelgesang@kit.edu>
   (Karlsruhe Institute of Technology)

   This library is free software; you can redistribute it and/or modify it
   under the terms of the GNU Lesser General Public License as published by the
   Free Software Foundation; either version 2.1 of the License, or (at your
   option) any later version.

   This library is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
   details.

   You should have received a copy of the GNU Lesser General Public License along
   with this library; if not, write to the Free Software Foundation, Inc., 51
   Franklin St, Fifth Floor, Boston, MA 02110, USA */

#include <glib-object.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include "uca-camera.h"

static UcaCamera *camera = NULL;

static void
sigint_handler(int signal)
{
    g_print ("Closing down libuca\n");
    uca_camera_stop_recording (camera, NULL);
    g_object_unref (camera);
    exit (signal);
}

static void
print_usage (void)
{
    gchar **types;

    g_print ("Usage: benchmark (");
    types = uca_camera_get_types ();

    for (guint i = 0; types[i] != NULL; i++) {
        if (types[i+1] == NULL)
            g_print ("%s)", types[i]);
        else
            g_print ("%s | ", types[i]);
    }

    g_print ("\n");
    g_strfreev (types);
}

static void
log_handler (const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user)
{
    gsize       n_written;
    GIOChannel *channel;
    GTimeZone  *tz;
    GDateTime  *date_time;
    gchar      *new_message;
    GError     *error = NULL;

    channel = user;
    tz = g_time_zone_new_local ();
    date_time = g_date_time_new_now (tz);

    new_message = g_strdup_printf ("[%s] %s\n",
                                   g_date_time_format (date_time, "%FT%H:%M:%S%z"), message);

    g_time_zone_unref (tz);
    g_date_time_unref (date_time);

    g_io_channel_write_chars (channel, new_message, strlen (new_message), &n_written, &error);
    g_assert_no_error (error);

    g_io_channel_flush (channel, &error);
    g_assert_no_error (error);

    g_free (new_message);
}

static void
benchmark (UcaCamera *camera)
{
    const guint n_runs = 3;
    const guint n_frames = 100;

    guint   sensor_width;
    guint   sensor_height;
    guint   roi_width;
    guint   roi_height;
    guint   bits;
    guint   n_bytes;
    gdouble total_time = 0.0;
    gdouble exposure = 0.001;
    gpointer buffer;
    GTimer *timer;
    GError *error = NULL;

    g_object_set (G_OBJECT (camera),
                  "exposure-time", exposure,
                  NULL);

    g_object_get (G_OBJECT (camera),
                  "sensor-width", &sensor_width,
                  "sensor-height", &sensor_height,
                  "sensor-bitdepth", &bits,
                  "roi-width", &roi_width,
                  "roi-height", &roi_height,
                  "exposure-time", &exposure,
                  NULL);

    g_print ("# --- General information ---\n");
    g_print ("# Sensor size: %ix%i\n", sensor_width, sensor_height);
    g_print ("# ROI size: %ix%i\n", roi_width, roi_height);
    g_print ("# Exposure time: %fs\n", exposure);

    g_print ("# %-10s%-10s%-16s%-16s\n", "n_frames", "n_runs", "frames/s", "MiB/s");
    g_print ("  %-10i%-10i", n_frames, n_runs);

    g_message ("Start synchronous benchmark");

    n_bytes = bits > 8 ? 2 : 1;
    buffer = g_malloc0(roi_width * roi_height * n_bytes);
    timer = g_timer_new ();
    uca_camera_start_recording (camera, &error);

    if (error == NULL) {
        gdouble fps;
        gdouble bandwidth;

        for (guint run = 0; run < n_runs; run++) {
            g_message ("Start run %i of %i", run, n_runs);
            g_timer_start (timer);

            for (guint i = 0; i < n_frames; i++) {
                uca_camera_grab(camera, &buffer, &error);

                if (error != NULL) {
                    g_warning ("Error grabbing frame %02i/%i: `%s'", i, n_frames, error->message);
                    g_error_free (error);
                    error = NULL;
                }
            }

            g_timer_stop (timer);
            total_time += g_timer_elapsed (timer, NULL);
        }

        uca_camera_stop_recording (camera, &error);

        fps = n_runs * n_frames / total_time;
        bandwidth = roi_width * roi_height * n_bytes * fps / 1024 / 1024;
        g_print ("%-16.2f%-16.2f\n", fps, bandwidth);
    }
    else
        g_print ("%s\n", error->message);

    g_timer_destroy (timer);
    g_free (buffer);
}

int
main (int argc, char *argv[])
{
    GIOChannel  *log_channel;
    GError      *error = NULL;

    (void) signal (SIGINT, sigint_handler);

    if (argc < 2) {
        print_usage();
        return 1;
    }

    log_channel = g_io_channel_new_file ("error.log", "a+", &error);
    g_assert_no_error (error);
    g_log_set_handler (NULL, G_LOG_LEVEL_MASK, log_handler, log_channel);

    g_type_init();
    camera = uca_camera_new(argv[1], &error);

    if (camera == NULL) {
        g_error ("Initialization: %s", error->message);
        return 1;
    }

    benchmark (camera);

    g_object_unref (camera);
    g_io_channel_shutdown (log_channel, TRUE, &error);
    g_assert_no_error (error);

    return 0;
}
