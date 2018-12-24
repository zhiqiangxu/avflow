#include "utils.h"


extern int read_packet_callback(void *ioctx, uint8_t *buf, int buf_size);

static int read_packet(void *ioctx, uint8_t *buf, int buf_size)
{
    printf("buf_size %d\n",buf_size);
    return read_packet_callback(ioctx, buf, buf_size);
}

int avformat_open_qrpc_input(AVFormatContext **ppctx, const char *fmt, void* ioctx)
{
    AVInputFormat *ifmt = av_find_input_format(fmt);
    if (!ifmt) {
        fprintf(stderr, "fmt not found:%s\n", fmt);
        return AVERROR(EINVAL);
    }

    if (!(*ppctx = avformat_alloc_context())) return AVERROR(ENOMEM);

    int ret;
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

    ret = avformat_open_input(ppctx, NULL, ifmt, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open input\n");
        goto end;
    }

    if ((ret = avformat_find_stream_info(*ppctx, NULL)) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        goto end;
    }

    printf("nstreams:%d\n", (*ppctx)->nb_streams);

end:
    if (ret < 0) {
        avformat_close_input(ppctx);
        /* note: the internal buffer could have changed, and be != avio_ctx_buffer */
        if (avio_ctx) {
            av_freep(&avio_ctx->buffer);
            av_freep(&avio_ctx);
        }
        return ret;
    }

    return 0;
}


AVFormatContext* AVFormat_Open(const char *fmt, uintptr_t ioctx) {
    AVFormatContext* ctx;
    if (avformat_open_qrpc_input(&ctx, fmt, (void*)ioctx) < 0) {
        printf("avformat_open_qrpc_input fail\n");
        return NULL;
    }

    return ctx;
}

void AVFormat_ReadFrame(AVFormatContext* ctx)
{
    AVPacket pkt;
    av_init_packet(&pkt);
    
    printf("before av_read_frame\n");
    int ret = av_read_frame(ctx, &pkt);
    if (ret < 0) {
        printf("av_read_frame ng:%d\n", ret);
    } else {
        printf("av_read_frame ok\n");
    }
    printf("after av_read_frame\n");
}

int GOAVERROR_EINVAL()
{
    return AVERROR(EINVAL);
}