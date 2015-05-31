#ifndef __CAM_H__
#define __CAM_H__

#include "interface/mmal/mmal.h"
#include "interface/mmal/mmal_logging.h"
#include "interface/mmal/mmal_buffer.h"
#include "interface/mmal/util/mmal_util.h"
#include "interface/mmal/util/mmal_util_params.h"
#include "interface/mmal/util/mmal_default_components.h"
#include "interface/mmal/util/mmal_connection.h"

#include "graphics.h"

extern GfxTexture cam_ytex, cam_utex, cam_vtex;

void create_camera_component(int Width, int Height, int Framerate);
void camera_release();
bool camera_read_frame(void);

#endif