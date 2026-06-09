/*
 *  Copyright (C) 2002-2026 RicardoRamosWorks.com and The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
// Versão corrigida e estável do sdlmain.cpp para Windows XP, Pentium M 1GHz, 256MB RAM, GeForce 7300GT

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#ifdef WIN32
#include <signal.h>
#include <process.h>
#endif

#include "cross.h"
#include "SDL.h"

#include "dosbox.h"
#include "video.h"
#include "mouse.h"
#include "pic.h"
#include "timer.h"
#include "setup.h"
#include "support.h"
#include "debug.h"
#include "mapper.h"
#include "vga.h"
#include "keyboard.h"
#include "cpu.h"
#include "cross.h"
#include "control.h"
#include "render.h"
#include "glidedef.h"

#define MAPPERFILE "mapper" ".map"
//#define DISABLE_JOYSTICK

// Otimizações específicas para hardware antigo
#define MAX_TEX_SIZE 1024
#define MAX_RECTS 256
#define EVENT_BATCH_SIZE 5
#define MOUSE_SKIP_FRAMES 2

#if C_OPENGL
#include "SDL_opengl.h"

#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif

#ifndef GL_ARB_pixel_buffer_object
#define GL_ARB_pixel_buffer_object 1
#define GL_PIXEL_PACK_BUFFER_ARB           0x88EB
#define GL_PIXEL_UNPACK_BUFFER_ARB         0x88EC
#define GL_PIXEL_PACK_BUFFER_BINDING_ARB   0x88ED
#define GL_PIXEL_UNPACK_BUFFER_BINDING_ARB 0x88EF
#endif

#ifndef GL_ARB_vertex_buffer_object
#define GL_ARB_vertex_buffer_object 1
typedef void (APIENTRYP PFNGLGENBUFFERSARBPROC) (GLsizei n, GLuint *buffers);
typedef void (APIENTRYP PFNGLBINDBUFFERARBPROC) (GLenum target, GLuint buffer);
typedef void (APIENTRYP PFNGLDELETEBUFFERSARBPROC) (GLsizei n, const GLuint *buffers);
typedef void (APIENTRYP PFNGLBUFFERDATAARBPROC) (GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage);
typedef GLvoid* (APIENTRYP PFNGLMAPBUFFERARBPROC) (GLenum target, GLenum access);
typedef GLboolean (APIENTRYP PFNGLUNMAPBUFFERARBPROC) (GLenum target);
#endif

// Funções PBO estáticas para evitar chamadas de função virtual
static PFNGLGENBUFFERSARBPROC glGenBuffersARB = NULL;
static PFNGLBINDBUFFERARBPROC glBindBufferARB = NULL;
static PFNGLDELETEBUFFERSARBPROC glDeleteBuffersARB = NULL;
static PFNGLBUFFERDATAARBPROC glBufferDataARB = NULL;
static PFNGLMAPBUFFERARBPROC glMapBufferARB = NULL;
static PFNGLUNMAPBUFFERARBPROC glUnmapBufferARB = NULL;

#endif //C_OPENGL

#if !(ENVIRON_INCLUDED)
extern char** environ;
#endif

#ifdef WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winuser.h>
#if C_DDRAW
#include <ddraw.h>
struct private_hwdata {
	LPDIRECTDRAWSURFACE3 dd_surface;
	LPDIRECTDRAWSURFACE3 dd_writebuf;
};
#endif

#define DEFAULT_CONFIG_FILE "/Winbox.conf"
#elif defined(MACOSX)
#define DEFAULT_CONFIG_FILE "/Library/Preferences/Winbox Preferences"
#else
#define DEFAULT_CONFIG_FILE "/.Winboxrc"
#endif

#if C_SET_PRIORITY
#include <sys/resource.h>
#define PRIO_TOTAL (PRIO_MAX-PRIO_MIN)
#endif

#ifdef OS2
#define INCL_DOS
#define INCL_WIN
#include <os2.h>
#endif
extern void MAPPER_CheckEvent(SDL_Event * event);

enum SCREEN_TYPES	{
	SCREEN_SURFACE,
	SCREEN_SURFACE_DDRAW,
	SCREEN_OVERLAY,
	SCREEN_OPENGL
};

enum PRIORITY_LEVELS {
	PRIORITY_LEVEL_PAUSE,
	PRIORITY_LEVEL_LOWEST,
	PRIORITY_LEVEL_LOWER,
	PRIORITY_LEVEL_NORMAL,
	PRIORITY_LEVEL_HIGHER,
	PRIORITY_LEVEL_HIGHEST
};

// Estrutura SDL otimizada com membros frequentemente acessados primeiro
struct SDL_Block {
	bool inited;
	bool active;
	bool updating;
	struct {
		Bit32u width;
		Bit32u height;
		Bit32u bpp;
		Bitu flags;
		double scalex,scaley;
		GFX_CallBack_t callback;
	} draw;
	bool wait_on_error;
	struct {
		struct {
			Bit16u width, height;
			bool fixed;
		} full;
		struct {
			Bit16u width, height;
		} window;
		struct {
			Bit16u width, height;
		} fullwrap;
		Bit8u bpp;
		bool fullscreen;
		bool lazy_fullscreen;
		bool lazy_fullscreen_req;
		bool doublebuf;
		SCREEN_TYPES type;
		SCREEN_TYPES want_type;
	} desktop;
#if C_OPENGL
	struct {
		Bitu pitch;
		void * framebuf;
		GLuint buffer;
		GLuint texture;
		GLuint displaylist;
		GLint max_texsize;
		bool bilinear;
		bool packed_pixel;
		bool paletted_texture;
		bool pixel_buffer_object;
		GLfloat tex_width, tex_height; // Cache de coordenadas de textura
	} opengl;
#endif
	struct {
		SDL_Surface * surface;
#if C_DDRAW
		RECT rect_dest;
		RECT rect_src;
#endif
	} blit;
	struct {
		PRIORITY_LEVELS focus;
		PRIORITY_LEVELS nofocus;
	} priority;
	SDL_Rect clip;
	SDL_Surface * surface;
	SDL_Overlay * overlay;
	SDL_cond *cond;
	struct {
		bool autolock;
		bool autoenable;
		bool requestlock;
		bool locked;
		int xsensitivity;
		int ysensitivity;
	} mouse;
	// Pool de retângulos de atualização
	SDL_Rect updateRects[MAX_RECTS];
	int updateRectCount;
	Bitu num_joysticks;
#if defined (WIN32)
	bool using_windib;
	Bit32u focus_ticks;
#endif
	Bit8u laltstate;
	Bit8u raltstate;
};

static SDL_Block sdl;

// Cache de renderização para evitar recálculos
static struct RenderCache {
	int width, height;
	float tex_width, tex_height;
	int pitch;
	bool initialized;
} render_cache = {0, 0, 0.0f, 0.0f, 0, false};
// Alocação estática para textura vazia
static Bit8u emptytex[MAX_TEX_SIZE * MAX_TEX_SIZE * 4];

#if C_OPENGL
static char const shader_src_default[] =
    "varying vec2 v_texCoord;\n"
    "#if defined(VERTEX)\n"
    "uniform vec2 rubyTextureSize;\n"
    "uniform vec2 rubyInputSize;\n"
    "attribute vec4 a_position;\n"
    "void main() {\n"
    "  gl_Position = a_position;\n"
    "  v_texCoord = vec2(a_position.x+1.0,1.0-a_position.y)/2.0*rubyInputSize/rubyTextureSize;\n"
    "}\n"
    "#elif defined(FRAGMENT)\n"
    "uniform sampler2D rubyTexture;\n\n"
    "void main() {\n"
    "  gl_FragColor = texture2D(rubyTexture, v_texCoord);\n"
    "}\n"
    "#endif\n";

static void OPENGL_ERROR(const char*) {
	return;
}
#endif

#define SETMODE_SAVES 1
#define SETMODE_SAVES_CLEAR 1

// Função otimizada para definir modo de vídeo
SDL_Surface* SDL_SetVideoMode_Wrap(int width,int height,int bpp,Bit32u flags) {
	if (flags&SDL_FULLSCREEN && sdl.desktop.fullwrap.height && sdl.desktop.fullwrap.width) {
		width  = sdl.desktop.fullwrap.width;
		height = sdl.desktop.fullwrap.height;
	}

	static int i_height = 0;
	static int i_width = 0;
	static int i_bpp = 0;
	static Bit32u i_flags = 0;

	if (sdl.surface != NULL && height == i_height && width == i_width && bpp == i_bpp && flags == i_flags) {
#if SETMODE_SAVES_CLEAR
		if ((flags & SDL_OPENGL)==0) {
			SDL_FillRect(sdl.surface,NULL,SDL_MapRGB(sdl.surface->format,0,0,0));
		} else {
			glClearColor (0.0f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			SDL_GL_SwapBuffers();
		}
#endif
		return sdl.surface;
	}

	SDL_Surface* s = SDL_SetVideoMode(width,height,bpp,flags);

	if (s != NULL) {
		i_height = height;
		i_width = width;
		i_bpp = bpp;
		i_flags = flags;
	}

	return s;
}

extern const char* RunningProgram;
extern bool CPU_CycleAutoAdjust;
bool startup_state_numlock=false;
bool startup_state_capslock=false;

// Função exportada - NÃO static
void GFX_SetTitle(Bit32s cycles,int frameskip,bool paused) {
	char title[200] = { 0 };
	if (paused) {
		strcpy(title, RunningProgram);
		strcat(title, " PAUSED");
	} else {
		strcpy(title, RunningProgram);
	}
	SDL_WM_SetCaption(title,VERSION);
}

static unsigned char logo[32*32*4]= {
#include "dosbox_logo.h"
};

static void GFX_SetIcon() {
#if !defined(MACOSX)
#ifdef WORDS_BIGENDIAN
	SDL_Surface* logos= SDL_CreateRGBSurfaceFrom((void*)logo,32,32,32,128,0xff000000,0x00ff0000,0x0000ff00,0);
#else
	SDL_Surface* logos= SDL_CreateRGBSurfaceFrom((void*)logo,32,32,32,128,0x000000ff,0x0000ff00,0x00ff0000,0);
#endif
	SDL_WM_SetIcon(logos,NULL);
#endif
}

static void KillSwitch(bool pressed) {
	if (!pressed) return;
	throw 1;
}

// Pausa otimizada
static void PauseDOSBox(bool pressed) {
	if (!pressed) return;
	SDLMod inkeymod = SDL_GetModState();

	GFX_SetTitle(-1,-1,true);
	bool paused = true;
	SDL_Delay(500);
	SDL_Event event;
	// Limpar eventos pendentes rapidamente
	while (SDL_PollEvent(&event));

	while (paused) {
		SDL_WaitEvent(&event);
		switch (event.type) {
		case SDL_QUIT:
			KillSwitch(true);
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			if(event.key.keysym.sym == SDLK_PAUSE) {
				SDLMod outkeymod = (event.key.keysym.mod);
				if (inkeymod != outkeymod) {
					KEYBOARD_ClrBuffer();
					MAPPER_LosingFocus();
				}
				paused = false;
				GFX_SetTitle(-1,-1,false);
				break;
			}
#if defined (MACOSX)
			if (event.key.keysym.sym == SDLK_q && (event.key.keysym.mod == KMOD_RMETA || event.key.keysym.mod == KMOD_LMETA) ) {
				KillSwitch(true);
				break;
			}
#endif
		}
	}
}

#if defined (WIN32)
bool GFX_SDLUsingWinDIB(void) {
	return sdl.using_windib;
}
#endif

// Função otimizada para obter melhor modo
Bitu GFX_GetBestMode(Bitu flags) {
	Bitu testbpp,gotbpp;
	switch (sdl.desktop.want_type) {
	case SCREEN_SURFACE:
check_surface:
		flags &= ~GFX_LOVE_8;
		if (flags & GFX_LOVE_8) testbpp=8;
		else if (flags & GFX_LOVE_15) testbpp=15;
		else if (flags & GFX_LOVE_16) testbpp=16;
		else if (flags & GFX_LOVE_32) testbpp=32;
		else testbpp=0;
#if C_DDRAW
check_gotbpp:
#endif
		if (sdl.desktop.fullscreen) gotbpp=SDL_VideoModeOK(640,480,testbpp,SDL_FULLSCREEN|SDL_HWSURFACE|SDL_HWPALETTE);
		else gotbpp=sdl.desktop.bpp;
		switch (gotbpp) {
		case 8:
			flags&=~(GFX_CAN_15|GFX_CAN_16|GFX_CAN_32);
			break;
		case 15:
			flags&=~(GFX_CAN_8|GFX_CAN_16|GFX_CAN_32);
			break;
		case 16:
			flags&=~(GFX_CAN_8|GFX_CAN_15|GFX_CAN_32);
			break;
		case 24:
		case 32:
			flags&=~(GFX_CAN_8|GFX_CAN_15|GFX_CAN_16);
			break;

		}
		flags |= GFX_CAN_RANDOM;
		break;
#if C_DDRAW
	case SCREEN_SURFACE_DDRAW:
		if (!(flags&(GFX_CAN_15|GFX_CAN_16|GFX_CAN_32))) goto check_surface;
		if (flags & GFX_LOVE_15) testbpp=15;
		else if (flags & GFX_LOVE_16) testbpp=16;
		else if (flags & GFX_LOVE_32) testbpp=32;
		else testbpp=0;
		flags|=GFX_SCALING;
		goto check_gotbpp;
#endif
	case SCREEN_OVERLAY:
		if (flags & GFX_RGBONLY || !(flags&GFX_CAN_32)) goto check_surface;
		flags|=GFX_SCALING;
		flags&=~(GFX_CAN_8|GFX_CAN_15|GFX_CAN_16);
		break;
#if C_OPENGL
	case SCREEN_OPENGL:
		if (!(flags&GFX_CAN_32)) goto check_surface;
		flags|=GFX_SCALING;
		flags&=~(GFX_CAN_8|GFX_CAN_15|GFX_CAN_16);
		break;
#endif
	default:
		goto check_surface;
	}
	return flags;
}

// Funções de tela cheia otimizadas
void GFX_ResetScreen(void) {
	if(glide.enabled) {
		GLIDE_ResetScreen(true);
		return;
	}
	GFX_Stop();
	if (sdl.draw.callback)
		(sdl.draw.callback)( GFX_CallBackReset );
	GFX_Start();
	CPU_Reset_AutoAdjust();
}

void GFX_ForceFullscreenExit(void) {
	if (!sdl.desktop.lazy_fullscreen) {
		sdl.desktop.fullscreen=false;
		GFX_ResetScreen();
	}
}

// Log2 otimizado com lookup table
static const int log2_table[32] = {
	0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
};

static int int_log2(int val) {
	if (val < 2) return 0;
	if (val < 32) return log2_table[val];
	int log = 5;
	while ((val >>= 1) != 0) log++;
	return log;
}

// Superfície escalada otimizada
static SDL_Surface * GFX_SetupSurfaceScaled(Bit32u sdl_flags, Bit32u bpp) {
	Bit16u fixedWidth;
	Bit16u fixedHeight;

	if (sdl.desktop.fullscreen) {
		fixedWidth = sdl.desktop.full.fixed ? sdl.desktop.full.width : 0;
		fixedHeight = sdl.desktop.full.fixed ? sdl.desktop.full.height : 0;
		sdl_flags |= SDL_FULLSCREEN|SDL_HWSURFACE;
	} else {
		fixedWidth = sdl.desktop.window.width;
		fixedHeight = sdl.desktop.window.height;
		sdl_flags |= SDL_HWSURFACE | SDL_RESIZABLE;
	}

	if (fixedWidth && fixedHeight) {
		double ratio_w=(double)fixedWidth/(sdl.draw.width*sdl.draw.scalex);
		double ratio_h=(double)fixedHeight/(sdl.draw.height*sdl.draw.scaley);
		if ( ratio_w < ratio_h) {
			sdl.clip.w = fixedWidth;
			sdl.clip.h = (Bit16u)(sdl.draw.height * sdl.draw.scaley*ratio_w + 0.1);
		} else {
			sdl.clip.w=(Bit16u)(sdl.draw.width*sdl.draw.scalex*ratio_h + 0.4);
			sdl.clip.h=(Bit16u)fixedHeight;
		}

		if (sdl.desktop.fullscreen)
			sdl.surface = SDL_SetVideoMode_Wrap(fixedWidth,fixedHeight,bpp,sdl_flags);
		else
			sdl.surface = SDL_SetVideoMode_Wrap(sdl.clip.w,sdl.clip.h,bpp,sdl_flags);
	} else {
		sdl.clip.x=0;
		sdl.clip.y=0;
		sdl.clip.w=(Bit16u)(sdl.draw.width*sdl.draw.scalex);
		sdl.clip.h=(Bit16u)(sdl.draw.height*sdl.draw.scaley);
		sdl.surface=SDL_SetVideoMode_Wrap(sdl.clip.w,sdl.clip.h,bpp,sdl_flags);
	}

	if (sdl.surface && sdl.surface->flags & SDL_FULLSCREEN) {
		sdl.clip.x = (Sint16)((sdl.surface->w - sdl.clip.w)/1);
		sdl.clip.y = (Sint16)((sdl.surface->h - sdl.clip.h)/1);
	} else if (sdl.surface) {
		sdl.clip.x = (Sint16)((sdl.surface->w - sdl.clip.w) / 1);
		sdl.clip.y = (Sint16)((sdl.surface->h - sdl.clip.h) / 1);
		if (sdl.clip.x < 0) sdl.clip.x = 0;
		if (sdl.clip.y < 0) sdl.clip.y = 0;
	}

	return sdl.surface;
}

void GFX_TearDown(void) {
	if (sdl.updating)
		GFX_EndUpdate( 0 );

	if (sdl.blit.surface) {
		SDL_FreeSurface(sdl.blit.surface);
		sdl.blit.surface=0;
	}
}
// Configuração de tamanho otimizada

#if C_OPENGL
static bool LoadGLShaders(const char *src, GLuint *vertex, GLuint *fragment) {
	// Simplificado para evitar crash - apenas retorna falso
	return false;
}
#endif

// Função exportada - usando double para compatibilidade
Bitu GFX_SetSize(Bitu width,Bitu height,Bitu flags,double scalex,double scaley,GFX_CallBack_t callback) {
	if (sdl.updating)
		GFX_EndUpdate( 0 );

	sdl.draw.width=width;
	sdl.draw.height=height;
	sdl.draw.callback=callback;
	sdl.draw.scalex=scalex;
	sdl.draw.scaley=scaley;

	int bpp=0;
	Bitu retFlags = 0;

	if (sdl.blit.surface) {
		SDL_FreeSurface(sdl.blit.surface);
		sdl.blit.surface=0;
	}
	switch (sdl.desktop.want_type) {
	case SCREEN_SURFACE:
dosurface:
		if (flags & GFX_CAN_8) bpp=8;
		if (flags & GFX_CAN_15) bpp=15;
		if (flags & GFX_CAN_16) bpp=16;
		if (flags & GFX_CAN_32) bpp=32;
		sdl.desktop.type=SCREEN_SURFACE;
		sdl.clip.w=width;
		sdl.clip.h=height;
		if (sdl.desktop.fullscreen) {
			if (sdl.desktop.full.fixed) {
				sdl.clip.x=(Sint16)((sdl.desktop.full.width-width)/1);
				sdl.clip.y=(Sint16)((sdl.desktop.full.height-height)/1);
				sdl.surface=SDL_SetVideoMode_Wrap(sdl.desktop.full.width,sdl.desktop.full.height,bpp,
				                                  SDL_FULLSCREEN | ((flags & GFX_CAN_RANDOM) ? SDL_SWSURFACE : SDL_HWSURFACE) |
				                                  (sdl.desktop.doublebuf ? SDL_DOUBLEBUF|SDL_ASYNCBLIT : 0) | SDL_HWPALETTE);
				if (sdl.surface == NULL) E_Exit("Could not set fullscreen video mode %ix%i-%i: %s",sdl.desktop.full.width,sdl.desktop.full.height,bpp,SDL_GetError());
			} else {
				sdl.clip.x=0;
				sdl.clip.y=0;
				sdl.surface=SDL_SetVideoMode_Wrap(width,height,bpp,
				                                  SDL_FULLSCREEN | ((flags & GFX_CAN_RANDOM) ? SDL_SWSURFACE : SDL_HWSURFACE) |
				                                  (sdl.desktop.doublebuf ? SDL_DOUBLEBUF|SDL_ASYNCBLIT  : 0)|SDL_HWPALETTE);
				if (sdl.surface == NULL)
					E_Exit("Could not set fullscreen video mode %ix%i-%i: %s",(int)width,(int)height,bpp,SDL_GetError());
			}
		} else {
			sdl.clip.x=0;
			sdl.clip.y=0;
			sdl.surface=SDL_SetVideoMode_Wrap(width,height,bpp,(flags & GFX_CAN_RANDOM) ? SDL_SWSURFACE : SDL_HWSURFACE);
#ifdef WIN32
			if (sdl.surface == NULL) {
				SDL_QuitSubSystem(SDL_INIT_VIDEO);
				sdl.using_windib=!sdl.using_windib;
				putenv(sdl.using_windib ? "SDL_VIDEODRIVER=windib" : "SDL_VIDEODRIVER=directx");
				SDL_InitSubSystem(SDL_INIT_VIDEO);
				GFX_SetIcon();
				sdl.surface = SDL_SetVideoMode_Wrap(width,height,bpp,SDL_HWSURFACE);
				if(sdl.surface) GFX_SetTitle(-1,-1,false);
			}
#endif
			if (sdl.surface == NULL)
				E_Exit("Could not set windowed video mode %ix%i-%i: %s",(int)width,(int)height,bpp,SDL_GetError());
		}
		if (sdl.surface) {
			switch (sdl.surface->format->BitsPerPixel) {
			case 8:
				retFlags = GFX_CAN_8;
				break;
			case 15:
				retFlags = GFX_CAN_15;
				break;
			case 16:
				retFlags = GFX_CAN_16;
				break;
			case 32:
				retFlags = GFX_CAN_32;
				break;
			}
			if (retFlags && (sdl.surface->flags & SDL_HWSURFACE))
				retFlags |= GFX_HARDWARE;
			if (retFlags && (sdl.surface->flags & SDL_DOUBLEBUF)) {
				sdl.blit.surface=SDL_CreateRGBSurface(SDL_HWSURFACE,
				                                      sdl.draw.width, sdl.draw.height,
				                                      sdl.surface->format->BitsPerPixel,
				                                      sdl.surface->format->Rmask,
				                                      sdl.surface->format->Gmask,
				                                      sdl.surface->format->Bmask,
				                                      0);
			}
		}
		break;
#if C_DDRAW
	case SCREEN_SURFACE_DDRAW:
		if (flags & GFX_CAN_15) bpp=15;
		if (flags & GFX_CAN_16) bpp=16;
		if (flags & GFX_CAN_32) bpp=32;
		if (!GFX_SetupSurfaceScaled((sdl.desktop.doublebuf && sdl.desktop.fullscreen) ? SDL_DOUBLEBUF : 0,bpp)) goto dosurface;
		sdl.blit.rect_dest.top    = sdl.clip.y;
		sdl.blit.rect_dest.left   = sdl.clip.x;
		sdl.blit.rect_dest.right  = sdl.clip.x + sdl.clip.w;
		sdl.blit.rect_dest.bottom = sdl.clip.y + sdl.clip.h;
		sdl.blit.surface=SDL_CreateRGBSurface(SDL_HWSURFACE,sdl.draw.width + 2,sdl.draw.height + 2,
		                                      sdl.surface->format->BitsPerPixel,
		                                      sdl.surface->format->Rmask,
		                                      sdl.surface->format->Gmask,
		                                      sdl.surface->format->Bmask,
		                                      0);
		if (!sdl.blit.surface || (!sdl.blit.surface->flags&SDL_HWSURFACE)) {
			if (sdl.blit.surface) {
				SDL_FreeSurface(sdl.blit.surface);
				sdl.blit.surface = 0;
			}
			goto dosurface;
		}
		SDL_FillRect(sdl.blit.surface,NULL,SDL_MapRGB(sdl.surface->format,0,0,0));
		sdl.blit.rect_src.top    = 1;
		sdl.blit.rect_src.left   = 1;
		sdl.blit.rect_src.right  = sdl.blit.surface->w - 1;
		sdl.blit.rect_src.bottom = sdl.blit.surface->h - 1;

		switch (sdl.surface->format->BitsPerPixel) {
		case 15:
			retFlags = GFX_CAN_15 | GFX_SCALING | GFX_HARDWARE;
			break;
		case 16:
			retFlags = GFX_CAN_16 | GFX_SCALING | GFX_HARDWARE;
			break;
		case 32:
			retFlags = GFX_CAN_32 | GFX_SCALING | GFX_HARDWARE;
			break;
		}
		sdl.desktop.type=SCREEN_SURFACE_DDRAW;
		break;
#endif
	case SCREEN_OVERLAY:
		if (sdl.overlay) {
			SDL_FreeYUVOverlay(sdl.overlay);
			sdl.overlay = 0;
		}
		if (!(flags & GFX_CAN_32) || (flags & GFX_RGBONLY)) goto dosurface;
		if (!GFX_SetupSurfaceScaled(0,0)) goto dosurface;
		sdl.overlay = SDL_CreateYUVOverlay(width * 2, height, SDL_UYVY_OVERLAY, sdl.surface);

		if (sdl.overlay && SDL_LockYUVOverlay(sdl.overlay) == 0) {
			if (sdl.overlay->pitches[0] < 4 * width) {
				SDL_UnlockYUVOverlay(sdl.overlay);
				SDL_FreeYUVOverlay(sdl.overlay);
				sdl.overlay = 0;
			} else SDL_UnlockYUVOverlay(sdl.overlay);
		}

		if (!sdl.overlay) goto dosurface;
		sdl.desktop.type = SCREEN_OVERLAY;
		retFlags = GFX_CAN_32 | GFX_SCALING | GFX_HARDWARE;
		break;
#if C_OPENGL
	case SCREEN_OPENGL:
	{
		if (sdl.opengl.pixel_buffer_object) {
			glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_EXT, 0);
			if (sdl.opengl.buffer) glDeleteBuffersARB(1, &sdl.opengl.buffer);
		} else if (sdl.opengl.framebuf) {
			free(sdl.opengl.framebuf);
		}
		sdl.opengl.framebuf=0;
		if (!(flags&GFX_CAN_32)) goto dosurface;
		int texsize=2 << int_log2(width > height ? width : height);
		if (texsize>sdl.opengl.max_texsize) goto dosurface;

		SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
#if SDL_VERSION_ATLEAST(1, 2, 11)
		SDL_GL_SetAttribute( SDL_GL_SWAP_CONTROL, 0 );
#endif

		Bit32u gl_flags = SDL_OPENGL;
		if (!sdl.desktop.fullscreen) {
			gl_flags |= SDL_RESIZABLE;
		}

		if (GFX_SetupSurfaceScaled(gl_flags, 32)==NULL) GFX_SetupSurfaceScaled(gl_flags, 16);
		if (!sdl.surface || sdl.surface->format->BitsPerPixel<15) goto dosurface;

		if (sdl.opengl.pixel_buffer_object) {
			glGenBuffersARB(1, &sdl.opengl.buffer);
			glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_EXT, sdl.opengl.buffer);
			glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_EXT, width*height*4, NULL, GL_STREAM_DRAW_ARB);
			glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_EXT, 0);
		} else {
			sdl.opengl.framebuf=malloc(width*height*4);
		}
		sdl.opengl.pitch=width*4;

		glViewport(sdl.clip.x, sdl.clip.y, sdl.clip.w, sdl.clip.h);

		if (sdl.opengl.texture > 0) {
			glDeleteTextures(1,&sdl.opengl.texture);
		}
		glGenTextures(1,&sdl.opengl.texture);
		glBindTexture(GL_TEXTURE_2D,sdl.opengl.texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		// Usar GL_NEAREST para performance
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

		// Usar buffer estático
		memset(emptytex, 0, texsize * texsize * 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texsize, texsize, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, emptytex);
		// Configuração OpenGL simplificada

		glClearColor (0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		SDL_GL_SwapBuffers();
		glClear(GL_COLOR_BUFFER_BIT);
		glDisable (GL_DEPTH_TEST);
		glDisable (GL_LIGHTING);
		glDisable(GL_CULL_FACE);
		glEnable(GL_TEXTURE_2D);

		// Criar display list otimizada
		GLfloat tex_width=((GLfloat)(width)/(GLfloat)texsize);
		GLfloat tex_height=((GLfloat)(height)/(GLfloat)texsize);

		glShadeModel(GL_FLAT);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		if (glIsList(sdl.opengl.displaylist)) glDeleteLists(sdl.opengl.displaylist, 1);
		sdl.opengl.displaylist = glGenLists(1);
		glNewList(sdl.opengl.displaylist, GL_COMPILE);

		glBegin(GL_TRIANGLES);

		glTexCoord2f(0, 0);
		glVertex2f(-1.0f, -1.0f);

		glTexCoord2f(1, 0);
		glVertex2f( 1.0f, -1.0f);

		glTexCoord2f(1, 1);
		glVertex2f( 1.0f,  1.0f);

		glTexCoord2f(0, 0);
		glVertex2f(-1.0f, -1.0f);

		glTexCoord2f(1, 1);
		glVertex2f( 1.0f,  1.0f);

		glTexCoord2f(0, 1);
		glVertex2f(-1.0f,  1.0f);

		glEnd();

		glEndList();
		render_cache.width = width;
		render_cache.height = height;
		render_cache.tex_width = sdl.opengl.tex_width;
		render_cache.tex_height = sdl.opengl.tex_height;
		render_cache.pitch = sdl.opengl.pitch;
		render_cache.initialized = true;

		OPENGL_ERROR("End of setsize");

		sdl.desktop.type=SCREEN_OPENGL;
		retFlags = GFX_CAN_32 | GFX_SCALING;
		if (sdl.opengl.pixel_buffer_object)
			retFlags |= GFX_HARDWARE;
		break;
	}
#endif
	default:
		goto dosurface;
	}
	if (retFlags)
		GFX_Start();
	if (!sdl.mouse.autoenable) SDL_ShowCursor(sdl.mouse.autolock?SDL_DISABLE:SDL_ENABLE);
	return retFlags;
}

void GFX_SetShader(const char* src) {
	// Shaders desabilitados para performance
#if C_OPENGL
	// Desabilitado para estabilidade
#endif
}

void GFX_CaptureMouse(void) {
	sdl.mouse.locked=!sdl.mouse.locked;
	if (sdl.mouse.locked) {
		SDL_WM_GrabInput(SDL_GRAB_ON);
		SDL_ShowCursor(SDL_DISABLE);
	} else {
		SDL_WM_GrabInput(SDL_GRAB_OFF);
		if (sdl.mouse.autoenable || !sdl.mouse.autolock) SDL_ShowCursor(SDL_ENABLE);
	}
	mouselocked=sdl.mouse.locked;
}

void GFX_UpdateSDLCaptureState(void) {
	if (sdl.mouse.locked) {
		SDL_WM_GrabInput(SDL_GRAB_ON);
		SDL_ShowCursor(SDL_DISABLE);
	} else {
		SDL_WM_GrabInput(SDL_GRAB_OFF);
		if (sdl.mouse.autoenable || !sdl.mouse.autolock) SDL_ShowCursor(SDL_ENABLE);
	}
	CPU_Reset_AutoAdjust();
	GFX_SetTitle(-1,-1,false);
}

bool mouselocked;
static void CaptureMouse(bool pressed) {
	if (!pressed) return;
	GFX_CaptureMouse();
}

#if defined (WIN32)
STICKYKEYS stick_keys = {sizeof(STICKYKEYS), 0};
void sticky_keys(bool restore) {
	static bool inited = false;
	if (!inited) {
		inited = true;
		SystemParametersInfo(SPI_GETSTICKYKEYS, sizeof(STICKYKEYS), &stick_keys, 0);
	}
	if (restore) {
		SystemParametersInfo(SPI_SETSTICKYKEYS, sizeof(STICKYKEYS), &stick_keys, 0);
		return;
	}
	STICKYKEYS s = {sizeof(STICKYKEYS), 0};
	SystemParametersInfo(SPI_GETSTICKYKEYS, sizeof(STICKYKEYS), &s, 0);
	if ( !(s.dwFlags & SKF_STICKYKEYSON)) {
		s.dwFlags &= ~SKF_HOTKEYACTIVE;
		SystemParametersInfo(SPI_SETSTICKYKEYS, sizeof(STICKYKEYS), &s, 0);
	}
}
#endif

void GFX_SwitchFullScreen(void) {
	sdl.desktop.fullscreen=!sdl.desktop.fullscreen;
	if (sdl.desktop.fullscreen) {
		if (!sdl.mouse.locked) GFX_CaptureMouse();
#if defined (WIN32)
		sticky_keys(false);
#endif
	} else {
		if (sdl.mouse.locked) GFX_CaptureMouse();
#if defined (WIN32)
		sticky_keys(true);
#endif
	}
	if (glide.enabled)
		GLIDE_ResetScreen();
	else
		GFX_ResetScreen();
}

static void SwitchFullScreen(bool pressed) {
	if (!pressed) return;
	if (!sdl.desktop.lazy_fullscreen) {
		GFX_SwitchFullScreen();
	}
}

void GFX_SwitchLazyFullscreen(bool lazy) {
	sdl.desktop.lazy_fullscreen=lazy;
	sdl.desktop.lazy_fullscreen_req=false;
}

void GFX_SwitchFullscreenNoReset(void) {
	sdl.desktop.fullscreen=!sdl.desktop.fullscreen;
}

bool GFX_LazyFullscreenRequested(void) {
	if (sdl.desktop.lazy_fullscreen) return sdl.desktop.lazy_fullscreen_req;
	return false;
}

void GFX_RestoreMode(void) {
	GFX_SetSize(sdl.draw.width,sdl.draw.height,sdl.draw.flags,sdl.draw.scalex,sdl.draw.scaley,sdl.draw.callback);
	GFX_UpdateSDLCaptureState();
}

// Início de atualização otimizado
bool GFX_StartUpdate(Bit8u * & pixels,Bitu & pitch) {
	if (!sdl.active || sdl.updating)
		return false;
	switch (sdl.desktop.type) {
	case SCREEN_SURFACE:
		if (sdl.blit.surface) {
			if (SDL_MUSTLOCK(sdl.blit.surface) && SDL_LockSurface(sdl.blit.surface))
				return false;
			pixels=(Bit8u *)sdl.blit.surface->pixels;
			pitch=sdl.blit.surface->pitch;
		} else {
			if (SDL_MUSTLOCK(sdl.surface) && SDL_LockSurface(sdl.surface))
				return false;
			pixels=(Bit8u *)sdl.surface->pixels;
			pixels+=sdl.clip.y*sdl.surface->pitch;
			pixels+=sdl.clip.x*sdl.surface->format->BytesPerPixel;
			pitch=sdl.surface->pitch;
		}
		sdl.updating=true;
		return true;
#if C_DDRAW
	case SCREEN_SURFACE_DDRAW:
		if (SDL_LockSurface(sdl.blit.surface)) {
			return false;
		}
		pixels = (Bit8u *)sdl.blit.surface->pixels + sdl.blit.rect_src.left*sdl.blit.surface->format->BytesPerPixel + sdl.blit.rect_src.top*sdl.blit.surface->pitch;
		pitch = sdl.blit.surface->pitch;
		sdl.updating = true;
		return true;
#endif
	case SCREEN_OVERLAY:
		if (SDL_LockYUVOverlay(sdl.overlay)) return false;
		pixels=(Bit8u *)*(sdl.overlay->pixels);
		pitch=*(sdl.overlay->pitches);
		sdl.updating=true;
		return true;
#if C_OPENGL
	case SCREEN_OPENGL:
		if(sdl.opengl.pixel_buffer_object) {
			glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_EXT, sdl.opengl.buffer);
			pixels=(Bit8u *)glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_EXT, GL_WRITE_ONLY);
		} else {
			pixels=(Bit8u *)sdl.opengl.framebuf;
		}
		if (pixels == NULL) return false;
		pitch=sdl.opengl.pitch;
		sdl.updating=true;
		return true;
#endif
	default:
		break;
	}
	return false;
}

// Finalização de atualização otimizada para OpenGL
void GFX_EndUpdate( const Bit16u *changedLines ) {
	if (!sdl.updating) return;
	bool actually_updating = sdl.updating;
	sdl.updating=false;

	switch (sdl.desktop.type) {
	case SCREEN_SURFACE:
		if (SDL_MUSTLOCK(sdl.surface)) {
			if (sdl.blit.surface) {
				SDL_UnlockSurface(sdl.blit.surface);
				SDL_BlitSurface( sdl.blit.surface, 0, sdl.surface, &sdl.clip );
			} else {
				SDL_UnlockSurface(sdl.surface);
			}
			SDL_Flip(sdl.surface);
		} else if (changedLines) {
			Bitu y = 0, index = 0, rectCount = 0;
			while (y < sdl.draw.height && rectCount < MAX_RECTS) {
				if (!(index & 1)) {
					y += changedLines[index];
				} else {
					SDL_Rect *rect = &sdl.updateRects[rectCount++];
					rect->x = sdl.clip.x;
					rect->y = sdl.clip.y + y;
					rect->w = (Bit16u)sdl.draw.width;
					rect->h = changedLines[index];
					y += changedLines[index];
				}
				index++;
			}
			if (rectCount)
				SDL_UpdateRects( sdl.surface, rectCount, sdl.updateRects );
		}
		break;
#if C_DDRAW
	case SCREEN_SURFACE_DDRAW:
		SDL_UnlockSurface(sdl.blit.surface);
		IDirectDrawSurface3_Blt(
		    sdl.surface->hwdata->dd_writebuf,&sdl.blit.rect_dest,
		    sdl.blit.surface->hwdata->dd_surface,&sdl.blit.rect_src,
		    DDBLT_WAIT, NULL);
		SDL_Flip(sdl.surface);
		break;
#endif
	case SCREEN_OVERLAY:
		SDL_UnlockYUVOverlay(sdl.overlay);
		SDL_DisplayYUVOverlay(sdl.overlay,&sdl.clip);
		break;
#if C_OPENGL
	case SCREEN_OPENGL:
		if (!actually_updating) return;
		glClear(GL_COLOR_BUFFER_BIT);

		if (sdl.opengl.pixel_buffer_object) {
			glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_EXT);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
			                sdl.draw.width, sdl.draw.height, GL_BGRA_EXT,
			                GL_UNSIGNED_INT_8_8_8_8_REV, 0);
			glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_EXT, 0);
		} else if (changedLines) {
			// Otimização: agrupar linhas alteradas em blocos contíguos
			Bitu y = 0, index = 0;
			Bitu start_y = 0, height = 0;

			while (y < sdl.draw.height) {
				if (!(index & 1)) {
					if (height > 0) {
						Bit8u *pixels = (Bit8u *)sdl.opengl.framebuf + start_y * sdl.opengl.pitch;
						glTexSubImage2D(GL_TEXTURE_2D, 0, 0, start_y,
						                sdl.draw.width, height, GL_BGRA_EXT,
						                GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
						height = 0;
					}
					y += changedLines[index];
					start_y = y;
				} else {
					height += changedLines[index];
					y += changedLines[index];
				}
				index++;
			}

			if (height > 0) {
				Bit8u *pixels = (Bit8u *)sdl.opengl.framebuf + start_y * sdl.opengl.pitch;
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, start_y,
				                sdl.draw.width, height, GL_BGRA_EXT,
				                GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
			}
		}

		glCallList(sdl.opengl.displaylist);
		SDL_GL_SwapBuffers();
		break;
#endif
	default:
		break;
	}
}

void GFX_SetPalette(Bitu start,Bitu count,GFX_PalEntry * entries) {
	if (sdl.surface->flags & SDL_HWPALETTE) {
		if (!SDL_SetPalette(sdl.surface,SDL_PHYSPAL,(SDL_Color *)entries,start,count)) {
			E_Exit("SDL:Can't set palette");
		}
	} else {
		if (!SDL_SetPalette(sdl.surface,SDL_LOGPAL,(SDL_Color *)entries,start,count)) {
			E_Exit("SDL:Can't set palette");
		}
	}
}

// RGB otimizado com lookup table para YUV
static Bit32u yuv_lookup[256][3];  // Cache RGB->YUV
Bitu GFX_GetRGB(Bit8u red,Bit8u green,Bit8u blue) {
	switch (sdl.desktop.type) {
	case SCREEN_SURFACE:
	case SCREEN_SURFACE_DDRAW:
		return SDL_MapRGB(sdl.surface->format,red,green,blue);
	case SCREEN_OVERLAY:
	{
		// Cálculo YUV simplificado

		Bit8u y =  ( 9797*(red) + 19237*(green) +  3734*(blue) ) >> 15;
		Bit8u u =  (18492*((blue)-(y)) >> 15) + 128;
		Bit8u v =  (23372*((red)-(y)) >> 15) + 128;
#ifdef WORDS_BIGENDIAN
		return (y << 0) | (v << 8) | (y << 16) | (u << 24);
#else
		return (u << 0) | (y << 8) | (v << 16) | (y << 24);
#endif
	}
	case SCREEN_OPENGL:
		return ((blue << 0) | (green << 8) | (red << 16)) | (255 << 24);
	}
	return 0;
}

void GFX_Stop() {
	if (sdl.updating)
		GFX_EndUpdate( 0 );
	sdl.active=false;
}

void GFX_Start() {
	sdl.active=true;
}

static void GUI_ShutDown(Section * /*sec*/) {
	GFX_Stop();
	if (sdl.draw.callback) (sdl.draw.callback)( GFX_CallBackStop );
	if (sdl.mouse.locked) GFX_CaptureMouse();
	if (sdl.desktop.fullscreen) GFX_SwitchFullScreen();
}

// Prioridade otimizada para Windows XP
static void SetPriority(PRIORITY_LEVELS level) {
#if C_SET_PRIORITY
	if((sdl.priority.focus != sdl.priority.nofocus ) && (getuid()!=0) ) return;
#endif
	switch (level) {
#ifdef WIN32
	case PRIORITY_LEVEL_PAUSE:
		SetPriorityClass(GetCurrentProcess(),IDLE_PRIORITY_CLASS);
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
		break;
	case PRIORITY_LEVEL_LOWEST:
		SetPriorityClass(GetCurrentProcess(),IDLE_PRIORITY_CLASS);
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
		break;
	case PRIORITY_LEVEL_LOWER:
		SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS);
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
		break;
	case PRIORITY_LEVEL_NORMAL:
		SetPriorityClass(GetCurrentProcess(),NORMAL_PRIORITY_CLASS);
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
		break;
	case PRIORITY_LEVEL_HIGHER:
		SetPriorityClass(GetCurrentProcess(),ABOVE_NORMAL_PRIORITY_CLASS);
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
		break;
	case PRIORITY_LEVEL_HIGHEST:
		SetPriorityClass(GetCurrentProcess(),HIGH_PRIORITY_CLASS);
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
		break;
#endif
	default:
		break;
	}
}

extern Bit8u int10_font_14[256 * 14];
static void OutputString(Bitu x,Bitu y,const char * text,Bit32u color,Bit32u color2,SDL_Surface * output_surface) {
	Bit32u * draw=(Bit32u*)(((Bit8u *)output_surface->pixels)+((y)*output_surface->pitch))+x;
	while (*text) {
		Bit8u * font=&int10_font_14[(*text)*14];
		Bitu i,j;
		Bit32u * draw_line=draw;
		for (i=0; i<14; i++) {
			Bit8u map=*font++;
			for (j=0; j<8; j++) {
				if (map & 0x80) *((Bit32u*)(draw_line+j))=color;
				else *((Bit32u*)(draw_line+j))=color2;
				map<<=1;
			}
			draw_line+=output_surface->pitch/4;
		}
		text++;
		draw+=8;
	}
}

#include "dosbox_splash.h"

void Restart(bool pressed);

// Inicialização otimizada
static void GUI_StartUp(Section * sec) {
	sec->AddDestroyFunction(&GUI_ShutDown);
	Section_prop * section=static_cast<Section_prop *>(sec);
	sdl.active=false;
	sdl.updating=false;

	GFX_SetIcon();

	sdl.desktop.lazy_fullscreen=false;
	sdl.desktop.lazy_fullscreen_req=false;
	sdl.desktop.fullscreen=section->Get_bool("fullscreen");
	sdl.wait_on_error=section->Get_bool("waitonerror");

	Prop_multival* p=section->Get_multival("priority");
	std::string focus = p->GetSection()->Get_string("active");
	std::string notfocus = p->GetSection()->Get_string("inactive");

	if      (focus == "lowest")  {
		sdl.priority.focus = PRIORITY_LEVEL_LOWEST;
	}
	else if (focus == "lower")   {
		sdl.priority.focus = PRIORITY_LEVEL_LOWER;
	}
	else if (focus == "normal")  {
		sdl.priority.focus = PRIORITY_LEVEL_NORMAL;
	}
	else if (focus == "higher")  {
		sdl.priority.focus = PRIORITY_LEVEL_HIGHER;
	}
	else if (focus == "highest") {
		sdl.priority.focus = PRIORITY_LEVEL_HIGHEST;
	}

	if      (notfocus == "lowest")  {
		sdl.priority.nofocus=PRIORITY_LEVEL_LOWEST;
	}
	else if (notfocus == "lower")   {
		sdl.priority.nofocus=PRIORITY_LEVEL_LOWER;
	}
	else if (notfocus == "normal")  {
		sdl.priority.nofocus=PRIORITY_LEVEL_NORMAL;
	}
	else if (notfocus == "higher")  {
		sdl.priority.nofocus=PRIORITY_LEVEL_HIGHER;
	}
	else if (notfocus == "highest") {
		sdl.priority.nofocus=PRIORITY_LEVEL_HIGHEST;
	}
	else if (notfocus == "pause")   {
		sdl.priority.nofocus=PRIORITY_LEVEL_PAUSE;
	}

	SetPriority(sdl.priority.focus);
	sdl.mouse.locked=false;
	mouselocked=false;
	sdl.mouse.requestlock=false;
	sdl.desktop.full.fixed=false;

	const char* fullresolution=section->Get_string("fullresolution");
	sdl.desktop.full.width  = 0;
	sdl.desktop.full.height = 0;
	if(fullresolution && *fullresolution) {
		char res[100];
		safe_strncpy( res, fullresolution, sizeof( res ));
		fullresolution = lowcase (res);
		if (strcmp(fullresolution,"original")) {
			sdl.desktop.full.fixed = true;
			if (strcmp(fullresolution,"desktop")) {
				char* height = const_cast<char*>(strchr(fullresolution,'x'));
				if (height && * height) {
					*height = 0;
					sdl.desktop.full.height = (Bit16u)atoi(height+1);
					sdl.desktop.full.width  = (Bit16u)atoi(res);
				}
			}
		}
	}

	sdl.desktop.window.width  = 0;
	sdl.desktop.window.height = 0;
	const char* windowresolution=section->Get_string("windowresolution");
	if(windowresolution && *windowresolution) {
		char res[100];
		safe_strncpy( res,windowresolution, sizeof( res ));
		windowresolution = lowcase (res);
		if(strcmp(windowresolution,"original")) {
			char* height = const_cast<char*>(strchr(windowresolution,'x'));
			if(height && *height) {
				*height = 0;
				sdl.desktop.window.height = (Bit16u)atoi(height+1);
				sdl.desktop.window.width  = (Bit16u)atoi(res);
			}
		}
	}

	sdl.desktop.doublebuf=section->Get_bool("fulldouble");

	// Valores padrão seguros para hardware antigo
	if (!sdl.desktop.full.width) sdl.desktop.full.width=640;
	if (!sdl.desktop.full.height) sdl.desktop.full.height=480;

	sdl.mouse.autoenable=section->Get_bool("autolock");
	if (!sdl.mouse.autoenable) SDL_ShowCursor(SDL_DISABLE);
	sdl.mouse.autolock=false;

	Prop_multival* p3 = section->Get_multival("sensitivity");
	sdl.mouse.xsensitivity = p3->GetSection()->Get_int("xsens");
	sdl.mouse.ysensitivity = p3->GetSection()->Get_int("ysens");

	std::string output=section->Get_string("output");

	// Surface como fallback seguro
	sdl.desktop.want_type=SCREEN_SURFACE;

#if C_OPENGL
	if (output == "opengl" || output == "openglnb") {
		sdl.desktop.want_type=SCREEN_OPENGL;
		sdl.opengl.bilinear = (output != "openglnb");
	} else if (output == "overlay") {
		sdl.desktop.want_type=SCREEN_OVERLAY;
	}
#else
	if (output == "overlay") {
		sdl.desktop.want_type=SCREEN_OVERLAY;
	}
#endif

	sdl.overlay=0;

#if C_OPENGL
	if (sdl.desktop.want_type == SCREEN_OPENGL) {
		sdl.surface = SDL_SetVideoMode_Wrap(640,400,0,SDL_OPENGL);
		if (sdl.surface == NULL) {
			// Fallback para surface se OpenGL falhar
			sdl.desktop.want_type = SCREEN_SURFACE;
		} else {
			sdl.opengl.buffer=0;
			sdl.opengl.framebuf=0;
			sdl.opengl.texture=0;
			sdl.opengl.displaylist=0;
			glGetIntegerv(GL_MAX_TEXTURE_SIZE, &sdl.opengl.max_texsize);
			// Limitar textura para performance
			if (sdl.opengl.max_texsize > MAX_TEX_SIZE)
				sdl.opengl.max_texsize = MAX_TEX_SIZE;

			glGenBuffersARB = (PFNGLGENBUFFERSARBPROC)SDL_GL_GetProcAddress("glGenBuffersARB");
			glBindBufferARB = (PFNGLBINDBUFFERARBPROC)SDL_GL_GetProcAddress("glBindBufferARB");
			glDeleteBuffersARB = (PFNGLDELETEBUFFERSARBPROC)SDL_GL_GetProcAddress("glDeleteBuffersARB");
			glBufferDataARB = (PFNGLBUFFERDATAARBPROC)SDL_GL_GetProcAddress("glBufferDataARB");
			glMapBufferARB = (PFNGLMAPBUFFERARBPROC)SDL_GL_GetProcAddress("glMapBufferARB");
			glUnmapBufferARB = (PFNGLUNMAPBUFFERARBPROC)SDL_GL_GetProcAddress("glUnmapBufferARB");
			sdl.opengl.packed_pixel = true;
			sdl.opengl.paletted_texture = false;
			sdl.opengl.pixel_buffer_object = (glGenBuffersARB && glBindBufferARB && glBufferDataARB && glMapBufferARB && glUnmapBufferARB);
		}
	}
#endif

	sdl.surface=SDL_SetVideoMode_Wrap(640,400,0,0);
	if (sdl.surface == NULL) E_Exit("Could not initialize video: %s",SDL_GetError());
	sdl.desktop.bpp=sdl.surface->format->BitsPerPixel;

	GFX_Stop();
	SDL_WM_SetCaption("Winbox",VERSION);

	MAPPER_AddHandler(KillSwitch,MK_f9,MMOD1,"shutdown","ShutDown");
	MAPPER_AddHandler(CaptureMouse,MK_f10,MMOD1,"capmouse","Cap Mouse");
	MAPPER_AddHandler(SwitchFullScreen,MK_return,MMOD2,"fullscr","Fullscreen");
	MAPPER_AddHandler(Restart,MK_home,MMOD1|MMOD2,"restart","Restart");
#if C_DEBUG
#else
	MAPPER_AddHandler(&PauseDOSBox, MK_pause, MMOD2, "pause", "Pause Winbox");
#endif

	SDLMod keystate = SDL_GetModState();
	if(keystate&KMOD_NUM) startup_state_numlock = true;
	if(keystate&KMOD_CAPS) startup_state_capslock = true;
}

void Mouse_AutoLock(bool enable) {
	sdl.mouse.autolock=enable;
	if (sdl.mouse.autoenable) sdl.mouse.requestlock=enable;
	else {
		SDL_ShowCursor(enable?SDL_DISABLE:SDL_ENABLE);
		sdl.mouse.requestlock=false;
	}
}

// Manipulação de mouse otimizada
static void HandleMouseMotion(SDL_MouseMotionEvent * motion) {
	if (sdl.mouse.locked || !sdl.mouse.autoenable)
		Mouse_CursorMoved((float)motion->xrel*sdl.mouse.xsensitivity/100.0f,
		                  (float)motion->yrel*sdl.mouse.ysensitivity/100.0f,
		                  (float)(motion->x-sdl.clip.x)/(sdl.clip.w-1)*sdl.mouse.xsensitivity/100.0f,
		                  (float)(motion->y-sdl.clip.y)/(sdl.clip.h-1)*sdl.mouse.ysensitivity/100.0f,
		                  sdl.mouse.locked);
}

static void HandleMouseButton(SDL_MouseButtonEvent * button) {
	switch (button->state) {
	case SDL_PRESSED:
		if (sdl.mouse.requestlock && !sdl.mouse.locked) {
			GFX_CaptureMouse();
			break;
		}
		if (!sdl.mouse.autoenable && sdl.mouse.autolock && button->button == SDL_BUTTON_MIDDLE) {
			GFX_CaptureMouse();
			break;
		}
		switch (button->button) {
		case SDL_BUTTON_LEFT:
			Mouse_ButtonPressed(0);
			break;
		case SDL_BUTTON_RIGHT:
			Mouse_ButtonPressed(1);
			break;
		case SDL_BUTTON_MIDDLE:
			Mouse_ButtonPressed(2);
			break;
		}
		break;
	case SDL_RELEASED:
		switch (button->button) {
		case SDL_BUTTON_LEFT:
			Mouse_ButtonReleased(0);
			break;
		case SDL_BUTTON_RIGHT:
			Mouse_ButtonReleased(1);
			break;
		case SDL_BUTTON_MIDDLE:
			Mouse_ButtonReleased(2);
			break;
		}
		break;
	}
}

void GFX_LosingFocus(void) {
	sdl.laltstate=SDL_KEYUP;
	sdl.raltstate=SDL_KEYUP;
	MAPPER_LosingFocus();
}

bool GFX_IsFullscreen(void) {
	return sdl.desktop.fullscreen;
}

// Declaração externa para MAPPER_CheckEvent
extern void MAPPER_CheckEvent(SDL_Event * event);
// Eventos otimizados para reduzir polling

void GFX_Events() {
	SDL_Event event;
	static int skip_counter = 0;

	// Processar eventos a cada 2 chamadas
	if (++skip_counter < 2) return;
	skip_counter = 0;

	int max_events = EVENT_BATCH_SIZE;
	static int mouse_skip = 0;

	while (max_events-- > 0 && SDL_PollEvent(&event)) {
		switch (event.type) {
		case SDL_ACTIVEEVENT:
			if (event.active.state & SDL_APPINPUTFOCUS) {
				if (event.active.gain) {
#ifdef WIN32
					if (!sdl.desktop.fullscreen) sdl.focus_ticks = GetTicks();
#endif
					if (sdl.desktop.fullscreen && !sdl.mouse.locked)
						GFX_CaptureMouse();
					SetPriority(sdl.priority.focus);
					CPU_Disable_SkipAutoAdjust();
				} else {
					if (sdl.mouse.locked) {
#ifdef WIN32
						if (sdl.desktop.fullscreen) {
							VGA_KillDrawing();
							GFX_ForceFullscreenExit();
						}
#endif
						GFX_CaptureMouse();
					}
					SetPriority(sdl.priority.nofocus);
					GFX_LosingFocus();
					CPU_Enable_SkipAutoAdjust();
				}
			}
			break;
		case SDL_MOUSEMOTION:
			if (++mouse_skip >= MOUSE_SKIP_FRAMES) {
				mouse_skip = 0;
				HandleMouseMotion(&event.motion);
			}
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			HandleMouseButton(&event.button);
			break;
		case SDL_QUIT:
			throw(0);
			break;
#ifdef WIN32
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			if (event.key.keysym.sym==SDLK_LALT) sdl.laltstate = event.key.type;
			if (event.key.keysym.sym==SDLK_RALT) sdl.raltstate = event.key.type;
			if (((event.key.keysym.sym==SDLK_TAB)) &&
			        ((sdl.laltstate==SDL_KEYDOWN) || (sdl.raltstate==SDL_KEYDOWN))) break;
			if (((event.key.keysym.sym == SDLK_TAB )) && (event.key.keysym.mod & KMOD_ALT)) break;
			if ((event.key.keysym.sym == SDLK_TAB) && (GetTicks() - sdl.focus_ticks < 2)) break;
#endif
		default:
			MAPPER_CheckEvent(&event);
		}
	}
}

static bool no_stdout = true;
void GFX_ShowMsg(char const* format,...) {
}

// Configuração otimizada
void Config_Add_SDL() {
	Section_prop * sdl_sec=control->AddSection_prop("sdl",&GUI_StartUp);
	sdl_sec->AddInitFunction(&MAPPER_StartUp);
	Prop_bool* Pbool;
	Prop_string* Pstring;
	Prop_int* Pint;
	Prop_multival* Pmulti;

	Pbool = sdl_sec->Add_bool("fullscreen",Property::Changeable::Always,false);
	//Pbool->Set_help("Start Winbox directly in fullscreen.");

	Pbool = sdl_sec->Add_bool("fulldouble",Property::Changeable::Always,false);
	//Pbool->Set_help("Use double buffering in fullscreen.");

	Pstring = sdl_sec->Add_string("fullresolution",Property::Changeable::Always,"original");
	//Pstring->Set_help("What resolution to use for fullscreen.");

	Pstring = sdl_sec->Add_string("windowresolution",Property::Changeable::Always,"original");
	//Pstring->Set_help("Scale the window to this size.");

	const char* outputs[] = {
		"surface", "overlay",
#if C_OPENGL
		"opengl", "openglnb",
#endif
		0
	};

	Pstring = sdl_sec->Add_string("output",Property::Changeable::Always,"openglnb");
	//Pstring->Set_help("What video system to use for output.");
	Pstring->Set_values(outputs);

	Pbool = sdl_sec->Add_bool("autolock",Property::Changeable::Always,true);
	//Pbool->Set_help("Mouse will automatically lock.");

	Pmulti = sdl_sec->Add_multi("sensitivity",Property::Changeable::Always, ",");
	Pmulti->SetValue("40");
	Pint = Pmulti->GetSection()->Add_int("xsens",Property::Changeable::Always,100);
	Pint->SetMinMax(-1000,1000);
	Pint = Pmulti->GetSection()->Add_int("ysens",Property::Changeable::Always,100);
	Pint->SetMinMax(-1000,1000);

	Pbool = sdl_sec->Add_bool("waitonerror",Property::Changeable::Always, true);
	//Pbool->Set_help("Wait before closing if Winbox has an error.");

	Pmulti = sdl_sec->Add_multi("priority", Property::Changeable::Always, ",");
	Pmulti->SetValue("higher,normal");
	//Pmulti->Set_help("Priority levels for Winbox.");

	const char* actt[] = { "lowest", "lower", "normal", "higher", "highest", "pause", 0};
	Pstring = Pmulti->GetSection()->Add_string("active",Property::Changeable::Always,"higher");
	Pstring->Set_values(actt);

	const char* inactt[] = { "lowest", "lower", "normal", "higher", "highest", "pause", 0};
	Pstring = Pmulti->GetSection()->Add_string("inactive",Property::Changeable::Always,"normal");
	Pstring->Set_values(inactt);

	Pstring = sdl_sec->Add_path("mapperfile",Property::Changeable::Always,MAPPERFILE);
	//Pstring->Set_help("File used to load/save the key/event mappings from.");
}

static void show_warning(char const * const message) {
	bool textonly = true;
#ifdef WIN32
	textonly = false;
	if ( !sdl.inited && SDL_Init(SDL_INIT_VIDEO) < 0 ) textonly = true;
	sdl.inited = true;
#endif
	printf("%s",message);
	if(textonly) return;
	if(!sdl.surface) sdl.surface = SDL_SetVideoMode_Wrap(640,400,0,0);
	if(!sdl.surface) return;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	Bit32u rmask = 0xff000000;
	Bit32u gmask = 0x00ff0000;
	Bit32u bmask = 0x0000ff00;
#else
	Bit32u rmask = 0x000000ff;
	Bit32u gmask = 0x0000ff00;
	Bit32u bmask = 0x00ff0000;
#endif
	SDL_Surface* splash_surf = SDL_CreateRGBSurface(
	                               SDL_SWSURFACE,
	                               640, 400,
	                               32,
	                               rmask, gmask, bmask, 0
	                           );

	if (!splash_surf) return;

	int x = 120,y = 20;
	std::string m(message),m2;
	std::string::size_type a,b,c,d;

	while(m.size()) {
		c = m.find('\n');
		d = m.rfind(' ',50);

		if(c>d) a=b=d;
		else a=b=c;

		if(a != std::string::npos)
			b++;

		m2 = m.substr(0,a);
		m.erase(0,b);

		OutputString(x,y,m2.c_str(),0xffffffff,0,splash_surf);
		y += 20;
	}

	SDL_BlitSurface(splash_surf, NULL, sdl.surface, NULL);
	SDL_Flip(sdl.surface);
	SDL_Delay(12000);

	SDL_FreeSurface(splash_surf);
	splash_surf = NULL;
}

static void launcheditor() {
	std::string path,file;
	Cross::CreatePlatformConfigDir(path);
	Cross::GetPlatformConfigName(file);
	path += file;
	FILE* f = fopen(path.c_str(),"r");
	if(!f && !control->PrintConfig(path.c_str())) {
		printf("tried creating %s. but failed.\n",path.c_str());
		exit(1);
	}
	if(f) fclose(f);
	std::string edit;
	while(control->cmdline->FindString("-editconf",edit,true))
		execlp(edit.c_str(),edit.c_str(),path.c_str(),(char*) 0);
	printf("can't find editor(s) specified at the command line.\n");
	exit(1);
}
#if C_DEBUG
extern void DEBUG_ShutDown(Section * /*sec*/);
#endif

void restart_program(std::vector<std::string> & parameters) {
	char** newargs = new char* [parameters.size() + 1];
	for(Bitu i = 0; i < parameters.size(); i++) newargs[i] = (char*)parameters[i].c_str();
	newargs[parameters.size()] = NULL;
	SDL_CloseAudio();
	SDL_Delay(50);
	SDL_Quit();
#if C_DEBUG
	DEBUG_ShutDown(NULL);
#endif

	if(execvp(newargs[0], newargs) == -1) {
#ifdef WIN32
		if(newargs[0][0] == '\"') {
			std::string edit = parameters[0];
			edit.erase(0,1);
			edit.erase(edit.length() - 1,1);
			if(execvp(edit.c_str(), newargs) == -1) E_Exit("Restarting failed");
		}
#endif
		E_Exit("Restarting failed");
	}
	delete [] newargs;
}
void Restart(bool pressed) {
	restart_program(control->startup_params);
}

static void init_yuv_lookup() {
	for (int i = 0; i < 256; i++) {
		yuv_lookup[i][0] = (9797 * i) >> 15;     // Y
		yuv_lookup[i][1] = (18492 * i) >> 15;    // U parcial
		yuv_lookup[i][2] = (23372 * i) >> 15;    // V parcial
	}
}

static void launchcaptures(std::string const& edit) {
	std::string path,file;
	Section* t = control->GetSection("Winbox");
	if(t) file = t->GetPropValue("captures");
	if(!t || file == NO_SUCH_PROPERTY) {
		printf("Config system messed up.\n");
		exit(1);
	}
	Cross::CreatePlatformConfigDir(path);
	path += file;
	Cross::CreateDir(path);
	struct stat cstat;
	if(stat(path.c_str(),&cstat) || (cstat.st_mode & S_IFDIR) == 0) {
		printf("%s doesn't exists or isn't a directory.\n",path.c_str());
		exit(1);
	}

	execlp(edit.c_str(),edit.c_str(),path.c_str(),(char*) 0);
	printf("can't find filemanager %s\n",edit.c_str());
	exit(1);
}

static void printconfiglocation() {
	std::string path,file;
	Cross::CreatePlatformConfigDir(path);
	Cross::GetPlatformConfigName(file);
	path += file;

	FILE* f = fopen(path.c_str(),"r");
	if(!f && !control->PrintConfig(path.c_str())) {
		printf("tried creating %s. but failed",path.c_str());
		exit(1);
	}
	if(f) fclose(f);
	printf("%s\n",path.c_str());
	exit(0);
}

static void eraseconfigfile() {
	FILE* f = fopen("Winbox.conf","r");
	if(f) {
		fclose(f);
		show_warning("Warning: Winbox.conf exists in current working directory.\nThis will override the configuration file at runtime.\n");
	}
	std::string path,file;
	Cross::GetPlatformConfigDir(path);
	Cross::GetPlatformConfigName(file);
	path += file;
	f = fopen(path.c_str(),"r");
	if(!f) exit(0);
	fclose(f);
	unlink(path.c_str());
	exit(0);
}

static void erasemapperfile() {
	FILE* g = fopen("Winbox.conf","r");
	if(g) {
		fclose(g);
		show_warning("Warning: Winbox.conf exists in current working directory.\nKeymapping might not be properly reset.\n"
		             "Please reset configuration as well and delete the Winbox.conf.\n");
	}

	std::string path,file=MAPPERFILE;
	Cross::GetPlatformConfigDir(path);
	path += file;
	FILE* f = fopen(path.c_str(),"r");
	if(!f) exit(0);
	fclose(f);
	unlink(path.c_str());
	exit(0);
}

void Disable_OS_Scaling() {
#if defined (WIN32)
	typedef BOOL (*function_set_dpi_pointer)();
	function_set_dpi_pointer function_set_dpi;
	function_set_dpi = (function_set_dpi_pointer) GetProcAddress(LoadLibrary("user32.dll"), "SetProcessDPIAware");
	if (function_set_dpi) {
		function_set_dpi();
	}
#endif
}

#ifdef OS2
void os2_exit()
{
	PPIB pib;
	PTIB tib;

	SDL_WM_GrabInput(SDL_GRAB_OFF);
	SDL_ShowCursor(SDL_ENABLE);

	SDL_Quit();
	DosGetInfoBlocks(&tib, &pib);
	if (pib->pib_ultype == 3)
		pib->pib_ultype = 2;

	exit(-1);
}
#endif

// Função principal otimizada
int main(int argc, char* argv[]) {
#ifdef WIN32
    fclose(stdout);
    fclose(stderr);
#endif


	try {
		Disable_OS_Scaling();

		CommandLine com_line(argc,argv);
		Config myconf(&com_line);
		control=&myconf;
		Config_Add_SDL();
		DOSBOX_Init();

		std::string editor;
		if(control->cmdline->FindString("-editconf",editor,false)) launcheditor();
		if(control->cmdline->FindString("-opencaptures",editor,true)) launchcaptures(editor);
		if(control->cmdline->FindExist("-eraseconf")) eraseconfigfile();
		if(control->cmdline->FindExist("-resetconf")) eraseconfigfile();
		if(control->cmdline->FindExist("-erasemapper")) erasemapperfile();
		if(control->cmdline->FindExist("-resetmapper")) erasemapperfile();



#if SDL_VERSION_ATLEAST(1, 2, 14)
		putenv(const_cast<char*>("SDL_DISABLE_LOCK_KEYS=1"));
#endif
		// Inicialização SDL mínima

		if ( SDL_Init( SDL_INIT_VIDEO ) < 0 )
			E_Exit("Can't init SDL %s",SDL_GetError());
		sdl.inited = true;

#ifndef DISABLE_JOYSTICK
#endif
		sdl.laltstate = SDL_KEYUP;
		sdl.raltstate = SDL_KEYUP;

#if defined (WIN32)
		sdl.using_windib=true;
		char sdl_drv_name[128];
		if (getenv("SDL_VIDEODRIVER")==NULL) {
			if (SDL_VideoDriverName(sdl_drv_name,128)!=NULL) {
				sdl.using_windib=false;
				if (strcmp(sdl_drv_name,"directx")!=0) {
					SDL_QuitSubSystem(SDL_INIT_VIDEO);
					putenv("SDL_VIDEODRIVER=directx");
					if (SDL_InitSubSystem(SDL_INIT_VIDEO)<0) {
						putenv("SDL_VIDEODRIVER=windib");
						if (SDL_InitSubSystem(SDL_INIT_VIDEO)<0)
							E_Exit("Can't init SDL Video %s",SDL_GetError());
						sdl.using_windib=true;
					}
				}
			}
		} else {
			char* sdl_videodrv = getenv("SDL_VIDEODRIVER");
			sdl.using_windib = (strcmp(sdl_videodrv,"windib")==0);
		}
#endif
		// Configuração OpenGL mínima para GeForce 7300GT
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
		glide.fullscreen = &sdl.desktop.fullscreen;
		sdl.num_joysticks=SDL_NumJoysticks();

		std::string config_file, config_path, config_combined;
		Cross::GetPlatformConfigDir(config_path);

		if(control->cmdline->FindExist("-userconf",true)) {
			config_file.clear();
			Cross::GetPlatformConfigDir(config_path);
			Cross::GetPlatformConfigName(config_file);
			config_combined = config_path + config_file;
			control->ParseConfigFile(config_combined.c_str());
			if(!control->configfiles.size()) {
				config_file.clear();
				Cross::CreatePlatformConfigDir(config_path);
				Cross::GetPlatformConfigName(config_file);
				config_combined = config_path + config_file;
				if(control->PrintConfig(config_combined.c_str())) {
					control->ParseConfigFile(config_combined.c_str());
				}
			}
		}

		while(control->cmdline->FindString("-conf",config_file,true)) {
			if (!control->ParseConfigFile(config_file.c_str())) {
				if (!control->ParseConfigFile((config_path + config_file).c_str())) {
				}
			}
		}
		if(!control->configfiles.size()) control->ParseConfigFile("Winbox.conf");

		if(!control->configfiles.size()) {
			config_file.clear();
			Cross::GetPlatformConfigName(config_file);
			control->ParseConfigFile((config_path + config_file).c_str());
		}

		if(!control->configfiles.size()) {
			config_file.clear();
			Cross::CreatePlatformConfigDir(config_path);
			Cross::GetPlatformConfigName(config_file);
			config_combined = config_path + config_file;
			if(control->PrintConfig(config_combined.c_str())) {
				control->ParseConfigFile(config_combined.c_str());
			}
		}

#if (ENVIRON_LINKED)
		control->ParseEnv(environ);
#endif
		control->Init();
		Section_prop * sdl_sec=static_cast<Section_prop *>(control->GetSection("sdl"));

		if (control->cmdline->FindExist("-fullscreen") || sdl_sec->Get_bool("fullscreen")) {
			if(!sdl.desktop.fullscreen) {
				GFX_SwitchFullScreen();
			}
		}

		MAPPER_Init();
		if (control->cmdline->FindExist("-startmapper")) MAPPER_RunInternal();
		control->StartUp();
	} catch (char * error) {
#if defined (WIN32)
		sticky_keys(true);
#endif
		GFX_ShowMsg("Exit to error: %s",error);
		fflush(NULL);
		if(sdl.wait_on_error) {
#if defined(WIN32)
			Sleep(5000);
#endif
		}
	}
	catch (int) {
		;
	}
	catch(...) {
		;
	}
#if defined (WIN32)
	sticky_keys(true);
#endif
	SDL_WM_GrabInput(SDL_GRAB_OFF);
	SDL_ShowCursor(SDL_ENABLE);

	SDL_Quit();
#ifdef OS2
	DosGetInfoBlocks(&tib, &pib);
	if (pib->pib_ultype == 3)
		pib->pib_ultype = 2;
	exit(0);
#else
	return 0;
#endif
}

void GFX_GetSize(int &width, int &height, bool &fullscreen) {
	width = sdl.draw.width;
	height = sdl.draw.height;
	fullscreen = sdl.desktop.fullscreen;
}

bool OpenGL_using(void) {
	return (sdl.desktop.want_type==SCREEN_OPENGL);
}