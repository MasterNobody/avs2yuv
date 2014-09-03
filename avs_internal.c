/*****************************************************************************
 * avs.c: avisynth input
 *****************************************************************************
 * Copyright (C) 2009-2011 x264 project
 *
 * Authors: Steven Walters <kemuri9@gmail.com>
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
 *
 * This program is also available under a commercial proprietary license.
 * For more information, contact us at licensing@x264.com.
 *****************************************************************************/

#include <windows.h>

#define AVSC_NO_DECLSPEC
#undef EXTERN_C
#include "avisynth_c.h"
#define AVSC_DECLARE_FUNC(name) name##_func name

/* AVS uses a versioned interface to control backwards compatibility */
/* YV12 support is required, which was added in 2.5 */
#define AVS_INTERFACE_25 2

#define LOAD_AVS_FUNC(name, continue_on_fail)\
{\
    h->func.name = (void*)GetProcAddress( h->library, #name );\
    if( !continue_on_fail && !h->func.name )\
        goto fail;\
}

typedef struct
{
    AVS_Clip *clip;
    AVS_ScriptEnvironment *env;
    HMODULE library;
    struct
    {
        AVSC_DECLARE_FUNC( avs_clip_get_error );
        AVSC_DECLARE_FUNC( avs_create_script_environment );
        AVSC_DECLARE_FUNC( avs_delete_script_environment );
        AVSC_DECLARE_FUNC( avs_get_error );
        AVSC_DECLARE_FUNC( avs_get_frame );
        AVSC_DECLARE_FUNC( avs_get_video_info );
        AVSC_DECLARE_FUNC( avs_function_exists );
        AVSC_DECLARE_FUNC( avs_invoke );
        AVSC_DECLARE_FUNC( avs_release_clip );
        AVSC_DECLARE_FUNC( avs_release_value );
        AVSC_DECLARE_FUNC( avs_release_video_frame );
        AVSC_DECLARE_FUNC( avs_take_clip );
    } func;
} avs_hnd_t;

/* load the library and functions we require from it */
static int internal_avs_load_library( avs_hnd_t *h )
{
    h->library = LoadLibrary( "avisynth" );
    if( !h->library )
        return -1;
    LOAD_AVS_FUNC( avs_clip_get_error, 0 );
    LOAD_AVS_FUNC( avs_create_script_environment, 0 );
    LOAD_AVS_FUNC( avs_delete_script_environment, 1 );
    LOAD_AVS_FUNC( avs_get_error, 1 );
    LOAD_AVS_FUNC( avs_get_frame, 0 );
    LOAD_AVS_FUNC( avs_get_video_info, 0 );
    LOAD_AVS_FUNC( avs_function_exists, 0 );
    LOAD_AVS_FUNC( avs_invoke, 0 );
    LOAD_AVS_FUNC( avs_release_clip, 0 );
    LOAD_AVS_FUNC( avs_release_value, 0 );
    LOAD_AVS_FUNC( avs_release_video_frame, 0 );
    LOAD_AVS_FUNC( avs_take_clip, 0 );
    return 0;
fail:
    FreeLibrary( h->library );
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
    h->func.avs_release_clip( h->clip );
    if( h->func.avs_delete_script_environment )
        h->func.avs_delete_script_environment( h->env );
    FreeLibrary( h->library );
    return 0;
}
