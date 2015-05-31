# picamgpu_minimal
Minimal example of capturing YUV image data from the Pi camera into OpenGL textures using MMAL.

The code is not stand-alone. Instead there is a header file and a source file. They allow starting the camera, and grabbing frames from it to three separate OpenGL textures (Y,U,V). It can be easily copied into other projects.
