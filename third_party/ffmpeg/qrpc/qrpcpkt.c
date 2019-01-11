#include "libavcodec/bytestream.h"
#include "libavutil/avstring.h"
#include "libavutil/intfloat.h"
#include "avformat.h"

#include "qrpcpkt.h"

int ff_qrpc_packet_create(QrpcPacket* pkt, QrpcStream stream_id, QrpcCmd cmd, int payload_len) 
{
    if (payload_len) {
        pkt->payload = av_realloc(NULL, payload_len);
        if (!pkt->payload)
            return AVERROR(ENOMEM);
    }

    pkt->stream_id = stream_id;
    pkt->cmd = cmd;
    pkt->payload_len = payload_len;


    return 0;
}

void ff_qrpc_packet_destroy(QrpcPacket *pkt)
{
    if (!pkt)
        return;
    av_freep(&pkt->payload);
    pkt->payload_len = 0;
}

int ff_qrpc_packet_write(URLContext *h, QrpcPacket *pkt)
{
    uint8_t pkt_hdr[16], *p = pkt_hdr;
    int ret;

    unsigned int size = 12 + pkt->payload_len;

    bytestream_put_be32(&p, size);
    bytestream_put_be64(&p, pkt->stream_id);
    unsigned int cmdflags = pkt->cmd;
    if (pkt->stream_id == QRPC_STREAM_PLAY) {
        cmdflags |= 1 << 24;
    }
    bytestream_put_be32(&p, cmdflags);

    if ((ret = ffurl_write(h, pkt_hdr, sizeof(pkt_hdr))) < 0)
        return ret;

    return ffurl_write(h, pkt->payload, pkt->payload_len);
    
}

int ff_qrpc_packet_read(URLContext *h, QrpcPacket *pkt)
{
    uint8_t pkt_hdr[16];
    const uint8_t *p = pkt_hdr;
    int ret;

    if ((ret = ffurl_read_complete(h, pkt_hdr, sizeof(pkt_hdr))) != sizeof(pkt_hdr))
        return ret;
    
    unsigned int size = bytestream_get_be32(&p);
    if (size < 12) {
        return AVERROR_INVALIDDATA;
    }
    uint64_t stream_id = bytestream_get_be64(&p);
    unsigned int cmd = bytestream_get_be32(&p) & 0xffffff;
    
    int payload_len = size-12;
    if ((ret = ff_qrpc_packet_create(pkt, stream_id, cmd, payload_len)) < 0)
        return ret;

    if (ffurl_read_complete(h, pkt->payload, payload_len) != payload_len) {
        ff_qrpc_packet_destroy(pkt);
        return AVERROR(EIO);
    }

    pkt->stream_id = stream_id;
    pkt->cmd = cmd;
    pkt->payload_len = payload_len;

    return 0;
}