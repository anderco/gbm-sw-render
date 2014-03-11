/*
 * Copyright © 2011 Kristian Høgsberg
 * Copyright © 2011 Benjamin Franzke
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>

#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES

#include <gbm.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <drm.h>
#include <xf86drmMode.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

struct kms {
   drmModeConnector *connector;
   drmModeEncoder *encoder;
   drmModeModeInfo mode;
   uint32_t fb_id;
};

static EGLBoolean
setup_kms(int fd, struct kms *kms)
{
   drmModeRes *resources;
   drmModeConnector *connector;
   drmModeEncoder *encoder;
   int i;

   resources = drmModeGetResources(fd);
   if (!resources) {
      fprintf(stderr, "drmModeGetResources failed\n");
      return EGL_FALSE;
   }

   for (i = 0; i < resources->count_connectors; i++) {
      connector = drmModeGetConnector(fd, resources->connectors[i]);
      if (connector == NULL)
	 continue;

      if (connector->connection == DRM_MODE_CONNECTED &&
	  connector->count_modes > 0)
	 break;

      drmModeFreeConnector(connector);
   }

   if (i == resources->count_connectors) {
      fprintf(stderr, "No currently active connector found.\n");
      return EGL_FALSE;
   }

   for (i = 0; i < resources->count_encoders; i++) {
      encoder = drmModeGetEncoder(fd, resources->encoders[i]);

      if (encoder == NULL)
	 continue;

      if (encoder->encoder_id == connector->encoder_id)
	 break;

      drmModeFreeEncoder(encoder);
   }

   kms->connector = connector;
   kms->encoder = encoder;
   kms->mode = connector->modes[0];

   return EGL_TRUE;
}

static void
render_stuff(int width, int height)
{
   GLfloat view_rotx = 0.0, view_roty = 0.0, view_rotz = 0.0;
   static const GLfloat verts[4][2] = {
      { -1, -1 },
      {  1, -1 },
      { -1,  1 },
      {  1,  1 }
   };
   static const GLfloat texcoord[4][2] = {
      { 0, 0 },
      { 1, 0 },
      { 0, 1 },
      { 1, 1 },
   };
   GLfloat ar = (GLfloat) width / (GLfloat) height;

   glViewport(0, 0, (GLint) width, (GLint) height);

   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glFrustum(-ar, ar, -1, 1, 5.0, 60.0);

   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();
   glTranslatef(0.0, 0.0, -5.1);

   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
   glClearColor(0.4, 0.4, 0.4, 0.0);

   glPushMatrix();
   glRotatef(view_rotx, 1, 0, 0);
   glRotatef(view_roty, 0, 1, 0);
   glRotatef(view_rotz, 0, 0, 1);

   glVertexPointer(2, GL_FLOAT, 0, verts);
   glTexCoordPointer(2, GL_FLOAT, 0, texcoord);
   glEnableClientState(GL_VERTEX_ARRAY);
   glEnableClientState(GL_TEXTURE_COORD_ARRAY);

   glEnable(GL_TEXTURE_2D);

   glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

   glDisableClientState(GL_VERTEX_ARRAY);
   glDisableClientState(GL_TEXTURE_COORD_ARRAY);

   glPopMatrix();

   glFinish();
}

static const char device_name[] = "/dev/dri/card0";

static const EGLint attribs[] = {
   EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
   EGL_RED_SIZE, 1,
   EGL_GREEN_SIZE, 1,
   EGL_BLUE_SIZE, 1,
   EGL_ALPHA_SIZE, 0,
   EGL_DEPTH_SIZE, 1,
   EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
   EGL_NONE
};

int main(int argc, char *argv[])
{
   EGLDisplay dpy;
   EGLContext ctx;
   EGLSurface surface;
   EGLConfig config;
   EGLint major, minor, n;
   const char *ver;
   uint32_t handle, stride;
   struct kms kms;
   int ret, fd;
   struct gbm_device *gbm;
   struct gbm_bo *bo;
   drmModeCrtcPtr saved_crtc;
   struct gbm_surface *gs;

   fd = open(device_name, O_RDWR);
   if (fd < 0) {
      /* Probably permissions error */
      fprintf(stderr, "couldn't open %s, skipping\n", device_name);
      return -1;
   }

   gbm = gbm_create_device(fd);
   if (gbm == NULL) {
      fprintf(stderr, "couldn't create gbm device\n");
      ret = -1;
      goto close_fd;
   }

   dpy = eglGetDisplay(gbm);
   if (dpy == EGL_NO_DISPLAY) {
      fprintf(stderr, "eglGetDisplay() failed\n");
      ret = -1;
      goto destroy_gbm_device;
   }
	
   if (!eglInitialize(dpy, &major, &minor)) {
      printf("eglInitialize() failed\n");
      ret = -1;
      goto egl_terminate;
   }

   ver = eglQueryString(dpy, EGL_VERSION);
   printf("EGL_VERSION = %s\n", ver);

   if (!setup_kms(fd, &kms)) {
      ret = -1;
      goto egl_terminate;
   }

   eglBindAPI(EGL_OPENGL_API);

   if (!eglChooseConfig(dpy, attribs, &config, 1, &n) || n != 1) {
      fprintf(stderr, "failed to choose argb config\n");
      goto egl_terminate;
   }
   
   ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, NULL);
   if (ctx == NULL) {
      fprintf(stderr, "failed to create context\n");
      ret = -1;
      goto egl_terminate;
   }

   gs = gbm_surface_create(gbm, kms.mode.hdisplay, kms.mode.vdisplay,
			   GBM_BO_FORMAT_XRGB8888,
			   GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
   surface = eglCreateWindowSurface(dpy, config, gs, NULL);

   if (!eglMakeCurrent(dpy, surface, surface, ctx)) {
      fprintf(stderr, "failed to make context current\n");
      ret = -1;
      goto destroy_context;
   }

   /* create texture */
   struct gbm_bo *tex_bo;
   uint32_t *tex_map;

   tex_bo = gbm_bo_create(gbm, 32, 32, GBM_BO_FORMAT_ARGB8888,
			  GBM_BO_USE_CPU_WRITE);
   if (!tex_bo)
      return -1;

   tex_map = gbm_bo_map(tex_bo);

   int x, y;
   for (y = 0; y < 32; y++) {
      for (x = 0; x < 32; x++)
	  tex_map[x] = ((int) (0xff * (x / 32.0))) << 16 |
	               ((int) (0xff * (y / 32.0))) <<  8 |
                       0xff000000;

      tex_map += gbm_bo_get_stride(tex_bo) / sizeof(tex_map[0]);
   }

   gbm_bo_unmap(tex_bo);

   EGLImageKHR egl_img;
   egl_img = eglCreateImageKHR(dpy, EGL_NO_CONTEXT,
			       EGL_NATIVE_PIXMAP_KHR, tex_bo, NULL);
   if (!egl_img) {
      fprintf(stderr, "Failed to create image from gbm bo\n");
      return -1;
   }


   GLuint tex;
   glGenTextures(1, &tex);
   glBindTexture(GL_TEXTURE_2D, tex);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

   glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_img);

   render_stuff(kms.mode.hdisplay, kms.mode.vdisplay);

   eglSwapBuffers(dpy, surface);

   bo = gbm_surface_lock_front_buffer(gs);
   handle = gbm_bo_get_handle(bo).u32;
   stride = gbm_bo_get_stride(bo);

   printf("handle=%d, stride=%d\n", handle, stride);

   ret = drmModeAddFB(fd,
		      kms.mode.hdisplay, kms.mode.vdisplay,
		      24, 32, stride, handle, &kms.fb_id);
   if (ret) {
      fprintf(stderr, "failed to create fb\n");
      goto rm_fb;
   }

   saved_crtc = drmModeGetCrtc(fd, kms.encoder->crtc_id);
   if (saved_crtc == NULL)
      goto rm_fb;

   ret = drmModeSetCrtc(fd, kms.encoder->crtc_id, kms.fb_id, 0, 0,
			&kms.connector->connector_id, 1, &kms.mode);
   if (ret) {
      fprintf(stderr, "failed to set mode: %m\n");
      goto free_saved_crtc;
   }

   getchar();

   ret = drmModeSetCrtc(fd, saved_crtc->crtc_id, saved_crtc->buffer_id,
                        saved_crtc->x, saved_crtc->y,
                        &kms.connector->connector_id, 1, &saved_crtc->mode);
   if (ret) {
      fprintf(stderr, "failed to restore crtc: %m\n");
   }

free_saved_crtc:
   drmModeFreeCrtc(saved_crtc);
rm_fb:
   drmModeRmFB(fd, kms.fb_id);
   eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
destroy_context:
   eglDestroyContext(dpy, ctx);
egl_terminate:
   eglTerminate(dpy);
destroy_gbm_device:
   gbm_device_destroy(gbm);
close_fd:
   close(fd);

   return ret;
}
