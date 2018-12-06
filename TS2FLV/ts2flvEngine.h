#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#define  __STDC_LIMIT_MACROS
#define  __STDC_CONSTANT_MACROS
#define __STDC_FORMAT_MACROS
#define snprintf _snprintf
#include <stdio.h>
#include <string.h>
#ifdef __cplusplus
extern "C"{
#endif
#include "libavformat/url.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavdevice/avdevice.h"
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"
#include "libswscale/swscale.h"
#include "libavutil/pixfmt.h"
#include "libavformat/avformat.h"
#include "libavutil/samplefmt.h"
#include "libavcodec/avcodec.h"
#include "libswresample/swresample.h"
#include "libavutil/audio_fifo.h"

#ifdef WIN32
__declspec(dllexport)
#else
#endif

//extern AVCodec ff_libx264_encoder;

#define DEFAULT_MEM (32 * 1024 * 1024)
#define ALLOW_DOWN_MIN (10 * 1024 * 1024)
#define MAX_FLV_SIZE	(1024*1024*50)
#define DISCONT_DELTA	100

typedef struct BufferData_ {
	uint8_t *ptr;
	int size; ///< size left in the buffer
}BufferData;

struct ResponseData {
	int64_t ts_timestamp_;
	int width;
	int height;
};

struct InitArgs {
	int threadCount;
	int videoBitrate;
	int audioBitrate;
	int64_t base_timestamp;

	int width;
	int height;

	char init_file_path[128];
	int block_header_leng;
};

enum RUNNING_STATE
{
	RUNNING,
	WAITING,
	RESTART,
	STOP,
	EXIT
};

struct InputStream {

	AVFifoBuffer *fifo;
	int isRunning;
	int stopState;
	int frame_flv;

	AVIOContext * inAVIOCtx;
	AVFormatContext *ifmt_ctx;
	AVFormatContext *ofmt_ctx;
	AVIOContext *outAVIOCtx;

	SwrContext *swr_ctx;
	AVAudioFifo *audioFifo;
	uint8_t *scaleFrameBuffer;
	AVBitStreamFilterContext *aacbsfc;
	struct SwsContext *img_convert_ctx;
};

static int headerSave;
static BufferData bufferData;
static BufferData headerData;
static int isrunning;
static struct InputStream inputStream;
static AVRational audioInputFmtTimebase = {0,0};
static AVRational videoInputFmtTimebase = { 0, 0 };
static AVRational audioInputCodecTimebase = { 0, 0 };
static AVRational videoInputCodecTimebase = { 0, 0 };

typedef void(*BackCallFunc)(uint8_t *f_p, uint32_t len, void* arg, void *resp);

static BackCallFunc g_func = NULL;
static void *g_func_arg = NULL;
static void *g_resp = NULL;

static struct InitArgs *g_initArgs;

/*
    @pram
    f: callback function
    arg: CPieceWriter handler
    resp: ResponseData handler
*/
extern void initialize(BackCallFunc f, void *arg, void *resp, struct InitArgs *init_args);

extern void allocMemory();
int initEncoder();
extern int canAddData(int32_t data_len);
extern int ts2flv(const unsigned char *ts_p, int ts_len);

extern int runLoop();
extern void stopLoop();

#ifdef __cplusplus
}
#endif
