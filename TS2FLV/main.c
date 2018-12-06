#include "ts2flvEngine.h"

static DWORD WINAPI threadWork(LPVOID threadNo) {	//Simulate the video live in memory, File instead.

#if 1
	char *sdpName = "hls+http://117.135.161.48/live/5/45/cb7c9f55962b4f479146fb09d6394d5e.m3u8?type=client&playback=0";
	sdpName = "hls+http://117.135.161.48/f62aaf545a677b7a79d83ed2eda8c311.m3u8?type=penetrator&fromBiz=prepull&rtype=redirect";
	//sdpName = "http://180.153.94.77:8081/300145_600";
	//sdpName = "http://180.153.94.77:8081/300145_1000";
	sdpName = "http://58.48.127.130:8142/live/07";
	//sdpName = "hls+http://pptvcdn.jump.synacast.com/108346a1f91a44b565f08e291c99a4e3.m3u8?type=penetrator&fromBiz=prepull&rtype=redirect";
	URLContext *h = NULL;
	int bufsize = 10000;
	unsigned char *myBuf = (unsigned char *)malloc(bufsize);
	int ret = ffurl_open(&h, sdpName, AVIO_FLAG_READ, NULL, NULL);
	int ccount = 0;
	while (isrunning==1) {
		int size = ffurl_read_complete(h, myBuf, bufsize);
		if (size > 0) {
			ret = -1;
			while (ret == -1&&isrunning==1) {
				ret = ts2flv(myBuf, size);
				//ccount++;
				//printf("%d\r\n", ccount);
				//if (ccount == 300) {
				//	stopLoop();
				//	exit(0);
				//}
			}
		}
		else break;
	}
	ffurl_close(h);

#else
	char *filename = "E:\\cloudcodec\\PPDownload\\movies\\007.ts";
	filename = "C:\\Users\\donglu\\Desktop\\6558ccde5237ce5544fa7f2cba5e7a93.flv";
	//filename = "aaaaaaaa.ts";
	//filename = "P:\\donglu\\abnormal_movies\\hunan_out.ts";
	//filename = "C:\\Users\\donglu\\Desktop\\hua.flv";
	//filename = "E:\\download\\hebin\\hebin.ts";
	//filename = "P:\\jx\\aaaaaaaabbbbbbb.ts";
	//char *filename = "D:/2017-1-8/m3u8/hebin.ts";
	FILE *pFile = fopen(filename, "rb");
	fseek(pFile, 0, SEEK_END);
	long lSize = ftell(pFile);
	rewind(pFile);

	long spareSize = lSize;
	char *myBuf = (char *)malloc(10000);
	while (spareSize > 0){
		int size = fread(myBuf, 1, 10000, pFile);
		if (size > 0){
			int ret = -1;
			while (ret == -1) {
				ret = ts2flv((const unsigned char*)myBuf, size);
			}
		}
		else {
			break;
		}
		spareSize -= size;
		//printf("spareSize = %d\r\n", spareSize);
	}
#endif
	exit(0);
	return 0;
}

int myCount = 0;
void writeBlock(uint8_t *flv_p, uint32_t flv_len, void *arg, void *resp) {

	char filename[100] = { 0 };
	sprintf(filename, "kkkkk%d.flv", myCount);
	FILE *outFile = fopen(filename, "wb");
	fwrite(flv_p, 1, flv_len, outFile);
	fclose(outFile);
	myCount++;
}

static DWORD WINAPI threadRunLoop(LPVOID threadNo) {
	runLoop();
	return 0;
}

int main(int argc, char* argv[]) {

	struct InitArgs initArgs;
	initArgs.audioBitrate = 32 * 1024;
	initArgs.videoBitrate = 7000 * 1024;
	initArgs.threadCount = 4;
	initArgs.base_timestamp = 0;
	initArgs.width = 640;
	initArgs.height = 360;
	//strcpy(initArgs.init_file_path, "wwwwwwww0.block");
	strcpy(initArgs.init_file_path, "");
	//strcpy(initArgs.init_file_path, "1496661060.block");
	initArgs.block_header_leng = 1400;

	struct ResponseData responseData;
	//av_log_set_level(AV_LOG_TRACE);
	isrunning = 1;
	initialize(writeBlock, NULL, &responseData, &initArgs);

	CreateThread(NULL, 0, threadWork, &inputStream, NULL, NULL);
	CreateThread(NULL, 0, threadRunLoop, NULL, NULL, NULL);

	while (1) {
		Sleep(100);
	}
}