/*
*	This file is part of KNLMeansCL,
*	Copyright(C) 2014-2015  Khanattila.
*
*	KNLMeansCL is free software: you can redistribute it and/or modify
*	it under the terms of the GNU Lesser General Public License as published by
*	the Free Software Foundation, either version 3 of the License, or
*	(at your option) any later version.
*
*	KNLMeansCL is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*	GNU Lesser General Public License for more details.
*
*	You should have received a copy of the GNU Lesser General Public License
*	along with KNLMeansCL. If not, see <http://www.gnu.org/licenses/>.
*/

#define DFT_D 0
#define DFT_A 2
#define DFT_S 4
#define DFT_wmode 1
#define DFT_h 1.2f
#define DFT_ocl_device "DEFAULT"
#define DFT_lsb false
#define DFT_info false

#define RGB16BIT(r, g, b) ((r << 11) | (g << 5) | b)
#define RED_MASK(var) (((var) & 0xF800) >> 8)
#define GREEN_MASK(var) (((var) & 0x07E0) >> 3)
#define BLUE_MASK(var) (((var) & 0x001F) << 3)

#include "KNLMeansCL.h"

//////////////////////////////////////////
// OrderedDithering
static const uint8_t dither_red[64] = {
    1, 7, 3, 5, 0, 8, 2, 6,
    7, 1, 5, 3, 8, 0, 6, 2,
    3, 5, 0, 8, 2, 6, 1, 7,
    5, 3, 8, 0, 6, 2, 7, 1,
    0, 8, 2, 6, 1, 7, 3, 5,
    8, 0, 6, 2, 7, 1, 5, 3,
    2, 6, 1, 7, 3, 5, 0, 8,
    6, 2, 7, 1, 5, 3, 8, 0
};

static const uint8_t dither_green[64] = {
    1, 3, 2, 2, 3, 1, 2, 2,
    2, 2, 0, 4, 2, 2, 4, 0,
    3, 1, 2, 2, 1, 3, 2, 2,
    2, 2, 4, 0, 2, 2, 0, 4,
    1, 3, 2, 2, 3, 1, 2, 2,
    2, 2, 0, 4, 2, 2, 4, 0,
    3, 1, 2, 2, 1, 3, 2, 2,
    2, 2, 4, 0, 2, 2, 0, 4
};

static const uint8_t dither_blue[64] = {
    5, 3, 8, 0, 6, 2, 7, 1,
    3, 5, 0, 8, 2, 6, 1, 7,
    8, 0, 6, 2, 7, 1, 5, 3,
    0, 8, 2, 6, 1, 7, 3, 5,
    6, 2, 7, 1, 5, 3, 8, 0,
    2, 6, 1, 7, 3, 5, 0, 8,
    7, 1, 5, 3, 8, 0, 6, 2,
    1, 7, 3, 5, 0, 8, 2, 6
};

//////////////////////////////////////////
// AviSynthEquals
#ifdef __AVISYNTH_6_H__
inline bool KNLMeansClass::avs_equals(VideoInfo *v, VideoInfo *w) {
    return v->width == w->width && v->height == w->height && v->fps_numerator == w->fps_numerator
        && v->fps_denominator == w->fps_denominator && v->num_frames == w->num_frames;
}
#endif //__AVISYNTH_6_H__

//////////////////////////////////////////
// VapourSynthEquals
#ifdef VAPOURSYNTH_H
inline bool vs_equals(const VSVideoInfo *v, const VSVideoInfo *w) {
    return v->width == w->width && v->height == w->height && v->fpsNum == w->fpsNum && v->fpsDen == w->fpsDen
        && v->numFrames == w->numFrames && v->format == w->format;
}
#endif //__VAPOURSYNTH_H__

//////////////////////////////////////////
// AviSynthMemoryManagement 
#ifdef __AVISYNTH_6_H__
inline cl_uint KNLMeansClass::readBufferImage(PVideoFrame &frm, cl_command_queue command_queue,
    cl_mem image, const size_t origin[3], const size_t region[3]) {

    cl_uint ret = CL_SUCCESS;
    switch (color) {
        case Gray:
            if (lsb) {
                ret = clEnqueueReadImage(command_queue, image, CL_TRUE, origin, region,
                    idmn[0] * sizeof(uint16_t), 0, hostBuffer, 0, NULL, NULL);
                uint16_t *bufferp = (uint16_t*) hostBuffer;
                int pitch = frm->GetPitch(PLANAR_Y);
                uint8_t *msbp = frm->GetWritePtr(PLANAR_Y);
                uint8_t *lsbp = msbp + idmn[1] * pitch;
                int bidx, fidx;
                #pragma omp parallel for private(bidx, fidx)
                for (int y = 0; y < (int) idmn[1]; y++) {
                    bidx = y * idmn[0];
                    fidx = y * pitch;
                    for (int x = 0; x < (int) idmn[0]; x++) {
                        lsbp[fidx + x] = (uint8_t) (bufferp[bidx + x] & 0x00FF);
                        msbp[fidx + x] = (uint8_t) ((bufferp[bidx + x] & 0xFF00) >> 8);
                    }
                }
                break;
            } else {
                ret = clEnqueueReadImage(command_queue, image, CL_TRUE, origin, region,
                    frm->GetPitch(PLANAR_Y), 0, frm->GetWritePtr(PLANAR_Y), 0, NULL, NULL);
                break;
            }
        case YUV:
            if (lsb) {
                ret = clEnqueueReadImage(command_queue, image, CL_TRUE, origin, region,
                    idmn[0] * 4 * sizeof(uint16_t), 0, hostBuffer, 0, NULL, NULL);
                uint16_t *bufferp = (uint16_t*) hostBuffer;
                int pitch[3] = {
                    frm->GetPitch(PLANAR_Y),
                    frm->GetPitch(PLANAR_U),
                    frm->GetPitch(PLANAR_V)
                };
                uint8_t *msbp[3] = {
                    frm->GetWritePtr(PLANAR_Y),
                    frm->GetWritePtr(PLANAR_U),
                    frm->GetWritePtr(PLANAR_V)
                };
                uint8_t *lsbp[3] = {
                    msbp[0] + idmn[1] * pitch[0],
                    msbp[1] + idmn[1] * pitch[1],
                    msbp[2] + idmn[1] * pitch[2]
                };
                int bidx, fidx[3];
                   #pragma omp parallel for private(bidx, fidx)
                for (int y = 0; y < (int) idmn[1]; y++) {
                    bidx = y * idmn[0] * 4;
                    fidx[0] = y * pitch[0];
                    fidx[1] = y * pitch[1];
                    fidx[2] = y * pitch[2];
                    for (int x = 0; x < (int) idmn[0]; x++) {
                        lsbp[0][fidx[0] + x] = (uint8_t) (bufferp[bidx + x * 4] & 0x00FF);
                        msbp[0][fidx[0] + x] = (uint8_t) ((bufferp[bidx + x * 4] & 0xFF00) >> 8);
                        lsbp[1][fidx[1] + x] = (uint8_t) (bufferp[bidx + x * 4 + 1] & 0x00FF);
                        msbp[1][fidx[1] + x] = (uint8_t) ((bufferp[bidx + x * 4 + 1] & 0xFF00) >> 8);
                        lsbp[2][fidx[2] + x] = (uint8_t) (bufferp[bidx + x * 4 + 2] & 0x00FF);
                        msbp[2][fidx[2] + x] = (uint8_t) ((bufferp[bidx + x * 4 + 2] & 0xFF00) >> 8);
                    }
                }
                break;
            } else {
                ret = clEnqueueReadImage(command_queue, image, CL_TRUE, origin, region,
                    idmn[0] * 4 * sizeof(uint8_t), 0, hostBuffer, 0, NULL, NULL);
                uint8_t *bufferp = (uint8_t*) hostBuffer;
                int pitch[3] = {
                    frm->GetPitch(PLANAR_Y),
                    frm->GetPitch(PLANAR_U),
                    frm->GetPitch(PLANAR_V)
                };
                uint8_t *frmp[3] = {
                    frm->GetWritePtr(PLANAR_Y),
                    frm->GetWritePtr(PLANAR_U),
                    frm->GetWritePtr(PLANAR_V)
                };
                int bidx, fidx[3];
                #pragma omp parallel for private(bidx, fidx)
                for (int y = 0; y < (int) idmn[1]; y++) {
                    bidx = y * idmn[0] * 4;
                    fidx[0] = y * pitch[0];
                    fidx[1] = y * pitch[1];
                    fidx[2] = y * pitch[2];
                    for (int x = 0; x < (int) idmn[0]; x++) {
                        frmp[0][fidx[0] + x] = bufferp[bidx + x * 4];
                        frmp[1][fidx[1] + x] = bufferp[bidx + x * 4 + 1];
                        frmp[2][fidx[2] + x] = bufferp[bidx + x * 4 + 2];
                    }
                }
                break;
            }
        case RGB16: {
            ret = clEnqueueReadImage(command_queue, image, CL_TRUE, origin, region,
                idmn[0] * sizeof(uint16_t), 0, hostBuffer, 0, NULL, NULL);
            uint16_t *bufferp = (uint16_t*) hostBuffer;
            int pitch = frm->GetPitch();
            uint8_t *frmp = frm->GetWritePtr();
            int bidx, fidx;
            #pragma omp parallel for private(bidx, fidx)
            for (int y = 0; y < (int) idmn[1]; y++) {
                bidx = y * idmn[0];
                fidx = y * pitch;
                for (int x = 0; x < (int) idmn[0]; x++) {
                    frmp[fidx + x * 3] = BLUE_MASK(bufferp[bidx + x]);
                    frmp[fidx + x * 3 + 1] = GREEN_MASK(bufferp[bidx + x]);
                    frmp[fidx + x * 3 + 2] = RED_MASK(bufferp[bidx + x]);
                }
            }
            break;
        }
        case RGB32:
            ret = clEnqueueReadImage(command_queue, image, CL_TRUE, origin, region,
                frm->GetPitch(), 0, frm->GetWritePtr(), 0, NULL, NULL);
            break;
    }
    return ret;
}

inline cl_uint KNLMeansClass::writeBufferImage(PVideoFrame &frm, cl_command_queue command_queue,
    cl_mem image, const size_t origin[3], const size_t region[3]) {

    cl_uint ret = CL_SUCCESS;
    switch (color) {
        case Gray:
            if (lsb) {
                uint16_t *bufferp = (uint16_t*) hostBuffer;
                int pitch = frm->GetPitch(PLANAR_Y);
                const uint8_t *msbp = frm->GetReadPtr(PLANAR_Y);
                const uint8_t *lsbp = msbp + idmn[1] * pitch;
                int bidx, fidx;
                #pragma omp parallel for private(bidx, fidx)
                for (int y = 0; y < (int) idmn[1]; y++) {
                    bidx = y * idmn[0];
                    fidx = y * pitch;        
                    for (int x = 0; x < (int) idmn[0]; x++) {
                        bufferp[bidx + x] = msbp[fidx + x] << 8 | lsbp[fidx + x];
                    }
                }
                ret = clEnqueueWriteImage(command_queue, image, CL_TRUE, origin, region,
                    idmn[0] * sizeof(uint16_t), 0, hostBuffer, 0, NULL, NULL);
                break;
            } else {
                ret = clEnqueueWriteImage(command_queue, image, CL_TRUE, origin, region,
                    frm->GetPitch(PLANAR_Y), 0, frm->GetReadPtr(PLANAR_Y), 0, NULL, NULL);
                break;
            }
        case YUV:
            if (lsb) {
                uint16_t *bufferp = (uint16_t*) hostBuffer;
                int pitch[3] = {
                    frm->GetPitch(PLANAR_Y),
                    frm->GetPitch(PLANAR_U),
                    frm->GetPitch(PLANAR_V)
                };
                const uint8_t *msbp[3] = {
                    frm->GetReadPtr(PLANAR_Y),
                    frm->GetReadPtr(PLANAR_U),
                    frm->GetReadPtr(PLANAR_V)
                };
                const uint8_t *lsbp[3] = {
                    msbp[0] + idmn[1] * pitch[0],
                    msbp[1] + idmn[1] * pitch[1],
                    msbp[2] + idmn[1] * pitch[2]
                };
                int bidx, fidx[3];
                #pragma omp parallel for private(bidx, fidx)
                for (int y = 0; y < (int) idmn[1]; y++) {
                    bidx = y * idmn[0] * 4;
                    fidx[0] = y * pitch[0];
                    fidx[1] = y * pitch[1];
                    fidx[2] = y * pitch[2];
                    for (int x = 0; x < (int) idmn[0]; x++) {
                        bufferp[bidx + x * 4] = (msbp[0][fidx[0] + x] << 8) | lsbp[0][fidx[0] + x];
                        bufferp[bidx + x * 4 + 1] = (msbp[1][fidx[1] + x] << 8) | lsbp[1][fidx[1] + x];
                        bufferp[bidx + x * 4 + 2] = (msbp[2][fidx[2] + x] << 8) | lsbp[2][fidx[2] + x];
                        bufferp[bidx + x * 4 + 3] = 0;
                    }
                }
                ret = clEnqueueWriteImage(command_queue, image, CL_TRUE, origin, region,
                    idmn[0] * 4 * sizeof(uint16_t), 0, hostBuffer, 0, NULL, NULL);
                break;
            } else {
                uint8_t *bufferp = (uint8_t*) hostBuffer;
                int pitch[3] = {
                    frm->GetPitch(PLANAR_Y),
                    frm->GetPitch(PLANAR_U),
                    frm->GetPitch(PLANAR_V)
                };
                const uint8_t *frmp[3] = {
                    frm->GetReadPtr(PLANAR_Y),
                    frm->GetReadPtr(PLANAR_U),
                    frm->GetReadPtr(PLANAR_V)
                };
                int bidx, fidx[3];
                #pragma omp parallel for private(bidx, fidx)
                for (int y = 0; y < (int) idmn[1]; y++) {
                    bidx = y * idmn[0] * 4;
                    fidx[0] = y * pitch[0];
                    fidx[1] = y * pitch[1];
                    fidx[2] = y * pitch[2];
                    for (int x = 0; x < (int) idmn[0]; x++) {
                        bufferp[bidx + x * 4] = frmp[0][fidx[0] + x];
                        bufferp[bidx + x * 4 + 1] = frmp[1][fidx[1] + x];
                        bufferp[bidx + x * 4 + 2] = frmp[2][fidx[2] + x];
                        bufferp[bidx + x * 4 + 3] = 0;
                    }
                }
                ret = clEnqueueWriteImage(command_queue, image, CL_TRUE, origin, region,
                    idmn[0] * 4 * sizeof(uint8_t), 0, hostBuffer, 0, NULL, NULL);
                break;
            }
        case RGB16: {
            uint16_t *bufferp = (uint16_t*) hostBuffer;
            int pitch = frm->GetPitch();
            const uint8_t *frmp = frm->GetReadPtr();
            int bidx, fidx;
            #pragma omp parallel for private(bidx, fidx)
            for (int y = 0; y < (int) idmn[1]; y++) {
                bidx = y * idmn[0];
                fidx = y * pitch;
                for (int x = 0; x < (int) idmn[0]; x++) {
                    uint8_t tid = ((y & 7) << 3) + (x & 7);
                    uint8_t r = fastmin(frmp[fidx + x * 3 + 2] + dither_red[tid], 0xFF) >> 3;
                    uint8_t g = fastmin(frmp[fidx + x * 3 + 1] + dither_green[tid], 0xFF) >> 2;
                    uint8_t b = fastmin(frmp[fidx + x * 3] + dither_blue[tid], 0xFF) >> 3;
                    bufferp[bidx + x] = RGB16BIT(r, g, b);
                }
            }
            ret = clEnqueueWriteImage(command_queue, image, CL_TRUE, origin, region,
                idmn[0] * sizeof(uint16_t), 0, hostBuffer, 0, NULL, NULL);
            break;
        }
        case RGB32:
            ret = clEnqueueWriteImage(command_queue, image, CL_TRUE, origin, region,
                frm->GetPitch(), 0, frm->GetReadPtr(), 0, NULL, NULL);
            break;
    }
    return ret;
}
#endif //__AVISYNTH_6_H__

//////////////////////////////////////////
// VapourSynthMemoryManagement
#ifdef VAPOURSYNTH_H
inline cl_uint readBufferImage(KNLMeansData *data, const VSAPI *vsapi, VSFrameRef *frm,
    cl_command_queue command_queue, cl_mem image, const size_t origin[3], const size_t region[3]) {

    cl_uint ret = CL_SUCCESS;
    switch (data->color) {
        case Gray:
            ret = clEnqueueReadImage(command_queue, image, CL_TRUE, origin, region,
                vsapi->getStride(frm, 0), 0, vsapi->getWritePtr(frm, 0), 0, NULL, NULL);
            break;
        case YUV:
        case RGB32: {
            int bsample = data->vi->format->bytesPerSample;
            ret = clEnqueueReadImage(command_queue, image, CL_TRUE, origin, region,
                data->idmn[0] * 4 * bsample, 0, data->hostBuffer, 0, NULL, NULL);
            int pitch[3] = {
                vsapi->getStride(frm, 0),
                vsapi->getStride(frm, 1),
                vsapi->getStride(frm, 2)
            };
            int bidx, fidx[3];
            switch (bsample) {
                case 1: {
                    uint8_t *bufferp = (uint8_t*) data->hostBuffer;
                    uint8_t *frmp[3] = {
                        vsapi->getWritePtr(frm, 0),
                        vsapi->getWritePtr(frm, 1),
                        vsapi->getWritePtr(frm, 2)
                    };
                    #pragma omp parallel for private(bidx, fidx)
                    for (int y = 0; y < (int) data->idmn[1]; y++) {
                        bidx = y * data->idmn[0] * 4;
                        fidx[0] = y * pitch[0];
                        fidx[1] = y * pitch[1];
                        fidx[2] = y * pitch[2];
                        for (int x = 0; x < (int) data->idmn[0]; x++) {
                            frmp[0][fidx[0] + x] = bufferp[bidx + x * 4];
                            frmp[1][fidx[1] + x] = bufferp[bidx + x * 4 + 1];
                            frmp[2][fidx[2] + x] = bufferp[bidx + x * 4 + 2];
                        }
                    }
                    break;
                }
                case 2: {
                    uint16_t *bufferp = (uint16_t*) data->hostBuffer;
                    uint16_t *frmp[3] = {
                        (uint16_t*) vsapi->getWritePtr(frm, 0),
                        (uint16_t*) vsapi->getWritePtr(frm, 1),
                        (uint16_t*) vsapi->getWritePtr(frm, 2)
                    };
                    #pragma omp parallel for private(bidx, fidx)
                    for (int y = 0; y < (int) data->idmn[1]; y++) {
                        bidx = y * data->idmn[0] * 4;
                        fidx[0] = y * pitch[0];
                        fidx[1] = y * pitch[1];
                        fidx[2] = y * pitch[2];
                        for (int x = 0; x < (int) data->idmn[0]; x++) {
                            frmp[0][fidx[0] + x] = bufferp[bidx + x * 4];
                            frmp[1][fidx[1] + x] = bufferp[bidx + x * 4 + 1];
                            frmp[2][fidx[2] + x] = bufferp[bidx + x * 4 + 2];
                        }
                    }
                    break;
                }
                case 4: {
                    uint32_t *bufferp = (uint32_t*) data->hostBuffer;
                    uint32_t *frmp[3] = {
                        (uint32_t*) vsapi->getWritePtr(frm, 0),
                        (uint32_t*) vsapi->getWritePtr(frm, 1),
                        (uint32_t*) vsapi->getWritePtr(frm, 2)
                    };
                    #pragma omp parallel for private(bidx, fidx)
                    for (int y = 0; y < (int) data->idmn[1]; y++) {
                        bidx = y * data->idmn[0] * 4;
                        fidx[0] = y * pitch[0];
                        fidx[1] = y * pitch[1];
                        fidx[2] = y * pitch[2];
                        for (int x = 0; x < (int) data->idmn[0]; x++) {
                            frmp[0][fidx[0] + x] = bufferp[bidx + x * 4];
                            frmp[1][fidx[1] + x] = bufferp[bidx + x * 4 + 1];
                            frmp[2][fidx[2] + x] = bufferp[bidx + x * 4 + 2];
                        }
                    }
                    break;
                }
            }
        }
    }
    return ret;
}

inline cl_uint writeBufferImage(KNLMeansData *data, const VSAPI *vsapi, const VSFrameRef *frm,
    cl_command_queue command_queue, cl_mem image, const size_t origin[3], const size_t region[3]) {

    cl_uint ret = CL_SUCCESS;
    switch (data->color) {
        case Gray:
            ret = clEnqueueWriteImage(command_queue, image, CL_TRUE, origin, region,
                vsapi->getStride(frm, 0), 0, vsapi->getReadPtr(frm, 0), 0, NULL, NULL);
            break;
        case YUV:
        case RGB32: {
            int bsample = data->vi->format->bytesPerSample;
            int pitch[3] = {
                vsapi->getStride(frm, 0),
                vsapi->getStride(frm, 1),
                vsapi->getStride(frm, 2)
            };
            int bidx, fidx[3];
            switch (bsample) {
                case 1: {
                    uint8_t *bufferp = (uint8_t*) data->hostBuffer;
                    const uint8_t *frmp[3] = {
                        vsapi->getReadPtr(frm, 0),
                        vsapi->getReadPtr(frm, 1),
                        vsapi->getReadPtr(frm, 2)
                    };
                    #pragma omp parallel for private(bidx, fidx)
                    for (int y = 0; y < (int) data->idmn[1]; y++) {
                        bidx = y * data->idmn[0] * 4;
                        fidx[0] = y * pitch[0];
                        fidx[1] = y * pitch[1];
                        fidx[2] = y * pitch[2];
                        for (int x = 0; x < (int) data->idmn[0]; x++) {
                            bufferp[bidx + x * 4] = frmp[0][fidx[0] + x];
                            bufferp[bidx + x * 4 + 1] = frmp[1][fidx[1] + x];
                            bufferp[bidx + x * 4 + 2] = frmp[2][fidx[2] + x];
                            bufferp[bidx + x * 4 + 3] = 0;
                        }
                    }
                    break;
                }
                case 2: {
                    uint16_t *bufferp = (uint16_t*) data->hostBuffer;
                    const uint16_t *frmp[3] = {
                        (uint16_t*) vsapi->getReadPtr(frm, 0),
                        (uint16_t*) vsapi->getReadPtr(frm, 1),
                        (uint16_t*) vsapi->getReadPtr(frm, 2)
                    };
                    #pragma omp parallel for private(bidx, fidx)
                    for (int y = 0; y < (int) data->idmn[1]; y++) {
                        bidx = y * data->idmn[0] * 4;
                        fidx[0] = y * pitch[0];
                        fidx[1] = y * pitch[1];
                        fidx[2] = y * pitch[2];
                        for (int x = 0; x < (int) data->idmn[0]; x++) {
                            bufferp[bidx + x * 4] = frmp[0][fidx[0] + x];
                            bufferp[bidx + x * 4 + 1] = frmp[1][fidx[1] + x];
                            bufferp[bidx + x * 4 + 2] = frmp[2][fidx[2] + x];
                            bufferp[bidx + x * 4 + 3] = 0;
                        }
                    }
                    break;
                }
                case 4: {
                    uint32_t *bufferp = (uint32_t*) data->hostBuffer;
                    const uint32_t *frmp[3] = {
                        (uint32_t*) vsapi->getReadPtr(frm, 0),
                        (uint32_t*) vsapi->getReadPtr(frm, 1),
                        (uint32_t*) vsapi->getReadPtr(frm, 2)
                    };
                    #pragma omp parallel for private(bidx, fidx)
                    for (int y = 0; y < (int) data->idmn[1]; y++) {
                        bidx = y * data->idmn[0] * 4;
                        fidx[0] = y * pitch[0];
                        fidx[1] = y * pitch[1];
                        fidx[2] = y * pitch[2];
                        for (int x = 0; x < (int) data->idmn[0]; x++) {
                            bufferp[bidx + x * 4] = frmp[0][fidx[0] + x];
                            bufferp[bidx + x * 4 + 1] = frmp[1][fidx[1] + x];
                            bufferp[bidx + x * 4 + 2] = frmp[2][fidx[2] + x];
                            bufferp[bidx + x * 4 + 3] = 0;
                        }
                    }
                    break;
                }
            }
            ret = clEnqueueWriteImage(command_queue, image, CL_TRUE, origin, region,
                data->idmn[0] * 4 * bsample, 0, data->hostBuffer, 0, NULL, NULL);
        }
    }
    return ret;
}
#endif //__VAPOURSYNTH_H__

//////////////////////////////////////////
// AviSynthInit
#ifdef __AVISYNTH_6_H__
KNLMeansClass::KNLMeansClass(PClip _child, const int _D, const int _A, const int _S, const int _wmode, const double _h,
    PClip _baby, const char* _ocl_device, const bool _lsb, const bool _info, IScriptEnvironment* env) :
    GenericVideoFilter(_child), D(_D), A(_A), S(_S), wmode(_wmode), h(_h), baby(_baby), ocl_device(_ocl_device),
    lsb(_lsb), info(_info) {

    // Checks AviSynth Version.
    env->CheckVersion(6);
    child->SetCacheHints(CACHE_WINDOW, D);
    baby->SetCacheHints(CACHE_WINDOW, D);

    // Checks source clip and rclip.
    VideoInfo rvi = baby->GetVideoInfo();
    cl_channel_order corder;
    cl_channel_type ctype;
    if (!avs_equals(&vi, &rvi))
        env->ThrowError("KNLMeansCL: rclip do not math source clip!");
    if (vi.IsPlanar() && (vi.IsY8() || vi.IsYV411() || vi.IsYV12() || vi.IsYV16())) {
        color = Gray;
        corder = CL_LUMINANCE;
        ctype = lsb ? CL_UNORM_INT16 : CL_UNORM_INT8;
    } else if (vi.IsPlanar() && vi.IsYV24()) {
        color = YUV;
        corder = CL_RGBA;
        ctype = lsb ? CL_UNORM_INT16 : CL_UNORM_INT8;
    } else if (vi.IsRGB() && vi.IsRGB24()) {
        color = RGB16;
        corder = CL_R;
        ctype = CL_UNSIGNED_INT16;
    } else if (vi.IsRGB() && vi.IsRGB32()) {
        color = RGB32;
        corder = CL_RGBA;
        ctype = CL_UNORM_INT8;
    } else {
        env->ThrowError("KNLMeansCL: planar YUV or RGB data!");
    }
     
    // Checks user value.
    if (vi.IsRGB() && lsb)
        env->ThrowError("KNLMeansCL: RGB48y is not supported!");
    if (D < 0)
        env->ThrowError("KNLMeansCL: D must be greater than or equal to 0!");
    if (A < 0)
        env->ThrowError("KNLMeansCL: A must be greater than or equal to 0!");
    if (S < 0 || S > 4)
        env->ThrowError("KNLMeansCL: S must be in range [0, 4]!");
    if (wmode < 0 || wmode > 2)
        env->ThrowError("KNLMeansCL: wmode must be in range [0, 2]!");
    if (h <= 0.0f)
        env->ThrowError("KNLMeansCL: h must be greater than 0!");
    cl_device_type device;
    if (!strcasecmp(ocl_device, "CPU")) {
        device = CL_DEVICE_TYPE_CPU;
    } else if (!strcasecmp(ocl_device, "GPU")) {
        device = CL_DEVICE_TYPE_GPU;
    } else if (!strcasecmp(ocl_device, "ACCELERATOR")) {
        device = CL_DEVICE_TYPE_ACCELERATOR;
    } else if (!strcasecmp(ocl_device, "DEFAULT")) {
        device = CL_DEVICE_TYPE_DEFAULT;
    } else {
        env->ThrowError("KNLMeansCL: device_type must be cpu, gpu, accelerator or default!");
    }

    // Gets PlatformID and DeviceID.
    cl_uint num_platforms = 0, num_devices = 0;
    cl_bool img_support = CL_FALSE, device_aviable = CL_FALSE;
    cl_int ret = clGetPlatformIDs(0, NULL, &num_platforms);
    if (num_platforms == 0) {
        env->ThrowError("KNLMeansCL: no opencl platforms available!");
    }
    cl_platform_id *temp_platforms = (cl_platform_id*) malloc(sizeof(cl_platform_id) * num_platforms);
    ret |= clGetPlatformIDs(num_platforms, temp_platforms, NULL);
    if (ret != CL_SUCCESS) {
        env->ThrowError("KNLMeansCL: AviSynthCreate error(0)!");
    }
    for (cl_uint i = 0; i < num_platforms; i++) {
        ret |= clGetDeviceIDs(temp_platforms[i], device, 0, 0, &num_devices);
        cl_device_id *temp_devices = (cl_device_id*) malloc(sizeof(cl_device_id) * num_devices);
        ret |= clGetDeviceIDs(temp_platforms[i], device, num_devices, temp_devices, NULL);
        for (cl_uint j = 0; j < num_devices; j++) {
            ret |= clGetDeviceInfo(temp_devices[j], CL_DEVICE_IMAGE_SUPPORT, sizeof(cl_bool), &img_support, NULL);
            if (device_aviable == CL_FALSE && img_support == CL_TRUE) {
                platformID = temp_platforms[i];
                deviceID = temp_devices[j];
                device_aviable = CL_TRUE;
            }
        }
        free(temp_devices);
    }
    free(temp_platforms);
    if (ret != CL_SUCCESS && ret != CL_DEVICE_NOT_FOUND) {
        env->ThrowError("KNLMeansCL: AviSynthCreate error(1)!");
    } else if (device_aviable == CL_FALSE) {
        env->ThrowError("KNLMeansCL: opencl device not available!");
    }

    // Creates an OpenCL context, 2D images and buffers object.
    context = clCreateContext(NULL, 1, &deviceID, NULL, NULL, NULL);
    const cl_image_format image_format = { corder, ctype };
    idmn[0] = vi.width;
    idmn[1] = lsb ? (vi.height / 2) : (vi.height);
    const size_t size = sizeof(float) * idmn[0] * idmn[1];
    mem_in[0] = clCreateImage2D(context, CL_MEM_READ_ONLY, &image_format, idmn[0], idmn[1], 0, NULL, &ret);
    if (ret == CL_IMAGE_FORMAT_NOT_SUPPORTED) 
        env->ThrowError("KNLMeansCL: image format is not supported by your device!");
    mem_in[2] = clCreateImage2D(context, CL_MEM_READ_ONLY, &image_format, idmn[0], idmn[1], 0, NULL, NULL);
    if (D) {
        mem_in[1] = clCreateImage2D(context, CL_MEM_READ_ONLY, &image_format, idmn[0], idmn[1], 0, NULL, NULL);
        mem_in[3] = clCreateImage2D(context, CL_MEM_READ_ONLY, &image_format, idmn[0], idmn[1], 0, NULL, NULL);
    }
    mem_out = clCreateImage2D(context, CL_MEM_WRITE_ONLY, &image_format, idmn[0], idmn[1], 0, NULL, NULL);
    mem_U[0] = clCreateBuffer(context, CL_MEM_READ_WRITE, color ? 4 * size : 2 * size, NULL, NULL);
    mem_U[1] = clCreateBuffer(context, CL_MEM_READ_WRITE, size, NULL, NULL);
    mem_U[2] = clCreateBuffer(context, CL_MEM_READ_WRITE, size, NULL, NULL);
    mem_U[3] = clCreateBuffer(context, CL_MEM_READ_WRITE, size, NULL, NULL);

    // Host buffer.
    if (lsb) {
        if (color == Gray) hostBuffer = malloc(idmn[0] * idmn[1] * sizeof(uint16_t));
        else if (color == YUV) hostBuffer = malloc(idmn[0] * idmn[1] * 4 * sizeof(uint16_t));
    } else {
        if (color == YUV) hostBuffer = malloc(idmn[0] * idmn[1] * 4 * sizeof(uint8_t));
        else if (color == RGB16) hostBuffer = malloc(idmn[0] * idmn[1] * sizeof(uint16_t));
        else if (color == RGB32) hostBuffer = malloc(idmn[0] * idmn[1] * 4 * sizeof(uint8_t));
    }

    // Creates and Build a program executable from the program source.
    program = clCreateProgramWithSource(context, 1, &source_code, NULL, NULL);
    char options[2048];
    snprintf(options, 2048, "-cl-denorms-are-zero -cl-fast-relaxed-math -Werror \
        -D H_BLOCK_X=%i -D H_BLOCK_Y=%i -D V_BLOCK_X=%i -D V_BLOCK_Y=%i \
        -D NLMK_TCOLOR=%i -D NLMK_S=%i -D NLMK_WMODE=%i -D NLMK_TEMPORAL=%i -D NLMK_H2_INV_NORM=%ff",
         H_BLOCK_X, H_BLOCK_Y, V_BLOCK_X, V_BLOCK_Y,
         color, S, wmode, D, 65025.0 / (h*h*(2 * S + 1) * (2 * S + 1)));
    ret = clBuildProgram(program, 1, &deviceID, options, NULL, NULL);
    if (ret != CL_SUCCESS) {
        size_t log_size;
        clGetProgramBuildInfo(program, deviceID, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
        char *log = (char*) malloc(log_size);
        clGetProgramBuildInfo(program, deviceID, CL_PROGRAM_BUILD_LOG, log_size, log, NULL);
        std::ofstream outfile("KNLMeansCL.txt", std::ofstream::binary);
        outfile.write(log, log_size);
        outfile.close();
        free(log);
        env->ThrowError("KNLMeansCL: AviSynthCreate error (2)!");
    }

    // Creates kernel objects.
    kernel[0] = clCreateKernel(program, "NLM_init", NULL);
    kernel[1] = clCreateKernel(program, "NLM_dist", NULL);
    kernel[2] = clCreateKernel(program, "NLM_horiz", NULL);
    kernel[3] = clCreateKernel(program, "NLM_vert", NULL);
    kernel[4] = clCreateKernel(program, "NLM_accu", NULL);
    kernel[5] = clCreateKernel(program, "NLM_finish", NULL);

    // Sets kernel arguments.
    ret = clSetKernelArg(kernel[0], 0, sizeof(cl_mem), &mem_U[0]);
    ret |= clSetKernelArg(kernel[0], 1, sizeof(cl_mem), &mem_U[3]);
    ret |= clSetKernelArg(kernel[0], 2, 2 * sizeof(cl_uint), &idmn);
    ret |= clSetKernelArg(kernel[1], 0, sizeof(cl_mem), &mem_in[2]);
    ret |= clSetKernelArg(kernel[1], 1, sizeof(cl_mem), &mem_in[D ? 3 : 2]);
    ret |= clSetKernelArg(kernel[1], 2, sizeof(cl_mem), &mem_U[1]);
    ret |= clSetKernelArg(kernel[1], 3, 2 * sizeof(cl_uint), &idmn);
    ret |= clSetKernelArg(kernel[2], 0, sizeof(cl_mem), &mem_U[1]);
    ret |= clSetKernelArg(kernel[2], 1, sizeof(cl_mem), &mem_U[2]);
    ret |= clSetKernelArg(kernel[2], 2, 2 * sizeof(cl_uint), &idmn);
    ret |= clSetKernelArg(kernel[3], 0, sizeof(cl_mem), &mem_U[2]);
    ret |= clSetKernelArg(kernel[3], 1, sizeof(cl_mem), &mem_U[1]);
    ret |= clSetKernelArg(kernel[3], 2, 2 * sizeof(cl_uint), &idmn);
    ret |= clSetKernelArg(kernel[4], 0, sizeof(cl_mem), &mem_in[D ? 1 : 0]);
    ret |= clSetKernelArg(kernel[4], 1, sizeof(cl_mem), &mem_U[0]);
    ret |= clSetKernelArg(kernel[4], 2, sizeof(cl_mem), &mem_U[1]);
    ret |= clSetKernelArg(kernel[4], 3, sizeof(cl_mem), &mem_U[3]);
    ret |= clSetKernelArg(kernel[4], 4, 2 * sizeof(cl_uint), &idmn);
    ret |= clSetKernelArg(kernel[5], 0, sizeof(cl_mem), &mem_in[0]);
    ret |= clSetKernelArg(kernel[5], 1, sizeof(cl_mem), &mem_out);
    ret |= clSetKernelArg(kernel[5], 2, sizeof(cl_mem), &mem_U[0]);
    ret |= clSetKernelArg(kernel[5], 3, sizeof(cl_mem), &mem_U[3]);
    ret |= clSetKernelArg(kernel[5], 4, 2 * sizeof(cl_uint), &idmn);
    if (ret != CL_SUCCESS) 	env->ThrowError("KNLMeansCL: AviSynthCreate error (3)!");
}
#endif //__AVISYNTH_6_H__

//////////////////////////////////////////
// VapourSynthInit
#ifdef VAPOURSYNTH_H
static void VS_CC knlmeansInit(VSMap *in, VSMap *out, void **instanceData,
    VSNode *node, VSCore *core, const VSAPI *vsapi) {

    KNLMeansData *d = (KNLMeansData*) *instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}
#endif //__VAPOURSYNTH_H__

//////////////////////////////////////////
// AviSynthGetFrame
#ifdef __AVISYNTH_6_H__
PVideoFrame __stdcall KNLMeansClass::GetFrame(int n, IScriptEnvironment* env) {
    // Variables.
    PVideoFrame src = child->GetFrame(n, env);
    PVideoFrame ref = baby->GetFrame(n, env);
    const size_t origin[3] = { 0, 0, 0 };
    const size_t region[3] = { idmn[0], idmn[1], 1 };
    const size_t global_work[2] = {
        mrounds(idmn[0], fastmax(H_BLOCK_X, V_BLOCK_X)),
        mrounds(idmn[1], fastmax(H_BLOCK_Y, V_BLOCK_Y))
    };
    const size_t local_horiz[2] = { H_BLOCK_X, H_BLOCK_Y };
    const size_t local_vert[2] = { V_BLOCK_X, V_BLOCK_Y };

    //Copy chroma.
    PVideoFrame dst = env->NewVideoFrame(vi);
    if (!vi.IsY8() && color == Gray) {
        env->BitBlt(dst->GetWritePtr(PLANAR_U), dst->GetPitch(PLANAR_U), src->GetReadPtr(PLANAR_U),
            src->GetPitch(PLANAR_U), src->GetRowSize(PLANAR_U), src->GetHeight(PLANAR_U));
        env->BitBlt(dst->GetWritePtr(PLANAR_V), dst->GetPitch(PLANAR_V), src->GetReadPtr(PLANAR_V),
            src->GetPitch(PLANAR_V), src->GetRowSize(PLANAR_V), src->GetHeight(PLANAR_V));
    }
    cl_command_queue command_queue = clCreateCommandQueue(context, deviceID, 0, NULL);

    // Processing.
    cl_int ret = CL_SUCCESS;
    ret |= writeBufferImage(src, command_queue, mem_in[0], origin, region);
    ret |= writeBufferImage(ref, command_queue, mem_in[2], origin, region);
    ret |= clEnqueueNDRangeKernel(command_queue, kernel[0], 2, NULL, global_work, NULL, 0, NULL, NULL);
    if (D) {
        // Temporal.
        for (int k = -D; k <= D; k++) {
            src = child->GetFrame(n + k, env);
            ref = baby->GetFrame(n + k, env);
            ret |= writeBufferImage(src, command_queue, mem_in[1], origin, region);
            ret |= writeBufferImage(ref, command_queue, mem_in[3], origin, region);
            for (int j = -A; j <= A; j++) {
                for (int i = -A; i <= A; i++) {
                    if (k || j || i) {
                        const cl_int q[2] = { i, j };
                        ret |= clSetKernelArg(kernel[1], 4, 2 * sizeof(cl_int), &q);
                        ret |= clEnqueueNDRangeKernel(command_queue, kernel[1],
                            2, NULL, global_work, NULL, 0, NULL, NULL);
                        ret |= clEnqueueNDRangeKernel(command_queue, kernel[2],
                            2, NULL, global_work, local_horiz, 0, NULL, NULL);
                        ret |= clEnqueueNDRangeKernel(command_queue, kernel[3],
                            2, NULL, global_work, local_vert, 0, NULL, NULL);
                        ret |= clSetKernelArg(kernel[4], 5, 2 * sizeof(cl_int), &q);
                        ret |= clEnqueueNDRangeKernel(command_queue, kernel[4],
                            2, NULL, global_work, NULL, 0, NULL, NULL);
                    }
                }
            }
        }
    } else {
        // Spatial.
        for (int j = -A; j <= 0; j++) {
            for (int i = -A; i <= A; i++) {
                if (j * (2 * A + 1) + i < 0) {
                    const cl_int q[2] = { i, j };
                    ret |= clSetKernelArg(kernel[1], 4, 2 * sizeof(cl_int), &q);
                    ret |= clEnqueueNDRangeKernel(command_queue, kernel[1],
                        2, NULL, global_work, NULL, 0, NULL, NULL);
                    ret |= clEnqueueNDRangeKernel(command_queue, kernel[2],
                        2, NULL, global_work, local_horiz, 0, NULL, NULL);
                    ret |= clEnqueueNDRangeKernel(command_queue, kernel[3],
                        2, NULL, global_work, local_vert, 0, NULL, NULL);
                    ret |= clSetKernelArg(kernel[4], 5, 2 * sizeof(cl_int), &q);
                    ret |= clEnqueueNDRangeKernel(command_queue, kernel[4],
                        2, NULL, global_work, NULL, 0, NULL, NULL);
                }
            }
        }
    }
    ret |= clEnqueueNDRangeKernel(command_queue, kernel[5], 2, NULL, global_work, NULL, 0, NULL, NULL);
    ret |= readBufferImage(dst, command_queue, mem_out, origin, region);
    ret |= clFinish(command_queue);
    ret |= clReleaseCommandQueue(command_queue);
    if (ret != CL_SUCCESS) env->ThrowError("KNLMeansCL: AviSynthGetFrame error!");

    // Info.
    if (info) {
        uint8_t y = 0, *frm = dst->GetWritePtr(PLANAR_Y);
        int pitch = dst->GetPitch(PLANAR_Y);
        char buffer[2048], str[2048], str1[2048];
        DrawString(frm, pitch, 0, y++, "KNLMeansCL");
        DrawString(frm, pitch, 0, y++, " Version " VERSION);
        DrawString(frm, pitch, 0, y++, " Copyright(C) Khanattila");
        snprintf(buffer, 2048, " D:%li  A:%lix%li  S:%lix%li", 2 * D + 1, 2 * A + 1, 2 * A + 1, 2 * S + 1, 2 * S + 1);
        DrawString(frm, pitch, 0, y++, buffer);
        snprintf(buffer, 2048, " Iterations: %li", ((2 * D + 1)*(2 * A + 1)*(2 * A + 1) - 1) / (D ? 1 : 2));
        DrawString(frm, pitch, 0, y++, buffer);
        snprintf(buffer, 2048, " Global work size: %lux%lu",
            (unsigned long) global_work[0], (unsigned long) global_work[1]);
        DrawString(frm, pitch, 0, y++, buffer);
        DrawString(frm, pitch, 0, y++, "Platform info");
        ret |= clGetPlatformInfo(platformID, CL_PLATFORM_NAME, sizeof(char) * 2048, str, NULL);
        snprintf(buffer, 2048, " Name: %s", str);
        DrawString(frm, pitch, 0, y++, buffer);
        ret |= clGetPlatformInfo(platformID, CL_PLATFORM_VENDOR, sizeof(char) * 2048, str, NULL);
        snprintf(buffer, 2048, " Vendor: %s", str);
        DrawString(frm, pitch, 0, y++, buffer);
        ret |= clGetPlatformInfo(platformID, CL_PLATFORM_VERSION, sizeof(char) * 2048, str, NULL);
        snprintf(buffer, 2048, " Version: %s", str);
        DrawString(frm, pitch, 0, y++, buffer);
        DrawString(frm, pitch, 0, y++, "Device info");
        ret |= clGetDeviceInfo(deviceID, CL_DEVICE_NAME, sizeof(char) * 2048, str, NULL);
        snprintf(buffer, 2048, " Name: %s", str);
        DrawString(frm, pitch, 0, y++, buffer);
        ret |= clGetDeviceInfo(deviceID, CL_DEVICE_VENDOR, sizeof(char) * 2048, str, NULL);
        snprintf(buffer, 2048, " Vendor: %s", str);
        DrawString(frm, pitch, 0, y++, buffer);
        ret |= clGetDeviceInfo(deviceID, CL_DEVICE_VERSION, sizeof(char) * 2048, str, NULL);
        ret |= clGetDeviceInfo(deviceID, CL_DRIVER_VERSION, sizeof(char) * 2048, str1, NULL);
        snprintf(buffer, 2048, " Version: %s %s", str, str1);
        DrawString(frm, pitch, 0, y++, buffer);
        if (ret != CL_SUCCESS) env->ThrowError("KNLMeansCL: AviSynthInfo error!");
    }
    return dst;
}
#endif //__AVISYNTH_6_H__

//////////////////////////////////////////
// VapourSynthGetFrame
#ifdef VAPOURSYNTH_H
static const VSFrameRef *VS_CC VapourSynthPluginGetFrame(int n, int activationReason, void **instanceData,
    void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {

    KNLMeansData *d = (KNLMeansData*) *instanceData;
    const int maxframe = d->vi->numFrames - 1;
    if (activationReason == arInitial) {
        for (int k = int64ToIntS(-d->d); k <= d->d; k++) {
            vsapi->requestFrameFilter(clamp(n + k, 0, maxframe), d->node, frameCtx);
            vsapi->requestFrameFilter(clamp(n + k, 0, maxframe), d->knot, frameCtx);
        }
    } else if (activationReason == arAllFramesReady) {
        // Variables.
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFrameRef *ref = vsapi->getFrameFilter(n, d->knot, frameCtx);
        const VSFormat *fi = d->vi->format;
        const size_t origin[3] = { 0, 0, 0 };
        const size_t region[3] = { d->idmn[0], d->idmn[1], 1 };
        const size_t global_work[2] = {
            mrounds(d->idmn[0], fastmax(H_BLOCK_X, V_BLOCK_X)),
            mrounds(d->idmn[1], fastmax(H_BLOCK_Y, V_BLOCK_Y))
        };
        const size_t local_horiz[2] = { H_BLOCK_X, H_BLOCK_Y };
        const size_t local_vert[2] = { V_BLOCK_X, V_BLOCK_Y };

        //Copy chroma.
        VSFrameRef *dst = vsapi->newVideoFrame(fi, d->idmn[0], d->idmn[1], NULL, core);
        if (fi->colorFamily == cmYUV || fi->colorFamily == cmYCoCg) {
            vs_bitblt(vsapi->getWritePtr(dst, 1), vsapi->getStride(dst, 1), vsapi->getReadPtr(src, 1),
                vsapi->getStride(src, 1), vsapi->getFrameWidth(src, 1) * fi->bytesPerSample,
                vsapi->getFrameHeight(src, 1));
            vs_bitblt(vsapi->getWritePtr(dst, 2), vsapi->getStride(dst, 2), vsapi->getReadPtr(src, 2),
                vsapi->getStride(src, 2), vsapi->getFrameWidth(src, 2) * fi->bytesPerSample,
                vsapi->getFrameHeight(src, 2));
        }
        cl_command_queue command_queue = clCreateCommandQueue(d->context, d->deviceID, 0, NULL);

        // Processing.
        cl_int ret = CL_SUCCESS;
        ret |= clEnqueueWriteImage(command_queue, d->mem_in[0], CL_TRUE, origin, region,
            vsapi->getStride(src, 0), 0, vsapi->getReadPtr(src, 0), 0, NULL, NULL);
        ret |= clEnqueueWriteImage(command_queue, d->mem_in[2], CL_TRUE, origin, region,
            vsapi->getStride(ref, 0), 0, vsapi->getReadPtr(ref, 0), 0, NULL, NULL);
        vsapi->freeFrame(src);
        vsapi->freeFrame(ref);
        ret |= clEnqueueNDRangeKernel(command_queue, d->kernel[0], 2, NULL, global_work, NULL, 0, NULL, NULL);
        if (d->d) {
            // Temporal.
            for (int k = int64ToIntS(-d->d); k <= d->d; k++) {
                src = vsapi->getFrameFilter(clamp(n + k, 0, maxframe), d->node, frameCtx);
                ref = vsapi->getFrameFilter(clamp(n + k, 0, maxframe), d->knot, frameCtx);
                ret |= clEnqueueWriteImage(command_queue, d->mem_in[1], CL_TRUE, origin, region,
                    vsapi->getStride(src, 0), 0, vsapi->getReadPtr(src, 0), 0, NULL, NULL);
                ret |= clEnqueueWriteImage(command_queue, d->mem_in[3], CL_TRUE, origin, region,
                    vsapi->getStride(ref, 0), 0, vsapi->getReadPtr(ref, 0), 0, NULL, NULL);
                vsapi->freeFrame(src);
                vsapi->freeFrame(ref);
                for (int j = int64ToIntS(-d->a); j <= d->a; j++) {
                    for (int i = int64ToIntS(-d->a); i <= d->a; i++) {
                        if (k || j || i) {
                            const cl_int q[2] = { i, j };
                            ret |= clSetKernelArg(d->kernel[1], 4, 2 * sizeof(cl_int), &q);
                            ret |= clEnqueueNDRangeKernel(command_queue, d->kernel[1],
                                2, NULL, global_work, NULL, 0, NULL, NULL);
                            ret |= clEnqueueNDRangeKernel(command_queue, d->kernel[2],
                                2, NULL, global_work, local_horiz, 0, NULL, NULL);
                            ret |= clEnqueueNDRangeKernel(command_queue, d->kernel[3],
                                2, NULL, global_work, local_vert, 0, NULL, NULL);
                            ret |= clSetKernelArg(d->kernel[4], 5, 2 * sizeof(cl_int), &q);
                            ret |= clEnqueueNDRangeKernel(command_queue, d->kernel[4],
                                2, NULL, global_work, NULL, 0, NULL, NULL);
                        }
                    }
                }
            }
        } else {
            // Spatial.
            for (int j = int64ToIntS(-d->a); j <= 0; j++) {
                for (int i = int64ToIntS(-d->a); i <= d->a; i++) {
                    if (j * (2 * d->a + 1) + i < 0) {
                        const cl_int q[2] = { i, j };
                        ret |= clSetKernelArg(d->kernel[1], 4, 2 * sizeof(cl_int), &q);
                        ret |= clEnqueueNDRangeKernel(command_queue, d->kernel[1],
                            2, NULL, global_work, NULL, 0, NULL, NULL);
                        ret |= clEnqueueNDRangeKernel(command_queue, d->kernel[2],
                            2, NULL, global_work, local_horiz, 0, NULL, NULL);
                        ret |= clEnqueueNDRangeKernel(command_queue, d->kernel[3],
                            2, NULL, global_work, local_vert, 0, NULL, NULL);
                        ret |= clSetKernelArg(d->kernel[4], 5, 2 * sizeof(cl_int), &q);
                        ret |= clEnqueueNDRangeKernel(command_queue, d->kernel[4],
                            2, NULL, global_work, NULL, 0, NULL, NULL);
                    }
                }
            }
        }
        ret |= clEnqueueNDRangeKernel(command_queue, d->kernel[5], 2, NULL, global_work, NULL, 0, NULL, NULL);
        ret |= clEnqueueReadImage(command_queue, d->mem_out, CL_TRUE, origin, region, vsapi->getStride(dst, 0),
            0, vsapi->getWritePtr(dst, 0), 0, NULL, NULL);
        ret |= clFinish(command_queue);
        ret |= clReleaseCommandQueue(command_queue);
        if (ret != CL_SUCCESS) {
            vsapi->setFilterError("knlm.KNLMeansCL: knlmeansGetFrame error!", frameCtx);
            vsapi->freeNode(d->node);
            vsapi->freeNode(d->knot);
            return 0;
        }

        // Info.
        if (d->info && fi->bitsPerSample == 8) {
            uint8_t y = 0, *frm = vsapi->getWritePtr(dst, 0);
            int pitch = vsapi->getStride(dst, 0);
            char buffer[2048], str[2048], str1[2048];
            DrawString(frm, pitch, 0, y++, "KNLMeansCL");
            DrawString(frm, pitch, 0, y++, " Version " VERSION);
            DrawString(frm, pitch, 0, y++, " Copyright(C) Khanattila");
            snprintf(buffer, 2048, " D:%li  A:%lix%li  S:%lix%li", 2 * d->d + 1,
                2 * d->a + 1, 2 * d->a + 1, 2 * d->s + 1, 2 * d->s + 1);
            DrawString(frm, pitch, 0, y++, buffer);
            snprintf(buffer, 2048, " Iterations: %li",
                ((2 * d->d + 1)*(2 * d->a + 1)*(2 * d->a + 1) - 1) / (d->d ? 1 : 2));
            DrawString(frm, pitch, 0, y++, buffer);
            snprintf(buffer, 2048, " Global work size: %lux%lu",
                (unsigned long) global_work[0], (unsigned long) global_work[1]);
            DrawString(frm, pitch, 0, y++, buffer);
            DrawString(frm, pitch, 0, y++, "Platform info");
            ret |= clGetPlatformInfo(d->platformID, CL_PLATFORM_NAME, sizeof(char) * 2048, str, NULL);
            snprintf(buffer, 2048, " Name: %s", str);
            DrawString(frm, pitch, 0, y++, buffer);
            ret |= clGetPlatformInfo(d->platformID, CL_PLATFORM_VENDOR, sizeof(char) * 2048, str, NULL);
            snprintf(buffer, 2048, " Vendor: %s", str);
            DrawString(frm, pitch, 0, y++, buffer);
            ret |= clGetPlatformInfo(d->platformID, CL_PLATFORM_VERSION, sizeof(char) * 2048, str, NULL);
            snprintf(buffer, 2048, " Version: %s", str);
            DrawString(frm, pitch, 0, y++, buffer);
            DrawString(frm, pitch, 0, y++, "Device info");
            ret |= clGetDeviceInfo(d->deviceID, CL_DEVICE_NAME, sizeof(char) * 2048, str, NULL);
            snprintf(buffer, 2048, " Name: %s", str);
            DrawString(frm, pitch, 0, y++, buffer);
            ret |= clGetDeviceInfo(d->deviceID, CL_DEVICE_VENDOR, sizeof(char) * 2048, str, NULL);
            snprintf(buffer, 2048, " Vendor: %s", str);
            DrawString(frm, pitch, 0, y++, buffer);
            ret |= clGetDeviceInfo(d->deviceID, CL_DEVICE_VERSION, sizeof(char) * 2048, str, NULL);
            ret |= clGetDeviceInfo(d->deviceID, CL_DRIVER_VERSION, sizeof(char) * 2048, str1, NULL);
            snprintf(buffer, 2048, " Version: %s %s", str, str1);
            DrawString(frm, pitch, 0, y++, buffer);
            if (ret != CL_SUCCESS) {
                vsapi->setFilterError("knlm.KNLMeansCL: VapourSynthInfo error!", frameCtx);
                vsapi->freeNode(d->node);
                vsapi->freeNode(d->knot);
                return 0;
            }
        }
        return dst;
    }
    return 0;
}
#endif //__VAPOURSYNTH_H__

//////////////////////////////////////////
// AviSynthFree
#ifdef __AVISYNTH_6_H__
KNLMeansClass::~KNLMeansClass() {
    clReleaseMemObject(mem_U[3]);
    clReleaseMemObject(mem_U[2]);
    clReleaseMemObject(mem_U[1]);
    clReleaseMemObject(mem_U[0]);
    clReleaseMemObject(mem_out);
    if (D) {
        clReleaseMemObject(mem_in[3]);
        clReleaseMemObject(mem_in[1]);
    }
    clReleaseMemObject(mem_in[2]);
    clReleaseMemObject(mem_in[0]);
    clReleaseKernel(kernel[5]);
    clReleaseKernel(kernel[4]);
    clReleaseKernel(kernel[3]);
    clReleaseKernel(kernel[2]);
    clReleaseKernel(kernel[1]);
    clReleaseKernel(kernel[0]);
    clReleaseProgram(program);
    clReleaseContext(context);
    free(hostBuffer);
}
#endif //__AVISYNTH_6_H__

//////////////////////////////////////////
// VapourSynthFree
#ifdef VAPOURSYNTH_H
static void VS_CC VapourSynthPluginFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    KNLMeansData *d = (KNLMeansData*) instanceData;
    vsapi->freeNode(d->node);
    vsapi->freeNode(d->knot);
    clReleaseMemObject(d->mem_U[3]);
    clReleaseMemObject(d->mem_U[2]);
    clReleaseMemObject(d->mem_U[1]);
    clReleaseMemObject(d->mem_U[0]);
    clReleaseMemObject(d->mem_out);
    if (d->d) {
        clReleaseMemObject(d->mem_in[3]);
        clReleaseMemObject(d->mem_in[1]);
    }
    clReleaseMemObject(d->mem_in[2]);
    clReleaseMemObject(d->mem_in[0]);
    clReleaseKernel(d->kernel[5]);
    clReleaseKernel(d->kernel[4]);
    clReleaseKernel(d->kernel[3]);
    clReleaseKernel(d->kernel[2]);
    clReleaseKernel(d->kernel[1]);
    clReleaseKernel(d->kernel[0]);
    clReleaseProgram(d->program);
    clReleaseContext(d->context);
    free(d->hostBuffer);
    free(d);
}
#endif //__VAPOURSYNTH_H__

//////////////////////////////////////////
// AviSynthCreate
#ifdef __AVISYNTH_6_H__
AVSValue __cdecl AviSynthPluginCreate(AVSValue args, void* user_data, IScriptEnvironment* env) {
    return new KNLMeansClass(args[0].AsClip(), args[1].AsInt(DFT_D), args[2].AsInt(DFT_A), args[3].AsInt(DFT_S),
        args[4].AsInt(DFT_wmode), args[5].AsFloat(DFT_h), args[6].Defined() ? args[6].AsClip() : args[0].AsClip(),
        args[7].AsString(DFT_ocl_device), args[8].AsBool(DFT_lsb), args[9].AsBool(DFT_info),
        env);
}
#endif //__AVISYNTH_6_H__

//////////////////////////////////////////
// VapourSynthCreate
#ifdef VAPOURSYNTH_H
static void VS_CC VapourSynthPluginCreate(const VSMap *in, VSMap *out,
    void *userData, VSCore *core, const VSAPI *vsapi) {

    // Checks sampleType, bitsPerSample and sets channel type.
    KNLMeansData d;
    cl_channel_type channel;
    int err;
    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.knot = vsapi->propGetNode(in, "rclip", 0, &err);
    if (err) d.knot = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);
    if (!vs_equals(d.vi, vsapi->getVideoInfo(d.knot))) {
        vsapi->setError(out, "knlm.KNLMeansCL: rclip do not math source clip!");
        vsapi->freeNode(d.node);
        vsapi->freeNode(d.knot);
        return;
    }
    if (isConstantFormat(d.vi)) {
        if (d.vi->format->colorFamily == cmRGB) {
            vsapi->setError(out, "knlm.KNLMeansCL: colorFamily must be cmGray, cmYUV or cmYCoCg!");
            vsapi->freeNode(d.node);
            vsapi->freeNode(d.knot);
            return;
        }
        if (d.vi->format->sampleType == stInteger) {
            if (d.vi->format->bitsPerSample == 8) {
                channel = CL_UNORM_INT8;
            } else if (d.vi->format->bitsPerSample == 16) {
                channel = CL_UNORM_INT16;
            } else {
                vsapi->setError(out, "knlm.KNLMeansCL: format must be INT8, INT16, HALF or FLOAT!");
                vsapi->freeNode(d.node);
                vsapi->freeNode(d.knot);
                return;
            }
        } else if (d.vi->format->sampleType == stFloat) {
            if (d.vi->format->bitsPerSample == 16) {
                channel = CL_HALF_FLOAT;
            } else if (d.vi->format->bitsPerSample == 32) {
                channel = CL_FLOAT;
            } else {
                vsapi->setError(out, "knlm.KNLMeansCL: format must be INT8, INT16, HALF or FLOAT!");
                vsapi->freeNode(d.node);
                vsapi->freeNode(d.knot);
                return;
            }
        } else {
            vsapi->setError(out, "knlm.KNLMeansCL: unknow sampleType!");
            vsapi->freeNode(d.node);
            vsapi->freeNode(d.knot);
            return;
        }
    } else {
        vsapi->setError(out, "knlm.KNLMeansCL: only constant format!");
        vsapi->freeNode(d.node);
        vsapi->freeNode(d.knot);
        return;
    }

    // Sets default value.
    d.d = vsapi->propGetInt(in, "d", 0, &err);
    if (err) d.d = DFT_D;
    d.a = vsapi->propGetInt(in, "a", 0, &err);
    if (err) d.a = DFT_A;
    d.s = vsapi->propGetInt(in, "s", 0, &err);
    if (err) d.s = DFT_S;
    d.wmode = vsapi->propGetInt(in, "wmode", 0, &err);
    if (err) d.wmode = DFT_wmode;
    d.h = vsapi->propGetFloat(in, "h", 0, &err);
    if (err) d.h = DFT_h;
    d.ocl_device = vsapi->propGetData(in, "device_type", 0, &err);
    if (err) d.ocl_device = DFT_ocl_device;
    d.info = vsapi->propGetInt(in, "info", 0, &err);
    if (err) d.info = DFT_info;

    // Checks user value.
    if (d.d < 0) {
        vsapi->setError(out, "knlm.KNLMeansCL: D must be greater than or equal to 0!");
        vsapi->freeNode(d.node);
        return;
    }
    if (d.a < 0) {
        vsapi->setError(out, "knlm.KNLMeansCL: A must be greater than or equal to 0!");
        vsapi->freeNode(d.node);
        return;
    }
    if (d.s < 0) {
        vsapi->setError(out, "knlm.KNLMeansCL: S must be greater than or equal to 0!");
        vsapi->freeNode(d.node);
        return;
    }
    if (d.wmode < 0 || d.wmode > 2) {
        vsapi->setError(out, "knlm.KNLMeansCL: wmode must be in range [0, 2]!");
        vsapi->freeNode(d.node);
        return;
    }
    if (d.h <= 0.0) {
        vsapi->setError(out, "knlm.KNLMeansCL: h must be greater than 0!");
        vsapi->freeNode(d.node);
        return;
    }
    cl_device_type device;
    if (!strcasecmp(d.ocl_device, "CPU")) {
        device = CL_DEVICE_TYPE_CPU;
    } else if (!strcasecmp(d.ocl_device, "GPU")) {
        device = CL_DEVICE_TYPE_GPU;
    } else if (!strcasecmp(d.ocl_device, "ACCELERATOR")) {
        device = CL_DEVICE_TYPE_ACCELERATOR;
    } else if (!strcasecmp(d.ocl_device, "DEFAULT")) {
        device = CL_DEVICE_TYPE_DEFAULT;
    } else {
        vsapi->setError(out, "knlm.KNLMeansCL: device_type must be cpu, gpu, accelerator or default!");
        vsapi->freeNode(d.node);
        return;
    }

    // Gets PlatformID and DeviceID.
    cl_uint num_platforms = 0, num_devices = 0;
    cl_bool img_support = CL_FALSE, device_aviable = CL_FALSE;
    cl_int ret = clGetPlatformIDs(0, NULL, &num_platforms);
    if (num_platforms == 0) {
        vsapi->setError(out, "knlm.KNLMeansCL: no opencl platforms available!");
        vsapi->freeNode(d.node);
        return;
    }
    cl_platform_id *temp_platforms = (cl_platform_id*) malloc(sizeof(cl_platform_id) * num_platforms);
    ret |= clGetPlatformIDs(num_platforms, temp_platforms, NULL);
    if (ret != CL_SUCCESS) {
        vsapi->setError(out, "knlm.KNLMeansCL: VapourSynthCreate error (0)!");
        vsapi->freeNode(d.node);
        return;
    }
    for (cl_uint i = 0; i < num_platforms; i++) {
        ret |= clGetDeviceIDs(temp_platforms[i], device, 0, 0, &num_devices);
        cl_device_id *temp_devices = (cl_device_id*) malloc(sizeof(cl_device_id) * num_devices);
        ret |= clGetDeviceIDs(temp_platforms[i], device, num_devices, temp_devices, NULL);
        for (cl_uint j = 0; j < num_devices; j++) {
            ret |= clGetDeviceInfo(temp_devices[j], CL_DEVICE_IMAGE_SUPPORT, sizeof(cl_bool), &img_support, NULL);
            if (device_aviable == CL_FALSE && img_support == CL_TRUE) {
                d.platformID = temp_platforms[i];
                d.deviceID = temp_devices[j];
                device_aviable = CL_TRUE;
            }
        }
        free(temp_devices);
    }
    free(temp_platforms);
    if (ret != CL_SUCCESS && ret != CL_DEVICE_NOT_FOUND) {
        vsapi->setError(out, "knlm.KNLMeansCL: VapourSynthCreate error (1)!");
        vsapi->freeNode(d.node);
        return;
    } else if (device_aviable == CL_FALSE) {
        vsapi->setError(out, "knlm.KNLMeansCL: opencl device not available!");
        vsapi->freeNode(d.node);
        return;
    }

    // Creates an OpenCL context, 2D images and buffers object.
    d.context = clCreateContext(NULL, 1, &d.deviceID, NULL, NULL, NULL);
    const cl_image_format image_format = { CL_LUMINANCE, channel };
    d.idmn[0] = d.vi->width;
    d.idmn[1] = d.vi->height;
    const size_t size = sizeof(float) * d.idmn[0] * d.idmn[1];
    d.mem_in[0] = clCreateImage2D(d.context, CL_MEM_READ_ONLY, &image_format,
        d.idmn[0], d.idmn[1], 0, NULL, NULL);
    d.mem_in[2] = clCreateImage2D(d.context, CL_MEM_READ_ONLY, &image_format,
        d.idmn[0], d.idmn[1], 0, NULL, NULL);
    if (d.d) {
        d.mem_in[1] = clCreateImage2D(d.context, CL_MEM_READ_ONLY, &image_format,
            d.idmn[0], d.idmn[1], 0, NULL, NULL);
        d.mem_in[3] = clCreateImage2D(d.context, CL_MEM_READ_ONLY, &image_format,
            d.idmn[0], d.idmn[1], 0, NULL, NULL);
    }
    d.mem_out = clCreateImage2D(d.context, CL_MEM_WRITE_ONLY, &image_format,
        d.idmn[0], d.idmn[1], 0, NULL, NULL);
    d.mem_U[0] = clCreateBuffer(d.context, CL_MEM_READ_WRITE, 2 * size, NULL, NULL);
    d.mem_U[1] = clCreateBuffer(d.context, CL_MEM_READ_WRITE, size, NULL, NULL);
    d.mem_U[2] = clCreateBuffer(d.context, CL_MEM_READ_WRITE, size, NULL, NULL);
    d.mem_U[3] = clCreateBuffer(d.context, CL_MEM_READ_WRITE, size, NULL, NULL);

    // Creates and Build a program executable from the program source.
    d.program = clCreateProgramWithSource(d.context, 1, &source_code, NULL, NULL);
    char options[2048];
    if (channel == CL_FLOAT) {
        snprintf(options, 2048, "-Werror \
            -D H_BLOCK_X=%i -D H_BLOCK_Y=%i -D V_BLOCK_X=%i -D V_BLOCK_Y=%i \
            -D NLMK_RGB=%i -D NLMK_S=%li -D NLMK_WMODE=%li -D NLMK_TEMPORAL=%li -D NLMK_H2_INV_NORM=%ff",
            H_BLOCK_X, H_BLOCK_Y, V_BLOCK_X, V_BLOCK_Y,
            0, d.s, d.wmode, d.d, 65025.0 / (d.h*d.h*(2 * d.s + 1) * (2 * d.s + 1)));
    } else {
        snprintf(options, 2048, "-cl-denorms-are-zero -cl-fast-relaxed-math -Werror \
           -D H_BLOCK_X=%i -D H_BLOCK_Y=%i -D V_BLOCK_X=%i -D V_BLOCK_Y=%i \
           -D NLMK_RGB=%i -D NLMK_S=%li -D NLMK_WMODE=%li -D NLMK_TEMPORAL=%li -D NLMK_H2_INV_NORM=%ff",
           H_BLOCK_X, H_BLOCK_Y, V_BLOCK_X, V_BLOCK_Y,
           0, d.s, d.wmode, d.d, 65025.0 / (d.h*d.h*(2 * d.s + 1) * (2 * d.s + 1)));
    }
    ret = clBuildProgram(d.program, 1, &d.deviceID, options, NULL, NULL);
    if (ret != CL_SUCCESS) {
        size_t log_size;
        clGetProgramBuildInfo(d.program, d.deviceID, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
        char *log = (char*) malloc(log_size);
        clGetProgramBuildInfo(d.program, d.deviceID, CL_PROGRAM_BUILD_LOG, log_size, log, NULL);
        std::ofstream outfile("KNLMeansCL.txt", std::ofstream::binary);
        outfile.write(log, log_size);
        outfile.close();
        free(log);
        vsapi->setError(out, "knlm.KNLMeansCL: VapourSynthCreate error (2)!");
        vsapi->freeNode(d.node);
        return;
    }

    // Creates kernel objects.
    d.kernel[0] = clCreateKernel(d.program, "NLM_init", NULL);
    d.kernel[1] = clCreateKernel(d.program, "NLM_dist", NULL);
    d.kernel[2] = clCreateKernel(d.program, "NLM_horiz", NULL);
    d.kernel[3] = clCreateKernel(d.program, "NLM_vert", NULL);
    d.kernel[4] = clCreateKernel(d.program, "NLM_accu", NULL);
    d.kernel[5] = clCreateKernel(d.program, "NLM_finish", NULL);

    // Sets kernel arguments.
    ret = clSetKernelArg(d.kernel[0], 0, sizeof(cl_mem), &d.mem_U[0]);
    ret |= clSetKernelArg(d.kernel[0], 1, sizeof(cl_mem), &d.mem_U[3]);
    ret |= clSetKernelArg(d.kernel[0], 2, 2 * sizeof(cl_uint), &d.idmn);
    ret |= clSetKernelArg(d.kernel[1], 0, sizeof(cl_mem), &d.mem_in[2]);
    ret |= clSetKernelArg(d.kernel[1], 1, sizeof(cl_mem), &d.mem_in[d.d ? 3 : 2]);
    ret |= clSetKernelArg(d.kernel[1], 2, sizeof(cl_mem), &d.mem_U[1]);
    ret |= clSetKernelArg(d.kernel[1], 3, 2 * sizeof(cl_uint), &d.idmn);
    ret |= clSetKernelArg(d.kernel[2], 0, sizeof(cl_mem), &d.mem_U[1]);
    ret |= clSetKernelArg(d.kernel[2], 1, sizeof(cl_mem), &d.mem_U[2]);
    ret |= clSetKernelArg(d.kernel[2], 2, 2 * sizeof(cl_uint), &d.idmn);
    ret |= clSetKernelArg(d.kernel[3], 0, sizeof(cl_mem), &d.mem_U[2]);
    ret |= clSetKernelArg(d.kernel[3], 1, sizeof(cl_mem), &d.mem_U[1]);
    ret |= clSetKernelArg(d.kernel[3], 2, 2 * sizeof(cl_uint), &d.idmn);
    ret |= clSetKernelArg(d.kernel[4], 0, sizeof(cl_mem), &d.mem_in[d.d ? 1 : 0]);
    ret |= clSetKernelArg(d.kernel[4], 1, sizeof(cl_mem), &d.mem_U[0]);
    ret |= clSetKernelArg(d.kernel[4], 2, sizeof(cl_mem), &d.mem_U[1]);
    ret |= clSetKernelArg(d.kernel[4], 3, sizeof(cl_mem), &d.mem_U[3]);
    ret |= clSetKernelArg(d.kernel[4], 4, 2 * sizeof(cl_uint), &d.idmn);
    ret |= clSetKernelArg(d.kernel[5], 0, sizeof(cl_mem), &d.mem_in[0]);
    ret |= clSetKernelArg(d.kernel[5], 1, sizeof(cl_mem), &d.mem_out);
    ret |= clSetKernelArg(d.kernel[5], 2, sizeof(cl_mem), &d.mem_U[0]);
    ret |= clSetKernelArg(d.kernel[5], 3, sizeof(cl_mem), &d.mem_U[3]);
    ret |= clSetKernelArg(d.kernel[5], 4, 2 * sizeof(cl_uint), &d.idmn);
    if (ret != CL_SUCCESS) {
        vsapi->setError(out, "knlm.KNLMeansCL: VapourSynthCreate error (3)!");
        vsapi->freeNode(d.node);
        return;
    }

    // Creates a new filter and returns a reference to it.
    KNLMeansData *data = (KNLMeansData*) malloc(sizeof(d));
    *data = d;
    vsapi->createFilter(in, out, "KNLMeansCL", knlmeansInit, VapourSynthPluginGetFrame,
        VapourSynthPluginFree, fmParallelRequests, 0, data, core);
}
#endif //__VAPOURSYNTH_H__

//////////////////////////////////////////
// AviSynthPluginInit
#ifdef __AVISYNTH_6_H__
const AVS_Linkage *AVS_linkage = 0;
extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(
    IScriptEnvironment* env, const AVS_Linkage* const vectors) {

    AVS_linkage = vectors;
    env->AddFunction("KNLMeansCL", "c[D]i[A]i[S]i[wmode]i[h]f[rclip]c[device_type]s[lsb_inout]b[info]b",
        AviSynthPluginCreate, 0);
    return "KNLMeansCL for AviSynth";
}
#endif //__AVISYNTH_6_H__

//////////////////////////////////////////
// VapourSynthPluginInit
#ifdef VAPOURSYNTH_H
VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc,
    VSRegisterFunction registerFunc, VSPlugin *plugin) {

    configFunc("com.Khanattila.KNLMeansCL", "knlm", "KNLMeansCL for VapourSynth", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("KNLMeansCL", "clip:clip;d:int:opt;a:int:opt;s:int:opt;wmode:int:opt;h:float:opt;\
rclip:clip:opt;device_type:data:opt;info:int:opt", VapourSynthPluginCreate, nullptr, plugin);
}
#endif //__VAPOURSYNTH_H__