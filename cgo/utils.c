#include "utils.h"


extern int go_callback(void *ioctx, uint8_t *buf, int buf_size);

static int read_packet(void *ioctx, uint8_t *buf, int buf_size)
{
    return go_callback(ioctx, buf, buf_size);
}

int avformat_open_qrpc_input(AVFormatContext **ppctx, const char *fmt, void* ioctx)
{
    AVOutputFormat *ofmt = av_guess_format(fmt, NULL, NULL);
    if (!ofmt) {
        fprintf(stderr, "fmt not found:%s", fmt);
        return AVERROR(EINVAL);
    }


    int ret = avformat_alloc_output_context2(ppctx, ofmt, NULL, NULL);
    if (ret < 0) return ret;
    
    size_t avio_ctx_buffer_size = 4096;
    uint8_t *avio_ctx_buffer = av_malloc(avio_ctx_buffer_size);
    if (!avio_ctx_buffer) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    
    AVIOContext *avio_ctx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size,
                                  0, ioctx, &read_packet, NULL, NULL);
    if (!avio_ctx) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    (*ppctx)->pb = avio_ctx;

end:
    if (ret < 0) {
        avformat_close_input(ppctx);
        /* note: the internal buffer could have changed, and be != avio_ctx_buffer */
        if (avio_ctx) {
            av_freep(&avio_ctx->buffer);
            av_freep(&avio_ctx);
        }
    }

    return 0;
}


AVFormatContext* AVFormat_Open(const char *fmt, uintptr_t ioctx) {
    AVFormatContext* ctx;
    if (avformat_open_qrpc_input(&ctx, fmt, (void*)ioctx) < 0) return NULL;

    return ctx;
}

void AVFormat_ReadFrame(AVFormatContext* ctx)
{
    AVPacket pkt;
    av_read_frame(ctx, &pkt);

}