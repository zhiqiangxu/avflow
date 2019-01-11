#include "avformat.h"
#include "libavutil/avassert.h"
#include "libavutil/parseutils.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"

#include "internal.h"
#include "network.h"
#include "os_support.h"
#include "url.h"
#if HAVE_POLL_H
#include <poll.h>
#endif
#include <stdbool.h>

#include "qrpcpkt.h"

typedef struct QrpcContext {
    const AVClass *class;
    URLContext*   stream;
    char*       payload;
    int         payload_len;
    int         offset;
    char        *id;
    char        *pass;
    char        *uri;
    bool        publish;
} QrpcContext;

#define OFFSET(x) offsetof(QrpcContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { NULL }
};

static const AVClass qrpc_class = {
    .class_name = "qrpc",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};



static int qrpc_read_packet(QrpcContext *qctx, QrpcPacket *pkt)
{
    return ff_qrpc_packet_read(qctx->stream, pkt);
}

static int qrpc_send_packet(QrpcContext *qctx, QrpcPacket *pkt) 
{
    int ret = ff_qrpc_packet_write(qctx->stream, pkt);
    ff_qrpc_packet_destroy(pkt);
    return ret;
}

static const char *json_escape_str(AVBPrint *dst, const char *src)
{
    static const char json_escape[] = {'"', '\\', '\b', '\f', '\n', '\r', '\t', 0};
    static const char json_subst[]  = {'"', '\\',  'b',  'f',  'n',  'r',  't', 0};
    const char *p;

    for (p = src; *p; p++) {
        char *s = strchr(json_escape, *p);
        if (s) {
            av_bprint_chars(dst, '\\', 1);
            av_bprint_chars(dst, json_subst[s - json_escape], 1);
        } else if ((unsigned char)*p < 32) {
            av_bprintf(dst, "\\u00%02x", *p & 0xff);
        } else {
            av_bprint_chars(dst, *p, 1);
        }
    }
    return dst->str;
}

/* return non zero if error */
static int qrpc_open(URLContext *s, const char *uri, int flags)
{
    int port;
    QrpcContext *qctx = s->priv_data;
    qctx->payload = qctx->id = qctx->pass = NULL;
    qctx->payload_len = qctx->offset = 0;
    
    const char *p;
    char buf[1024];
    int ret;
    char hostname[1024],proto[10], path[256];

    av_url_split(proto, sizeof(proto), NULL, 0, hostname, sizeof(hostname),
        &port, path, sizeof(path), uri);
    if (strcmp(proto, "qrpc"))
        return AVERROR(EINVAL);
    if (port <= 0 || port >= 65536) {
        av_log(s, AV_LOG_ERROR, "Port missing in uri\n");
        return AVERROR(EINVAL);
    }
    p = strchr(uri, '?');
    if (p) {
        if (av_find_info_tag(buf, sizeof(buf), "id", p)) {
            qctx->id = av_malloc(strlen(buf) + 1);
            av_strlcpy(qctx->id, buf, strlen(buf) + 1);
        } else {
            av_log(s, AV_LOG_ERROR, "id missing in uri\n");
            return AVERROR(EINVAL);
        }
        if (av_find_info_tag(buf, sizeof(buf), "pass", p)) {
            qctx->pass = av_malloc(strlen(buf) + 1);
            av_strlcpy(qctx->pass, buf, strlen(buf) + 1);
        } else {
            av_log(s, AV_LOG_ERROR, "pass missing in uri\n");
            return AVERROR(EINVAL);
        }
        if (av_find_info_tag(buf, sizeof(buf), "mode", p)) {
            qctx->publish = !strcmp(buf, "publish");
        } else {
            av_log(s, AV_LOG_ERROR, "mode missing in uri\n");
            return AVERROR(EINVAL);
        }
        if (!qctx->publish) {
            p = strchr(path, '?');
            if (p == path) {
                av_log(s, AV_LOG_ERROR, "uri missing for non-publisher\n");
                return AVERROR(EINVAL);
            }
            qctx->uri = av_malloc(p - path + 1);
            av_strlcpy(qctx->uri, path, p - path + 1);
        }
    }
    

    ff_url_join(buf, sizeof(buf), "tcp", NULL, hostname, port, NULL);

    if ((ret = ffurl_open_whitelist(&qctx->stream, buf, AVIO_FLAG_READ_WRITE,
                                    &s->interrupt_callback, NULL,
                                    s->protocol_whitelist, s->protocol_blacklist, s)) < 0) {
        av_log(s , AV_LOG_ERROR, "Cannot open connection %s\n", buf);
        return ret;
    }

    QrpcPacket pkt;
    if ((ret = ff_qrpc_packet_create(&pkt, QRPC_STREAM_AUTH, QRPC_CMD_AUTH, 0)) < 0)
        return ret;

    {
        AVBPrint bp;
        av_bprint_init(&bp, 1, AV_BPRINT_SIZE_UNLIMITED);
        av_bprintf(&bp,"{\"id\":\"");
        json_escape_str(&bp, qctx->id);
        av_bprintf(&bp,"\",\"pass\":\"");
        json_escape_str(&bp, qctx->pass);
        av_bprintf(&bp,"\"}");
        
        char* json;
        av_bprint_finalize(&bp, &json);
        pkt.payload = json;
        pkt.payload_len = bp.len;
    }
    

    if ((ret = qrpc_send_packet(qctx, &pkt)) < 0)
        return ret;

    if ((ret = qrpc_read_packet(qctx, &pkt)) < 0)
        return ret;
    
    if (!(pkt.payload_len == 2 && pkt.payload[0] == 'O' && pkt.payload[1] == 'K'))
        ret = AVERROR_INVALIDDATA;
    
    printf("qrpc_open:ret = %d, len = %d\n", ret, pkt.payload_len);
    ff_qrpc_packet_destroy(&pkt);

    if ((ret = ff_qrpc_packet_create(&pkt, QRPC_STREAM_PLAY, QRPC_CMD_PLAY, 0)) < 0)
        return ret;

    
    {
        AVBPrint bp;
        av_bprint_init(&bp, 1, AV_BPRINT_SIZE_UNLIMITED);
        av_bprintf(&bp,"{\"publish\":%d", qctx->publish ? 1 : 0);
        if (!qctx->publish) {
            av_bprintf(&bp,",\"uri\":\"");
            json_escape_str(&bp, qctx->uri);
            av_bprintf(&bp,"\"");
        }
        av_bprintf(&bp,"}");
        
        char* json;
        av_bprint_finalize(&bp, &json);
        pkt.payload = json;
        pkt.payload_len = bp.len;
    }

    if ((ret = qrpc_send_packet(qctx, &pkt)) < 0)
        return ret;

    if ((ret = qrpc_read_packet(qctx, &pkt)) < 0)
        return ret;
    
    if (!(pkt.payload_len == 2 && pkt.payload[0] == 'O' && pkt.payload[1] == 'K'))
        ret = AVERROR_INVALIDDATA;

    ff_qrpc_packet_destroy(&pkt);    

    return ret;
}


static int qrpc_read(URLContext *h, uint8_t *buf, int size)
{
    QrpcContext *qctx = h->priv_data;
    int ret;

    QrpcPacket pkt;
    int offset = 0;
    while (true) {

        int remain = size - offset;

        if (qctx->payload) {
            int last_remain = qctx->payload_len - qctx->offset;
            if (last_remain <= remain) {
                memcpy(buf+offset, qctx->payload+qctx->offset, last_remain);
                offset += last_remain;
                av_free(qctx->payload);
                qctx->payload = NULL;
                qctx->payload_len = qctx->offset = 0;
            } else {
                memcpy(buf+offset, qctx->payload+qctx->offset, remain);
                offset += remain;
                qctx->offset += remain;
            }

            if (offset == size) return size;

            remain = size - offset;
        }

        ret = qrpc_read_packet(qctx, &pkt);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "qrpc_read_packet error:%d\n", ret);
            return ret;
        }
        
        if (pkt.payload_len <= remain) {
            memcpy(buf+offset, pkt.payload, pkt.payload_len);
            offset += pkt.payload_len;
            ff_qrpc_packet_destroy(&pkt);
        } else {
            memcpy(buf+offset, pkt.payload, remain);
            offset += remain;
            qctx->payload = pkt.payload;
            qctx->payload_len = pkt.payload_len;
            qctx->offset = remain;
            pkt.payload = NULL;
        }

        if (offset == size) return size;
    }
    
}

static int qrpc_write(URLContext *h, const uint8_t *buf, int size)
{
    QrpcContext *qctx = h->priv_data;
    QrpcPacket pkt;
    int ret;
    if ((ret = ff_qrpc_packet_create(&pkt, QRPC_STREAM_PLAY, QRPC_CMD_PLAY, size)) < 0)
        return ret;

    memcpy(pkt.payload, buf, size);
    return qrpc_send_packet(qctx, &pkt);
}

static int qrpc_close(URLContext *h)
{
    QrpcContext *qctx = h->priv_data;
    if (qctx->id) av_free(qctx->id);
    if (qctx->pass) av_free(qctx->pass);

    return ffurl_close(qctx->stream);
}



const URLProtocol ff_qrpc_protocol = {
    .name                = "qrpc",
    .url_open            = qrpc_open,
    .url_read            = qrpc_read,
    .url_write           = qrpc_write,
    .url_close           = qrpc_close,
    .priv_data_size      = sizeof(QrpcContext),
    .flags               = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class     = &qrpc_class,
    .default_whitelist   = "tcp",
};