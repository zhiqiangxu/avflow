#ifndef AVFORMAT_RTMPPKT_H
#define AVFORMAT_RTMPPKT_H

#include "libavcodec/bytestream.h"
#include "avformat.h"
#include "url.h"


typedef enum QrpcStream {
    QRPC_STREAM_AUTH  =  1,
    QRPC_STREAM_PLAY  =  3,
} QrpcStream;

typedef enum QrpcCmd {
    QRPC_CMD_AUTH   =  1,
    QRPC_CMD_PLAY   =  3,
} QrpcCmd;

typedef struct QrpcPacket {
    QrpcStream    stream_id;
	QrpcCmd     cmd;
	char*       payload;
    int         payload_len;
} QrpcPacket;

int ff_qrpc_packet_create(QrpcPacket* pkt, QrpcStream stream_id, QrpcCmd cmd, int size);

void ff_qrpc_packet_destroy(QrpcPacket *pkt);

int ff_qrpc_packet_write(URLContext *h, QrpcPacket *p);

int ff_qrpc_packet_read(URLContext *h, QrpcPacket *p);
#endif