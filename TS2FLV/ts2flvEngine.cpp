#include "ts2flvEngine.h"

struct lilogger
{
	enum
	{
		kLevelError,
		kLevelDebug
	};
};


typedef struct ALilo
{
	void printf(unsigned int level, const char *format, ...)
	{

	}
}ALilo;

static ALilo *g_lilo = new ALilo();

ALilo& get_logger()
{
	return *g_lilo;
}

typedef struct FLVContext {
	int     reserved;
	int64_t duration_offset;
	int64_t filesize_offset;
	int64_t duration;
	int64_t delay;      ///< first dts delay (needed for AVC & Speex)

	AVCodecContext *audio_enc;
	AVCodecContext *video_enc;
	double framerate;
	AVCodecContext *data_enc;
} FLVContext;

int my_read(void *opaque, uint8_t *buf, int buf_size) {	//Read to buf

	AVFifoBuffer *fifo = (AVFifoBuffer *)opaque;
	int space = 0;
	while (space == 0) {
		if (inputStream.isRunning != RUNNING) {
			return 0;
		}
		space = av_fifo_size(fifo);
		if (space > 0) {
			if (space > buf_size) {
				av_fifo_generic_read(fifo, buf, buf_size, NULL);
				return buf_size;
			}
			else {
				av_fifo_generic_read(fifo, buf, space, NULL);
				return space;
			}
		}
	}
	return -1;
}

int my_write(void *opaque, uint8_t *buf, int size) {	//Write to buf
	if (headerSave == 0){
		memcpy(headerData.ptr + headerData.size, buf, size);
		headerData.size += size;
	}
	else if(headerSave == 1){
		memcpy(bufferData.ptr + bufferData.size, buf, size);
		bufferData.size += size;
	}
	return size;
}

int canAddData(int32_t data_len) {
	return av_fifo_space(inputStream.fifo) > data_len ? 1 : 0;
}

int ts2flv(const unsigned char *ts_p, int ts_len) {
	int space = av_fifo_space(inputStream.fifo);
	int size = 0;
	if (space >= ts_len) {
		size = av_fifo_generic_write(inputStream.fifo, (void *)ts_p, ts_len, NULL);
		return size;
	}
	else {
		return -1;
	}
	return size;
}

void initialize(BackCallFunc f, void *arg, void *resp, struct InitArgs *init_args) {

	av_register_all();
	avformat_network_init();
	avfilter_register_all();

	g_resp = resp;
	g_func = f;
	g_func_arg = arg;
	g_initArgs = init_args;

	char *filePath = init_args->init_file_path;
	int blockHeaderLen = init_args->block_header_leng;
	char flvHeader[2000] = { 0 };
	int flvHeaderLen = 0;

	allocMemory();

	if (strlen(filePath) != 0) {

		//Read header of flv
		FILE *pFile = fopen(filePath, "rb");
		fseek(pFile, blockHeaderLen, 0);
		fread(flvHeader, 13, 1, pFile);
		flvHeaderLen += 13;

		fread(flvHeader + flvHeaderLen, 4, 1, pFile);
		flvHeaderLen += 4;
		int tagLen = ((((int)flvHeader[flvHeaderLen - 3] << 8) | (int)(flvHeader[flvHeaderLen - 2])) << 8) | (int)flvHeader[flvHeaderLen - 1];
		fread(flvHeader + flvHeaderLen, tagLen+11, 1, pFile);	//metadata
		flvHeaderLen += (tagLen+11);

		fread(flvHeader + flvHeaderLen, 4, 1, pFile);
		flvHeaderLen += 4;
		tagLen = ((((int)flvHeader[flvHeaderLen - 3] << 8) | (int)(flvHeader[flvHeaderLen - 2])) << 8) | (int)flvHeader[flvHeaderLen - 1];
		fread(flvHeader + flvHeaderLen, tagLen+11, 1, pFile);	//A/V sequqnce header
		flvHeaderLen += (tagLen+11);

		fread(flvHeader + flvHeaderLen, 4, 1, pFile);
		flvHeaderLen += 4;
		tagLen = ((((int)flvHeader[flvHeaderLen - 3] << 8) | (int)(flvHeader[flvHeaderLen - 2])) << 8) | (int)flvHeader[flvHeaderLen - 1];
		fread(flvHeader + flvHeaderLen, tagLen + 11, 1, pFile);	//V/A sequqnce header
		flvHeaderLen += (tagLen + 11);
		fclose(pFile);
		memcpy(headerData.ptr, flvHeader, flvHeaderLen);
		headerData.size = flvHeaderLen;
		headerSave = 2;
	}
}

static void setup_array(uint8_t* out[32], AVFrame* in_frame, int format, int samples) {

	if (av_sample_fmt_is_planar((AVSampleFormat)format)) {
		int i;
		int plane_size = av_get_bytes_per_sample((AVSampleFormat)(format & 0xFF)) * samples;
		format &= 0xFF;
		in_frame->data[0] + i*plane_size;
		for (i = 0; i < in_frame->channels; i++) {
			out[i] = in_frame->data[i];
		}
	}
	else {
		out[0] = in_frame->data[0];
	}
}

void allocMemory() {

	inputStream.fifo = NULL;
	inputStream.isRunning = EXIT;
	inputStream.stopState = 0;
	inputStream.frame_flv = 125;

	if (inputStream.fifo == NULL) {
		inputStream.fifo = av_fifo_alloc(DEFAULT_MEM);
	}
	uint8_t *inBuffer = (uint8_t *)av_malloc(DEFAULT_MEM);	//Read in memory.

	AVIOContext * inAVIOCtx = avio_alloc_context(inBuffer, DEFAULT_MEM, 0, inputStream.fifo, my_read, NULL, NULL);
	inAVIOCtx->max_packet_size = DEFAULT_MEM;
	inputStream.inAVIOCtx = inAVIOCtx;
	inputStream.img_convert_ctx = NULL;

	bufferData.ptr = NULL;
	bufferData.size = 0;
	headerData.ptr = NULL;
	headerData.size = 0;
	if (bufferData.ptr == NULL) {
		bufferData.ptr = (uint8_t *)malloc(MAX_FLV_SIZE);
		bufferData.size = 0;
	}
	if (headerData.ptr == NULL) {
		headerData.ptr = (uint8_t *)malloc(MAX_FLV_SIZE / 10);
		headerData.size = 0;
	}
}

int initEncoder() {

	int ret = 0;

	AVFormatContext *ifmt_ctx = avformat_alloc_context();
	ifmt_ctx->pb = inputStream.inAVIOCtx;

	AVDictionary *opts = NULL;
	av_dict_set(&opts, "probesize", "60000000", 0);
	av_dict_set(&opts, "analyzeduration", "60000000", 0);
	ret = avformat_open_input(&ifmt_ctx, NULL, NULL, NULL);
	if (ret < 0) {
		get_logger().printf(lilogger::kLevelError, "avformat_open_input failed");
		printf("avformat_open_input failed\n");
		return ret;
	}

	if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
		get_logger().printf(lilogger::kLevelError, "Failed to retrieve input stream information");
		printf("Failed to retrieve input stream information\n");
		return ret;
	}

	for (int i = 0; i < ifmt_ctx->nb_streams; i++) {	//Need to open video decoder.

		AVCodecContext *dec_ctx = ifmt_ctx->streams[i]->codec;
		/* init the video decoder */
		if ((ret = avcodec_open2(dec_ctx, avcodec_find_decoder(dec_ctx->codec_id), NULL)) < 0) {
			get_logger().printf(lilogger::kLevelError, "Cannot open video decoder");
			printf("Cannot open video decoder\n");
			return ret;
		}
		if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
			audioInputFmtTimebase = ifmt_ctx->streams[i]->time_base;
			audioInputCodecTimebase = dec_ctx->time_base;
		}
		else if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoInputFmtTimebase = ifmt_ctx->streams[i]->time_base;
			videoInputCodecTimebase = dec_ctx->time_base;
		}
	}

	inputStream.ifmt_ctx = ifmt_ctx;

	uint8_t *outBuffer = (uint8_t *)av_malloc(DEFAULT_MEM);	//Write to memory.

	AVIOContext *outAVIOCtx = avio_alloc_context(outBuffer, DEFAULT_MEM, 1, NULL, NULL, my_write, NULL);	//Writable.

	AVFormatContext *ofmt_ctx = NULL;

	avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", NULL);	//Define the mux format.
	ofmt_ctx->pb = outAVIOCtx;
	ofmt_ctx->flags = AVFMT_FLAG_CUSTOM_IO;

	inputStream.ofmt_ctx = ofmt_ctx;
	inputStream.outAVIOCtx = outAVIOCtx;

	for (int i = 0; i < ifmt_ctx->nb_streams; i++) {

		AVStream *in_stream = ifmt_ctx->streams[i];
		AVCodecContext *dec_ctx = in_stream->codec;

		AVStream *out_stream = avformat_new_stream(ofmt_ctx, NULL);
		if (!out_stream) {
			get_logger().printf(lilogger::kLevelError, "Failed allocating output stream");
			av_log(NULL, AV_LOG_ERROR, "Failed allocating output stream\n");
			return AVERROR_UNKNOWN;
		}
		AVRational rational;
		rational.num = 1;
		rational.den = 90000;
		out_stream->time_base = rational;
		AVCodecContext *enc_ctx = out_stream->codec;
		if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
			dec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
			out_stream->avg_frame_rate = in_stream->avg_frame_rate;
			inputStream.frame_flv = 5 * out_stream->avg_frame_rate.num / out_stream->avg_frame_rate.den;

			/* in this example, we choose transcoding to same codec */
			AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_H264); //dec_ctx->codec_id
			if (!encoder) {
				get_logger().printf(lilogger::kLevelError, "Necessary encoder not found");
				av_log(NULL, AV_LOG_FATAL, "Necessary encoder not found\n");
				return AVERROR_INVALIDDATA;
			}
			if (g_initArgs->width == 0 && g_initArgs->height == 0) {
				enc_ctx->height = dec_ctx->height;
				enc_ctx->width = dec_ctx->width;
			}
			else if (g_initArgs->width == 0 && g_initArgs->height > 0) {
				enc_ctx->height = g_initArgs->height;
				enc_ctx->width = enc_ctx->height * dec_ctx->width / dec_ctx->height;
				if (enc_ctx->width % 4 != 0) {
					if ((enc_ctx->width + 1) % 4 == 0) {
						enc_ctx->width += 1;
					}
					else if ((enc_ctx->width + 2) % 4 == 0) {
						enc_ctx->width += 2;
					}
					else if ((enc_ctx->width + 3) % 4 == 0) {
						enc_ctx->width += 3;
					}
				}
			}
			else if (g_initArgs->height == 0 && g_initArgs->width > 0) {
				enc_ctx->width = g_initArgs->width;
				enc_ctx->height = enc_ctx->width * dec_ctx->height / dec_ctx->width;
				if (enc_ctx->height % 4 != 0) {
					if ((enc_ctx->height + 1) % 4 == 0) {
						enc_ctx->height += 1;
					}
					else if ((enc_ctx->height + 2) % 4 == 0) {
						enc_ctx->height += 2;
					}
					else if ((enc_ctx->height + 3) % 4 == 0) {
						enc_ctx->height += 3;
					}
				}
			}
			else {
				enc_ctx->height = g_initArgs->height;
				enc_ctx->width = g_initArgs->width;
			}
			uint8_t *scaleFrameBuffer = (uint8_t *)malloc(enc_ctx->width*enc_ctx->height * 3 / 2);
			inputStream.scaleFrameBuffer = scaleFrameBuffer;

			struct ResponseData *responseData = (struct ResponseData *)g_resp;
			responseData->height = enc_ctx->height;
			responseData->width = enc_ctx->width;

			enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
			enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
			enc_ctx->time_base = dec_ctx->time_base;
			enc_ctx->bit_rate = g_initArgs->videoBitrate;
			enc_ctx->max_b_frames = 0;

			enc_ctx->me_range = 16;
			enc_ctx->max_qdiff = 4;
			enc_ctx->qmin = 10;
			enc_ctx->qmax = 51;
			enc_ctx->qcompress = 0.6;
			enc_ctx->thread_count = g_initArgs->threadCount;
			if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
				enc_ctx->flags |= CODEC_FLAG_GLOBAL_HEADER;

			enc_ctx->codec_tag = 0;
			/* Third parameter can be used to pass settings to encoder */
			ret = avcodec_open2(enc_ctx, encoder, NULL);
			if (ret < 0) {
				av_log(NULL, AV_LOG_ERROR, "Cannot open video encoder for stream #%u\n", i);
				get_logger().printf(lilogger::kLevelError, "Cannot open video encoder for stream #%u", i);
				return ret;
			}
		}
		else {
			AVBitStreamFilterContext *aacbsfc = av_bitstream_filter_init("aac_adtstoasc");
			inputStream.aacbsfc = aacbsfc;

			//AVCodec *encoder = avcodec_find_encoder(dec_ctx->codec_id);//dec_ctx->codec_id
			AVCodec *encoder = avcodec_find_encoder_by_name("aac");
			if (!encoder) {
				av_log(NULL, AV_LOG_FATAL, "Necessary encoder not found\n");
				get_logger().printf(lilogger::kLevelError, "Necessary encoder not found");
				return AVERROR_INVALIDDATA;
			}

			enc_ctx->sample_rate = 44100;
			enc_ctx->channels = 2;
			enc_ctx->bit_rate = g_initArgs->audioBitrate;
			enc_ctx->channel_layout = av_get_default_channel_layout(enc_ctx->channels);
			enc_ctx->sample_fmt = encoder->sample_fmts[0];
			enc_ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
			enc_ctx->time_base = dec_ctx->time_base;
			enc_ctx->profile = 1;
			enc_ctx->codec_tag = 0;
			enc_ctx->thread_count = g_initArgs->threadCount;
			if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
				enc_ctx->flags |= CODEC_FLAG_GLOBAL_HEADER;

			ret = avcodec_open2(enc_ctx, encoder, NULL);
			if (ret < 0) {
				av_log(NULL, AV_LOG_ERROR, "Cannot open audio encoder for stream #%u\n", i);
				get_logger().printf(lilogger::kLevelError, "Cannot open audio encoder for stream #%u", i);
				return ret;
			}
			SwrContext *swr_ctx = swr_alloc();
			av_opt_set_int(swr_ctx, "ich", dec_ctx->channels, 0);
			av_opt_set_int(swr_ctx, "och", enc_ctx->channels, 0);
			av_opt_set_int(swr_ctx, "in_sample_rate", dec_ctx->sample_rate, 0);
			av_opt_set_int(swr_ctx, "out_sample_rate", enc_ctx->sample_rate, 0);
			av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", dec_ctx->sample_fmt, 0);
			av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", enc_ctx->sample_fmt, 0);
			swr_init(swr_ctx);
			inputStream.swr_ctx = swr_ctx;

			AVAudioFifo *audioFifo = av_audio_fifo_alloc(enc_ctx->sample_fmt, enc_ctx->channels, 1);
			inputStream.audioFifo = audioFifo;
		}
	}
	ret = avformat_write_header(ofmt_ctx, NULL);
	if (ret < 0) {
		fprintf(stderr, "Error occurred when opening output file\n");
		get_logger().printf(lilogger::kLevelError, "Error occurred when opening output file");
		return ret;
	}
	avio_flush(ofmt_ctx->pb);	//Save flv header information.
	headerSave = 1;
	memcpy(bufferData.ptr, headerData.ptr, headerData.size);
	bufferData.size += headerData.size;
	return ret;
}

void release() {

	if (bufferData.size <= headerData.size) {
		return;
	}
	g_func(bufferData.ptr, bufferData.size, g_func_arg, g_resp);
	free(bufferData.ptr);	//free by invoker
	bufferData.ptr = (uint8_t *)malloc(MAX_FLV_SIZE);
	if (bufferData.ptr == NULL) {
		printf("buffer malloc failed.\r\n");
		return;
	}
	bufferData.size = 0;
	if (headerSave == 1) {
		memcpy(bufferData.ptr, headerData.ptr, headerData.size);
		bufferData.size += headerData.size;
	}
}

void freeMemory() {

	AVFormatContext *ifmt_ctx = inputStream.ifmt_ctx;
	if (ifmt_ctx != NULL) {
		for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
			avcodec_close(ifmt_ctx->streams[i]->codec);
		}
		avformat_close_input(&ifmt_ctx);
	}

	AVFormatContext *ofmt_ctx = inputStream.ofmt_ctx;
	if (ofmt_ctx != NULL) {
		for (int i = 0; i < ofmt_ctx->nb_streams; i++) {
			if (ofmt_ctx && ofmt_ctx->nb_streams > i && ofmt_ctx->streams[i] && ofmt_ctx->streams[i]->codec)
				avcodec_close(ofmt_ctx->streams[i]->codec);
		}

		if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
			avio_closep(&ofmt_ctx->pb);
		avformat_free_context(ofmt_ctx);
	}

	SwrContext *swr_ctx = inputStream.swr_ctx;
	AVAudioFifo *audioFifo = inputStream.audioFifo;
	AVBitStreamFilterContext *aacbsfc = inputStream.aacbsfc;

	if (swr_ctx != NULL) {
		swr_free(&swr_ctx);
		inputStream.swr_ctx = NULL;
	}
	if (audioFifo != NULL) {
		av_audio_fifo_free(audioFifo);
		inputStream.audioFifo = NULL;
	}
	if (inputStream.scaleFrameBuffer != NULL) {
		free(inputStream.scaleFrameBuffer);
		inputStream.scaleFrameBuffer = NULL;
	}
	if (aacbsfc != NULL) {
		av_bitstream_filter_close(aacbsfc);
		inputStream.aacbsfc = NULL;
	}
}

int runLoop() {

	int frameCount = 1;
	int64_t ts_offset = 0;
	int64_t next_dts[10] = { 0 };
	int64_t durations[10] = { 0 };
	int64_t audio_pts = -1;
	int64_t delta_abs = 0;
	int vibrate_count = 0;
	int firstStartFlag = 1;

	int audioIndex = 1;
	int videoIndex = 0;
	int key_frame = 1;
	int64_t stream_starttime[10] = { 0 };
	int64_t startTime = 0;

	inputStream.isRunning = RUNNING;
	int ret = initEncoder();
	if (ret < 0) {
		inputStream.isRunning = EXIT;
		get_logger().printf(lilogger::kLevelError, "initEncoder error!!!");
		return ret;
	}
	AVFormatContext *ofmt_ctx = inputStream.ofmt_ctx;
	for (int i = 0; i < ofmt_ctx->nb_streams; i++) {
		if (AVMEDIA_TYPE_VIDEO == ofmt_ctx->streams[i]->codec->codec_type) {
			videoIndex = i;
		}
		else if (AVMEDIA_TYPE_AUDIO == ofmt_ctx->streams[i]->codec->codec_type) {
			audioIndex = i;
		}
	}
	for (int i = 0; i < inputStream.ifmt_ctx->nb_streams; i++) {
		if (AVMEDIA_TYPE_VIDEO == inputStream.ifmt_ctx->streams[i]->codec->codec_type) {
			stream_starttime[AVMEDIA_TYPE_VIDEO] = inputStream.ifmt_ctx->streams[i]->first_dts;
		}
		else if (AVMEDIA_TYPE_AUDIO == inputStream.ifmt_ctx->streams[i]->codec->codec_type) {
			stream_starttime[AVMEDIA_TYPE_AUDIO] = inputStream.ifmt_ctx->streams[i]->first_dts;
		}
	}
	startTime = stream_starttime[AVMEDIA_TYPE_VIDEO];
	FLVContext *flv = (FLVContext *)ofmt_ctx->priv_data;
	flv->delay = 0;

	while (1) {

		if (inputStream.isRunning == RUNNING) {
			if (frameCount%(inputStream.frame_flv+1) == 0) {	//Next FLV file.
				avio_flush(ofmt_ctx->pb);
				release();
				frameCount++;
				key_frame = 1;
			}
			if (av_fifo_size(inputStream.fifo) == 0) {
#ifdef WIN32
				Sleep(10);
#else
				usleep(10 * 1000);
#endif // WIN32
				continue;
			}
			AVPacket pkt;
			ret = av_read_frame(inputStream.ifmt_ctx, &pkt);
			if (ret < 0) {
				continue;
			}
			int stream_index = pkt.stream_index;
			AVStream *in_stream = inputStream.ifmt_ctx->streams[stream_index];
			int type = in_stream->codec->codec_type;

			if (type == AVMEDIA_TYPE_VIDEO) {
				stream_index = videoIndex;
				if (firstStartFlag == 1) {
					if (!(pkt.flags & AV_PKT_FLAG_KEY)) {
						av_free_packet(&pkt);
						continue;
					}
					else {
						firstStartFlag = 0;
					}
				}
			}
			else {
				stream_index = audioIndex;
				if (firstStartFlag == 1) {
					av_free_packet(&pkt);
					continue;
				}
			}
			AVStream *out_stream = ofmt_ctx->streams[stream_index];

			if (pkt.dts != AV_NOPTS_VALUE)  {
				pkt.dts -= startTime;
			}
			if (pkt.pts != AV_NOPTS_VALUE) {
				pkt.pts -= startTime;
			}

			if (pkt.dts != AV_NOPTS_VALUE) {

				if (next_dts[stream_index] != 0 && durations[stream_index] != 0) {	//has duration.
					pkt.dts += ts_offset;
					int64_t delta = pkt.dts - next_dts[stream_index];

					if (delta > 1LL * DISCONT_DELTA * durations[stream_index] || delta < -1LL * DISCONT_DELTA * durations[stream_index]) {
						int64_t deltaCompare = delta_abs + delta;
						if (deltaCompare <= (delta_abs * 2 + pkt.duration) && deltaCompare >= (delta_abs * 2 - pkt.duration)) {
							if (vibrate_count < 0) {
								vibrate_count = 1 + abs(vibrate_count);
							}
							else {
								delta_abs = 0;
								vibrate_count = 0;
							}
						}
						else if (deltaCompare <= pkt.duration && deltaCompare >= -pkt.duration) {
							if (vibrate_count > 0) {
								vibrate_count = -1 - abs(vibrate_count);
							}
							else {
								delta_abs = 0;
								vibrate_count = 0;
							}
						}
						else {
							delta_abs = llabs(delta);
							if (delta > 0) {
								vibrate_count = 1;
							}
							else {
								vibrate_count = -1;
							}
						}
						if (abs(vibrate_count) > 10) {
							if (delta > 0) {
								delta_abs = 0;
								vibrate_count = 0;
								delta = 0;
							}
						}
						delta -= durations[stream_index];
						ts_offset -= delta;
						pkt.dts -= delta;
					}
					if (pkt.pts != AV_NOPTS_VALUE) {
						pkt.pts += ts_offset;
					}
				}
				else {	//need to get duration.
					if (pkt.duration != 0) {
						durations[stream_index] = pkt.duration;
					}
					else if (next_dts[stream_index] != 0) {
						durations[stream_index] = pkt.dts - next_dts[stream_index];
					}
				}

				next_dts[stream_index] = pkt.dts;
			}

			AVFrame *frame = av_frame_alloc();
			if (!frame) {
				ret = AVERROR(ENOMEM);
				get_logger().printf(lilogger::kLevelError, "av_frame_alloc failed.");
				av_log(NULL, AV_LOG_WARNING, "av_frame_alloc failed.\r\n");
				break;
			}
			int got_frame;
			if (type == AVMEDIA_TYPE_VIDEO) {
				av_packet_rescale_ts(&pkt, videoInputFmtTimebase, videoInputCodecTimebase);
				ret = avcodec_decode_video2(in_stream->codec, frame, &got_frame, &pkt);
			}
			else if (type == AVMEDIA_TYPE_AUDIO) {
				av_packet_rescale_ts(&pkt, audioInputFmtTimebase, audioInputCodecTimebase);
				ret = avcodec_decode_audio4(in_stream->codec, frame, &got_frame, &pkt);
			}
			av_free_packet(&pkt);
			if (ret < 0 || !got_frame) {
				av_frame_free(&frame);
				continue;
			}

			frame->pts = av_frame_get_best_effort_timestamp(frame);
			if (type == AVMEDIA_TYPE_VIDEO) {
				AVFrame* scaledFrame = av_frame_alloc();
				scaledFrame->width = out_stream->codec->width;
				scaledFrame->height = out_stream->codec->height;
				scaledFrame->format = PIX_FMT_YUV420P;

				avpicture_fill((AVPicture *)scaledFrame, inputStream.scaleFrameBuffer, (AVPixelFormat)scaledFrame->format, scaledFrame->width, scaledFrame->height);

				if (inputStream.img_convert_ctx == NULL) {
					inputStream.img_convert_ctx = sws_alloc_context();
					av_opt_set_int(inputStream.img_convert_ctx, "sws_flags", SWS_BICUBIC | SWS_PRINT_INFO, 0);
					av_opt_set_int(inputStream.img_convert_ctx, "srcw", frame->width, 0);
					av_opt_set_int(inputStream.img_convert_ctx, "srch", frame->height, 0);
					av_opt_set_int(inputStream.img_convert_ctx, "src_format", frame->format, 0);
					av_opt_set_int(inputStream.img_convert_ctx, "dstw", scaledFrame->width, 0);
					av_opt_set_int(inputStream.img_convert_ctx, "dsth", scaledFrame->height, 0);
					av_opt_set_int(inputStream.img_convert_ctx, "dst_format", scaledFrame->format, 0);
					sws_init_context(inputStream.img_convert_ctx, NULL, NULL);
				}

				sws_scale(inputStream.img_convert_ctx, frame->data, frame->linesize, 0, frame->height, scaledFrame->data, scaledFrame->linesize);

				scaledFrame->pts = frame->pts;
				scaledFrame->pkt_dts = frame->pkt_dts;
				scaledFrame->pkt_pts = frame->pkt_pts;

				av_frame_free(&frame);
				frame = scaledFrame;

				AVPacket packet;
				packet.data = NULL;
				packet.size = 0;
				av_init_packet(&packet);
				if (key_frame == 1) {
					frame->pict_type = AV_PICTURE_TYPE_I;
				}
				else {
					frame->pict_type = AV_PICTURE_TYPE_NONE;
				}
				key_frame = 0;
				ret = avcodec_encode_video2(out_stream->codec, &packet, frame, &got_frame);
				av_frame_free(&frame);
				if (ret < 0 || !got_frame) {
					av_log(NULL, AV_LOG_WARNING, "avcodec_encode_video2 failed.\r\n");
					//get_logger().printf(lilogger::kLevelError, "avcodec_encode_video2 failed.");
					av_free_packet(&packet);
					continue;
				}

				/* prepare packet for muxing */
				packet.stream_index = stream_index;
				av_packet_rescale_ts(&packet, out_stream->codec->time_base, out_stream->time_base);
				if (packet.dts != AV_NOPTS_VALUE) {
					packet.dts += g_initArgs->base_timestamp;
					struct ResponseData *responseData = (struct ResponseData *)g_resp;
					responseData->ts_timestamp_ = packet.dts;
				}
				if (packet.pts != AV_NOPTS_VALUE) {
					packet.pts += g_initArgs->base_timestamp;
				}
				ret = av_interleaved_write_frame(ofmt_ctx, &packet); 
				avio_flush(ofmt_ctx->pb);
				av_free_packet(&packet);
				if (ret < 0) {
					av_log(NULL, AV_LOG_WARNING, "Error muxing video packet.\r\n");
					//get_logger().printf(lilogger::kLevelError, "Error muxing video packet.");
					continue;
				}
				frameCount++;
			}
			else if (type == AVMEDIA_TYPE_AUDIO) {

				AVFrame *resampleFrame = av_frame_alloc();
				resampleFrame->channel_layout = out_stream->codec->channel_layout;
				resampleFrame->format = out_stream->codec->sample_fmt;
				resampleFrame->channels = out_stream->codec->channels;
				resampleFrame->sample_rate = out_stream->codec->sample_rate;
				resampleFrame->nb_samples = av_rescale_rnd(swr_get_delay(inputStream.swr_ctx, out_stream->codec->sample_rate) + frame->nb_samples,
					out_stream->codec->sample_rate, in_stream->codec->sample_rate, AV_ROUND_UP);

				ret = av_samples_alloc(resampleFrame->data,
					&resampleFrame->linesize[0],
					out_stream->codec->channels,
					resampleFrame->nb_samples,
					out_stream->codec->sample_fmt, 0);

				if (ret < 0) {
					av_log(NULL, AV_LOG_WARNING, "Audio resample: Could not allocate samples Buffer\n");
					get_logger().printf(lilogger::kLevelError, "Audio resample: Could not allocate samples Buffer");
					av_frame_free(&frame);
					av_free(resampleFrame->data[0]);
					av_frame_free(&resampleFrame);
					continue;
				}

				uint8_t* m_ain[32];
				setup_array(m_ain, frame, in_stream->codec->sample_fmt, frame->nb_samples);

				int len = swr_convert(inputStream.swr_ctx, resampleFrame->data, resampleFrame->nb_samples, (const uint8_t**)m_ain, frame->nb_samples);
				if (len < 0) {
					get_logger().printf(lilogger::kLevelError, "swr_convert failed");
					av_log(NULL, AV_LOG_WARNING, "swr_convert failed.\r\n");
					av_frame_free(&frame);
					av_free(resampleFrame->data[0]);
					av_frame_free(&resampleFrame);
					continue;
				}

				audio_pts = frame->pts;
				av_audio_fifo_write(inputStream.audioFifo, (void **)resampleFrame->data, len);
				av_frame_free(&frame);
				av_free(resampleFrame->data[0]);
				av_frame_free(&resampleFrame);

				while (av_audio_fifo_size(inputStream.audioFifo) >= out_stream->codec->frame_size) {

					AVFrame *resampleFrame2 = av_frame_alloc();
					resampleFrame2->nb_samples = out_stream->codec->frame_size;
					resampleFrame2->channels = out_stream->codec->channels;
					resampleFrame2->channel_layout = out_stream->codec->channel_layout;
					resampleFrame2->format = out_stream->codec->sample_fmt;
					resampleFrame2->sample_rate = out_stream->codec->sample_rate;

					ret = av_frame_get_buffer(resampleFrame2, 0);
					if (ret >= 0) {
						ret = av_audio_fifo_read(inputStream.audioFifo, (void **)resampleFrame2->data, out_stream->codec->frame_size);
					}
					if (ret < 0) {
						av_frame_free(&resampleFrame2);
						continue;
					}
					resampleFrame2->pts = audio_pts;
					if (audio_pts != AV_NOPTS_VALUE) {
						audio_pts += resampleFrame2->nb_samples;
					}

					pkt.data = NULL;
					pkt.size = 0;
					av_init_packet(&pkt);
					ret = avcodec_encode_audio2(out_stream->codec, &pkt, resampleFrame2, &got_frame);

					if (ret < 0 || !got_frame) {
						av_free_packet(&pkt);
						av_frame_free(&resampleFrame2);
						continue;
					}
					/* prepare packet for muxing */
					pkt.stream_index = stream_index;
					av_packet_rescale_ts(&pkt, out_stream->codec->time_base, out_stream->time_base);
					if (pkt.dts != AV_NOPTS_VALUE) {
						pkt.dts += g_initArgs->base_timestamp;
						struct ResponseData *responseData = (struct ResponseData *)g_resp;
						responseData->ts_timestamp_ = pkt.dts;
					}
					if (pkt.pts != AV_NOPTS_VALUE) {
						pkt.pts += g_initArgs->base_timestamp;
					}
					ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
					av_free_packet(&pkt);
					av_frame_free(&resampleFrame2);
					if (ret < 0) {
						//get_logger().printf(lilogger::kLevelError, "Error muxing audio packet");
						av_log(NULL, AV_LOG_WARNING, "Error muxing audio packet.\r\n");
						continue;
					}

				}
			}
		}
		else if (inputStream.isRunning == STOP) {

			inputStream.isRunning = WAITING;
			release();
			frameCount = 1;
			key_frame = 1;

			AVFormatContext *ifmt_ctx = inputStream.ifmt_ctx;
			for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
				avcodec_close(ifmt_ctx->streams[i]->codec);
			}
			avformat_close_input(&inputStream.ifmt_ctx);
			inputStream.stopState = 1;
		}
		else if (inputStream.isRunning == RESTART) {
			uint8_t *inBuffer = (uint8_t *)av_malloc(DEFAULT_MEM);	//Read in memory.
			AVIOContext *inAVIOCtx = avio_alloc_context(inBuffer, DEFAULT_MEM, 0, inputStream.fifo, my_read, NULL, NULL);
			inAVIOCtx->max_packet_size = DEFAULT_MEM;
			inputStream.ifmt_ctx = avformat_alloc_context();
			inputStream.ifmt_ctx->pb = inAVIOCtx;
			inputStream.frame_flv = 125;
			inputStream.isRunning = RUNNING;

			int ret = avformat_open_input(&inputStream.ifmt_ctx, NULL, NULL, NULL);
			if (ret < 0) {
				char buf[1024] = { 0 };
				av_strerror(ret, buf, 1024);
				get_logger().printf(lilogger::kLevelError, "*********************************dump!!!");
				av_log(NULL, AV_LOG_ERROR, "*********************************dump!!!\r\n");
				get_logger().printf(lilogger::kLevelError, "Couldn't open file : %d(%s)", ret, buf);
				printf("Couldn't open file : %d(%s)", ret, buf);
				inputStream.isRunning = EXIT;
				return ret;
			}

			if ((ret = avformat_find_stream_info(inputStream.ifmt_ctx, NULL)) < 0) {
				get_logger().printf(lilogger::kLevelError, "Failed to retrieve input stream information");
				printf("Failed to retrieve input stream information\n");
				inputStream.isRunning = EXIT;
				return ret;
			}

			for (int i = 0; i < inputStream.ifmt_ctx->nb_streams; i++) {	//Need to open video decoder.

				AVCodecContext *dec_ctx = inputStream.ifmt_ctx->streams[i]->codec;
				/* init the video decoder */
				if ((ret = avcodec_open2(dec_ctx, avcodec_find_decoder(dec_ctx->codec_id), NULL)) < 0) {
					get_logger().printf(lilogger::kLevelError, "Cannot open video decoder");
					printf("Cannot open video decoder\n");
					inputStream.isRunning = EXIT;
					return ret;
				}
				if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
					inputStream.frame_flv = 5 * inputStream.ifmt_ctx->streams[i]->avg_frame_rate.num / inputStream.ifmt_ctx->streams[i]->avg_frame_rate.den;
				}
			}
			inputStream.isRunning = RUNNING;
			inputStream.stopState = 0;
		}
	}
	return 0;
}

void stopLoop() {

	if (inputStream.isRunning == EXIT || inputStream.isRunning == WAITING) {
		return;
	}
	inputStream.isRunning = STOP;
	while (inputStream.stopState != 1) {
		get_logger().printf(lilogger::kLevelDebug, "is running %d", inputStream.isRunning);
#ifdef _WIN32
		Sleep(10);
#else
		usleep(10 * 1000);
#endif
	}
	av_fifo_reset(inputStream.fifo);
	inputStream.isRunning = RESTART;
}
