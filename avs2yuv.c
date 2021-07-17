// Avs2YUV by Loren Merritt

// Contributors: Anton Mitrofanov (aka BugMaster)
//               Oka Motofumi (aka Chikuzen)

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include <sys/timeb.h>
#include "avs_internal.c"

#ifndef INT_MAX
#define INT_MAX 0x7fffffff
#endif

#ifdef _MSC_VER
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define snprintf _snprintf
#else
#include <strings.h>
#endif

#define MY_VERSION "Avs2YUV 0.24bm6"
#define MAX_FH 10
#define AVS_BUFSIZE (128*1024)
#define CSP_AUTO (-1)
#define CSP_I400 1
#define CSP_I420 2
#define CSP_I422 3
#define CSP_I444 4

static int csp_to_int(const char *arg)
{
    if(!strcasecmp(arg, "auto"))
        return CSP_AUTO;
    if(!strcasecmp(arg, "i400"))
        return CSP_I400;
    if(!strcasecmp(arg, "i420"))
        return CSP_I420;
    if(!strcasecmp(arg, "i422"))
        return CSP_I422;
    if(!strcasecmp(arg, "i444"))
        return CSP_I444;
    return 0;
}

inline long get_date() {
    struct timeb tb;
    ftime( &tb );
    return ((long)tb.time * 1000 + (long)tb.millitm) * 1000;
}

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
    int no_mt = 0;
    int interlaced = 0;
    int tff = 0;
    int csp = CSP_I420;
    int input_depth = 8;
    int showfps = 0;
    unsigned fps_num = 0;
    unsigned fps_den = 0;
    unsigned par_width = 0;
    unsigned par_height = 0;

    for(int i = 1; i < argc; i++) {
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
            } else if(!strcmp(argv[i], "-no-mt")) {
                no_mt = 1;
            } else if(!strcmp(argv[i], "-csp")) {
                if(i > argc-2) {
                    fprintf(stderr, "-csp needs an argument\n");
                    return 2;
                }
                csp = csp_to_int(argv[++i]);
                if(!csp) {
                    fprintf(stderr, "-csp \"%s\" is unknown\n", argv[i]);
                    return 2;
                }
            } else if(!strcmp(argv[i], "-depth")) {
                if(i > argc-2) {
                    fprintf(stderr, "-depth needs an argument\n");
                    return 2;
                }
                input_depth = atoi(argv[++i]);
                if(input_depth < 8 || input_depth > 16) {
                    fprintf(stderr, "-depth \"%s\" is not supported\n", argv[i]);
                    return 2;
                }
            } else if(!strcmp(argv[i], "-fps")) {
                if(i > argc-2) {
                    fprintf(stderr, "-fps needs an argument\n");
                    return 2;
                }
                const char *str = argv[++i];
                if(sscanf(str, "%u/%u", &fps_num, &fps_den) != 2) {
                    char *str_end;
                    double fps = strtod(str, &str_end);
                    if(str_end == str || *str_end != '\0' || fps <= 0 || fps > INT_MAX)
                        fps_num = 0;
                    else if(fps <= INT_MAX/1000) {
                        fps_num = (unsigned)(fps * 1000 + .5);
                        fps_den = 1000;
                    } else {
                        fps_num = (unsigned)(fps + .5);
                        fps_den = 1;
                    }
                }
                if(!fps_num || !fps_den) {
                    fprintf(stderr, "-fps \"%s\" is not supported\n", str);
                    return 2;
                }
            } else if(!strcmp(argv[i], "-par")) {
                if(i > argc-2) {
                    fprintf(stderr, "-par needs an argument\n");
                    return 2;
                }
                const char *str = argv[++i];
                if( (sscanf(str, "%u:%u", &par_width, &par_height) != 2) &&
                    (sscanf(str, "%u/%u", &par_width, &par_height) != 2) )
                {
                    fprintf(stderr, "-par \"%s\" is not supported\n", str);
                    return 2;
                }
            } else if(!strcmp(argv[i], "-showfps")) {
                showfps = 1;
            } else {
                fprintf(stderr, "no such option: %s\n", argv[i]);
                return 2;
            }
        } else if(!infile) {
            infile = argv[i];
            const char *dot = strrchr(infile, '.');
            if(!dot || strcmp(".avs", dot))
                fprintf(stderr, "infile \"%s\" doesn't look like an avisynth script\n", infile);
        } else {
add_outfile:
            if(out_fhs > MAX_FH-1) {
                fprintf(stderr, "too many output files\n");
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
        "-v\t\tprint the frame number after processing each frame (overrides -showfps)\n"
        "-showfps\tshow script execution speed in fps\n"
        "-seek\t\tseek to the given frame number\n"
        "-frames\t\tstop after processing this many frames\n"
        "-slave\t\tread a list of frame numbers from stdin (one per line)\n"
        "-no-mt\t\tdisable detection of AviSynth MT which adds Distributor()\n"
        "-raw\t\toutput raw I400/I420/I422/I444 instead of yuv4mpeg\n"
        "-csp\t\tconvert to I400/I420/I422/I444 or AUTO colorspace (default I420)\n"
        "-depth\t\tspecify input bit depth (default 8)\n"
        "-fps\t\toverwrite input framerate\n"
        "-par\t\tspecify pixel aspect ratio\n"
        "The outfile may be \"-\", meaning stdout.\n"
        "Output format is yuv4mpeg, as used by MPlayer, FFmpeg, Libav, x264, mjpegtools.\n"
        "Huffyuv output requires ffmpeg, and probably doesn't work in Wine.\n"
        );
        return 2;
    }

    int retval = 1;
    avs_hnd_t avs_h = {0};
    if(internal_avs_load_library(&avs_h) < 0) {
        fprintf(stderr, "error: failed to load avisynth.dll\n");
        goto fail;
    }

    avs_h.env = avs_h.func.avs_create_script_environment(AVS_INTERFACE_25);
    if(avs_h.func.avs_get_error) {
        const char *error = avs_h.func.avs_get_error(avs_h.env);
        if(error) {
            fprintf(stderr, "error: %s\n", error);
            goto fail;
        }
    }

    AVS_Value arg = avs_new_value_string(infile);
    AVS_Value res = avs_h.func.avs_invoke(avs_h.env, "Import", arg, NULL);
    if(avs_is_error(res)) {
        fprintf(stderr, "error: %s\n", avs_as_error(res));
        goto fail;
    }
    if(!no_mt) {
        /* check if the user is using a multi-threaded script and apply distributor if necessary.
           adapted from avisynth's vfw interface */
        AVS_Value mt_test = avs_h.func.avs_invoke(avs_h.env, "GetMTMode", avs_new_value_bool(0), NULL);
        int mt_mode = avs_is_int(mt_test) ? avs_as_int(mt_test) : 0;
        avs_h.func.avs_release_value(mt_test);
        if( mt_mode > 0 && mt_mode < 5 ) {
            AVS_Value temp = avs_h.func.avs_invoke(avs_h.env, "Distributor", res, NULL);
            avs_h.func.avs_release_value(res);
            res = temp;
        }
    }
    if(!avs_is_clip(res)) {
        fprintf(stderr, "error: \"%s\" didn't return a video clip\n", infile);
        goto fail;
    }
    avs_h.clip = avs_h.func.avs_take_clip(res, avs_h.env);
    const AVS_VideoInfo *inf = avs_h.func.avs_get_video_info(avs_h.clip);
    if(!avs_has_video(inf)) {
        fprintf(stderr, "error: \"%s\" has no video data\n", infile);
        goto fail;
    }
    /* if the clip is made of fields instead of frames, call weave to make them frames */
    if(avs_is_field_based(inf)) {
        fprintf(stderr, "detected fieldbased (separated) input, weaving to frames\n");
        AVS_Value tmp = avs_h.func.avs_invoke(avs_h.env, "Weave", res, NULL);
        if(avs_is_error(tmp)) {
            fprintf(stderr, "error: couldn't weave fields into frames: %s\n", avs_as_error(tmp));
            goto fail;
        }
        res = internal_avs_update_clip(&avs_h, &inf, tmp, res);
        interlaced = 1;
        tff = avs_is_tff(inf);
    }

    int component_size = avs_h.func.avs_component_size ? avs_h.func.avs_component_size(inf) : 1;
    int bits_per_component = avs_h.func.avs_bits_per_component ? avs_h.func.avs_bits_per_component(inf) : 8 * component_size;
    int input_width  = inf->width;
    int input_height = inf->height;
    int is_16bit_hack = 0;
    if(bits_per_component > 8) {
        input_depth = bits_per_component;
    } else if(input_depth > 8) {
        if(input_width & 3) {
            fprintf(stderr, "avisynth 16-bit hack requires that width is at least mod4\n");
            goto fail;
        }
        fprintf(stderr, "avisynth 16-bit hack enabled\n");
        is_16bit_hack = 1;
        input_width >>= 1;
    }
    if(!fps_num || !fps_den) {
        fps_num = inf->fps_numerator;
        fps_den = inf->fps_denominator;
    }

    AVS_Value pixel_type = avs_h.func.avs_invoke(avs_h.env, "PixelType", res, NULL);
    const char *pixel_type_name = avs_is_string(pixel_type) ? avs_as_string(pixel_type) : "unknown";

    fprintf(stderr, "%s: %dx%d, %s, %d-bits, %s, ",
            infile, input_width, input_height, pixel_type_name, input_depth,
            interlaced ? tff ? "tff" : "bff" : "progressive");
    if(fps_den == 1)
        fprintf(stderr, "%u fps, ", fps_num);
    else
        fprintf(stderr, "%u/%u fps, ", fps_num, fps_den);
    fprintf(stderr, "%d frames\n", inf->num_frames);

    if(csp == CSP_AUTO) {
        if(avs_h.func.avs_is_420 ? avs_h.func.avs_is_420(inf) : avs_is_yv12(inf))
            csp = CSP_I420;
        else if((avs_h.func.avs_is_422 ? avs_h.func.avs_is_422(inf) : avs_is_yv16(inf)) || avs_is_yuy2(inf))
            csp = CSP_I422;
        else if(avs_h.func.avs_is_444 ? avs_h.func.avs_is_444(inf) : avs_is_yv24(inf))
            csp = CSP_I444;
        else if(avs_h.func.avs_is_y ? avs_h.func.avs_is_y(inf) : avs_is_y8(inf))
            csp = CSP_I400;
        else
            csp = CSP_I420; // not supported colorspaces (like RGB) we try convert to I420
    }

    if( (csp == CSP_I420 && !(avs_h.func.avs_is_420 ? avs_h.func.avs_is_420(inf) : avs_is_yv12(inf))) ||
        (csp == CSP_I422 && !(avs_h.func.avs_is_422 ? avs_h.func.avs_is_422(inf) : avs_is_yv16(inf))) ||
        (csp == CSP_I444 && !(avs_h.func.avs_is_444 ? avs_h.func.avs_is_444(inf) : avs_is_yv24(inf))) ||
        (csp == CSP_I400 && !(avs_h.func.avs_is_y ? avs_h.func.avs_is_y(inf) : avs_is_y8(inf))) )
    {
        if(is_16bit_hack) {
            fprintf(stderr, "error: colorspace conversion is not possible with avisynth 16-bit hack\n");
            goto fail;
        }
        const char *csp_name;
        if(avs_h.func.avs_is_420 || avs_h.func.avs_is_422 || avs_h.func.avs_is_444) {
            csp_name = csp == CSP_I400 ? "Y" :
                       csp == CSP_I444 ? "YUV444" :
                       csp == CSP_I422 ? "YUV422" :
                       "YUV420";
        } else {
            csp_name = csp == CSP_I400 ? "Y8" :
                       csp == CSP_I444 ? "YV24" :
                       csp == CSP_I422 ? "YV16" :
                       "YV12";
        }
        fprintf(stderr, "converting input clip to %s\n", csp_name);
        if(csp != CSP_I400) {
            if(csp < CSP_I444 && (inf->width&1)) {
                fprintf(stderr, "error: input clip width not divisible by 2 (%dx%d)\n", inf->width, inf->height);
                goto fail;
            }
            if(csp == CSP_I420 && interlaced && (inf->height&3)) {
                fprintf(stderr, "error: input clip height not divisible by 4 (%dx%d)\n", inf->width, inf->height);
                goto fail;
            }
            if((csp == CSP_I420 || interlaced) && (inf->height&1)) {
                fprintf(stderr, "error: input clip height not divisible by 2 (%dx%d)\n", inf->width, inf->height);
                goto fail;
            }
        }
        char conv_func[16];
        snprintf(conv_func, sizeof(conv_func), "ConvertTo%s", csp_name);
        conv_func[sizeof(conv_func)-1] = 0;
        AVS_Value arg_arr[2];
        const char *arg_name[2];
        int arg_count = 1;
        arg_arr[0] = res;
        arg_name[0] = NULL;
        if(csp != CSP_I400) {
            arg_arr[arg_count] = avs_new_value_bool(interlaced);
            arg_name[arg_count] = "interlaced";
            arg_count++;
        }
        AVS_Value tmp = avs_h.func.avs_invoke(avs_h.env, conv_func, avs_new_value_array(arg_arr, arg_count), arg_name);
        if(avs_is_error(tmp)) {
            fprintf(stderr, "error: couldn't convert input clip to %s: %s\n", csp_name, avs_as_error(tmp));
            goto fail;
        }
        res = internal_avs_update_clip(&avs_h, &inf, tmp, res);
    }
    avs_h.func.avs_release_value(res);

    for(int i = 0; i < out_fhs; i++) {
        if(!strcmp(outfile[i], "-")) {
            for(int j = 0; j < i; j++)
                if(out_fh[j] == stdout) {
                    fprintf(stderr, "error: can't write to stdout multiple times\n");
                    goto fail;
                }
            int dupout = _dup(_fileno(stdout));
            fclose(stdout);
            _setmode(dupout, _O_BINARY);
            out_fh[i] = _fdopen(dupout, "wb");
        } else {
            out_fh[i] = fopen(outfile[i], "wb");
            if(!out_fh[i]) {
                fprintf(stderr, "error: failed to create/open \"%s\"\n", outfile[i]);
                goto fail;
            }
        }
    }
    if(hfyufile) {
        char *cmd = malloc(100+strlen(hfyufile));
        if(!cmd) {
           fprintf(stderr, "error: malloc failed\n");
           goto fail;
        }
        sprintf(cmd, "ffmpeg -loglevel quiet -v 0 -y -f yuv4mpegpipe -i - -vcodec ffvhuff -an -f avi \"%s\"", hfyufile);
        out_fh[out_fhs] = _popen(cmd, "wb");
        free(cmd);
        if(!out_fh[out_fhs]) {
            fprintf(stderr, "error: failed to exec ffmpeg\n");
            goto fail;
        }
        y4m_headers[out_fhs] = 1;
        out_fhs++;
    }

    char *interlace_type = interlaced ? tff ? "t" : "b" : "p";
    char csp_type[200];
    int chroma_h_shift = 0;
    int chroma_v_shift = 0;
    switch(csp) {
        case CSP_I400:
            if(input_depth > 8)
                sprintf(csp_type, "Cmono%d", input_depth);
            else
                strcpy(csp_type, "Cmono");
            break;
        case CSP_I420:
            if(input_depth > 8)
                sprintf(csp_type, "C420p%d XYSCSS=420P%d", input_depth, input_depth);
            else
                strcpy(csp_type, "C420mpeg2 XYSCSS=420MPEG2");
            chroma_h_shift = 1;
            chroma_v_shift = 1;
            break;
        case CSP_I422:
            if(input_depth > 8)
                sprintf(csp_type, "C422p%d XYSCSS=422P%d", input_depth, input_depth);
            else
                strcpy(csp_type, "C422 XYSCSS=422");
            chroma_h_shift = 1;
            break;
        case CSP_I444:
            if(input_depth > 8)
                sprintf(csp_type, "C444p%d XYSCSS=444P%d", input_depth, input_depth);
            else
                strcpy(csp_type, "C444 XYSCSS=444");
            break;
        default:
            goto fail; //can't happen
    }
    for(int i = 0; i < out_fhs; i++) {
        if(setvbuf(out_fh[i], NULL, _IOFBF, AVS_BUFSIZE))
        {
            fprintf(stderr, "error: failed to create buffer for \"%s\"\n", outfile[i]);
            goto fail;
        }
        if(!y4m_headers[i])
            continue;
        fprintf(out_fh[i], "YUV4MPEG2 W%d H%d F%u:%u I%s A%u:%u %s\n",
            input_width, input_height, fps_num, fps_den, interlace_type, par_width, par_height, csp_type);
        fflush(out_fh[i]);
    }

    int planes_count = 1;
    int frame_size = inf->width * inf->height;
    if(csp != CSP_I400) {
        planes_count += 2;
        frame_size += 2 * (inf->width >> chroma_h_shift) * (inf->height >> chroma_v_shift);
    }
    int write_target = out_fhs * frame_size; // how many bytes per frame we expect to write

    if(slave) {
        seek = 0;
        end = INT_MAX;
    } else {
        end += seek;
        if(end <= seek || end > inf->num_frames)
            end = inf->num_frames;
    }

    long start_time = get_date();

    for(int frm = seek; frm < end; ++frm) {
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
            fprintf(stderr, "error: %s occurred while reading frame %d\n", err, frm);
            goto fail;
        }

        if(out_fhs) {
            static const int planes[] = {AVS_PLANAR_Y, AVS_PLANAR_U, AVS_PLANAR_V};
            int wrote = 0;

            for(int i = 0; i < out_fhs; i++)
                if(y4m_headers[i])
                    fwrite("FRAME\n", 1, 6, out_fh[i]);

            for(int p = 0; p < planes_count; p++) {
                int w = inf->width  >> (p ? chroma_h_shift : 0);
                int h = inf->height >> (p ? chroma_v_shift : 0);
                int pitch = avs_get_pitch_p(f, planes[p]);
                const BYTE* data = avs_get_read_ptr_p(f, planes[p]);
                for(int y = 0; y < h; y++) {
                    for(int i = 0; i < out_fhs; i++)
                        wrote += fwrite(data, component_size, w, out_fh[i]);
                    data += pitch;
                }
            }
            if(wrote != write_target) {
                fprintf(stderr, "error: wrote only %d of %d bytes\n", wrote * component_size, write_target * component_size);
                goto fail;
            }
            if(slave) { // assume timing doesn't matter in other modes
                for(int i = 0; i < out_fhs; i++)
                    fflush(out_fh[i]);
            }
        }

        if(verbose)
            fprintf(stderr, "%d\n", frm);
        else if(showfps) {
            long elapsed_time = get_date() - start_time;
            double fps = elapsed_time > 0 ? frm * 1000000. / elapsed_time : 0;
            fprintf(stderr, "%.2f fps     \r", fps);
        }


        avs_h.func.avs_release_video_frame(f);
    }

    for(int i = 0; i < out_fhs; i++)
        fflush(out_fh[i]);

close_files:
    retval = 0;
fail:
    if(hfyufile) {
        if(out_fh[out_fhs-1])
            _pclose(out_fh[out_fhs-1]);
        out_fhs--;
    }
    for(int i = 0; i < out_fhs; i++)
        if(out_fh[i])
            fclose(out_fh[i]);
    if(avs_h.library)
        internal_avs_close_library(&avs_h);
    return retval;
}
