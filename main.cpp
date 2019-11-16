#include <gst/gst.h>
#include <png.h>
#include <string>

bool writePngFile(const char *filename, int width, int height, png_bytep rawData)
{
    FILE *fp = fopen(filename, "wb");
    if(!fp) return false;

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) return false;

    png_infop info = png_create_info_struct(png);
    if (!info) return false;

    if (setjmp(png_jmpbuf(png))) return false;

    png_init_io(png, fp);

    // Output is 8bit depth, RGB format.
    png_set_IHDR(png,
                 info,
                 width, height,
                 8,
                 PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    // To remove the alpha channel for PNG_COLOR_TYPE_RGB format,
    // Use png_set_filler().
    //png_set_filler(png, 0, PNG_FILLER_AFTER);

    if (!rawData) {
        png_destroy_write_struct(&png, &info);
        return false;
    }

    for(int h = 0; h < height; ++h) {
        png_write_row(png, &rawData[width*h*3]);
    }
    png_write_end(png, nullptr);

    fclose(fp);

    png_destroy_write_struct(&png, &info);
    return true;
}

static gboolean printStructureField(GQuark field, const GValue *value, gpointer prefix)
{
    gchar *str = gst_value_serialize(value);
    g_print ("%s  %15s: %s\n", (gchar *) prefix, g_quark_to_string(field), str);
    g_free (str);
    return TRUE;
}

static gboolean printBuffer(GstBuffer *buffer, gpointer userData)
{
    gsize bufSize = gst_buffer_get_size(buffer);
    g_print ("%s  %15s: %d\n", (gchar *) userData, "size", bufSize);
    return TRUE;
}

static gboolean printBufferList(GstBuffer **buffer, guint idx, gpointer userData)
{
    return printBuffer(buffer[idx], userData);
}


static GstFlowReturn onNewVideoSample(GstElement *sink, void *ctx) {
    GstSample *sample;
    const gchar *prefix = "    ";

    static int frameCtr = 0;

    // Retrieve the buffer
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if(++frameCtr%25 != 0) {
        //skip most of the samples, take one per second from most of cameras (generally 25fps)
        if (sample) {
            gst_sample_unref(sample);
            return GST_FLOW_OK;
        }
    }

    if (sample) {
        //g_print("=================\n");
        gint width = 0;
        gint height = 0;

        GstCaps *sampleCaps = gst_sample_get_caps(sample);
        if (sampleCaps) {
            if (gst_caps_is_any(sampleCaps)) {
                g_print("%sANY\n", prefix);
            }
            if (gst_caps_is_empty(sampleCaps)) {
                g_print("%sEMPTY\n", prefix);
            }

            for (unsigned int i = 0; i < gst_caps_get_size(sampleCaps); i++) {
                GstStructure *structure = gst_caps_get_structure(sampleCaps, i);

                //g_print ("%s%s\n", prefix, gst_structure_get_name(structure));
                //gst_structure_foreach(structure, printStructureField, (gpointer) prefix);

                //if (gst_structure_has_field(structure, "width")) {
                //    GType type = gst_structure_get_field_type(structure, "width");
                //    g_print("Width type %s\n" , g_type_name(type));
                //}

                gst_structure_get_int(structure, "width", &width);
                gst_structure_get_int(structure, "height", &height);
            }
        }

        const GstStructure *sampleInfo = gst_sample_get_info(sample);
        if (sampleInfo) {
            g_print ("%s%s\n", prefix, gst_structure_get_name(sampleInfo));
            gst_structure_foreach(sampleInfo, printStructureField, (gpointer) prefix);
        }

        GstBufferList *sampleBuffers = gst_sample_get_buffer_list(sample);
        if (sampleBuffers) {
            g_print ("%sBuffers %d\n", prefix, gst_buffer_list_length(sampleBuffers));
            gst_buffer_list_foreach(sampleBuffers, printBufferList, (gpointer) prefix);
        }

        GstBuffer *sampleBuffer = gst_sample_get_buffer(sample);
        if (sampleBuffer) {
            //g_print ("%sOnly Buffer\n", prefix);
            //printBuffer(sampleBuffer,(gpointer) prefix);

            g_print ("%sDim %dx%d\n", prefix, width, height);
            //g_print ("%sCalc size %d\n", prefix, width*height*3);//BGR

            if(width * height > 0) {
                GstMapInfo map;
                if (gst_buffer_map(sampleBuffer, &map, GST_MAP_READ)) {
                    png_bytep raw = (png_bytep)map.data;

                    static int a = 0;
                    static const std::string path("/tmp/");
                    std::string fileName = std::to_string(a++%20);
                    std::string pathFileName = path + fileName + ".png";
                    if (!writePngFile(pathFileName.c_str(), width, height, raw)) {
                        g_print("Can't write png file!!!\n");
                    }


                    gst_buffer_unmap(sampleBuffer, &map);
                }
            }
        }

        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    return GST_FLOW_ERROR;
}

//#define USE_PLAYBIN

int main(int argc, char *argv[])
{
    // Initialize GStreamer
    gst_init (&argc, &argv);

#ifdef USE_PLAYBIN
    // Build the pipeline
    GstElement *pipeline = gst_parse_launch("playbin uri=rtsp://userName:password@ipForYouRtspCamera:554/videoMain", NULL);

    // Build own elements
    GstElement *convert = gst_element_factory_make("videoconvert", "convert");
    if (!convert) {
        g_printerr("Not all elements could be created.\n");
        return -1;
    }

    #define REDIRECT_TO_OTHER_SICK false
    #if(REDIRECT_TO_OTHER_SICK)
    GstElement *sink = gst_element_factory_make("autovideosink", "video_sink");
    if (!sink) {
        g_printerr("Not all elements could be created.\n");
        return -1;
    }
    #else
    GstElement *appSink = gst_element_factory_make ("appsink", "app_sink");
    if (!appSink) {
        g_printerr("Not all elements could be created.\n");
        return -1;
    }
    #endif

    // Connect to pipeline through bin element
    GstElement *bin = gst_bin_new("video_sink_bin");
    #if(REDIRECT_TO_OTHER_SICK)
    gst_bin_add_many(GST_BIN(bin), convert, sink, NULL);
    gst_element_link_many(convert, sink, NULL);
    #else
    gst_bin_add_many(GST_BIN(bin), convert, appSink, NULL);
    gst_element_link_many(convert, appSink, NULL);
    #endif
    GstPad *pad = gst_element_get_static_pad(convert, "sink");
    GstPad *ghost_pad = gst_ghost_pad_new("sink", pad);
    gst_pad_set_active(ghost_pad, TRUE);
    gst_element_add_pad(bin, ghost_pad);
    gst_object_unref(pad);

    // Set playbin's audio sink to be our sink bin
    g_object_set(GST_OBJECT(pipeline), "video-sink", bin, NULL);

#else
    //GstElement *pipeline = gst_parse_launch("uridecodebin uri=rtsp://userName:password@ipForYouRtspCamera:554/videoMain ! videoconvert ! appsink name=mysink", NULL);
    GstElement *pipeline = gst_parse_launch("v4l2src ! videoconvert ! appsink name=mysink", NULL);

    // Get own elements
    GstElement *appSink = gst_bin_get_by_name(GST_BIN(pipeline), "mysink");
    if (!pipeline || !appSink) {
        g_printerr("Not all elements could be created.\n");
        return -1;
    }
#endif

    // Configure appsink
    GstCaps *appVideoCaps = gst_caps_new_simple("video/x-raw",
                                                "format", G_TYPE_STRING, "RGB",//"BGR",
                                                nullptr);
    g_object_set(appSink, "emit-signals", TRUE, "caps", appVideoCaps, nullptr);
    void *newSampleCtx = nullptr;
    g_signal_connect(appSink, "new-sample", G_CALLBACK(onNewVideoSample), newSampleCtx);
    gst_caps_unref(appVideoCaps);

    // Start playing
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    // Wait until error or EOS
    GstBus *bus = gst_element_get_bus (pipeline);
    GstMessage *msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    // Free resources
    if (msg != nullptr)
        gst_message_unref(msg);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    return 0;
}
