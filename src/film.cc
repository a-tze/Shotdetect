/*
 * Copyright (C) 2007 Johan MATHE - johan.mathe@tremplin-utc.net - Centre
 * Pompfidou - IRI This library is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either version
 * 2.1 of the License, or (at your option) any later version. This
 * library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details. You should have received a copy of the GNU
 * Lesser General Public License along with this library; if not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA $Id: film.cpp 141 2007-04-02 16:10:53Z
 * johmathe $ $Date: 2010-10-01 01:35:11 +0200 (Fri, 01 Oct 2010) $
 */

#ifndef UINT64_C
#define UINT64_C(c) (c##ULL)
#endif

#include <sys/time.h>
#include <time.h>
extern "C" {
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include "src/film.h"
#include "src/graph.h"

#define DEBUG

int film::idfilm = 0;

void film::do_stats(int frame_number) {
  double perctmp = percent;
  struct timeval time_now;
  struct timezone timezone;
  gettimeofday(&time_now, &timezone);

  show_started = 1;
}

void film::create_main_dir() {
  ostringstream str;
  struct stat *buf;
  str.str("");
  str << this->global_path << "/" << this->alphaid;

  buf = (struct stat *)malloc(sizeof(struct stat));
  if (stat(str.str().c_str(), buf) == -1) {
#if defined(__WINDOWS__) || defined(__MINGW32__)
    mkdir(str.str().c_str());
#else
    mkdir(str.str().c_str(), 0777);
#endif
  }
}

bool film::scene_change(int diff, int score, int timestamp) {
  bool change = (diff > this->threshold) && (score > this->threshold);
  if (!change && !in_change) {
    return false;
  }
  if (change && !in_change) {
    in_change = true;
    last_change_begin_time = timestamp;
    last_change_end_time = timestamp;
    return false;
  }
  if (change && in_change) {
    last_change_end_time = timestamp;
    if (max_scene_change_duration >= 0 && timestamp >= last_change_begin_time + max_scene_change_duration) {
      // this is a very long scene change, produce intermediate positives
      last_change_begin_time = timestamp;
      last_time = timestamp;
      return true;
    }
    return false;
  }
  if (!change && in_change) {
    if (timestamp >= last_change_end_time + after_scene_change_offset) {
      in_change = false;
      last_time = timestamp;
      return true;
    }
    return false;
  }
  // should never reach here
  return false;
}

void film::get_yuv_colors(AVFrame &pFrameYUV) {
  int x;
  int y;
  int c1, c2, c3;
  int c1tot, c2tot, c3tot;

  c1tot = 0;
  c2tot = 0;
  c3tot = 0;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      c1 = pFrameYUV.data[0][pFrameYUV.linesize[0] * y + x];  // Y
      c2 = pFrameYUV.data[1][pFrameYUV.linesize[0] * y + x];  // Cb
      c3 = pFrameYUV.data[2][pFrameYUV.linesize[0] * y + x];  // Cr

      c1tot += int(c1);
      c2tot += int(c2);
      c3tot += int(c3);
    }
  }

  g->push_yuv(c1, c2, c3);
}

/*
 * This function gathers the RGB values per frame and evaluates the
 * possibility if this frame is a detected shot.
 * If a shot is detected, this function also creates the image files
 * for this scene cut.
 */
void film::CompareFrameRGB(AVFrame *pFrame, AVFrame *pFramePrev) {
  int x;
  int y;
  int diff;
//  int frame_number = pCodecCtx->frame_number;
  int c1, c2, c3;
  int c1tot, c2tot, c3tot;
  c1tot = 0;
  c2tot = 0;
  c3tot = 0;
  int c1prev, c2prev, c3prev;
  int score;
  score = 0;

  // IDEA! Split image in slices and calculate score per-slice.
  // This would allow to detect areas on the image which have stayed
  // the same, and (a) increase score if all areas have changed
  // and (b) decrease score if some areas have changed less (ot not at all).
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      c1 = *(pFrame->data[0] + y * pFrame->linesize[0] + x * 3);
      c2 = *(pFrame->data[0] + y * pFrame->linesize[0] + x * 3 + 1);
      c3 = *(pFrame->data[0] + y * pFrame->linesize[0] + x * 3 + 2);

      c1prev = *(pFramePrev->data[0] + y * pFramePrev->linesize[0] + x * 3);
      c2prev = *(pFramePrev->data[0] + y * pFramePrev->linesize[0] + x * 3 + 1);
      c3prev = *(pFramePrev->data[0] + y * pFramePrev->linesize[0] + x * 3 + 2);

      c1tot += int((char)c1 + 127);
      c2tot += int((char)c2 + 127);
      c3tot += int((char)c3 + 127);

      score += abs(c1 - c1prev);
      score += abs(c2 - c2prev);
      score += abs(c3 - c3prev);
    }
  }
  int nbpx = (height * width);

  /*
   * On se ramene à la moyenne
   */
  score /= nbpx;
  c1tot /= nbpx;
  c2tot /= nbpx;
  c3tot /= nbpx;

  /*
   * Calculate numerical difference between this and the previous frame
   */
  diff = abs(score - prev_score);
  prev_score = score;

  /*
   * Store gathered data
   */
  g->push_data(score);
  g->push_rgb(c1tot, c2tot, c3tot);
  g->push_rgb_to_hsv(c1tot, c2tot, c3tot);

  /*
   * Take care of storing frame position and images of detecte scene cut
   */
  if (scene_change(diff, score, int((frame_number * 1000) / fps))) {
    shot s;
    s.fbegin = frame_number;
    s.msbegin = int((frame_number * 1000) / fps);
    s.myid = shots.back().myid + 1;

#ifdef DEBUG
    cerr << "Shot log :: " << s.msbegin << endl;
#endif

    /*
     * Convert to ms
     */
    shots.back().fduration = frame_number - shots.back().fbegin;
    shots.back().msduration = int(((shots.back().fduration) * 1000) / fps);

/*
 * Create images if necessary
 */
    if (this->first_img_set)
    {
      image *im_begin = new image(this, width, height, s.myid, BEGIN,
                                  this->thumb_set, this->shot_set);
      im_begin->SaveFrame(pFrame, frame_number, AV_PIX_FMT_RGB24);
      s.img_begin = im_begin;
    }

    if (this->last_img_set)
    {
      image *im_end = new image(this, width, height, s.myid - 1, END,
                                this->thumb_set, this->shot_set);
      im_end->SaveFrame(pFramePrev, frame_number, AV_PIX_FMT_RGB24);
      shots.back().img_end = im_end;
    }
    shots.push_back(s);

  }
}

/*
 * This function gathers the YUV values per frame and evaluates the
 * possibility if this frame is a detected shot.
 * If a shot is detected, this function also creates the image files
 * for this scene cut.
 */
void film::CompareFrameYUV(AVFrame &pFrameYUV, AVFrame &pFrameYUVPrev) {
  int x;
  int y;
  int diff;
//  int frame_number = pCodecCtx->frame_number;
  int c1, c2, c3;
  int c1tot, c2tot, c3tot;
  c1tot = 0;
  c2tot = 0;
  c3tot = 0;
  int c1prev, c2prev, c3prev;
  int score = 0;
  int drastic_score = 0;

  // IDEA! Split image in slices and calculate score per-slice.
  // This would allow to detect areas on the image which have stayed
  // the same, and (a) increase score if all areas have changed
  // and (b) decrease score if some areas have changed less (ot not at all).
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      c1 = pFrameYUV.data[0][pFrameYUV.linesize[0] * y + x];  // Y
      c2 = pFrameYUV.data[1][pFrameYUV.linesize[0] * y + x];  // Cb
      c3 = pFrameYUV.data[2][pFrameYUV.linesize[0] * y + x];  // Cr

      c1prev = pFrameYUVPrev.data[0][pFrameYUVPrev.linesize[0] * y + x];  // Y
      c2prev = pFrameYUVPrev.data[1][pFrameYUVPrev.linesize[0] * y + x];  // Cb
      c3prev = pFrameYUVPrev.data[2][pFrameYUVPrev.linesize[0] * y + x];  // Cr

      c1tot += int((char)c1);
      c2tot += int((char)c2);
      c3tot += int((char)c3);

      drastic_score += abs(c1 - c1prev) >> 4;
    }
  }
  int nbpx = (height * width);

  /*
   * On se ramene à la moyenne
   */
  score = drastic_score / (nbpx / 1000); // score == promille of pixels that changed more than 6% in Y channel
  c1tot /= nbpx;
  c2tot /= nbpx;
  c3tot /= nbpx;

  /*
   * Calculate numerical difference between this and the previous frame
   */
  diff = abs(score - prev_score);
  prev_score = score;

  /*
   * Store gathered data
   */
  g->push_data(score);
  g->push_yuv(c1tot, c2tot, c3tot);

  /*
   * Take care of storing frame position and images of detecte scene cut
   */
  if (scene_change(diff, score, int((frame_number * 1000) / fps))) {
    shot s;
    s.fbegin = frame_number;
    s.msbegin = int((frame_number * 1000) / fps);
    s.myid = shots.back().myid + 1;

#ifdef DEBUG
    cerr << "Shot log :: " << s.msbegin << " with score :: " << score << endl;
#endif

    /*
     * Convert to ms
     */
    shots.back().fduration = frame_number - shots.back().fbegin;
    shots.back().msduration = int(((shots.back().fduration) * 1000) / fps);

/*
 * Create images if necessary
 */
    if (this->first_img_set)
    {
      image *im_begin = new image(this, width, height, s.myid, BEGIN,
                                  this->thumb_set, this->shot_set);
      im_begin->SaveFrame(&pFrameYUV, frame_number, AV_PIX_FMT_YUV444P);
      s.img_begin = im_begin;
    }

    if (this->last_img_set)
    {
      image *im_end = new image(this, width, height, s.myid - 1, END,
                                this->thumb_set, this->shot_set);
      im_end->SaveFrame(&pFrameYUVPrev, frame_number, AV_PIX_FMT_YUV444P);
      shots.back().img_end = im_end;
    }
    shots.push_back(s);

  }
}

void film::CompareFrameY(AVFrame &pFrameY, AVFrame &pFrameYPrev) {
  int x;
  int y;
  int diff;
//  int frame_number = pCodecCtx->frame_number;
  int c1;
  int c1tot = 0;
  c1tot = 0;
  int c1prev;
  int score = 0;
  int drastic_score = 0;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      c1 = pFrameY.data[0][pFrameY.linesize[0] * y + x];  // Y
      c1prev = pFrameYPrev.data[0][pFrameYPrev.linesize[0] * y + x];  // Y
      c1tot += int((char)c1);
      score += abs(c1 - c1prev);
      drastic_score += abs(c1 - c1prev) >> 4;
    }
  }
  int nbpx = (height * width);
  score = drastic_score / (nbpx / 1000); // score == promille of pixels that changed more than 6% in Y channel
  c1tot /= nbpx;

  /*
   * Calculate numerical difference between this and the previous frame
   */
  diff = abs(score - prev_score);
  prev_score = score;
  // TODO also compare against values of last detected shot change

  /*
   * Store gathered data
   */
  g->push_data(score);
  g->push_yuv(c1tot, 0, 0);

  /*
   * Take care of storing frame position and images of detecte scene cut
   */
  if (scene_change(diff, score, int((frame_number * 1000) / fps))) {
    shot s;
    s.fbegin = frame_number;
    s.msbegin = int((frame_number * 1000) / fps);
    s.myid = shots.back().myid + 1;

#ifdef DEBUG
    cerr << "Shot log :: " << s.msbegin << " with score :: " << score << endl;
#endif

    /*
     * Convert to ms
     */
    shots.back().fduration = frame_number - shots.back().fbegin;
    shots.back().msduration = int(((shots.back().fduration) * 1000) / fps);

/*
 * Create images if necessary
 */
    if (this->first_img_set)
    {
      image *im_begin = new image(this, width, height, s.myid, BEGIN,
                                  this->thumb_set, this->shot_set);
      im_begin->SaveFrame(&pFrameY, frame_number, AV_PIX_FMT_GRAY8);
      s.img_begin = im_begin;
    }

    if (this->last_img_set)
    {
      image *im_end = new image(this, width, height, s.myid - 1, END,
                                this->thumb_set, this->shot_set);
      im_end->SaveFrame(&pFrameYPrev, frame_number, AV_PIX_FMT_GRAY8);
      shots.back().img_end = im_end;
    }
    shots.push_back(s);

  }
}

void film::update_metadata() {
  char buf[256];
  /* Video metadata */
  if (videoStream != -1) {
    this->height = int(pFormatCtx->streams[videoStream]->codec->height);
    this->width = int(pFormatCtx->streams[videoStream]->codec->width);
    this->fps = av_q2d(pFormatCtx->streams[videoStream]->r_frame_rate);
    avcodec_string(buf, sizeof(buf), pFormatCtx->streams[videoStream]->codec,
                   0);
    this->codec.video = buf;

  } else {
    this->codec.video = "null";
    this->height = 0;
    this->width = 0;
    this->fps = 0;
  }

  /* Audio metadata */
  if (audioStream != -1) {
    avcodec_string(buf, sizeof(buf), pCodecCtxAudio, 0);
    this->codec.audio = buf;
    this->nchannel = pCodecCtxAudio->channels;
    this->samplerate = pCodecCtxAudio->sample_rate;
  } else {
    this->codec.audio = "null";
    this->nchannel = 0;
    this->samplerate = 0;
  }

  duration.secs = pFormatCtx->duration / AV_TIME_BASE;
  duration.us = pFormatCtx->duration % AV_TIME_BASE;
  duration.mstotal = int(duration.secs * 1000 + duration.us / 1000);
  duration.mins = duration.secs / 60;
  duration.secs %= 60;
  duration.hours = duration.mins / 60;
  duration.mins %= 60;
}

void film::shotlog(string message) {
  cerr << "Shot log :: " << message << endl;
}

int film::process() {
  int audioSize;
  int frameFinished;
  shot s;
  static struct SwsContext *img_convert_ctx = NULL;
  static struct SwsContext *img_ctx = NULL;
  static struct SwsContext *img_gray_ctx = NULL;
//  int frame_number;

  create_main_dir();

  string graphpath = this->global_path + "/" + this->alphaid;
  g = new graph(600, 400, graphpath, threshold, this);

  /*
   * Register all formats and codecs
   */
  av_register_all();
  pFormatCtx = avformat_alloc_context();
  if (avformat_open_input(&pFormatCtx, input_path.c_str(), NULL, NULL) != 0) {
    string error_msg = "Could not open file ";
    error_msg += input_path;
    shotlog(error_msg);
    return -1;  // Couldn't open file
  }

  /*
   * Retrieve stream information
   */
  if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
    return -1;  // Couldn't find stream information

  av_dump_format(pFormatCtx, 0, input_path.c_str(), false);
  videoStream = -1;
  audioStream = -1;

  /*
   * Detect streams types
   */
  for (int j = 0; j < pFormatCtx->nb_streams; j++) {
    switch (pFormatCtx->streams[j]->codec->codec_type) {
      case AVMEDIA_TYPE_VIDEO:
        videoStream = j;
        break;

      case AVMEDIA_TYPE_AUDIO:
        audioStream = j;
        break;

      default:
        break;
    }
  }

  /*
   * Get a pointer to the codec context for the video stream
   */
  if (audioStream != -1) {
    if (audio_set) {
      string xml_audio = graphpath + "/" + alphaid + "_audio.xml";
      init_xml(xml_audio);
    }

    pCodecCtxAudio = pFormatCtx->streams[audioStream]->codec;
    pCodecAudio = avcodec_find_decoder(pCodecCtxAudio->codec_id);

    if (pCodecAudio == NULL) return -1;  // Codec not found
    if (avcodec_open2(pCodecCtxAudio, pCodecAudio, NULL) < 0)
      return -1;  // Could not open codec
  }
  update_metadata();

  /*
   * Find the decoder for the video stream
   */
  if (videoStream != -1) {
    pCodecCtx = pFormatCtx->streams[videoStream]->codec;
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);

    if (pCodec == NULL) return -1;  // Codec not found
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
      return -1;  // Could not open codec

    /*
     * Allocate current and previous video frames
     */
    pFrame = av_frame_alloc();
    // RGB:
    pFrameRGB = av_frame_alloc();      // current frame
    pFrameRGBprev = av_frame_alloc();  // previous frame
    // YUV:
    pFrameYUV = av_frame_alloc();  // current frame
    pFrameYUVprev = av_frame_alloc();  // previous frame
    // Y:
    pFrameY = av_frame_alloc();  // current frame
    pFrameYprev = av_frame_alloc();  // previous frame

    /*
     * Allocate memory for the pixels of a picture and setup the AVPicture
     * fields for it
     */
    // RGB:
    av_image_alloc(((AVPicture *)pFrameRGB)->data, ((AVPicture *)pFrameRGB)->linesize, width, height, AV_PIX_FMT_RGB24, 4);
    av_image_alloc(((AVPicture *)pFrameRGBprev)->data, ((AVPicture *)pFrameRGBprev)->linesize, width, height, AV_PIX_FMT_RGB24, 4);
    // YUV:
    av_image_alloc(((AVPicture *)pFrameYUV)->data, ((AVPicture *)pFrameYUV)->linesize, width, height, AV_PIX_FMT_YUV444P, 4);
    av_image_alloc(((AVPicture *)pFrameYUVprev)->data, ((AVPicture *)pFrameYUVprev)->linesize, width, height, AV_PIX_FMT_YUV444P, 4);
    // Y:
    av_image_alloc(((AVPicture *)pFrameY)->data, ((AVPicture *)pFrameY)->linesize, width, height, AV_PIX_FMT_GRAY8, 4);
    av_image_alloc(((AVPicture *)pFrameYprev)->data, ((AVPicture *)pFrameYprev)->linesize, width, height, AV_PIX_FMT_GRAY8, 4);

    /*
     * Mise en place du premier plan
     */
    s.fbegin = 1;
    s.msbegin = 0;
    s.myid = 0;
    shots.push_back(s);
  }

  checknumber = (samplerate * samplearg) / 1000;

  /*
   * Main loop to control the movie processing flow
   */
  while (av_read_frame(pFormatCtx, &packet) >= 0) {
    if (packet.stream_index == videoStream) {
      avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

      if (frameFinished) {
        frame_number = pCodecCtx->frame_number;  // Current frame number

        // apply skip-frames parameter
        frame_number -= start_frame;

        if (frame_number < 1) {
          /* skip frame parameter is set and we didn't reach that frame yet */
          av_free_packet(&packet);
          continue;
        }

        if (end_frame >= 0 && (frame_number - 1) >= end_frame) {
          av_free_packet(&packet);
          break;
        }

        // Convert the image into YUV444
        if (!img_ctx) {
          img_ctx =
              sws_getContext(width, height, pCodecCtx->pix_fmt, width, height,
                             AV_PIX_FMT_YUV444P, SWS_BICUBIC, NULL, NULL, NULL);
          if (!img_ctx) {
            fprintf(stderr,
                    "Cannot initialize the converted YUV image context!\n");
            exit(1);
          }
        }
        // Convert the image into GRAY8
        if (!img_gray_ctx) {
          img_gray_ctx =
              sws_getContext(width, height, pCodecCtx->pix_fmt, width, height,
                             AV_PIX_FMT_GRAY8, SWS_BICUBIC, NULL, NULL, NULL);
          if (!img_gray_ctx) {
            fprintf(stderr,
                    "Cannot initialize the converted Y image context!\n");
            exit(1);
          }
        }

        // Convert the image into RGB24
        if (!img_convert_ctx) {
          img_convert_ctx =
              sws_getContext(width, height, pCodecCtx->pix_fmt, width, height,
                             AV_PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL);
          if (!img_convert_ctx) {
            fprintf(stderr,
                    "Cannot initialize the converted RGB image context!\n");
            exit(1);
          }
        }

        /*
         * Calling "sws_scale" is used to copy the data from "pFrame->data" to
         *other
         * frame buffers for later processing. It is also used to convert
         *between
         * different pix_fmts.
         *
         * API: int sws_scale(SwsContext *c, uint8_t *src, int srcStride[], int
         *srcSliceY, int srcSliceH, uint8_t dst[], int dstStride[] )
        */
        sws_scale(img_convert_ctx, pFrame->data, pFrame->linesize, 0,
                  pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);

        sws_scale(img_ctx, pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
                  pFrameYUV->data, pFrameYUV->linesize);

        sws_scale(img_gray_ctx, pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
                  pFrameY->data, pFrameY->linesize);

        /* Extract pixel color information  */
        get_yuv_colors(*pFrameYUV);

        /* If it's not the first image */
        if (frame_number != 1) {
          if (!this->fifth_set || (frame_number % 5) == 0) {
// TODO: unite the 3 functions, introduce options to choose comparison method
//          CompareFrameRGB(pFrameRGB, pFrameRGBprev);
            CompareFrameYUV(*pFrameYUV, *pFrameYUVprev);
//          CompareFrameY(*pFrameY, *pFrameYprev);
          } else {
            av_free_packet(&packet);
            continue;
          }
        } else {
          /*
           * Cas ou c'est la premiere image, on cree la premiere image dans tous
           * les cas
           */
          image *begin_i = new image(this, width, height, s.myid, BEGIN,
                                     this->thumb_set, this->shot_set);
          begin_i->create_img_dir();

          if (this->first_img_set)
          {
            begin_i->SaveFrame(pFrameRGB, frame_number);
            shots.back().img_begin = begin_i;
          }
        }
        /* Copy current frame as "previous" for next round */
        av_picture_copy((AVPicture *)pFrameYUVprev, (AVPicture *)pFrameYUV,
                        AV_PIX_FMT_YUV444P, width, height);
        av_picture_copy((AVPicture *)pFrameYprev, (AVPicture *)pFrameY,
                        AV_PIX_FMT_GRAY8, width, height);
        av_picture_copy((AVPicture *)pFrameRGBprev, (AVPicture *)pFrameRGB,
                        AV_PIX_FMT_RGB24, width, height);

        if (display) do_stats(frame_number);
      }
    }
    if (audio_set && (packet.stream_index == audioStream)) {
      process_audio();
    }
    /*
     * Free the packet that was allocated by av_read_frame
     */
    av_free_packet(&packet);
  }

  if (videoStream != -1) {
    /* Mise en place de la dernière image */
    shots.back().fduration = pFrame->coded_picture_number - shots.back().fbegin;
    shots.back().msduration = int(((shots.back().fduration) * 1000) / fps);
    duration.mstotal = int(shots.back().msduration + shots.back().msbegin);
    if (this->last_img_set)
    {
      image *end_i = new image(this, width, height, shots.back().myid, END,
                               this->thumb_set, this->shot_set);
      end_i->SaveFrame(pFrameRGB, frame_number);
      shots.back().img_end = end_i;
    }

    /*
     * Graph 'quantity of movement'
     */
    g->init_gd();
    g->draw_all_canvas();
//    g->draw_color_datas();
    g->draw_datas();
    if (video_set) {
      string xml_color = graphpath + "/" + alphaid + "_video.xml";
      g->write_xml(xml_color);
    }
    g->save();

    /*
     * Free the RGB images
     */
    av_free(pFrame);
    av_free(pFrameRGB);
    av_free(pFrameRGBprev);
    av_free(pFrameYUV);
    av_free(pFrameYUVprev);
    avcodec_close(pCodecCtx);
  }

  // Close the codec
  if (audioStream != -1) {
    /* Close the XML file */
    if (audio_set) close_xml();
    avcodec_close(pCodecCtxAudio);
  }

  // Close the video file
  avformat_close_input(&pFormatCtx);
  return 0;
}

void film::init_xml(string filename) {
  fd_xml_audio = fopen(filename.c_str(), "w+");
  if (fd_xml_audio == NULL) {
    printf("Impossible to open xml audio file %s.", filename.c_str());
    exit(EXIT_FAILURE);
  }
  // FIXME: This may produce an uncaught segmentation fault if the output path
  // doesn't exist:
  fprintf(fd_xml_audio,
          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<iri>\n<sound "
          "sampling=\"%d\" nchannels=\"%d\">",
          samplearg, nchannel);
}

int film::close_xml() {
  fprintf(fd_xml_audio, "</sound>\n</iri>");
  return (fclose(fd_xml_audio));
}

void film::process_audio() {
  int len1;
  int len;
  int data_size;
  static unsigned int samples_size = 0;
  int i;
  uint8_t *ptr;

  ptr = packet.data;
  len = packet.size;

  while (len > 0) {
    this->audio_buf = av_frame_alloc();
    // (short *) av_fast_realloc (this->audio_buf, &samples_size, FFMAX
    // (packet.size, AVCODEC_MAX_AUDIO_FRAME_SIZE));
    data_size = samples_size;
    // DEPRECATED: len1 = avcodec_decode_audio (pCodecCtxAudio, audio_buf,
    // &data_size, ptr, len);
    len1 =
        avcodec_decode_audio4(pCodecCtxAudio, audio_buf, &data_size, &packet);

    if (len1 < 0) {
      // Error, breaking the frame
      len = 0;
      break;
    }

    len -= len1;
    ptr += len1;

    if (data_size > 0) {
      samples += data_size / (pCodecCtxAudio->channels * 2);
      for (i = 0; i < data_size; i += 2 * pCodecCtxAudio->channels) {
        sample_right = *((signed short int *)(audio_buf + i));
        /*
         * If it's only one channel: right sample = left sample
         */
        if (pCodecCtxAudio->channels >= 1)
          sample_left = *((signed short int *)(audio_buf + i + 2));
        else
          sample_left = sample_right;

        /*
         * Update minimum/maximum values:
         */
        if (minright > sample_right) minright = sample_right;
        if (maxright < sample_right) maxright = sample_right;
        if (minleft > sample_left) minleft = sample_left;
        if (maxleft < sample_left) maxleft = sample_left;

        /*
         * Get sample
         */
        if (ech++ == checknumber) {
          if (minright > minleft) minright = minleft;
          if (maxright > maxleft) maxright = maxleft;
          fprintf(fd_xml_audio, "<v c1d =\"%d\" c1u=\"%d\" />\n",
                  minright / RATIO, maxright / RATIO);
          minright = MAX_INT;
          maxright = MIN_INT;
          minleft = MAX_INT;
          maxleft = MIN_INT;

          /*
           * Reset sample number
           */
          ech = 0;
        }
      }
    }
  }
}

film::film() {
  // Initialization of default values (non GUI)
  display = 0;
  threshold = DEFAULT_THRESHOLD;
  max_scene_change_duration = -1;
  after_scene_change_offset = DEFAULT_OFFSET;
  last_change_begin_time = 0;
  last_change_end_time = 0;
  last_time = -1;
  in_change = false;
  start_frame = 0;
  end_frame = -1;
  samplearg = 1000;
  samples = 0;
  minright = MAX_INT;
  maxright = MIN_INT;
  minleft = MAX_INT;
  maxleft = MIN_INT;
  ech = 0;
  nchannel = 1;
  audio_buf = NULL;

  this->first_img_set = false;
  this->last_img_set = false;
  this->audio_set = false;
  this->video_set = false;
  this->thumb_set = false;
  this->shot_set = false;
}
