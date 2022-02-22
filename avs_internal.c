/*****************************************************************************
 * avs_internal.c: avisynth input
 *****************************************************************************
 * Copyright (C) 2011-2022 avs2yuv project
 *
 * Authors: Anton Mitrofanov <BugMaster@narod.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *****************************************************************************/

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#define avs_open() LoadLibraryW( L"avisynth" )
#define avs_close FreeLibrary
#define avs_address GetProcAddress
#else
#include <dlfcn.h>
#if defined(__APPLE__)
#define avs_open() dlopen( "libavisynth.dylib", RTLD_NOW )
#else
#define avs_open() dlopen( "libavisynth.so", RTLD_NOW )
#endif
#define avs_close dlclose
#define avs_address dlsym
#endif

#define AVSC_NO_DECLSPEC
#undef EXTERN_C
#include "avisynth_c.h"
#define AVSC_DECLARE_FUNC(name) name##_func name

/* AVS uses a versioned interface to control backwards compatibility */
/* YV12 support is required, which was added in 2.5 */
#define AVS_INTERFACE_25 2

#define LOAD_AVS_FUNC(name, continue_on_fail)\
{\
    h->func.name = (void*)avs_address( h->library, #name );\
    if( !continue_on_fail && !h->func.name )\
        goto fail;\
}

#define LOAD_AVS_FUNC_ALIAS(name, alias, continue_on_fail)\
{\
    if( !h->func.name )\
        h->func.name = (void*)avs_address( h->library, alias );\
    if( !continue_on_fail && !h->func.name )\
        goto fail;\
}

typedef struct
{
    AVS_Clip *clip;
    AVS_ScriptEnvironment *env;
    void *library;
    struct
    {
        AVSC_DECLARE_FUNC( avs_clip_get_error );
        AVSC_DECLARE_FUNC( avs_create_script_environment );
        AVSC_DECLARE_FUNC( avs_delete_script_environment );
        AVSC_DECLARE_FUNC( avs_get_error );
        AVSC_DECLARE_FUNC( avs_get_frame );
        AVSC_DECLARE_FUNC( avs_get_video_info );
        AVSC_DECLARE_FUNC( avs_invoke );
        AVSC_DECLARE_FUNC( avs_release_clip );
        AVSC_DECLARE_FUNC( avs_release_value );
        AVSC_DECLARE_FUNC( avs_release_video_frame );
        AVSC_DECLARE_FUNC( avs_take_clip );
        AVSC_DECLARE_FUNC( avs_is_yv24 );
        AVSC_DECLARE_FUNC( avs_is_yv16 );
        AVSC_DECLARE_FUNC( avs_is_yv12 );
        AVSC_DECLARE_FUNC( avs_is_y8 );
        AVSC_DECLARE_FUNC( avs_get_pitch_p );
        AVSC_DECLARE_FUNC( avs_get_read_ptr_p );
        // AviSynth+ extension
        AVSC_DECLARE_FUNC( avs_is_444 );
        AVSC_DECLARE_FUNC( avs_is_422 );
        AVSC_DECLARE_FUNC( avs_is_420 );
        AVSC_DECLARE_FUNC( avs_is_y );
        AVSC_DECLARE_FUNC( avs_component_size );
        AVSC_DECLARE_FUNC( avs_bits_per_component );
    } func;
} avs_hnd_t;

/* load the library and functions we require from it */
static int internal_avs_load_library( avs_hnd_t *h )
{
    h->library = avs_open();
    if( !h->library )
        return -1;
    LOAD_AVS_FUNC( avs_clip_get_error, 0 );
    LOAD_AVS_FUNC( avs_create_script_environment, 0 );
    LOAD_AVS_FUNC( avs_delete_script_environment, 1 );
    LOAD_AVS_FUNC( avs_get_error, 1 );
    LOAD_AVS_FUNC( avs_get_frame, 0 );
    LOAD_AVS_FUNC( avs_get_video_info, 0 );
    LOAD_AVS_FUNC( avs_invoke, 0 );
    LOAD_AVS_FUNC( avs_release_clip, 0 );
    LOAD_AVS_FUNC( avs_release_value, 0 );
    LOAD_AVS_FUNC( avs_release_video_frame, 0 );
    LOAD_AVS_FUNC( avs_take_clip, 0 );
    LOAD_AVS_FUNC( avs_is_yv24, 1 );
    LOAD_AVS_FUNC( avs_is_yv16, 1 );
    LOAD_AVS_FUNC( avs_is_yv12, 1 );
    LOAD_AVS_FUNC( avs_is_y8, 1 );
    LOAD_AVS_FUNC( avs_get_pitch_p, 1 );
    LOAD_AVS_FUNC( avs_get_read_ptr_p, 1 );
    // AviSynth+ extension
    LOAD_AVS_FUNC( avs_is_444, 1 );
    LOAD_AVS_FUNC( avs_is_422, 1 );
    LOAD_AVS_FUNC( avs_is_420, 1 );
    LOAD_AVS_FUNC( avs_is_y, 1 );
    LOAD_AVS_FUNC( avs_component_size, 1 );
    LOAD_AVS_FUNC( avs_bits_per_component, 1 );
    return 0;
fail:
    avs_close( h->library );
    h->library = NULL;
    return -1;
}

static AVS_Value internal_avs_update_clip( avs_hnd_t *h, const AVS_VideoInfo **vi, AVS_Value res, AVS_Value release )
{
    h->func.avs_release_clip( h->clip );
    h->clip = h->func.avs_take_clip( res, h->env );
    h->func.avs_release_value( release );
    *vi = h->func.avs_get_video_info( h->clip );
    return res;
}

static int internal_avs_close_library( avs_hnd_t *h )
{
    if( h->func.avs_release_clip && h->clip )
        h->func.avs_release_clip( h->clip );
    if( h->func.avs_delete_script_environment && h->env )
        h->func.avs_delete_script_environment( h->env );
    if( h->library )
        avs_close( h->library );
    return 0;
}
