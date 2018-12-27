#include "libavformat/avformat.h"

typedef  AVFormatContext* AVFormatContextPtr;
typedef AVPacket* AVPacketPtr;



AVFormatContext* AVFormat_Open(const char *fmt, uintptr_t ioctx);
void AVFormat_Free(AVFormatContext*);

int AVFormat_ReadFrame(AVFormatContext* ctx);

int AVFormat_ReadLatestVideoFrame(AVFormatContext* ctx, const char *fmt, uint64_t seq);

int GOAVERROR_EINVAL();

int AV_STRERROR(int errnum, char *errbuf, int errbuf_size);