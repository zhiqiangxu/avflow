#include "libavformat/avformat.h"

typedef  AVFormatContext* AVFormatContextPtr;



AVFormatContext* AVFormat_Open(const char *fmt, void* ioctx);

void AVFormat_ReadFrame(AVFormatContext* ctx);