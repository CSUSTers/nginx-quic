/*
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_EVENT_QUIC_CONNECTION_H_INCLUDED_
#define _NGX_EVENT_QUIC_CONNECTION_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_quic_transport.h>
#include <ngx_event_quic_protection.h>


#define NGX_QUIC_SEND_CTX_LAST  (NGX_QUIC_ENCRYPTION_LAST - 1)


typedef struct ngx_quic_connection_s  ngx_quic_connection_t;

/*  0-RTT and 1-RTT data exist in the same packet number space,
 *  so we have 3 packet number spaces:
 *
 *  0 - Initial
 *  1 - Handshake
 *  2 - 0-RTT and 1-RTT
 */
#define ngx_quic_get_send_ctx(qc, level)                                      \
    ((level) == ssl_encryption_initial) ? &((qc)->send_ctx[0])                \
        : (((level) == ssl_encryption_handshake) ? &((qc)->send_ctx[1])       \
                                                 : &((qc)->send_ctx[2]))


typedef struct {
    ngx_queue_t                       queue;
    uint64_t                          seqnum;
    size_t                            len;
    u_char                            id[NGX_QUIC_CID_LEN_MAX];
    u_char                            sr_token[NGX_QUIC_SR_TOKEN_LEN];
} ngx_quic_client_id_t;


typedef struct {
    ngx_udp_connection_t              udp;
    ngx_quic_connection_t            *quic;
    ngx_queue_t                       queue;
    uint64_t                          seqnum;
    size_t                            len;
    u_char                            id[NGX_QUIC_CID_LEN_MAX];
} ngx_quic_server_id_t;


typedef struct {
    ngx_rbtree_t                      tree;
    ngx_rbtree_node_t                 sentinel;

    uint64_t                          received;
    uint64_t                          sent;
    uint64_t                          recv_max_data;
    uint64_t                          send_max_data;

    uint64_t                          server_max_streams_uni;
    uint64_t                          server_max_streams_bidi;
    uint64_t                          server_streams_uni;
    uint64_t                          server_streams_bidi;

    uint64_t                          client_max_streams_uni;
    uint64_t                          client_max_streams_bidi;
    uint64_t                          client_streams_uni;
    uint64_t                          client_streams_bidi;
} ngx_quic_streams_t;


typedef struct {
    size_t                            in_flight;
    size_t                            window;
    size_t                            ssthresh;
    ngx_msec_t                        recovery_start;
} ngx_quic_congestion_t;


/*
 * 12.3.  Packet Numbers
 *
 *  Conceptually, a packet number space is the context in which a packet
 *  can be processed and acknowledged.  Initial packets can only be sent
 *  with Initial packet protection keys and acknowledged in packets which
 *  are also Initial packets.
*/
typedef struct {
    enum ssl_encryption_level_t       level;

    uint64_t                          pnum;        /* to be sent */
    uint64_t                          largest_ack; /* received from peer */
    uint64_t                          largest_pn;  /* received from peer */

    ngx_queue_t                       frames;
    ngx_queue_t                       sent;

    uint64_t                          pending_ack; /* non sent ack-eliciting */
    uint64_t                          largest_range;
    uint64_t                          first_range;
    ngx_msec_t                        largest_received;
    ngx_msec_t                        ack_delay_start;
    ngx_uint_t                        nranges;
    ngx_quic_ack_range_t              ranges[NGX_QUIC_MAX_RANGES];
    ngx_uint_t                        send_ack;
} ngx_quic_send_ctx_t;


struct ngx_quic_connection_s {
    uint32_t                          version;

    ngx_str_t                         scid;  /* initial client ID */
    ngx_str_t                         dcid;  /* server (our own) ID */
    ngx_str_t                         odcid; /* original server ID */

    struct sockaddr                  *sockaddr;
    socklen_t                         socklen;

    ngx_queue_t                       client_ids;
    ngx_queue_t                       server_ids;
    ngx_queue_t                       free_client_ids;
    ngx_queue_t                       free_server_ids;
    ngx_uint_t                        nclient_ids;
    ngx_uint_t                        nserver_ids;
    uint64_t                          max_retired_seqnum;
    uint64_t                          client_seqnum;
    uint64_t                          server_seqnum;

    ngx_uint_t                        client_tp_done;
    ngx_quic_tp_t                     tp;
    ngx_quic_tp_t                     ctp;

    ngx_quic_send_ctx_t               send_ctx[NGX_QUIC_SEND_CTX_LAST];

    ngx_quic_frames_stream_t          crypto[NGX_QUIC_ENCRYPTION_LAST];

    ngx_quic_keys_t                  *keys;

    ngx_quic_conf_t                  *conf;

    ngx_event_t                       push;
    ngx_event_t                       pto;
    ngx_event_t                       close;
    ngx_msec_t                        last_cc;

    ngx_msec_t                        latest_rtt;
    ngx_msec_t                        avg_rtt;
    ngx_msec_t                        min_rtt;
    ngx_msec_t                        rttvar;

    ngx_uint_t                        pto_count;

    ngx_queue_t                       free_frames;
    ngx_chain_t                      *free_bufs;
    ngx_buf_t                        *free_shadow_bufs;

#ifdef NGX_QUIC_DEBUG_ALLOC
    ngx_uint_t                        nframes;
    ngx_uint_t                        nbufs;
#endif

    ngx_quic_streams_t                streams;
    ngx_quic_congestion_t             congestion;
    off_t                             received;

    ngx_uint_t                        error;
    enum ssl_encryption_level_t       error_level;
    ngx_uint_t                        error_ftype;
    const char                       *error_reason;

    ngx_uint_t                        shutdown_code;
    const char                       *shutdown_reason;

    unsigned                          error_app:1;
    unsigned                          send_timer_set:1;
    unsigned                          closing:1;
    unsigned                          shutdown:1;
    unsigned                          draining:1;
    unsigned                          key_phase:1;
    unsigned                          validated:1;
};


ngx_quic_frame_t *ngx_quic_alloc_frame(ngx_connection_t *c);
void ngx_quic_queue_frame(ngx_quic_connection_t *qc, ngx_quic_frame_t *frame);
void ngx_quic_close_connection(ngx_connection_t *c, ngx_int_t rc);
ngx_msec_t ngx_quic_pto(ngx_connection_t *c, ngx_quic_send_ctx_t *ctx);

#endif