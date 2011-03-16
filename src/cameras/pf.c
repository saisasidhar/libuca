
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <libpf/pfcam.h>
#include "uca.h"
#include "uca-cam.h"
#include "uca-grabber.h"

#define set_void(p, type, value) { *((type *) p) = value; }

struct uca_pf_map {
    enum uca_property_ids uca_prop;
    const char *pf_prop;
};

static struct uca_pf_map uca_to_pf[] = {
    { UCA_PROP_NAME,        "CameraName" },
    { UCA_PROP_WIDTH,       "Window.W" },
    { UCA_PROP_WIDTH_MIN,   "Window.W.Min" },
    { UCA_PROP_WIDTH_MAX,   "Window.W.Max" },
    { UCA_PROP_HEIGHT,      "Window.H" },
    { UCA_PROP_HEIGHT_MIN,  "Window.H.Min" },
    { UCA_PROP_HEIGHT_MAX,  "Window.H.Max" },
    { UCA_PROP_X_OFFSET,    "Window.X" },
    { UCA_PROP_X_OFFSET_MIN,"Window.X.Min" },
    { UCA_PROP_X_OFFSET_MAX,"Window.X.Max" },
    { UCA_PROP_Y_OFFSET,    "Window.Y" },
    { UCA_PROP_Y_OFFSET_MIN,"Window.Y.Min" },
    { UCA_PROP_Y_OFFSET_MAX,"Window.Y.Max" },
    { UCA_PROP_EXPOSURE,    "ExposureTime" },
    { UCA_PROP_EXPOSURE_MIN,"ExposureTime.Min" },
    { UCA_PROP_EXPOSURE_MAX,"ExposureTime.Max" },
    { UCA_PROP_DELAY,       "Trigger.Delay" },
    { UCA_PROP_DELAY_MIN,   "Trigger.Delay.Min" },
    { UCA_PROP_DELAY_MAX,   "Trigger.Delay.Max" },
    { UCA_PROP_FRAMERATE,   "FrameRate" },
    { UCA_PROP_TRIGGER_MODE,"Trigger.Source" },
    { -1, NULL }
};

static uint32_t uca_pf_set_property(struct uca_camera_t *cam, enum uca_property_ids property, void *data)
{
    struct uca_grabber_t *grabber = cam->grabber;
    TOKEN t = INVALID_TOKEN;
    int i = 0;

    /* Find a valid pf token for the property */
    while (uca_to_pf[i].uca_prop != -1) {
        if (uca_to_pf[i].uca_prop == property) {
            t = pfProperty_ParseName(0, uca_to_pf[i].pf_prop);
            break;
        }
        i++;
    }
    if (t == INVALID_TOKEN)
        return UCA_ERR_PROP_INVALID;

    PFValue value;

    switch (property) {
        case UCA_PROP_WIDTH:
            if (grabber->set_property(grabber, UCA_GRABBER_WIDTH, (uint32_t *) data) != UCA_NO_ERROR)
                return UCA_ERR_PROP_VALUE_OUT_OF_RANGE;

            value.value.i = *((uint32_t *) data);
            if (pfDevice_SetProperty(0, t, &value) < 0)
                return UCA_ERR_PROP_VALUE_OUT_OF_RANGE;

            cam->frame_width = value.value.i;
            break;

        case UCA_PROP_HEIGHT:
            if (grabber->set_property(grabber, UCA_GRABBER_HEIGHT, (uint32_t *) data) != UCA_NO_ERROR)
                return UCA_ERR_PROP_VALUE_OUT_OF_RANGE;

            value.value.i = *((uint32_t *) data);
            if (pfDevice_SetProperty(0, t, &value) < 0)
                return UCA_ERR_PROP_VALUE_OUT_OF_RANGE;

            cam->frame_height = value.value.i;
            break;

        case UCA_PROP_X_OFFSET:
            if (grabber->set_property(grabber, UCA_GRABBER_OFFSET_X, (uint32_t *) data) != UCA_NO_ERROR)
                return UCA_ERR_PROP_VALUE_OUT_OF_RANGE;
            break;

        case UCA_PROP_Y_OFFSET:
            if (grabber->set_property(grabber, UCA_GRABBER_OFFSET_Y, (uint32_t *) data) != UCA_NO_ERROR)
                return UCA_ERR_PROP_VALUE_OUT_OF_RANGE;
            break;

        case UCA_PROP_EXPOSURE:
            /* I haven't found a specification but it looks like PF uses milli
             * seconds. We also by-pass the frame grabber... */
            value.type = PF_FLOAT;
            value.value.f = (float) *((uint32_t *) data) / 1000.0;
            if (pfDevice_SetProperty(0, t, &value) < 0)
                return UCA_ERR_PROP_VALUE_OUT_OF_RANGE;
            break;

        default:
            return UCA_ERR_PROP_INVALID;
    }
    return UCA_NO_ERROR;
}


static uint32_t uca_pf_get_property(struct uca_camera_t *cam, enum uca_property_ids property, void *data, size_t num)
{
    TOKEN t;    /* You gotta love developers who name types capitalized... */
    PFValue value;

    int i = 0;
    while (uca_to_pf[i].uca_prop != -1) {
        if (uca_to_pf[i].uca_prop == property) {
            t = pfProperty_ParseName(0, uca_to_pf[i].pf_prop);
            if (t == INVALID_TOKEN || (pfDevice_GetProperty(0, t, &value) < 0))
                return UCA_ERR_PROP_INVALID;

            switch (value.type) {
                case PF_INT:
                    set_void(data, uint32_t, value.value.i);
                    break;

                case PF_FLOAT:
                    set_void(data, uint32_t, (uint32_t) floor((value.value.f * 1000.0)+0.5));
                    break;

                case PF_STRING:
                    if (property == UCA_PROP_FRAMERATE) {
                        set_void(data, uint32_t, (uint32_t) floor(atof(value.value.p)+0.5));
                    }
                    else {
                        strncpy((char *) data, value.value.p, num);
                    }
                    break;

                case PF_MODE:
                    set_void(data, uint32_t, (uint32_t) value.value.i);
                    break;

                default:
                    break;
            }
            return UCA_NO_ERROR;
        }
        i++;
    }

    /* Handle all special cases */
    switch (property) {
        case UCA_PROP_BITDEPTH:
            set_void(data, uint32_t, 8);
            break;

        default:
            return UCA_ERR_PROP_INVALID;
    }
    return UCA_NO_ERROR;
}

uint32_t uca_pf_start_recording(struct uca_camera_t *cam)
{
    return cam->grabber->acquire(cam->grabber, -1);
}

uint32_t uca_pf_stop_recording(struct uca_camera_t *cam)
{
    return UCA_NO_ERROR;
}

uint32_t uca_pf_grab(struct uca_camera_t *cam, char *buffer)
{
    uint16_t *frame;
    uint32_t err = cam->grabber->grab(cam->grabber, (void **) &frame);
    if (err != UCA_NO_ERROR)
        return err;
    /* FIXME: choose according to data format */
    memcpy(buffer, frame, cam->frame_width*cam->frame_height);
    return UCA_NO_ERROR;
}

static uint32_t uca_pf_destroy(struct uca_camera_t *cam)
{
    pfDeviceClose(0);
    return UCA_NO_ERROR;
}

uint32_t uca_pf_init(struct uca_camera_t **cam, struct uca_grabber_t *grabber)
{
    int num_ports;
    if ((grabber == NULL) || (pfPortInit(&num_ports) < 0) || (pfDeviceOpen(0) < 0))
        return UCA_ERR_CAM_NOT_FOUND;

    /* We could check if a higher baud rate is supported, but... forget about
     * it. We don't need high speed configuration. */

    struct uca_camera_t *uca = (struct uca_camera_t *) malloc(sizeof(struct uca_camera_t));
    uca->grabber = grabber;
    uca->grabber->asynchronous = true;

    /* Camera found, set function pointers... */
    uca->destroy = &uca_pf_destroy;
    uca->set_property = &uca_pf_set_property;
    uca->get_property = &uca_pf_get_property;
    uca->start_recording = &uca_pf_start_recording;
    uca->stop_recording = &uca_pf_stop_recording;
    uca->grab = &uca_pf_grab;

    /* Prepare frame grabber for recording */
    int val = UCA_CL_8BIT_FULL_8;
    grabber->set_property(grabber, UCA_GRABBER_CAMERALINK_TYPE, &val);

    val = UCA_FORMAT_GRAY8;
    grabber->set_property(grabber, UCA_GRABBER_FORMAT, &val);

    val = UCA_TRIGGER_FREERUN;
    grabber->set_property(grabber, UCA_GRABBER_TRIGGER_MODE, &val);

    uca_pf_get_property(uca, UCA_PROP_WIDTH, &uca->frame_width, 0);
    uca_pf_get_property(uca, UCA_PROP_HEIGHT, &uca->frame_height, 0);

    grabber->set_property(grabber, UCA_GRABBER_WIDTH, &uca->frame_width);
    grabber->set_property(grabber, UCA_GRABBER_HEIGHT, &uca->frame_height);

    uca->state = UCA_CAM_CONFIGURABLE;
    *cam = uca;

    return UCA_NO_ERROR;
}
