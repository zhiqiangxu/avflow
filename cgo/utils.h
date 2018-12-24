#include "libavformat/avformat.h"

typedef  AVFormatContext* AVFormatContextPtr;



AVFormatContext* AVFormat_Open(const char *fmt, uintptr_t ioctx);

void AVFormat_ReadFrame(AVFormatContext* ctx);

int GOAVERROR_EINVAL();