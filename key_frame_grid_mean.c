/*
 * Copyright (c) 2012 Stefano Sabatini
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * Demuxing and decoding example.
 *
 * Show how to use the libavformat and libavcodec API to demux and
 * decode audio and video data.
 * @example demuxing_decoding.c
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *video_dec_ctx = NULL;
static int width, height;
static enum AVPixelFormat pix_fmt;
static AVStream *video_stream = NULL;
static struct SwsContext *sws_ctx;
static const char *src_filename = NULL;
static const char *cvs_dst_filename = NULL;
static FILE *cvs_dst_file = NULL;

static int grid_w = 0;
static int grid_h = 0;
static int grid_cell_w = 0;
static int grid_cell_h = 0;
static int grind_cells_num = 0;
static int grid_cell_pix_num = 0;
static unsigned char *tmp_data;
static unsigned char *median_data;

static uint8_t *video_dst_data[4] = {NULL};
static int      video_dst_linesize[4];
static int video_dst_bufsize;

static int video_stream_idx = -1;
static AVFrame *frame = NULL;
static AVPacket pkt;

static const int refcount = 0;

// static void pgm_save(unsigned char *buf, int wrap, int xsize, int ysize,
//                      char *filename)
// {
//     FILE *f;
//     int i;

//     f = fopen(filename,"w");
//     fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);
//     for (i = 0; i < ysize; i++)
//         fwrite(buf + i * wrap, 1, xsize, f);
//     fclose(f);
// }

static int median_compare_func( const void *a, const void *b) {
  return *(char*)a - *(char*)b;
}

static uint8_t get_median(unsigned char *data, int size){
    qsort((void*)data, (size_t)size, (size_t) sizeof(unsigned char),median_compare_func);
    if(size%2 != 0) {
        return (int) data[size/2];
    }
    return (int)(data[size/2-1] + data[size/2])/2;
}

static int init_grid_variables(){
    grid_cell_w = width/grid_w;
    grid_cell_h = height/grid_h;
    grind_cells_num = grid_w*grid_h;
    grid_cell_pix_num = grid_cell_w*grid_cell_h;
    tmp_data = malloc(grid_cell_pix_num);
    if(tmp_data == NULL){
        return 1;
    }
    median_data = malloc(grid_w*grid_h);
    if(median_data == NULL){
        free(tmp_data);
        return 2;
    }
    return 0;
}

static int split_grid(unsigned char*data, int width, int height){
    int k,i,j,img_y_offset,img_x_offset;

    for(i = 0; i < grid_h; i++) {
        img_y_offset = i*grid_cell_h*width;
        for(k = 0; k < grid_w; k++) {
            img_x_offset = img_y_offset + k*grid_cell_w;
            for(j = 0; j < grid_cell_h; j++) {
                memcpy(&tmp_data[j*grid_cell_w], &data[img_x_offset+j*width], grid_cell_w);
            }
            int median = get_median(tmp_data, grid_cell_pix_num);
            // TODO make pointer
            median_data[i*grid_w+k] = median;
        }
    }
    return 0;
}

static int decode_packet(int *got_frame, int cached)
{
    int ret = 0, k;
    int decoded = pkt.size;
    *got_frame = 0;

    if (pkt.stream_index == video_stream_idx) {
        /* decode video frame */
        ret = avcodec_decode_video2(video_dec_ctx, frame, got_frame, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error decoding video frame (%s)\n", av_err2str(ret));
            return ret;
        }

        if (*got_frame && frame->key_frame == 1) { 

            // skipp frame if frame dimensions differ from vide dimensions
            if (frame->width != width || frame->height != height) {
                return -1;
            } 
            printf("%.2f", frame->best_effort_timestamp/(float)video_stream->time_base.den);
            fprintf(cvs_dst_file,"%.2f", frame->best_effort_timestamp/(float)video_stream->time_base.den);
            // snprintf(buf, sizeof(buf), "%s-%d.pgm", "test", video_dec_ctx->frame_number);     
            // pgm_save(frame->data[0], frame->linesize[0],
            //      frame->width, frame->height, buf); 

            // convert to destination format 
            sws_scale(sws_ctx, (const uint8_t *const *)frame->data, 
                frame->linesize, 0, height, 
                video_dst_data, video_dst_linesize);

            // split frame into grid and find median of each grid cell
            split_grid((unsigned char *)frame->data[0], width, height);
            for(k = 0; k < grind_cells_num; k++){
                printf(",%d",median_data[k]);
                fprintf(cvs_dst_file,",%d",median_data[k]);
            }
            printf("\n");
            fprintf(cvs_dst_file,"\n");
        }
    } 
   if (*got_frame && refcount)
        av_frame_unref(frame);

    return decoded;
}

static int open_codec_context(int *stream_idx,
                              AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret, stream_index;
    AVStream *st;
    AVCodec *dec = NULL;
    AVDictionary *opts = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find %s stream in input file '%s'\n",
                av_get_media_type_string(type), src_filename);
        return ret;
    } else {
        stream_index = ret;
        st = fmt_ctx->streams[stream_index];

        /* find decoder for the stream */
        dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec) {
            fprintf(stderr, "Failed to find %s codec\n",
                    av_get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        /* Allocate a codec context for the decoder */
        *dec_ctx = avcodec_alloc_context3(dec);
        if (!*dec_ctx) {
            fprintf(stderr, "Failed to allocate the %s codec context\n",
                    av_get_media_type_string(type));
            return AVERROR(ENOMEM); 
        }

        /* Copy codec parameters from input stream to output codec context */
        if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
            fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
                    av_get_media_type_string(type));
            return ret;
        }

        /* Init the decoders, with or without reference counting */
        av_dict_set(&opts, "refcounted_frames", refcount ? "1" : "0", 0);
        if ((ret = avcodec_open2(*dec_ctx, dec, &opts)) < 0) {
            fprintf(stderr, "Failed to open %s codec\n",
                    av_get_media_type_string(type));
            return ret;
        }
        *stream_idx = stream_index;
    }

    return 0;
}

int main (int argc, char **argv)
{
    int ret = 0, got_frame;
    enum AVPixelFormat dst_pix_fmt = AV_PIX_FMT_GRAY8;

    if (argc != 4 && argc != 5) {
        fprintf(stderr, "usage: %s input_vide_file output_cvs_file grid_widthxgrid_height \n"
                "where input_vide_file is a valid file containing vide data.\n"
                "output_cvs_file is name of output file where frame data will be written\n"
                "and grid_widthxgrid_height is the width and height of grind for example 3x3\n"
                "\n", argv[0]);
        exit(1);
    } 
    src_filename = argv[1];
    cvs_dst_filename = argv[2];
    sscanf(argv[3],"%dx%d", &grid_w, &grid_h);

    /* open input file, and allocate format context */
    if (avformat_open_input(&fmt_ctx, src_filename, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open source file %s\n", src_filename);
        exit(1);
    }

    /* retrieve stream information */
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    if (open_codec_context(&video_stream_idx, &video_dec_ctx, fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0) {
        video_stream = fmt_ctx->streams[video_stream_idx];
        /* allocate image where the grayscale image will be put after  conversion*/
        width = video_dec_ctx->width;
        height = video_dec_ctx->height;
        pix_fmt = video_dec_ctx->pix_fmt;
        ret = av_image_alloc(video_dst_data, video_dst_linesize,
                             width, height, pix_fmt, 1);
        if (ret < 0) {
            fprintf(stderr, "Could not allocate raw video buffer\n");
            goto end;
        }
        video_dst_bufsize = ret;
    }

    if (!video_stream) {
        fprintf(stderr, "Could not find video stream in the input, aborting\n");
        ret = 1;
        goto end;
    }

    cvs_dst_file = fopen(cvs_dst_filename, "w");
    if (!cvs_dst_file) {
        fprintf(stderr, "Could not open destination file %s\n", cvs_dst_filename);
        ret = 1;
        goto end;
    }

    av_dump_format(fmt_ctx, 0, src_filename, 0);

    /* prepare grid variables */
    ret = init_grid_variables();
    if(ret){
        goto end;
    }

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate frame\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* create scaling context */
    sws_ctx = sws_getContext(width, height, video_dec_ctx->pix_fmt,
                             width, height, dst_pix_fmt,
                             SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws_ctx) {
        fprintf(stderr,
                "Impossible to create scale context for the conversion "
                "fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
                av_get_pix_fmt_name(video_dec_ctx->pix_fmt), width, height,
                av_get_pix_fmt_name(dst_pix_fmt), width, width);
        ret = AVERROR(EINVAL);
        goto end;
    }

    /* initialize packet, set data to NULL, let the demuxer fill it */
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    /* read frames from the file */
    while (av_read_frame(fmt_ctx, &pkt) >= 0) {
        AVPacket orig_pkt = pkt;
        do {
            ret = decode_packet(&got_frame, 0);
            if (ret < 0)
                break;
            pkt.data += ret;
            pkt.size -= ret;
        } while (pkt.size > 0);
        av_packet_unref(&orig_pkt);
    }

    /* flush cached frames */
    pkt.data = NULL;
    pkt.size = 0;
    do {
        decode_packet(&got_frame, 1);
    } while (got_frame);

    printf("Processing succeeded.\n");

 

end:
    avcodec_free_context(&video_dec_ctx);
    avformat_close_input(&fmt_ctx);
    if (cvs_dst_file)
        fclose(cvs_dst_file);
    av_frame_free(&frame);
    av_free(video_dst_data[0]);
    if(tmp_data){
        free(tmp_data);
    }
    if(median_data){
        free(median_data);
    }
    return ret < 0;
}
