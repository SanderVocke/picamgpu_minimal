#include "cam.h"

#include "bcm_host.h"
#include "interface/vcos/vcos.h"


#include "interface/mmal/mmal.h"
#include "interface/mmal/mmal_logging.h"
#include "interface/mmal/mmal_buffer.h"
#include "interface/mmal/util/mmal_util.h"
#include "interface/mmal/util/mmal_util_params.h"
#include "interface/mmal/util/mmal_default_components.h"
#include "interface/mmal/util/mmal_connection.h"

#include "GLES2/gl2.h"
#include "EGL/egl.h"
#include "EGL/eglext.h"
#include "lodepng.h"
#include <SDL/SDL.h>

#include <bcm_host.h>
#include <GLES2/gl2.h>
#include <GLES/gl.h>
#include <GLES/glext.h>
#include "interface/vcos/vcos.h"
#include "EGL/eglext_brcm.h"

// Standard port setting for the camera component
#define MMAL_CAMERA_PREVIEW_PORT 0
#define MMAL_CAMERA_VIDEO_PORT 1
#define MMAL_CAMERA_CAPTURE_PORT 2


// Stills format information
// 0 implies variable
#define STILLS_FRAME_RATE_NUM 0
#define STILLS_FRAME_RATE_DEN 1

/// Video render needs at least 2 buffers.
#define VIDEO_OUTPUT_BUFFERS_NUM 3

#define MAX_USER_EXIF_TAGS      32
#define MAX_EXIF_PAYLOAD_LENGTH 128

/// Frame advance method
#define FRAME_NEXT_SINGLE        0
#define FRAME_NEXT_TIMELAPSE     1
#define FRAME_NEXT_KEYPRESS      2
#define FRAME_NEXT_FOREVER       3
#define FRAME_NEXT_GPIO          4
#define FRAME_NEXT_SIGNAL        5
#define FRAME_NEXT_IMMEDIATELY   6

#define check() assert(glGetError() == 0)

MMAL_PORT_T *preview_port = NULL;
MMAL_PORT_T *video_port = NULL;
MMAL_PORT_T *still_port = NULL;
MMAL_QUEUE_T *video_queue = NULL;
MMAL_POOL_T *video_pool = NULL;

RASPICAM_CAMERA_PARAMETERS cameraParameters;

GLuint cam_ytex, cam_utex, cam_vtex;
EGLImageKHR yimg = EGL_NO_IMAGE_KHR;
EGLImageKHR uimg = EGL_NO_IMAGE_KHR;
EGLImageKHR vimg = EGL_NO_IMAGE_KHR;

void camera_control_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer);
void video_output_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer);

bool camera_read_frame(void){
	if(MMAL_BUFFER_HEADER_T* buf = mmal_queue_get(video_queue)){
	
		//mmal_buffer_header_mem_lock(buf);
		
		//printf("Buffer received with length %d\n", buf->length);
	
		glBindTexture(GL_TEXTURE_EXTERNAL_OES, cam_ytex);
		check();
		if(yimg != EGL_NO_IMAGE_KHR){
			eglDestroyImageKHR(GDisplay, yimg);
			yimg = EGL_NO_IMAGE_KHR;
		}
		yimg = eglCreateImageKHR(GDisplay, 
			EGL_NO_CONTEXT, 
			EGL_IMAGE_BRCM_MULTIMEDIA_Y, 
			(EGLClientBuffer) buf->data, 
			NULL);
		check();
		glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, yimg);
		check();
		
		glBindTexture(GL_TEXTURE_EXTERNAL_OES, cam_utex);
		check();
		if(uimg != EGL_NO_IMAGE_KHR){
			eglDestroyImageKHR(GDisplay, uimg);
			uimg = EGL_NO_IMAGE_KHR;
		}
		uimg = eglCreateImageKHR(GDisplay, 
			EGL_NO_CONTEXT, 
			EGL_IMAGE_BRCM_MULTIMEDIA_U, 
			(EGLClientBuffer) buf->data, 
			NULL);
		check();
		glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, uimg);
		check();
		
		glBindTexture(GL_TEXTURE_EXTERNAL_OES, cam_vtex);
		check();
		if(vimg != EGL_NO_IMAGE_KHR){
			eglDestroyImageKHR(GDisplay, vimg);
			vimg = EGL_NO_IMAGE_KHR;
		}
		vimg = eglCreateImageKHR(GDisplay, 
			EGL_NO_CONTEXT, 
			EGL_IMAGE_BRCM_MULTIMEDIA_V, 
			(EGLClientBuffer) buf->data, 
			NULL);
		check();
		glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, vimg);
		check();
		
		//mmal_buffer_header_mem_unlock(buf);
		mmal_buffer_header_release(buf);
		
		if(preview_port->is_enabled){
			MMAL_STATUS_T status;
			MMAL_BUFFER_HEADER_T *new_buffer;
			new_buffer = mmal_queue_get(video_pool->queue);
			if (new_buffer)
				status = mmal_port_send_buffer(preview_port, new_buffer);
			if (!new_buffer || status != MMAL_SUCCESS)
				printf("Unable to return a buffer to the video port\n\n");
		}
		
		return true;
	}
	
	return false; //no buffer received
}

void create_camera_component(int Width, int Height, int FrameRate){
	MMAL_COMPONENT_T *camera = 0;
	MMAL_ES_FORMAT_T *format;
	
	MMAL_STATUS_T status;
	
	/* Create the component */
	status = mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &camera);

	if (status != MMAL_SUCCESS)	{
		printf("Failed to create camera component : error %d\n", status);
		exit(1);
	}

	if (!camera->output_num)	{
		status = MMAL_ENOSYS;
		printf("Camera doesn't have output ports\n");
		exit(1);
	}

	preview_port = camera->output[MMAL_CAMERA_PREVIEW_PORT];
	video_port = camera->output[MMAL_CAMERA_VIDEO_PORT];
	still_port = camera->output[MMAL_CAMERA_CAPTURE_PORT];
	
	// Enable the camera, and tell it its control callback function
	status = mmal_port_enable(camera->control, camera_control_callback);
	if (status != MMAL_SUCCESS)	{
		printf("Unable to enable control port : error %d\n", status);
		mmal_component_destroy(camera);
		exit(1);
	}
	
	//set camera parameters.
	MMAL_PARAMETER_CAMERA_CONFIG_T cam_config;
	cam_config.hdr = MMAL_PARAMETER_CAMERA_CONFIG;
	cam_config.hdr.size = sizeof(cam_config);
	cam_config.max_stills_w = Width;
	cam_config.max_stills_h = Height;
	cam_config.stills_yuv422 = 0;
	cam_config.one_shot_stills = 0;
	cam_config.max_preview_video_w = Width;
	cam_config.max_preview_video_h = Height;
	cam_config.num_preview_video_frames = 3;
	cam_config.stills_capture_circular_buffer_height = 0;
	cam_config.fast_preview_resume = 0;
	cam_config.use_stc_timestamp = MMAL_PARAM_TIMESTAMP_MODE_RESET_STC;
	status = mmal_port_parameter_set(camera->control, &cam_config.hdr);

	if (status != MMAL_SUCCESS)	{
		printf("Unable to set camera parameters : error %d\n", status);
		mmal_component_destroy(camera);
		exit(1);
	}
	
	// setup preview port format - QUESTION: Needed if we aren't using preview?
	format = preview_port->format;
	format->encoding = MMAL_ENCODING_OPAQUE;
	format->encoding_variant = MMAL_ENCODING_I420;
	format->es->video.width = Width;
	format->es->video.height = Height;
	format->es->video.crop.x = 0;
	format->es->video.crop.y = 0;
	format->es->video.crop.width = Width;
	format->es->video.crop.height = Height;
	format->es->video.frame_rate.num = FrameRate;
	format->es->video.frame_rate.den = 1;
	status = mmal_port_format_commit(preview_port);
	if (status != MMAL_SUCCESS)
	{
		printf("Couldn't set preview port format : error %d\n", status);
		mmal_component_destroy(camera);
		exit(1);
	}

	//setup video port format
	format = video_port->format;
	format->encoding = MMAL_ENCODING_I420;
	format->encoding_variant = MMAL_ENCODING_I420;
	format->es->video.width = Width;
	format->es->video.height = Height;
	format->es->video.crop.x = 0;
	format->es->video.crop.y = 0;
	format->es->video.crop.width = Width;
	format->es->video.crop.height = Height;
	format->es->video.frame_rate.num = FrameRate;
	format->es->video.frame_rate.den = 1;
	status = mmal_port_format_commit(video_port);
	if (status != MMAL_SUCCESS)
	{
		printf("Couldn't set video port format : error %d\n", status);
		mmal_component_destroy(camera);
		exit(1);
	}
	
	//setup still port format
	format = still_port->format;
	format->encoding = MMAL_ENCODING_OPAQUE;
	format->encoding_variant = MMAL_ENCODING_I420;
	format->es->video.width = Width;
	format->es->video.height = Height;
	format->es->video.crop.x = 0;
	format->es->video.crop.y = 0;
	format->es->video.crop.width = Width;
	format->es->video.crop.height = Height;
	format->es->video.frame_rate.num = 1;
	format->es->video.frame_rate.den = 1;
	status = mmal_port_format_commit(still_port);
	if (status != MMAL_SUCCESS)
	{
		printf("Couldn't set still port format : error %d\n", status);
		mmal_component_destroy(camera);
		exit(1);
	}
	
	status = mmal_port_parameter_set_boolean(preview_port,
	MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
	if (status != MMAL_SUCCESS)
	{
		printf("Failed to enable zero copy on camera video port\n");
		exit(1);
	}
	
	status = mmal_port_format_commit(preview_port);
	if (status != MMAL_SUCCESS)
	{
		printf("camera format couldn't be set\n");
		exit(1);
	}
	
	/* For GL a pool of opaque buffer handles must be allocated in the client.
	* These buffers are used to create the EGL images.
	*/
	preview_port->buffer_num = 3;
	preview_port->buffer_size = preview_port->buffer_size_recommended;

	/* Pool + queue to hold preview frames */
	video_pool = mmal_port_pool_create(preview_port,preview_port->buffer_num,preview_port->buffer_size);
	if (!video_pool)
	{
		printf("Error allocating camera video pool. Buffer num: %d Buffer size: %d\n", preview_port->buffer_num, preview_port->buffer_size);
		status = MMAL_ENOMEM;
		exit(1);
	}
	printf("Allocated %d MMAL buffers of size %d.\n", preview_port->buffer_num, preview_port->buffer_size);

	/* Place filled buffers from the preview port in a queue to render */
	video_queue = mmal_queue_create();
	if (!video_queue)
	{
		printf("Error allocating video buffer queue\n");
		status = MMAL_ENOMEM;
		exit(1);
	}

	/* Enable video port callback */
	//port->userdata = (struct MMAL_PORT_USERDATA_T *)this;
	status = mmal_port_enable(preview_port, video_output_callback);
	if (status != MMAL_SUCCESS)
	{
		printf("Failed to enable video port\n");
		exit(1);
	}
	
	// Set up the camera_parameters to default
	raspicamcontrol_set_defaults(&cameraParameters);
	//apply all camera parameters
	raspicamcontrol_set_all_parameters(camera, &cameraParameters);

	//enable the camera
	status = mmal_component_enable(camera);
	if (status != MMAL_SUCCESS)
	{
		printf("Couldn't enable camera\n\n");
		mmal_component_destroy(camera);
		exit(1);
	}
	
	//send all the buffers in our pool to the video port ready for use
	{
		int num = mmal_queue_length(video_pool->queue);
		int q;
		for (q=0;q<num;q++)
		{
			MMAL_BUFFER_HEADER_T *buffer = mmal_queue_get(video_pool->queue);
			if (!buffer)
			{
				printf("Unable to get a required buffer %d from pool queue\n\n", q);
				exit(1);
			}
			else if (mmal_port_send_buffer(preview_port, buffer)!= MMAL_SUCCESS)
			{
				printf("Unable to send a buffer to port (%d)\n\n", q);
				exit(1);
			}
		}
	}
	
	/*
	//begin capture
	if (mmal_port_parameter_set_boolean(preview_port, MMAL_PARAMETER_CAPTURE, 1) != MMAL_SUCCESS)
	{
		printf("Failed to start capture\n\n");
		exit(1);
	}
	*/
	
	printf("Camera initialized.\n");
	
	
	
	//Setup the camera's textures and EGL images.
	glGenTextures(1, &cam_ytex);
	glGenTextures(1, &cam_utex);
	glGenTextures(1, &cam_vtex);
	
	return;
}

void camera_control_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer){
	printf("Camera control callback\n\n");
	return;
}

void video_output_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer){
	//to handle the user not reading frames, remove and return any pre-existing ones
	if(mmal_queue_length(video_queue)>=2)
	{
		if(MMAL_BUFFER_HEADER_T* existing_buffer = mmal_queue_get(video_queue))
		{
			mmal_buffer_header_release(existing_buffer);
			if (port->is_enabled)
			{
				MMAL_STATUS_T status;
				MMAL_BUFFER_HEADER_T *new_buffer;
				new_buffer = mmal_queue_get(video_pool->queue);
				if (new_buffer)
				status = mmal_port_send_buffer(port, new_buffer);
				if (!new_buffer || status != MMAL_SUCCESS)
				printf("Unable to return a buffer to the video port\n\n");
			}	
		}
	}

	//add the buffer to the output queue
	mmal_queue_put(video_queue,buffer);

	//printf("Video buffer callback, output queue len=%d\n\n", mmal_queue_length(OutputQueue));
}

void camera_release()
{
	if(video_queue)
		mmal_queue_destroy(video_queue);
	if(video_pool)
		mmal_port_pool_destroy(preview_port,video_pool);
}