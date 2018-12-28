#include "utils.h"
#include <stdatomic.h>
#include <pthread.h>
#include <stdbool.h>

typedef struct AVFormatQrpcContextSubcriber {
    AVFormatContext *sctx;
    uint64_t seq;
    struct AVFormatQrpcContextSubcriber *next;
} AVFormatQrpcContextSubcriber;

typedef struct AVFormatQrpcContextSubcriberList {
    AVFormatQrpcContextSubcriber *first;
    AVFormatQrpcContextSubcriber *last;
    int n;
} AVFormatQrpcContextSubcriberList;

typedef struct AVFormatQrpcContext {
    AVCodecContext **cctx;// for decode input
    int nb_cctx;
    AVFrame **latest; // store latest frame
    void* ioctx; // reference to go
    pthread_mutex_t mutex;
    AVFormatQrpcContextSubcriberList *subcribers;
} AVFormatQrpcContext;



extern int read_packet_callback(void *ioctx, uint8_t *buf, int buf_size);
extern int read_latest_callback(void *ioctx, uint64_t seq, uint8_t *buf, int buf_size);
extern int write_packet_callback(void *ioctx, uint64_t seq, uint8_t *buf, int bufSize);

static int open_codec_context(int stream_idx, AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx);
static void free_qrpc_context(AVFormatQrpcContext *qrpcCtx);
static void write_subcribers(AVFormatQrpcContext *qrpcCtx, AVFrame *frame);
static AVFormatQrpcContextSubcriber* add_subscriber(AVFormatQrpcContext *qrpcCtx, AVFormatContext *oc, uint64_t seq);
static void del_subscriber(AVFormatQrpcContext* qrpcCtx, uint64_t seq, bool locked);
static void free_subcriber(AVFormatQrpcContextSubcriber* sub);
static int write_subcriber_callback(void* sub, uint8_t *buf, int buf_size);
    


int avformat_open_qrpc_input(AVFormatContext **ppctx, const char *fmt, void* ioctx)
{
    AVIOContext *avio_ctx = NULL;
    AVFormatQrpcContext* qrpcCtx = NULL;

    AVInputFormat *ifmt = av_find_input_format(fmt);
    if (!ifmt) {
        fprintf(stderr, "fmt not found:%s\n", fmt);
        return AVERROR(EINVAL);
    }

    if (!(*ppctx = avformat_alloc_context())) return AVERROR(ENOMEM);
    (*ppctx)->opaque = NULL;

    int ret;
    size_t avio_ctx_buffer_size = 4096;
    uint8_t *avio_ctx_buffer = av_malloc(avio_ctx_buffer_size);
    if (!avio_ctx_buffer) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    avio_ctx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size,
                                  0, ioctx, &read_packet_callback, NULL, NULL);
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

    qrpcCtx = av_malloc(sizeof(AVFormatQrpcContext));
    if (!qrpcCtx) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    qrpcCtx->ioctx = ioctx;
    qrpcCtx->nb_cctx = (*ppctx)->nb_streams;
    qrpcCtx->cctx = av_mallocz_array(qrpcCtx->nb_cctx, sizeof(AVCodecContext*));
    if (!qrpcCtx->cctx) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    qrpcCtx->latest = av_mallocz_array(qrpcCtx->nb_cctx, sizeof(AVFrame*));
    if (!qrpcCtx->latest) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    for (int i = 0; i < qrpcCtx->nb_cctx; i++) {
        ret = open_codec_context(i, &qrpcCtx->cctx[i], *ppctx);
        if (ret < 0) goto end;
    }
    
    if ((ret = pthread_mutex_init(&qrpcCtx->mutex, NULL)) < 0) goto end;
    qrpcCtx->subcribers = NULL;

end:
    if (ret < 0) {
        avformat_close_input(ppctx);
        /* note: the internal buffer could have changed, and be != avio_ctx_buffer */
        if (avio_ctx) {
            av_freep(&avio_ctx->buffer);
            av_freep(&avio_ctx);
        }
        free_qrpc_context(qrpcCtx);
        return ret;
    } else {
        (*ppctx)->opaque = qrpcCtx;
        return 0;
    }
    
}


AVFormatContext* AVFormat_Open(const char *fmt, uintptr_t ioctx) {
    AVFormatContext* ctx;
    if (avformat_open_qrpc_input(&ctx, fmt, (void*)ioctx) < 0) {
        printf("avformat_open_qrpc_input fail\n");
        return NULL;
    }

    return ctx;
}

int AVFormat_ReadFrame(AVFormatContext* ctx)
{
    AVPacket pkt;
    av_init_packet(&pkt);
    
    int ret = av_read_frame(ctx, &pkt);
    if (ret < 0) {
        fprintf(stderr, "av_read_frame ng:%d\n", ret);
        return ret;
    }

    AVFormatQrpcContext *qrpcCtx = ctx->opaque;
    AVCodecContext *dec_ctx = qrpcCtx->cctx[pkt.stream_index];
    ret = avcodec_send_packet(dec_ctx, &pkt);

    AVFrame *frame = av_frame_alloc();
    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0) {
            fprintf(stderr, "Error while receiving a frame from the decoder\n");
            break;
        }
        if (!qrpcCtx->latest[pkt.stream_index]) {
            AVFrame *latest = av_frame_clone(frame);
            qrpcCtx->latest[pkt.stream_index] = latest;
        } else {
            av_frame_copy(qrpcCtx->latest[pkt.stream_index], frame);
        }
        
        write_subcribers(qrpcCtx, frame);
    }

end:
    av_frame_free(&frame);
    av_packet_unref(&pkt);


    return ret;
}

int AVFormat_ReadLatestVideoFrame(AVFormatContext* ctx, const char *fmt, uint64_t seq)
{
    AVCodec *enc = avcodec_find_encoder_by_name(fmt);
    if (!enc) {
        fprintf(stderr, "encoder %s not found\n", fmt);
        return AVERROR(EINVAL);
    }

    AVCodecContext *encctx = avcodec_alloc_context3(enc);
    if (!encctx) {
        fprintf(stderr, "avcodec_alloc_context3 fail\n");
        return AVERROR(ENOMEM);
    }
    encctx->time_base = (AVRational){1, 25};

    int ret = 0;
    AVFormatQrpcContext *qrpcCtx = ctx->opaque;
    if (!qrpcCtx) {
        fprintf(stderr, "qrpcCtx not found\n");
        ret = AVERROR(EINVAL);
        goto end;
    }
    for (int i = 0; i < qrpcCtx->nb_cctx; i++) {
        AVCodecContext *cctx = qrpcCtx->cctx[i];
        if (!cctx) continue;
        if (cctx->codec->type != AVMEDIA_TYPE_VIDEO) continue;
        if (!qrpcCtx->latest[i]) continue;
        
        encctx->pix_fmt = cctx->pix_fmt;
        encctx->width = qrpcCtx->latest[i]->width;
        encctx->height = qrpcCtx->latest[i]->height;
        if ((ret = avcodec_open2(encctx, enc, NULL)) < 0) goto end;

        AVPacket pkt;
        av_init_packet(&pkt);
    
        int ret = avcodec_send_frame(encctx, qrpcCtx->latest[i]);
        if (ret < 0) {
            fprintf(stderr, "avcodec_send_frame err:%d", ret);
            goto endloop;
        }
        while (ret >= 0) {
            ret = avcodec_receive_packet(encctx, &pkt);
            if (ret >= 0) {
                ret = read_latest_callback(qrpcCtx->ioctx, seq, pkt.data, pkt.size);
                if (ret < 0) ret = AVERROR(EPIPE);
            }
        }

        endloop:
        av_packet_unref(&pkt);
        break;
    }

end:
    avcodec_free_context(&encctx);
    return ret;
}

int AVFormat_SubcribeAVFrame(AVFormatContext* ctx, const char *fmt, uint64_t seq)
{
    AVFormatQrpcContext *qrpcCtx = ctx->opaque;
    if (!qrpcCtx) return AVERROR(EINVAL);
    
    AVOutputFormat *ofmt = av_guess_format(fmt, NULL, NULL);
    if (!ofmt) return AVERROR(EINVAL);

    AVFormatContext *oc;
    int ret;
    if ((ret = avformat_alloc_output_context2(&oc, ofmt, NULL, NULL)) < 0) return ret;
    oc->opaque = ctx;

    size_t avio_ctx_buffer_size = 4096;
    uint8_t *avio_ctx_buffer = av_malloc(avio_ctx_buffer_size);
    if (!avio_ctx_buffer) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    AVFormatQrpcContextSubcriber *subcriber = add_subscriber(qrpcCtx, oc, seq);
    AVIOContext *avio_ctx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size,
                                  0, subcriber, NULL, &write_subcriber_callback, NULL);
    if (!avio_ctx) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    oc->pb = avio_ctx;
    if ((ret = avformat_write_header(oc, NULL)) < 0) goto end;
    

 end:
    if (ret < 0) {
        avformat_free_context(oc);
        if (avio_ctx) {
            av_freep(&avio_ctx->buffer);
            av_freep(&avio_ctx);
        }

        return ret;
    }

    return 0;
}

void AVFormat_UnsubcribeAVFrame(AVFormatContext* ctx, uint64_t seq)
{
    AVFormatQrpcContext *qrpcCtx = ctx->opaque;
    if (!qrpcCtx) return;

    del_subscriber(qrpcCtx, seq, false);
}

void write_subcribers(AVFormatQrpcContext *qrpcCtx, AVFrame *frame)
{
    pthread_mutex_lock(&qrpcCtx->mutex);
    if (qrpcCtx->subcribers) {
        AVFormatQrpcContextSubcriber *sub = qrpcCtx->subcribers->first;
        while (sub) {
            if (av_interleaved_write_frame(sub->sctx, NULL) < 0) del_subscriber(qrpcCtx, sub->seq, true);
            sub = sub->next;
        }
    }
    pthread_mutex_unlock(&qrpcCtx->mutex);
}

static AVFormatQrpcContextSubcriber* add_subscriber(AVFormatQrpcContext* qrpcCtx, AVFormatContext *oc, uint64_t seq)
{
    AVFormatQrpcContextSubcriber *subcriber = NULL;
    pthread_mutex_lock(&qrpcCtx->mutex);
    if (!qrpcCtx->subcribers) {
        AVFormatQrpcContextSubcriberList *subcribers = av_malloc(sizeof(AVFormatQrpcContextSubcriberList));
        subcriber = av_malloc(sizeof(AVFormatQrpcContextSubcriber));
        subcriber->seq = seq;
        subcriber->sctx = oc;
        subcriber->next = NULL;
        subcribers->first = subcribers->last = subcriber;
        subcribers->n = 1;
        qrpcCtx->subcribers = subcribers;
        return subcriber;
    } else {
        subcriber = av_malloc(sizeof(AVFormatQrpcContextSubcriber));
        subcriber->seq = seq;
        subcriber->sctx = oc;
        subcriber->next = NULL;
        if (!qrpcCtx->subcribers->last) {
            qrpcCtx->subcribers->first = qrpcCtx->subcribers->last = subcriber;
        }
        else {
            qrpcCtx->subcribers->last->next = subcriber;
            qrpcCtx->subcribers->last = subcriber;
        }
        qrpcCtx->subcribers->n ++;
    }
    pthread_mutex_unlock(&qrpcCtx->mutex);
    return subcriber;
}

static void del_subscriber(AVFormatQrpcContext* qrpcCtx, uint64_t seq, bool locked)
{
    if (!locked) pthread_mutex_lock(&qrpcCtx->mutex);
    if (qrpcCtx->subcribers) {
        AVFormatQrpcContextSubcriber* prev = NULL;
        AVFormatQrpcContextSubcriber* sub = qrpcCtx->subcribers->first;
        while (sub) {
            if (sub->seq == seq) {
                if (!prev) {
                    qrpcCtx->subcribers->first = qrpcCtx->subcribers->last = NULL;
                    free_subcriber(sub);
                } else {
                    prev->next = sub->next;
                    if (qrpcCtx->subcribers->last == sub) qrpcCtx->subcribers->last = prev;
                    free_subcriber(sub);
                }
                qrpcCtx->subcribers->n --;
                return;
            }
            prev = sub;
            sub = sub->next;
        }
    }
    if (!locked) pthread_mutex_unlock(&qrpcCtx->mutex);
}

void free_subcriber(AVFormatQrpcContextSubcriber* sub)
{
    avformat_free_context(sub->sctx);
}

int write_subcriber_callback(void* subvoid, uint8_t *buf, int buf_size)
{
    AVFormatQrpcContextSubcriber* sub = subvoid;
    return write_packet_callback(sub->sctx->opaque, sub->seq, buf, buf_size);
}

void AVFormat_Free(AVFormatContext* ctx)
{
    AVFormatQrpcContext *qrpcCtx = ctx->opaque;
    free_qrpc_context(qrpcCtx);
    avformat_free_context(ctx);
}

void free_qrpc_context(AVFormatQrpcContext *qrpcCtx)
{
    if (qrpcCtx) {
        if (qrpcCtx->cctx) {
            for (int i = 0; i < qrpcCtx->nb_cctx; i++) {
                if (qrpcCtx->cctx[i]) {
                    avcodec_free_context(&qrpcCtx->cctx[i]);
                }
            }
            av_free(qrpcCtx->cctx);
        }
        if (qrpcCtx->latest) {
            for (int i = 0; i < qrpcCtx->nb_cctx; i++) {
                if (qrpcCtx->latest[i]) {
                    av_frame_free(&qrpcCtx->latest[i]);
                }
            }
            av_free(qrpcCtx->latest);
        }
        av_free(qrpcCtx);
    }
}

int GOAVERROR_EINVAL = AVERROR(EINVAL);
int GOAVERROR_EOF = AVERROR_EOF;
int GOAVERROR_EAGAIN = AVERROR(EAGAIN);

int open_codec_context(int stream_idx, AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx)
{
    AVStream* st = fmt_ctx->streams[stream_idx];
    AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) {
        fprintf(stderr, "Failed to find codec: %d\n", st->codecpar->codec_id);
        return AVERROR(EINVAL);
    }

    *dec_ctx = avcodec_alloc_context3(dec);
    if (!*dec_ctx) {
        fprintf(stderr, "Failed to allocate the codec context: %d\n", st->codecpar->codec_id);
        return AVERROR(ENOMEM);
    }

    int ret;

    /* Copy codec parameters from input stream to output codec context */
    if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
        fprintf(stderr, "Failed to copy codec parameters to decoder context:%d\n", st->codecpar->codec_id);
        goto end;
    }

    if ((ret = avcodec_open2(*dec_ctx, dec, NULL)) < 0) {
        fprintf(stderr, "Failed to open codec:%d\n", st->codecpar->codec_id);
        return ret;
    }

end:
    if (ret < 0) {
        if (*dec_ctx) {
            avcodec_free_context(dec_ctx);
        }
        return ret;
    }

    return 0;
}


int AV_STRERROR(int errnum, char *errbuf, int errbuf_size)
{
    return av_strerror(errnum, errbuf, (size_t)errbuf_size);
}