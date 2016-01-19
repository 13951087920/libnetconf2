/**
 * \file session_p.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \author Michal Vasko <mvasko@cesnet.cz>
 * \brief libnetconf2 session manipulation
 *
 * Copyright (c) 2015 CESNET, z.s.p.o.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 */

#ifndef NC_SESSION_PRIVATE_H_
#define NC_SESSION_PRIVATE_H_

#include <stdint.h>
#include <pthread.h>

#include <libyang/libyang.h>

#include "libnetconf.h"
#include "netconf.h"
#include "session.h"

#ifdef ENABLE_SSH

#   include <libssh/libssh.h>
#   include <libssh/callbacks.h>
#   include <libssh/server.h>

/* seconds */
#   define NC_SSH_TIMEOUT 10

#   define NC_SSH_AUTH_COUNT 3

struct nc_ssh_client_opts {
    /* SSH authentication method preferences */
    struct {
        NC_SSH_AUTH_TYPE type;
        short int value;
    } auth_pref[NC_SSH_AUTH_COUNT];

    /* SSH key pairs */
    struct {
        char *pubkey_path;
        char *privkey_path;
        int privkey_crypt;
    } *keys;
    int key_count;
};

struct nc_ssh_server_opts {
    ssh_bind sshbind;
    pthread_mutex_t sshbind_lock;

    struct {
        const char *path;
        const char *username;
    } *authkeys;
    uint16_t authkey_count;
    pthread_mutex_t authkey_lock;

    int auth_methods;
    uint16_t auth_attempts;
    uint16_t auth_timeout;
};

#endif /* ENABLE_SSH */

#ifdef ENABLE_TLS

#   include <openssl/bio.h>
#   include <openssl/ssl.h>

struct nc_tls_client_opts {
    SSL_CTX *tls_ctx;
    X509_STORE *tls_store;
};

struct nc_tls_server_opts {
    SSL_CTX *tls_ctx;
    pthread_mutex_t tls_ctx_lock;

    X509_STORE *crl_store;
    pthread_mutex_t crl_lock;

    struct {
        uint32_t id;
        const char *fingerprint;
        NC_TLS_CTN_MAPTYPE map_type;
        const char *name;
    } *ctn;
    uint16_t ctn_count;
    pthread_mutex_t ctn_lock;

    pthread_key_t verify_key;
    pthread_once_t verify_once;
};

#endif /* ENABLE_TLS */

struct nc_server_opts {
    struct ly_ctx *ctx;
    pthread_mutex_t ctx_lock;

    NC_WD_MODE wd_basic_mode;
    int wd_also_supported;
    int interleave_capab;

    uint16_t hello_timeout;
    uint16_t idle_timeout;

    struct nc_bind {
        const char *address;
        uint16_t port;
        int sock;
        NC_TRANSPORT_IMPL ti;
    } *binds;
    uint16_t bind_count;
    pthread_mutex_t bind_lock;

    uint32_t new_session_id;
    pthread_spinlock_t sid_lock;
};

/**
 * Sleep time in microseconds to wait between unsuccessful reading due to EAGAIN or EWOULDBLOCK.
 */
#define NC_READ_SLEEP 100

/**
 * Number of sockets kept waiting to be accepted.
 */
#define NC_REVERSE_QUEUE 1

/**
 * @brief type of the session
 */
typedef enum {
    NC_CLIENT,        /**< client side */
    NC_SERVER         /**< server side */
} NC_SIDE;

/**
 * @brief Enumeration of the supported NETCONF protocol versions
 */
typedef enum {
    NC_VERSION_10 = 0,  /**< NETCONV 1.0 - RFC 4741, 4742 */
    NC_VERSION_11 = 1   /**< NETCONF 1.1 - RFC 6241, 6242 */
} NC_VERSION;

#define NC_VERSION_10_ENDTAG "]]>]]>"
#define NC_VERSION_10_ENDTAG_LEN 6

/**
 * @brief Container to serialize PRC messages
 */
struct nc_msg_cont {
    struct lyxml_elem *msg;
    struct nc_msg_cont *next;
};

/**
 * @brief NETCONF session structure
 */
struct nc_session {
    NC_STATUS status;            /**< status of the session */
    NC_SESSION_TERM_REASON term_reason; /**< reason of termination, if status is NC_STATUS_INVALID */
    NC_SIDE side;                /**< side of the session: client or server */

    /* NETCONF data */
    uint32_t id;                 /**< NETCONF session ID (session-id-type) */
    NC_VERSION version;          /**< NETCONF protocol version */
    pthread_t *notif;            /**< running notifications thread - TODO server-side only? */

    /* Transport implementation */
    NC_TRANSPORT_IMPL ti_type;   /**< transport implementation type to select items from ti union */
    pthread_mutex_t *ti_lock;    /**< lock to access ti. Note that in case of libssh TI, it can be shared with other
                                      NETCONF sessions on the same SSH session (but different SSH channel) */
    union {
        struct {
            int in;              /**< input file descriptor */
            int out;             /**< output file descriptor */
        } fd;                    /**< NC_TI_FD transport implementation structure */
#ifdef ENABLE_SSH
        struct {
            ssh_channel channel;
            ssh_session session;
            struct nc_session *next; /**< pointer to the next NETCONF session on the same
                                          SSH session, but different SSH channel. If no such session exists, it is NULL.
                                          otherwise there is a ring list of the NETCONF sessions */
        } libssh;
#endif
#ifdef ENABLE_TLS
        SSL *tls;
#endif
    } ti;                          /**< transport implementation data */
    const char *username;
    const char *host;
    uint16_t port;

    /* other */
    struct ly_ctx *ctx;            /**< libyang context of the session */
    uint8_t flags;                 /**< various flags of the session - TODO combine with status and/or side */
#define NC_SESSION_SHAREDCTX 0x01

    /* client side only data */
    uint64_t msgid;
    const char **cpblts;           /**< list of server's capabilities on client side */
    struct nc_msg_cont *replies;   /**< queue for RPC replies received instead of notifications */
    struct nc_msg_cont *notifs;    /**< queue for notifications received instead of RPC reply */

    /* server side only data */
#ifdef ENABLE_SSH
    /* additional flags */
#   define NC_SESSION_SSH_AUTHENTICATED 0x02
#   define NC_SESSION_SSH_SUBSYS_NETCONF 0x04

    uint16_t ssh_auth_attempts;
#endif
#ifdef ENABLE_TLS
    X509 *tls_cert;
#endif
};

struct nc_pollsession {
    struct {
        int fd;
        short events;
        short revents;
        struct nc_session *session;
    } *sessions;
    uint16_t session_count;
};

NC_MSG_TYPE nc_send_msg(struct nc_session *session, struct lyd_node *op);

int nc_timedlock(pthread_mutex_t *lock, int timeout, int *elapsed);

/**
 * @brief Fill libyang context in \p session. Context models are based on the stored session
 *        capabilities. If the server does not support \<get-schema\>, the models are searched
 *        for in the directory set using nc_schema_searchpath().
 *
 * @param[in] session Session to create the context for.
 * @return 0 on success, non-zero on failure.
 */
int nc_ctx_check_and_fill(struct nc_session *session);

/**
 * @brief Create and connect a socket.
 *
 * @param[in] host Hostname to connect to.
 * @param[in] port Port to connect on.
 * @return Connected socket or -1 on error.
 */
int nc_connect_getsocket(const char *host, unsigned short port);

/**
 * @brief Perform NETCONF handshake on \p session.
 *
 * @param[in] session NETCONF session to use.
 * @return 0 on success, non-zero on failure.
 */
int nc_handshake(struct nc_session *session);

/**
 * @brief Accept a new (Call Home) connection.
 *
 * @param[in] port Port to listen on.
 * @param[in] timeout Timeout in msec.
 * @param[out] server_port Port the new connection is connected on. Can be NULL.
 * @param[out] server_host Host the new connection was initiated from. Can be NULL.
 * @return Connected socket with the new connection, -1 on error.
 */
int nc_callhome_accept_connection(uint16_t port, int32_t timeout, uint16_t *server_port, char **server_host);

/**
 * @brief Create a listening socket.
 *
 * @param[in] address IP address to listen on.
 * @param[in] port Port to listen on.
 * @return Listening socket, -1 on error.
 */
int nc_sock_listen(const char *address, uint32_t port);

/**
 * @brief Accept a new connection on a listening socket.
 *
 * @param[in] binds Structure with the listening sockets.
 * @param[in] bind_count Number of \p binds.
 * @param[in] timeout Timeout for accepting.
 * @param[out] ti Type of transport of the accepted connection. Can be NULL.
 * @param[out] host Host of the remote peer. Can be NULL.
 * @param[out] port Port of the new connection. Can be NULL.
 * @return Accepted socket of the new connection, -1 on error.
 */
int nc_sock_accept(struct nc_bind *binds, uint16_t bind_count, int timeout, NC_TRANSPORT_IMPL *ti, char **host, uint16_t *port);

#ifdef ENABLE_SSH

/**
 * @brief Establish SSH transport on a socket.
 *
 * @param[in] session Session structure of the new connection.
 * @param[in] sock Socket of the new connection.
 * @param[in] timeout Timeout for all the related tasks.
 * @return 1 on success, 0 on timeout, -1 on error.
 */
int nc_accept_ssh_session(struct nc_session *session, int sock, int timeout);

#endif

#ifdef ENABLE_TLS

/**
 * @brief Establish TLS transport on a socket.
 *
 * @param[in] session Session structure of the new connection.
 * @param[in] sock Socket of the new connection.
 * @param[in] timeout Timeout for all the related tasks.
 * @return 1 on success, 0 on timeout, -1 on error.
 */
int nc_accept_tls_session(struct nc_session *session, int sock, int timeout);

#endif

/**
 * Functions
 * - io.c
 */

/**
 * @brief Read message from the wire.
 *
 * Accepts hello, rpc, rpc-reply and notification. Received string is transformed into
 * libyang XML tree and the message type is detected from the top level element.
 *
 * @param[in] session NETCONF session from which the message is being read.
 * @param[in] timeout Timeout in milliseconds. Negative value means infinite timeout,
 *            zero value causes to return immediately.
 * @param[out] data XML tree built from the read data.
 * @return Type of the read message. #NC_MSG_WOULDBLOCK is returned if timeout is positive
 * (or zero) value and it passed out without any data on the wire. #NC_MSG_ERROR is
 * returned on error and #NC_MSG_NONE is never returned by this function.
 */
NC_MSG_TYPE nc_read_msg_poll(struct nc_session* session, int timeout, struct lyxml_elem **data);

/**
 * @brief Read message from the wire.
 *
 * Accepts hello, rpc, rpc-reply and notification. Received string is transformed into
 * libyang XML tree and the message type is detected from the top level element.
 *
 * @param[in] session NETCONF session from which the message is being read.
 * @param[out] data XML tree built from the read data.
 * @return Type of the read message. #NC_MSG_WOULDBLOCK is returned if timeout is positive
 * (or zero) value and it passed out without any data on the wire. #NC_MSG_ERROR is
 * returned on error and #NC_MSG_NONE is never returned by this function.
 */
NC_MSG_TYPE nc_read_msg(struct nc_session* session, struct lyxml_elem **data);

/**
 * @brief Write message into wire.
 *
 * @param[in] session NETCONF session to which the message will be written.
 * @param[in] type Type of the message to write. According to the type, the
 * specific additional parameters are required or accepted:
 * - #NC_MSG_RPC
 *   - `struct lyd_node *op;` - operation (content of the \<rpc/\> to be sent. Required parameter.
 *   - `const char *attrs;` - additional attributes to be added into the \<rpc/\> element.
 *     Required parameter.
 *     `message-id` attribute is added automatically and default namespace is set to #NC_NS_BASE.
 *     Optional parameter.
 * - #NC_MSG_REPLY
 *   - `struct lyxml_node *rpc_elem;` - root of the RPC object to reply to. Required parameter.
 *   - `struct nc_server_reply *reply;` - RPC reply. Required parameter.
 * - #NC_MSG_NOTIF
 *   - TODO: content
 * @return 0 on success
 */
int nc_write_msg(struct nc_session *session, NC_MSG_TYPE type, ...);

/**
 * @brief Check whether a session is still connected (on transport layer).
 *
 * @param[in] session Session to check.
 * @return 1 if connected, 0 if not.
 */
int nc_session_is_connected(struct nc_session *session);

#endif /* NC_SESSION_PRIVATE_H_ */
