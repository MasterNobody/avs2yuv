// Avs2YUV by Loren Merritt

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include "avs_internal.c"

#ifndef INT_MAX
#define INT_MAX 0x7fffffff
#endif

#define MY_VERSION "Avs2YUV 0.24"
#define MAX_FH 10

int main(int argc, const char* argv[])
{
    const char* infile = NULL;
    const char* hfyufile = NULL;
    const char* outfile[MAX_FH] = {NULL};
    int         y4m_headers[MAX_FH] = {0};
    FILE* out_fh[10] = {NULL};
    int out_fhs = 0;
    int verbose = 0;
    int usage = 0;
    int seek = 0;
    int end = 0;
    int slave = 0;
    int rawyuv = 0;
    int frm = -1;
    int write_target = 0; // how many bytes per frame we expect to write
    int i;

    for(i=1; i<argc; i++) {
        if(argv[i][0] == '-' && argv[i][1] != 0) {
            if(!strcmp(argv[i], "-v"))
                verbose = 1;
            else if(!strcmp(argv[i], "-h"))
                usage = 1;
            else if(!strcmp(argv[i], "-o")) {
                if(i > argc-2) {
                    fprintf(stderr, "-o needs an argument\n");
                    return 2;
                }
                i++;
                goto add_outfile;
            } else if(!strcmp(argv[i], "-seek")) {
                if(i > argc-2) {
                    fprintf(stderr, "-seek needs an argument\n");
                    return 2;
                }
                seek = atoi(argv[++i]);
                if(seek < 0) usage = 1;
            } else if(!strcmp(argv[i], "-frames")) {
                if(i > argc-2) {
                    fprintf(stderr, "-frames needs an argument\n");
                    return 2;
                }
                end = atoi(argv[++i]);
            } else if(!strcmp(argv[i], "-hfyu")) {
                if(i > argc-2) {
                    fprintf(stderr, "-hfyu needs an argument\n");
                    return 2;
                }
                hfyufile = argv[++i];
            } else if(!strcmp(argv[i], "-raw")) {
                rawyuv = 1;
            } else if(!strcmp(argv[i], "-slave")) {
                slave = 1;
            } else {
                fprintf(stderr, "no such option: %s\n", argv[i]);
                return 2;
            }
        } else if(!infile) {
            infile = argv[i];
            const char *dot = strrchr(infile, '.');
            if(!dot || strcmp(".avs", dot))
                fprintf(stderr, "infile (%s) doesn't look like an avisynth script\n", infile);
        } else {
add_outfile:
            if(out_fhs > MAX_FH-1) {
                fprintf(stderr, "too many output file\n");
                return 2;
            }
            outfile[out_fhs] = argv[i];
            y4m_headers[out_fhs] = !rawyuv;
            out_fhs++;
        }
    }

    if(usage || !infile || (!out_fhs && !hfyufile && !verbose)) {
        fprintf(stderr, MY_VERSION "\n"
        "Usage: avs2yuv [options] in.avs [-o out.y4m] [-o out2.y4m] [-hfyu out.avi]\n"
        "-v\tprint the frame number after processing each frame\n"
        "-seek\tseek to the given frame number\n"
        "-frames\tstop after processing this many frames\n"
        "-slave\tread a list of frame numbers from stdin (one per line)\n"
        "-raw\toutputs raw I420 instead of yuv4mpeg\n"
        "The outfile may be \"-\", meaning stdout.\n"
        "Output format is yuv4mpeg, as used by MPlayer and mjpegtools\n"
        "Huffyuv output requires MEncoder, and probably doesn't work in Wine.\n"
        );
        return 2;
    }

    int retval = 1;
    avs_hnd_t avs_h = {0};
    if(internal_avs_load_library(&avs_h) < 0) {
        fprintf(stderr, "failed to load avisynth.dll\n");
        goto fail;
    }

    avs_h.env = avs_h.func.avs_create_script_environment(AVS_INTERFACE_25);
    if(avs_h.func.avs_get_error) {
        const char *error = avs_h.func.avs_get_error(avs_h.env);
        if(error) {
            fprintf(stderr, "%s\n", error);
            goto fail;
        }
    }

    AVS_Value arg = avs_new_value_string(infile);
    AVS_Value res = avs_h.func.avs_invoke(avs_h.env, "Import", arg, NULL);
    if(avs_is_error(res)) {
        fprintf(stderr, "%s\n", avs_as_string(res));
        goto fail;
    }
    /* check if the user is using a multi-threaded script and apply distributor if necessary.
       adapted from avisynth's vfw interface */
    AVS_Value mt_test = avs_h.func.avs_invoke(avs_h.env, "GetMTMode", avs_new_value_bool(0), NULL);
    int mt_mode = avs_is_int(mt_test) ? avs_as_int(mt_test) : 0;
    avs_h.func.avs_release_value(mt_test);
    if( mt_mode > 0 && mt_mode < 5 )
    {
        AVS_Value temp = avs_h.func.avs_invoke(avs_h.env, "Distributor", res, NULL);
        avs_h.func.avs_release_value(res);
        res = temp;
    }
    if(!avs_is_clip(res)) {
        fprintf(stderr, "Error: '%s' didn't return a video clip.\n", infile);
        goto fail;
    }
    avs_h.clip = avs_h.func.avs_take_clip(res, avs_h.env);
    const AVS_VideoInfo *inf = avs_h.func.avs_get_video_info(avs_h.clip);
    if(!avs_has_video(inf)) {
        fprintf(stderr, "Error: '%s' has no video data\n", infile);
        goto fail;
    }

    fprintf(stderr, "%s: %dx%d, ", infile, inf->width, inf->height);
    if(inf->fps_denominator == 1)
        fprintf(stderr, "%d fps, ", inf->fps_numerator);
    else
        fprintf(stderr, "%d/%d fps, ", inf->fps_numerator, inf->fps_denominator);
    fprintf(stderr, "%d frames\n", inf->num_frames);

    if(!avs_is_yv12(inf)) {
        fprintf(stderr, "converting %s -> YV12\n", avs_is_yuy2(inf) ? "YUY2" : avs_is_rgb(inf) ? "RGB" : "?");
        AVS_Value res2 = avs_h.func.avs_invoke(avs_h.env, "ConvertToYV12", res, NULL);
        if(avs_is_error(res2)) {
            fprintf(stderr, "Couldn't convert input clip to YV12\n");
            goto fail;
        }
        res = internal_avs_update_clip(&avs_h, &inf, res2, res);
    }
    if(!avs_is_yv12(inf)) {
        fprintf(stderr, "Couldn't convert input to YV12\n");
        goto fail;
    }
    if(avs_is_field_based(inf)) {
        fprintf(stderr, "Needs progressive input\n");
        goto fail;
    }
    avs_h.func.avs_release_value(res);

    for(i=0; i<out_fhs; i++) {
        if(!strcmp(outfile[i], "-")) {
            for(int j=0; j<i; j++)
                if(out_fh[j] == stdout) {
                    fprintf(stderr, "can't write to stdout multiple times\n");
                    goto fail;
                }
            int dupout = _dup(_fileno(stdout));
            fclose(stdout);
            _setmode(dupout, _O_BINARY);
            out_fh[i] = _fdopen(dupout, "wb");
        } else {
            out_fh[i] = fopen(outfile[i], "wb");
            if(!out_fh[i]) {
                fprintf(stderr, "fopen(\"%s\") failed", outfile[i]);
                goto fail;
            }
        }
    }
    if(hfyufile) {
        char *cmd = malloc(100+strlen(hfyufile));
        if(!cmd) {
           fprintf(stderr, "malloc failed");
           goto fail;
        }
        sprintf(cmd, "mencoder - -o \"%s\" -quiet -ovc lavc -lavcopts vcodec=ffvhuff:vstrict=-1:pred=2:context=1", hfyufile);
        out_fh[out_fhs] = _popen(cmd, "wb");
        free(cmd);
        if(!out_fh[out_fhs]) {
            fprintf(stderr, "failed to exec mencoder\n");
            goto fail;
        }
        y4m_headers[out_fhs] = 1;
        out_fhs++;
    }

    for(i=0; i<out_fhs; i++) {
        if(!y4m_headers[i])
            continue;
        fprintf(out_fh[i], "YUV4MPEG2 W%d H%d F%u:%u Ip A0:0\n",
            inf->width, inf->height, inf->fps_numerator, inf->fps_denominator);
        fflush(out_fh[i]);
    }

    write_target = out_fhs*inf->width*inf->height*3/2;

    if(slave) {
        seek = 0;
        end = INT_MAX;
    } else {
        end += seek;
        if(end <= seek || end > inf->num_frames)
            end = inf->num_frames;
    }

    for(frm = seek; frm < end; ++frm) {
        if(slave) {
            char input[80];
            frm = -1;
            do {
                if(!fgets(input, 80, stdin))
                    goto close_files;
                sscanf(input, "%d", &frm);
            } while(frm < 0);
            if(frm >= inf->num_frames)
                frm = inf->num_frames-1;
        }

        AVS_VideoFrame *f = avs_h.func.avs_get_frame(avs_h.clip, frm); 
        const char *err = avs_h.func.avs_clip_get_error(avs_h.clip);
        if(err) {
            fprintf(stderr, "%s occurred while reading frame %d\n", err, frm);
            goto fail;
        }

        if(out_fhs) {
            static const int planes[] = {AVS_PLANAR_Y, AVS_PLANAR_U, AVS_PLANAR_V};
            int wrote = 0;

            for(i=0; i<out_fhs; i++)
                if(y4m_headers[i])
                    fwrite("FRAME\n", 1, 6, out_fh[i]);

            for(int p=0; p<3; p++) {
                int w = inf->width  >> (p ? 1 : 0);
                int h = inf->height >> (p ? 1 : 0);
                int pitch = avs_get_pitch_p(f, planes[p]);
                const BYTE* data = avs_get_read_ptr_p(f, planes[p]);
                int y;
                for(y=0; y<h; y++) {
                    for(i=0; i<out_fhs; i++)
                        wrote += fwrite(data, 1, w, out_fh[i]);
                    data += pitch;
                }
            }
            if(wrote != write_target) {
                fprintf(stderr, "Output error: wrote only %d of %d bytes\n", wrote, write_target);
                goto fail;
            }
            if(slave) { // assume timing doesn't matter in other modes
                for(i=0; i<out_fhs; i++)
                    fflush(out_fh[i]);
            }
        }

        if(verbose)
            fprintf(stderr, "%d\n", frm);

        avs_h.func.avs_release_video_frame(f);
    }

close_files:
    retval = 0;
fail:
    if(hfyufile) {
        if(out_fh[out_fhs-1])
            _pclose(out_fh[out_fhs-1]);
        out_fhs--;
    }
    for(i=0; i<out_fhs; i++)
        if(out_fh[i])
            fclose(out_fh[i]);
    if(avs_h.library)
        internal_avs_close_library(&avs_h);
    return retval;
}
