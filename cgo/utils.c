#include "utils.h"
#include <stdatomic.h>
#include <stdbool.h>


typedef struct AVFormatQrpcContextSubscriber {
    AVCodecContext **enc_ctx;
    AVFormatContext *sctx;
    uint64_t seq;
    struct AVFormatQrpcContextSubscriber *next;
} AVFormatQrpcContextSubscriber;

typedef struct AVFormatQrpcContextSubscriberList {
    AVFormatQrpcContextSubscriber *first;
    AVFormatQrpcContextSubscriber *last;
    int n;
} AVFormatQrpcContextSubscriberList;

typedef struct AVFormatQrpcContext {
    AVCodecContext **dec_ctx;// for decode input
    int nb_streams;
    AVFrame **latest; // store latest frame
    void* goctx; // reference to go
    pthread_mutex_t mutex;
    AVFormatQrpcContextSubscriberList *subscribers;
} AVFormatQrpcContext;

typedef struct IOSeqContext {
    void *goctx;
    uint64_t seq;
} IOSeqContext;


extern int read_packet_callback(void *goctx, uint8_t *buf, int buf_size);
extern int read_packet_seq_callback(void *goctx, uint64_t seq, uint8_t *buf, int buf_size);
extern int write_packet_seq_callback(void *goctx, uint64_t seq, uint8_t *buf, int bufSize);

static int open_codec_context(int stream_idx, AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx);
static void free_qrpc_context(AVFormatQrpcContext *qrpcCtx);
static void write_subscribers(AVFormatQrpcContext *qrpcCtx, int stream_index, AVFrame *frame);
static AVFormatQrpcContextSubscriber* new_subscriber(AVFormatContext *oc, uint64_t seq);
static void add_subscriber(AVFormatQrpcContext *qrpcCtx, AVFormatQrpcContextSubscriber *subscriber);
static void del_subscriber(AVFormatQrpcContext* qrpcCtx, uint64_t seq, bool locked);
static void free_subscriber(AVFormatQrpcContextSubscriber* sub);
static int write_subscriber_callback(void* sub, uint8_t *buf, int buf_size);
static int prepare_avformatcontext_for_output(AVFormatContext *ifc, AVFormatQrpcContextSubscriber *subscriber);
static int encode_avframe(AVFrame *frame, AVCodecContext *enc_ctx, void *opaque, int(*on_pkt)(void *, AVPacket *));
static int on_ioseq_pkt(void *opaque, AVPacket *pkt);
static int on_subscriber_pkt(void *opaque, AVPacket *pkt);
    


int avformat_open_qrpc_input(AVFormatContext **ppctx, const char *fmt, void* goctx)
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
                                  0, goctx, &read_packet_callback, NULL, NULL);
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
    qrpcCtx->goctx = goctx;
    qrpcCtx->nb_streams = (*ppctx)->nb_streams;
    qrpcCtx->dec_ctx = av_mallocz_array(qrpcCtx->nb_streams, sizeof(AVCodecContext*));
    if (!qrpcCtx->dec_ctx) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    qrpcCtx->latest = av_mallocz_array(qrpcCtx->nb_streams, sizeof(AVFrame*));
    if (!qrpcCtx->latest) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    for (int i = 0; i < qrpcCtx->nb_streams; i++) {
        ret = open_codec_context(i, &qrpcCtx->dec_ctx[i], *ppctx);
        if (ret < 0) goto end;
    }
    
    if ((ret = pthread_mutex_init(&qrpcCtx->mutex, NULL)) < 0) goto end;
    qrpcCtx->subscribers = NULL;

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


AVFormatContext* AVFormat_Open(const char *fmt, uintptr_t goctx) {
    AVFormatContext* ctx;
    if (avformat_open_qrpc_input(&ctx, fmt, (void*)goctx) < 0) {
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

    AVCodecContext *dec_ctx = qrpcCtx->dec_ctx[pkt.stream_index];
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


        write_subscribers(qrpcCtx, pkt.stream_index, frame);

        if (!qrpcCtx->latest[pkt.stream_index]) {
            AVFrame *latest = av_frame_clone(frame);
            qrpcCtx->latest[pkt.stream_index] = latest;
        } else {
            av_frame_copy(qrpcCtx->latest[pkt.stream_index], frame);
        }
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
    for (int i = 0; i < qrpcCtx->nb_streams; i++) {
        AVCodecContext *cctx = qrpcCtx->dec_ctx[i];
        if (!cctx) continue;
        if (cctx->codec->type != AVMEDIA_TYPE_VIDEO) continue;
        if (!qrpcCtx->latest[i]) continue;
        
        encctx->pix_fmt = cctx->pix_fmt;
        encctx->width = qrpcCtx->latest[i]->width;
        encctx->height = qrpcCtx->latest[i]->height;
        if ((ret = avcodec_open2(encctx, enc, NULL)) < 0) goto end;

    
        IOSeqContext ioseq = {qrpcCtx->goctx, seq};
        int ret = encode_avframe(qrpcCtx->latest[i], encctx, &ioseq, on_ioseq_pkt);
        if (ret < 0) {
            fprintf(stderr, "encode_avframe err:%d", ret);
            goto endloop;
        }

        endloop:
        break;
    }

end:
    avcodec_free_context(&encctx);
    return ret;
}

int on_ioseq_pkt(void *opaque, AVPacket *pkt)
{
    IOSeqContext *ioseq = (IOSeqContext *)opaque;
    return read_packet_seq_callback(ioseq->goctx, ioseq->seq, pkt->data, pkt->size);
}

int encode_avframe(AVFrame *frame, AVCodecContext *enc_ctx, void *opaque, int(*on_pkt)(void *, AVPacket *))
{
    int ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "avcodec_send_frame err:%d", ret);
        goto end;
    }
    while (ret >= 0) {
        AVPacket pkt;
        av_init_packet(&pkt);
        ret = avcodec_receive_packet(enc_ctx, &pkt);
        if (ret >= 0) {
            ret = on_pkt(opaque, &pkt);
        }
    }

end:    
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
    oc->opaque = qrpcCtx->goctx;

    size_t avio_ctx_buffer_size = 4096;
    uint8_t *avio_ctx_buffer = av_malloc(avio_ctx_buffer_size);
    if (!avio_ctx_buffer) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    AVFormatQrpcContextSubscriber *subscriber = new_subscriber(oc, seq);
    AVIOContext *avio_ctx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size,
                                  AVIO_FLAG_WRITE, subscriber, NULL, &write_subscriber_callback, NULL);
    if (!avio_ctx) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    oc->pb = avio_ctx;
    if ((ret = prepare_avformatcontext_for_output(ctx, subscriber)) < 0) goto end;
    
    add_subscriber(qrpcCtx, subscriber);

 end:
    if (ret < 0) {
        if (subscriber) free_subscriber(subscriber);
        else avformat_free_context(oc);

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

void write_subscribers(AVFormatQrpcContext *qrpcCtx, int stream_index, AVFrame *frame)
{
    pthread_mutex_lock(&qrpcCtx->mutex);
    if (qrpcCtx->subscribers) {
        int ret;
        AVFormatQrpcContextSubscriber *subscriber = qrpcCtx->subscribers->first;
        while (subscriber) {
            AVFormatQrpcContextSubscriber *next = subscriber->next;
            if (!subscriber->enc_ctx[stream_index]) {
                subscriber = next;
                continue;
            }
            
            ret = encode_avframe(frame, subscriber->enc_ctx[stream_index], subscriber->sctx, on_subscriber_pkt);
            
            if (ret < 0 && ret != AVERROR(EAGAIN)) {
                del_subscriber(qrpcCtx, subscriber->seq, true);

                char errStr[30];
                av_strerror(ret, errStr, sizeof(errStr));
                av_log(NULL, AV_LOG_ERROR, "Failed encode_avframe:%s\n", errStr);
            }
            subscriber = next;
        }
    }
    pthread_mutex_unlock(&qrpcCtx->mutex);
}

int on_subscriber_pkt(void *opaque, AVPacket *pkt)
{
    AVFormatContext *ofc = opaque;
    int ret = av_interleaved_write_frame(ofc, pkt);
    return ret;
}

AVFormatQrpcContextSubscriber* new_subscriber(AVFormatContext *oc, uint64_t seq)
{
    AVFormatQrpcContextSubscriber *subscriber = av_malloc(sizeof(AVFormatQrpcContextSubscriber));
    
    subscriber->sctx = oc;
    subscriber->seq = seq;
    subscriber->next = NULL;
    subscriber->enc_ctx = NULL;
    return subscriber;
}

void add_subscriber(AVFormatQrpcContext* qrpcCtx, AVFormatQrpcContextSubscriber *subscriber)
{
    pthread_mutex_lock(&qrpcCtx->mutex);
    if (!qrpcCtx->subscribers) {
        AVFormatQrpcContextSubscriberList *subscribers = av_malloc(sizeof(AVFormatQrpcContextSubscriberList));
        subscribers->first = subscribers->last = subscriber;
        subscribers->n = 1;
        qrpcCtx->subscribers = subscribers;
    } else {
        if (!qrpcCtx->subscribers->last) {
            qrpcCtx->subscribers->first = qrpcCtx->subscribers->last = subscriber;
        }
        else {
            qrpcCtx->subscribers->last->next = subscriber;
            qrpcCtx->subscribers->last = subscriber;
        }
        qrpcCtx->subscribers->n ++;
    }
    pthread_mutex_unlock(&qrpcCtx->mutex);
}

static void del_subscriber(AVFormatQrpcContext* qrpcCtx, uint64_t seq, bool locked)
{
    if (!locked) {
        pthread_mutex_lock(&qrpcCtx->mutex);
    }
    if (qrpcCtx->subscribers) {
        AVFormatQrpcContextSubscriber* prev = NULL;
        AVFormatQrpcContextSubscriber* sub = qrpcCtx->subscribers->first;
        while (sub) {
            if (sub->seq == seq) {
                if (!prev) {
                    qrpcCtx->subscribers->first = qrpcCtx->subscribers->last = NULL;
                    free_subscriber(sub);
                } else {
                    prev->next = sub->next;
                    if (qrpcCtx->subscribers->last == sub) qrpcCtx->subscribers->last = prev;
                    free_subscriber(sub);
                }
                qrpcCtx->subscribers->n --;
                goto end;
            }
            prev = sub;
            sub = sub->next;
        }
    }
end:    
    if (!locked) {
        pthread_mutex_unlock(&qrpcCtx->mutex);
    }
}

void free_subscriber(AVFormatQrpcContextSubscriber* subscriber)
{
    for (int i = 0; i < subscriber->sctx->nb_streams; i++) {
        if (subscriber->enc_ctx[i]) avcodec_free_context(&subscriber->enc_ctx[i]);
    }
    avformat_free_context(subscriber->sctx);
    av_free(subscriber);
}

int write_subscriber_callback(void* subvoid, uint8_t *buf, int buf_size)
{
    AVFormatQrpcContextSubscriber* subscriber = subvoid;
    return write_packet_seq_callback(subscriber->sctx->opaque, subscriber->seq, buf, buf_size);
}

int prepare_avformatcontext_for_output(AVFormatContext *ifc, AVFormatQrpcContextSubscriber *subscriber)
{
    AVFormatContext *ofc = subscriber->sctx;
    subscriber->enc_ctx = av_mallocz_array(ofc->nb_streams, sizeof(AVCodecContext *));
    if (!subscriber->enc_ctx) {
        av_log(NULL, AV_LOG_ERROR, "Failed allocating AVCodecContext *\n");
        return AVERROR(ENOMEM);
    }
    int ret = 0;
    AVFormatQrpcContext *qrpcCtx = ifc->opaque;
    for (int i = 0; i < ifc->nb_streams; i++) {
        AVStream *out_stream = avformat_new_stream(ofc, NULL);
        if (!out_stream) {
            av_log(NULL, AV_LOG_ERROR, "Failed allocating output stream\n");
            return AVERROR_UNKNOWN;
        }

        AVStream *in_stream = ifc->streams[i];
        AVCodecContext *dec_ctx = qrpcCtx->dec_ctx[i];
        
        if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO
                || dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            AVCodec *encoder = avcodec_find_encoder(dec_ctx->codec_id);
            if (!encoder) {
                av_log(NULL, AV_LOG_FATAL, "Necessary encoder not found\n");
                return AVERROR_INVALIDDATA;
            }
            AVCodecContext *enc_ctx = avcodec_alloc_context3(encoder);
            if (!enc_ctx) {
                av_log(NULL, AV_LOG_FATAL, "Failed to allocate the encoder context\n");
                return AVERROR(ENOMEM);
            }
            if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
                enc_ctx->height = dec_ctx->height;
                enc_ctx->width = dec_ctx->width;
                enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
                /* take first format from list of supported formats */
                if (encoder->pix_fmts)
                    enc_ctx->pix_fmt = encoder->pix_fmts[0];
                else
                    enc_ctx->pix_fmt = dec_ctx->pix_fmt;
                /* video time_base can be set to whatever is handy and supported by encoder */
                enc_ctx->time_base = av_inv_q(dec_ctx->framerate);
            } else {
                enc_ctx->sample_rate = dec_ctx->sample_rate;
                enc_ctx->channel_layout = dec_ctx->channel_layout;
                enc_ctx->channels = av_get_channel_layout_nb_channels(enc_ctx->channel_layout);
                /* take first format from list of supported formats */
                enc_ctx->sample_fmt = encoder->sample_fmts[0];
                enc_ctx->time_base = (AVRational){1, enc_ctx->sample_rate};
            }

            if (ofc->oformat->flags & AVFMT_GLOBALHEADER)
                enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            
             /* Third parameter can be used to pass settings to encoder */
            ret = avcodec_open2(enc_ctx, encoder, NULL);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Cannot open video encoder for stream #%u\n", i);
                return ret;
            }
            ret = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Failed to copy encoder parameters to output stream #%u\n", i);
                return ret;
            }

            out_stream->time_base = enc_ctx->time_base;
            subscriber->enc_ctx[i] = enc_ctx;
        }
    }

     /* init muxer, write output file header */
    ret = avformat_write_header(ofc, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file\n");
        return ret;
    }

    return 0;
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
        if (qrpcCtx->dec_ctx) {
            for (int i = 0; i < qrpcCtx->nb_streams; i++) {
                if (qrpcCtx->dec_ctx[i]) {
                    avcodec_free_context(&qrpcCtx->dec_ctx[i]);
                }
            }
            av_free(qrpcCtx->dec_ctx);
        }
        if (qrpcCtx->latest) {
            for (int i = 0; i < qrpcCtx->nb_streams; i++) {
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