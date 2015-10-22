/**
 * \file test_io.c
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief libnetconf2 tests - input/output functions
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

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cmocka.h>
#include <libyang/libyang.h>

#include <session_p.h>
#include <messages_p.h>
#include "config.h"

static int
setup_read(void **state)
{
    int fd;
    struct nc_session *session;

    session = calloc(1, sizeof *session);
    /* test IO with standard file descriptors */
    session->ti_type = NC_TI_FD;

    session->status = NC_STATUS_RUNNING;
    session->ctx = ly_ctx_new(TESTS_DIR"../schemas");
    session->ti_lock = malloc(sizeof *session->ti_lock);
    pthread_mutex_init(session->ti_lock, NULL);

    /* ietf-netconf */
    fd = open(TESTS_DIR"../schemas/ietf-netconf.yin", O_RDONLY);
    if (fd == -1) {
        return -1;
    }

    lys_read(session->ctx, fd, LYS_IN_YIN);
    close(fd);

    *state = session;
    return 0;
}

static int
teardown_read(void **state)
{
    struct nc_session *session = (struct nc_session *)*state;

    nc_session_free(session);
    *state = NULL;

    return 0;
}

static void
test_read_rpc_10(void **state)
{
    struct nc_session *session = (struct nc_session *)*state;
    struct nc_rpc_server *rpc = NULL;
    NC_MSG_TYPE type;

    session->ti.fd.in = open(TESTS_DIR"/data/nc10/rpc-lock", O_RDONLY);
    session->version = NC_VERSION_10;
    session->side = NC_SERVER;

    type = nc_recv_rpc(session, 1000, &rpc);
    assert_int_equal(type, NC_MSG_RPC);
    assert_non_null(rpc);

    nc_rpc_free((struct nc_rpc *)rpc);
}

static void
test_read_rpc_10_bad(void **state)
{
    struct nc_session *session = (struct nc_session *)*state;
    struct nc_rpc_server *rpc = NULL;
    NC_MSG_TYPE type;

    session->ti.fd.in = open(TESTS_DIR"/data/nc10/rpc-lock", O_RDONLY);
    session->version = NC_VERSION_10;
    session->side = NC_CLIENT;

    type = nc_recv_rpc(session, 1000, &rpc);
    assert_int_equal(type, NC_MSG_ERROR);
    assert_null(rpc);

    nc_rpc_free((struct nc_rpc *)rpc);
}

static void
test_read_rpc_11(void **state)
{
    struct nc_session *session = (struct nc_session *)*state;
    struct nc_rpc_server *rpc = NULL;
    NC_MSG_TYPE type;

    session->ti.fd.in = open(TESTS_DIR"/data/nc11/rpc-lock", O_RDONLY);
    session->version = NC_VERSION_11;
    session->side = NC_SERVER;

    type = nc_recv_rpc(session, 1000, &rpc);
    assert_int_equal(type, NC_MSG_RPC);
    assert_non_null(rpc);

    nc_rpc_free((struct nc_rpc *)rpc);
}

static void
test_read_rpc_11_bad(void **state)
{
    struct nc_session *session = (struct nc_session *)*state;
    struct nc_rpc_server *rpc = NULL;
    NC_MSG_TYPE type;

    session->ti.fd.in = open(TESTS_DIR"/data/nc11/rpc-lock", O_RDONLY);
    session->version = NC_VERSION_11;
    session->side = NC_CLIENT;

    type = nc_recv_rpc(session, 1000, &rpc);
    assert_int_equal(type, NC_MSG_ERROR);
    assert_null(rpc);

    nc_rpc_free((struct nc_rpc *)rpc);
}


struct wr {
    struct nc_session *session;
    struct nc_rpc *rpc;
};

static int
setup_write(void **state)
{
    (void) state; /* unused */
    int fd;
    struct wr *w;

    w = malloc(sizeof *w);
    w->session = calloc(1, sizeof *w->session);
    w->session->ctx = ly_ctx_new(TESTS_DIR"../schemas");
    w->session->ti_lock = malloc(sizeof *w->session->ti_lock);
    pthread_mutex_init(w->session->ti_lock, NULL);

    /* ietf-netconf */
    fd = open(TESTS_DIR"../schemas/ietf-netconf.yin", O_RDONLY);
    if (fd == -1) {
        return -1;
    }

    lys_read(w->session->ctx, fd, LYS_IN_YIN);
    close(fd);

    w->session->status = NC_STATUS_RUNNING;
    w->session->version = NC_VERSION_10;
    w->session->msgid = 999;
    w->session->ti_type = NC_TI_FD;
    w->session->ti.fd.in = STDIN_FILENO;
    w->session->ti.fd.out = STDOUT_FILENO;

    /* get rpc to write */
    w->rpc = nc_rpc_lock(NC_DATASTORE_RUNNING);
    assert_non_null(w->rpc);

    close(fd);
    w->session->ti.fd.in = -1;

    *state = w;

    return 0;
}

static int
teardown_write(void **state)
{
    struct wr *w = (struct wr *)*state;

    nc_rpc_free(w->rpc);
    w->session->ti.fd.in = -1;
    w->session->ti.fd.out = -1;
    nc_session_free(w->session);
    free(w);
    *state = NULL;

    return 0;
}

static void
test_write_rpc(void **state)
{
    struct wr *w = (struct wr *)*state;
    NC_MSG_TYPE type;

    w->session->side = NC_CLIENT;

    do {
        type = nc_send_rpc(w->session, w->rpc);
    } while(type == NC_MSG_WOULDBLOCK);

    assert_int_equal(type, NC_MSG_RPC);

    write(w->session->ti.fd.out, "\n", 1);
}

static void
test_write_rpc_10(void **state)
{
    struct wr *w = (struct wr *)*state;

    w->session->version = NC_VERSION_10;

    return test_write_rpc(state);
}

static void
test_write_rpc_11(void **state)
{
    struct wr *w = (struct wr *)*state;

    w->session->version = NC_VERSION_11;

    return test_write_rpc(state);
}

static void
test_write_rpc_bad(void **state)
{
    struct wr *w = (struct wr *)*state;
    NC_MSG_TYPE type;

    w->session->side = NC_SERVER;

    do {
        type = nc_send_rpc(w->session, w->rpc);
    } while(type == NC_MSG_WOULDBLOCK);

    assert_int_equal(type, NC_MSG_ERROR);
}

static void
test_write_rpc_10_bad(void **state)
{
    struct wr *w = (struct wr *)*state;

    w->session->version = NC_VERSION_10;

    return test_write_rpc_bad(state);
}

static void
test_write_rpc_11_bad(void **state)
{
    struct wr *w = (struct wr *)*state;

    w->session->version = NC_VERSION_11;

    return test_write_rpc_bad(state);
}
int main(void)
{
    const struct CMUnitTest io[] = {
        cmocka_unit_test_setup_teardown(test_read_rpc_10, setup_read, teardown_read),
        cmocka_unit_test_setup_teardown(test_read_rpc_10_bad, setup_read, teardown_read),
        cmocka_unit_test_setup_teardown(test_read_rpc_11, setup_read, teardown_read),
        cmocka_unit_test_setup_teardown(test_read_rpc_11_bad, setup_read, teardown_read),
        cmocka_unit_test_setup_teardown(test_write_rpc_10, setup_write, teardown_write),
        cmocka_unit_test_setup_teardown(test_write_rpc_10_bad, setup_write, teardown_write),
        cmocka_unit_test_setup_teardown(test_write_rpc_11, setup_write, teardown_write),
        cmocka_unit_test_setup_teardown(test_write_rpc_11_bad, setup_write, teardown_write)};

    return cmocka_run_group_tests(io, NULL, NULL);
}
