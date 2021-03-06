#ifndef CGO_UTILS_H
#define CGO_UTILS_H

#include "libavformat/avformat.h"
#include <pthread.h>

typedef  AVFormatContext* AVFormatContextPtr;
typedef AVPacket* AVPacketPtr;



AVFormatContext* AVFormat_Open(const char *fmt, uintptr_t ioctx);
void AVFormat_Free(AVFormatContext*);

int AVFormat_ReadFrame(AVFormatContext* ctx);

int AVFormat_ReadLatestVideoFrame(AVFormatContext* ctx, const char *fmt, uint64_t seq);
int AVFormat_SubcribeAVFrame(AVFormatContext* ctx, const char *fmt, uint64_t seq);
void AVFormat_UnsubcribeAVFrame(AVFormatContext* ctx, uint64_t seq);


extern int GOAVERROR_EINVAL;
extern int GOAVERROR_EOF;
extern int GOAVERROR_EAGAIN;

int AV_STRERROR(int errnum, char *errbuf, int errbuf_size);


#endif