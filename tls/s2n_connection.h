/*
 * Copyright 2014 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#pragma once

#include <stdint.h>
#include <signal.h>
#include <errno.h>
#include <s2n.h>

#include "tls/s2n_tls_parameters.h"
#include "tls/s2n_handshake.h"
#include "tls/s2n_crypto.h"
#include "tls/s2n_config.h"
#include "tls/s2n_prf.h"

#include "stuffer/s2n_stuffer.h"

#include "crypto/s2n_hash.h"
#include "crypto/s2n_hmac.h"

#include "utils/s2n_timer.h"
#include "utils/s2n_mem.h"

#define S2N_TLS_PROTOCOL_VERSION_LEN    2

struct s2n_connection {
    /* The configuration (cert, key .. etc ) */
    struct s2n_config *config;

    /* The read and write fds don't have to be the same (e.g. two pipes) */
    int readfd;
    int writefd;

    /* Is this connection a client or a server connection */
    s2n_mode mode;

    /* Does s2n handle the blinding, or does the application */
    s2n_blinding blinding;

    /* A timer to measure the time between record writes */
    struct s2n_timer write_timer;

    /* When fatal errors occurs, s2n imposes a pause before
     * the connection is closed. If non-zero, this value tracks
     * how many nanoseconds to pause - which will be relative to
     * the write_timer value. */
    uint64_t delay;

    /* The session id */
    uint8_t session_id[S2N_TLS_SESSION_ID_MAX_LEN];
    uint8_t session_id_len;

    /* The version advertised by the client, by the
     * server, and the actual version we are currently
     * speaking. */
    uint8_t client_hello_version;
    uint8_t client_protocol_version;
    uint8_t server_protocol_version;
    uint8_t actual_protocol_version;
    uint8_t actual_protocol_version_established;

    /* Our crypto paramaters */
    struct s2n_crypto_parameters initial;
    struct s2n_crypto_parameters secure;

    /* Which set is the client/server actually using? */
    struct s2n_crypto_parameters *client;
    struct s2n_crypto_parameters *server;

    /* The PRF needs some storage elements to work with */
    union s2n_prf_working_space prf_space;

    /* Our workhorse stuffers, used for buffering the plaintext
     * and encrypted data in both directions.
     */
    uint8_t header_in_data[S2N_TLS_RECORD_HEADER_LENGTH];
    struct s2n_stuffer header_in;
    struct s2n_stuffer in;
    struct s2n_stuffer out;
    enum { ENCRYPTED, PLAINTEXT } in_status;

    /* How big is the record we are actively reading? */
    uint16_t current_in_record_size;

    /* An alert may be fragmented across multiple records,
     * this stuffer is used to re-assemble.
     */
    uint8_t alert_in_data[S2N_ALERT_LENGTH];
    struct s2n_stuffer alert_in;

    /* An alert may be partially written in the outbound
     * direction, so we keep this as a small 2 byte queue.
     *
     * We keep seperate queues for alerts generated by
     * readers (a response to an alert from a peer) and writers (an
     * intentional shutdown) so that the s2n reader and writer
     * can be seperate duplex I/O threads.
     */
    uint8_t reader_alert_out_data[S2N_ALERT_LENGTH];
    uint8_t writer_alert_out_data[S2N_ALERT_LENGTH];
    struct s2n_stuffer reader_alert_out;
    struct s2n_stuffer writer_alert_out;

    /* Determines if we're currently sending or receiving in s2n_shutdown */
    unsigned int close_notify_queued:1;

    /* Our handshake state machine */
    struct s2n_handshake handshake;

    uint16_t max_fragment_length;

    /* Keep some accounting on each connection */
    uint64_t wire_bytes_in;
    uint64_t wire_bytes_out;

    /* Is the connection open or closed ? We use C's only
     * atomic type as both the reader and the writer threads
     * may declare a connection closed. 
     * 
     * A connection can be gracefully closed or hard-closed.
     * When gracefully closed the reader or the writer mark
     * the connection as closing, and then the writer will
     * send an alert message before closing the connection
     * and marking it as closed.
     *
     * A hard-close goes straight to closed with no alert
     * message being sent.
     */
    sig_atomic_t closing;
    sig_atomic_t closed;

    /* TLS extension data */
    char server_name[256];
    char application_protocol[256];
    /* s2n does not support renegotiation.
     * RFC5746 Section 4.3 suggests servers implement a minimal version of the
     * renegotiation_info extension even if renegotiation is not supported. 
     * Some clients may fail the handshake if a corresponding renegotiation_info
     * extension is not sent back by the server.
     */
    unsigned int secure_renegotiation:1;

    /* OCSP stapling response data */
    s2n_status_request_type status_type;
    struct s2n_blob status_response;
};

/* Kill a bad connection */
int s2n_connection_kill(struct s2n_connection *conn);
