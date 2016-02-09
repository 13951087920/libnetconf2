/**
 * \file test_server_thread.c
 * \author Michal Vasko <mvasko@cesnet.cz>
 * \brief libnetconf2 tests - thread-safety of all server functions
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

#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <libyang/libyang.h>

#include <session_client.h>
#include <session_server.h>
#include <log.h>
#include "config.h"

/* millisec */
#define NC_ACCEPT_TIMEOUT 5000
/* millisec */
#define NC_PS_POLL_TIMEOUT 5000
/* sec */
#define CLIENT_SSH_AUTH_TIMEOUT 10

pthread_barrier_t barrier;

static int
setup_lib(void)
{
    nc_verbosity(NC_VERB_VERBOSE);

#if defined(ENABLE_SSH) && defined(ENABLE_TLS)
    nc_ssh_tls_init();
#elif defined(ENABLE_SSH)
    nc_ssh_init();
#elif defined(ENABLE_TLS)
    nc_tls_init();
#endif

    return 0;
}

static int
teardown_lib(void)
{
#if defined(ENABLE_SSH) && defined(ENABLE_TLS)
    nc_ssh_tls_destroy();
#elif defined(ENABLE_SSH)
    nc_ssh_destroy();
#elif defined(ENABLE_TLS)
    nc_tls_destroy();
#endif

    return 0;
}

#if defined(ENABLE_SSH) || defined(ENABLE_TLS)

static void *
server_thread(void *arg)
{
    (void)arg;
    int ret;
    struct nc_pollsession *ps;
    struct nc_session *session;

    ps = nc_ps_new();
    assert(ps);

    pthread_barrier_wait(&barrier);

#if defined(ENABLE_SSH) && defined(ENABLE_TLS)
    ret = nc_accept(NC_ACCEPT_TIMEOUT, &session);
    assert(ret == 1);

    nc_ps_add_session(ps, session);
    ret = nc_ps_poll(ps, NC_PS_POLL_TIMEOUT);
    assert(ret == 3);
    nc_ps_clear(ps);
#endif

    ret = nc_accept(NC_ACCEPT_TIMEOUT, &session);
    assert(ret == 1);

    nc_ps_add_session(ps, session);
    ret = nc_ps_poll(ps, NC_PS_POLL_TIMEOUT);
    assert(ret == 3);
    nc_ps_clear(ps);

    nc_ps_free(ps);

    nc_thread_destroy();
    return NULL;
}

#endif /* ENABLE_SSH || ENABLE_TLS */

#ifdef ENABLE_SSH

static void *
ssh_add_endpt_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_ssh_add_endpt_listen("tertiary", "0.0.0.0", 6003);
    assert(!ret);

    return NULL;
}

static void *
ssh_endpt_set_port_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_ssh_endpt_set_port("quaternary", 6005);
    assert(!ret);

    return NULL;
}

static void *
ssh_del_endpt_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_ssh_del_endpt("secondary");
    assert(!ret);

    return NULL;
}

static void *
ssh_endpt_set_hostkey_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_ssh_endpt_set_hostkey("main", TESTS_DIR"/data/key_dsa");
    assert(!ret);

    return NULL;
}

static void *
ssh_endpt_set_banner_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_ssh_endpt_set_banner("main", "Howdy, partner!");
    assert(!ret);

    return NULL;
}

static void *
ssh_endpt_set_auth_methods_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_ssh_endpt_set_auth_methods("main", NC_SSH_AUTH_PUBLICKEY | NC_SSH_AUTH_PASSWORD | NC_SSH_AUTH_INTERACTIVE);
    assert(!ret);

    return NULL;
}

static void *
ssh_endpt_set_auth_attempts_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_ssh_endpt_set_auth_attempts("main", 2);
    assert(!ret);

    return NULL;
}

static void *
ssh_endpt_set_auth_timeout_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_ssh_endpt_set_auth_timeout("main", 5);
    assert(!ret);

    return NULL;
}

static void *
ssh_endpt_add_authkey_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_ssh_endpt_add_authkey("main", TESTS_DIR"/data/key_rsa.pub", "test3");
    assert(!ret);

    return NULL;
}

static void *
ssh_endpt_del_authkey_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_ssh_endpt_del_authkey("main", TESTS_DIR"/data/key_ecdsa.pub", "test2");
    assert(!ret);

    return NULL;
}

static void *
ssh_client_thread(void *arg)
{
    (void)arg;
    int ret;
    long timeout = CLIENT_SSH_AUTH_TIMEOUT;
    uint32_t port;
    ssh_session sshsession;
    ssh_key pubkey, privkey;
    struct nc_session *session;

    /* We cannot use nc_connect_ssh(), because we want to skip the knownhost check.
    ret = nc_client_ssh_set_username("test");
    assert_int_equal(ret, 0);

    ret = nc_client_ssh_add_keypair("data/key_dsa.pub", "data/key_dsa");
    assert_int_equal(ret, 0);

    nc_client_ssh_set_auth_pref(NC_SSH_AUTH_PUBLICKEY, 1);
    nc_client_ssh_set_auth_pref(NC_SSH_AUTH_PASSWORD, -1);
    nc_client_ssh_set_auth_pref(NC_SSH_AUTH_INTERACTIVE, -1);

    session = nc_session_connect("127.0.0.1", NC_PORT_SSH, NULL);*/

    port = 6001;

    sshsession = ssh_new();
    ssh_options_set(sshsession, SSH_OPTIONS_HOST, "127.0.0.1");
    ssh_options_set(sshsession, SSH_OPTIONS_PORT, &port);
    ssh_options_set(sshsession, SSH_OPTIONS_USER, "test");
    ssh_options_set(sshsession, SSH_OPTIONS_TIMEOUT, &timeout);
    ssh_options_set(sshsession, SSH_OPTIONS_HOSTKEYS, "ssh-ed25519,ssh-rsa,ssh-dss,ssh-rsa1");

    ret = ssh_connect(sshsession);
    assert(ret == SSH_OK);

    /* authentication */
    ret = ssh_userauth_none(sshsession, NULL);
    assert(ret == SSH_AUTH_DENIED);
    assert(ssh_userauth_list(sshsession, NULL) & SSH_AUTH_METHOD_PUBLICKEY);
    ret = ssh_pki_import_pubkey_file(TESTS_DIR"/data/key_dsa.pub", &pubkey);
    assert(ret == SSH_OK);
    ret = ssh_userauth_try_publickey(sshsession, NULL, pubkey);
    assert(ret == SSH_AUTH_SUCCESS);
    ret = ssh_pki_import_privkey_file(TESTS_DIR"/data/key_dsa", NULL, NULL, NULL, &privkey);
    assert(ret == SSH_OK);
    ret = ssh_userauth_publickey(sshsession, NULL, privkey);
    assert(ret == SSH_AUTH_SUCCESS);
    ssh_key_free(pubkey);
    ssh_key_free(privkey);

    session = nc_connect_libssh(sshsession, NULL);
    assert(session);

    nc_session_free(session);

    nc_client_ssh_destroy_opts();

    nc_thread_destroy();
    return NULL;
}

pid_t
fork_ssh_client(void)
{
    pid_t client_pid;

    if (!(client_pid = fork())) {
        /* cleanup */
        //nc_server_destroy();
        //ly_ctx_destroy(ctx, NULL);
        pthread_barrier_destroy(&barrier);

        ssh_client_thread(NULL);

        teardown_lib();
        exit(0);
    }

    return client_pid;
}

pthread_t
thread_ssh_client(void)
{
    pthread_t client_tid;

    pthread_create(&client_tid, NULL, ssh_client_thread, NULL);

    return client_tid;
}

#endif /* ENABLE_SSH */

#ifdef ENABLE_TLS

static void *
tls_add_endpt_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_tls_add_endpt_listen("tertiary", "0.0.0.0", 6503);
    assert(!ret);

    return NULL;
}

static void *
tls_endpt_set_port_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_tls_endpt_set_port("quaternary", 6505);
    assert(!ret);

    return NULL;
}

static void *
tls_del_endpt_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_tls_del_endpt("secondary");
    assert(!ret);

    return NULL;
}

static void *
tls_endpt_set_cert_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_tls_endpt_set_cert("quaternary", "MIIEKjCCAxICCQDqSTPpuoUZkzANBgkqhkiG9w0BAQUFADBYMQswCQYDVQQGEwJB\n"
                                       "VTETMBEGA1UECAwKU29tZS1TdGF0ZTEhMB8GA1UECgwYSW50ZXJuZXQgV2lkZ2l0\n"
                                       "cyBQdHkgTHRkMREwDwYDVQQDDAhzZXJ2ZXJjYTAeFw0xNjAyMDgxMTE0MzdaFw0y\n"
                                       "NjAyMDUxMTE0MzdaMFYxCzAJBgNVBAYTAkFVMRMwEQYDVQQIDApTb21lLVN0YXRl\n"
                                       "MSEwHwYDVQQKDBhJbnRlcm5ldCBXaWRnaXRzIFB0eSBMdGQxDzANBgNVBAMMBnNl\n"
                                       "cnZlcjCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAOqI7Y3w5r8kD9WZ\n"
                                       "CMAaa/e3ig7nm76aIJUR0Xb1bk6X/4FNVQKwEJsBodOYupZvE5FZdZ6DJSMSyQ3F\n"
                                       "rJWnlZ+isr7F9B4bELV8Kj6sJGuVAr+mpcH/4rwL3DaXF9Y9Lf7iBgiOHUoip80A\n"
                                       "sn9BU4q80JI6w2VHd5ng4TUE67gmpRleIHzViKt3taBrsAJ9bS5bvaE6xOB8zKYG\n"
                                       "zRFOsDZrEqqcBsVIWC6EmjO29HS5qj/mXM0ktFGnNDxTZHoRkNgmCE/NH+fNKOFx\n"
                                       "raCwlFBpKemAky+GdgngRGiQAVowyAx/nSmCFAalKc+E4ddoFwD/oft6iOvvXqaX\n"
                                       "h6368wEQ7Hy48FDcUCbHtUEgK4wMrX9BSrRh6zkXO1tE4ghb0dM2qFDS0ypO3p04\n"
                                       "kUPa31mTgLuOH1LzwmlwxOs113mlYKCgqOFR5YaN+nq1HI5RATPo5NvCMpG2RrQW\n"
                                       "+ooCr2GtbT0oHmJv8yaBVY0HJ69eLnIv37dfjWvoTiBKBBIisXAD5Nm9rwSjZUSF\n"
                                       "u1iyd7u2YrkBCUzZuvt3BOPpX8GgQgagU6BPnac76FF6DMhRUXlBXdTuWsbuH14L\n"
                                       "dNIzGjkMZhNL/Tpkf6S/z1iH5VReGc+clTjWGg1XO5fr3mNKBGa7hDydIZRIMbgs\n"
                                       "y63DIY7n5dqhNkO30CGmr/9TagVZAgMBAAEwDQYJKoZIhvcNAQEFBQADggEBAEVr\n"
                                       "4skCpwuMuR+3WCmH6S17sYzWMYogJCGQdbZtFqmf4W3EDlNClk4HszAeUdmROMj6\n"
                                       "MdqNDUnDM/GPxHB4Aje1DZOH1h68CCAl9W32LFRDC0KaUOquuYIG4rnZADJl6P4T\n"
                                       "WVlaXfuE2bQjE7iYPhWGNWJtkb7JNIHmB8EAIa4tt3+XJs+vZiSpVDpiP2ucgrCn\n"
                                       "BltsK0iOMPDLVlXdk1hpU5HvlMXdBHQebfTiCFDQSX7ViKc4wSJUHDt4CyoCzchY\n"
                                       "mbQIcTc7uNDE5chQWV8Z3Vxkp4yuqZM3HdLskoo4IgFDOoj8eCAi+58+YRuKpaEQ\n"
                                       "fWt+A9rvlaOApWryMW4=");
    assert(!ret);

    nc_thread_destroy();
    return NULL;
}

static void *
tls_endpt_set_key_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_tls_endpt_set_key("quaternary", "MIIJKAIBAAKCAgEA6ojtjfDmvyQP1ZkIwBpr97eKDuebvpoglRHRdvVuTpf/gU1V\n"
                                      "ArAQmwGh05i6lm8TkVl1noMlIxLJDcWslaeVn6KyvsX0HhsQtXwqPqwka5UCv6al\n"
                                      "wf/ivAvcNpcX1j0t/uIGCI4dSiKnzQCyf0FTirzQkjrDZUd3meDhNQTruCalGV4g\n"
                                      "fNWIq3e1oGuwAn1tLlu9oTrE4HzMpgbNEU6wNmsSqpwGxUhYLoSaM7b0dLmqP+Zc\n"
                                      "zSS0Uac0PFNkehGQ2CYIT80f580o4XGtoLCUUGkp6YCTL4Z2CeBEaJABWjDIDH+d\n"
                                      "KYIUBqUpz4Th12gXAP+h+3qI6+9eppeHrfrzARDsfLjwUNxQJse1QSArjAytf0FK\n"
                                      "tGHrORc7W0TiCFvR0zaoUNLTKk7enTiRQ9rfWZOAu44fUvPCaXDE6zXXeaVgoKCo\n"
                                      "4VHlho36erUcjlEBM+jk28IykbZGtBb6igKvYa1tPSgeYm/zJoFVjQcnr14uci/f\n"
                                      "t1+Na+hOIEoEEiKxcAPk2b2vBKNlRIW7WLJ3u7ZiuQEJTNm6+3cE4+lfwaBCBqBT\n"
                                      "oE+dpzvoUXoMyFFReUFd1O5axu4fXgt00jMaOQxmE0v9OmR/pL/PWIflVF4Zz5yV\n"
                                      "ONYaDVc7l+veY0oEZruEPJ0hlEgxuCzLrcMhjufl2qE2Q7fQIaav/1NqBVkCAwEA\n"
                                      "AQKCAgAeRZw75Oszoqj0jfMmMILdD3Cfad+dY3FvLESYESeyt0XAX8XoOed6ymQj\n"
                                      "1qPGxQGGkkBvPEgv1b3jrC8Rhfb3Ct39Z7mRpTar5iHhwwBUboBTUmQ0vR173iAH\n"
                                      "X8sw2Oa17mCO/CDlr8Fu4Xcom7r3vlVBepo72VSjpPYMjN0MANjwhEi3NCyWzTXB\n"
                                      "RgUK3TuZbzfzto0w2Irlpx0S7dAqxfk70jXBgwv2vSDWKfg1lL1X0BkMVX98xpMk\n"
                                      "cjMW2muSqp4KBtTma4GqT6z0f7Y1Bs3lGLZmvPlBXxQVVvkFtiQsENCtSd/h17Gk\n"
                                      "2mb4EbReaaBzwCYqJdRWtlpJ54kzy8U00co+Yn//ZS7sbbIDkqHPnXkpdIr+0rED\n"
                                      "MlOw2Y3vRZCxqZFqfWCW0uzhwKqk2VoYqtDL+ORKG/aG/KTBQ4Y71Uh+7aabPwj5\n"
                                      "R+NaVMjbqmrVeH70eKjoNVgcNYY1C9rGVF1d+LQEm7UsqS0DPp4wN9QKLAqIfuar\n"
                                      "AhQBhZy1R7Sj1r5macD9DsGxsurM4mHZV0LNmYLZiFHjTUb6iRSPD5RBFW80vcNt\n"
                                      "xZ0cxmkLtxrj/DVyExV11Cl0SbZLLa9mScYvxdl/qZutXt3PQyab0NiYxGzCD2Rn\n"
                                      "LkCyxkh1vuHHjhvIWYfbd2VgZB/qGr+o9T07FGfMCu23//fugQKCAQEA9UH38glH\n"
                                      "/rAjZ431sv6ryUEFY8I2FyLTijtvoj9CNGcQn8vJQAHvUPfMdyqDoum6wgcTmG+U\n"
                                      "XA6mZzpGQCiY8JW5CoItgXRoYgNzpvVVe2aLf51QGtNLLEFpNDMpCtI+I+COpAmG\n"
                                      "vWAukku0pZfRjm9eb1ydvTpHlFC9+VhVUsLzw3VtSC5PVW6r65mZcYcB6SFVPap+\n"
                                      "31ENP/9jOMFoymh57lSMZJMxTEA5b0l2miFb9Rp906Zqiud5zv2jIqF6gL70giW3\n"
                                      "ovVxR7LGKKTKIa9pxawHwB6Ithygs7YoJkjF2dm8pZTMZKsQN92K70XGj07SmYRL\n"
                                      "ZpkVD7i+cqbbKQKCAQEA9M6580Rcw6W0twfcy0/iB4U5ZS52EcCjW8vHlL+MpUo7\n"
                                      "YvXadSgV1ZaM28zW/ZGk3wE0zy1YT5s30SQkm0NiWN3t/J0l19ccAOxlPWfjhF7v\n"
                                      "IQZr7XMo5HeaK0Ak5+68J6bx6KgcXmlJOup7INaE8DyGXB6vd4K6957IXyqs3/bf\n"
                                      "JAUmz49hnveCfLFdTVVT/Uq4IoPKfQSbSZc0BvPBsnBCF164l4jllGBaWS302dhg\n"
                                      "W4cgxzG0SZGgNwow4AhB+ygiiS8yvOa7UcHfUObVrzWeeq9mYSQ1PkvUTjkWR2/Y\n"
                                      "8xy7WP0TRBdJOVSs90H51lerEDGNQWvQvI97S9ZOsQKCAQB59u9lpuXtqwxAQCFy\n"
                                      "fSFSuQoEHR2nDcOjF4GhbtHum15yCPaw5QVs/33nuPWze4ZLXReKk9p0mTh5V0p+\n"
                                      "N3IvGlXl+uzEVu5d55eI7LIw5sLymHmwjWjxvimiMtrzLbCHSPHGc5JU9NLUH9/b\n"
                                      "BY/JxGpy+NzcsHHOOQTwTdRIjviIOAo7fgQn2RyX0k+zXE8/7zqjqvji9zyemdNu\n"
                                      "8we4uJICSntyvJwkbj/hrufTKEnBrwXpzfVn1EsH+6w32ZPBGLUhT75txJ8r56SR\n"
                                      "q7l1XPU9vxovmT+lSMFF/Y0j1MbHWnds5H1shoFPNtYTvWBL/gfPHjIc+H23zsiu\n"
                                      "3XlZAoIBAC2xB/Pnpoi9vOUMiqFH36AXtYa1DURy+AqCFlYlClMvb7YgvQ1w1eJv\n"
                                      "nwrHSLk7HdKhnwGsLPduuRRH8q0n/osnoOutSQroE0n41UyIv2ZNccRwNmSzQcai\n"
                                      "rBu2dSz02hlsh2otNl5IuGpOqXyPjXBpW4qGD6n2tH7THALnLC0BHtTSQVQsJsRM\n"
                                      "3gX39LoiWvLDp2qJvplm6rTpi8Rgap6rZSqHe1yNKIxxD2vlr/WY9SMgLXYASO4S\n"
                                      "SBz9wfGOmQIPk6KXNJkdV4kC7nNjIi75iwLLCgjHgUiHTrDq5sWekpeNnUoWsinb\n"
                                      "Tsdsjnv3zHG9GyiClyLGxMbs4M5eyYECggEBAKuC8ZMpdIrjk6tERYB6g0LnQ7mW\n"
                                      "8XYbDFAmLYMLs9yfG2jcjVbsW9Kugsr+3poUUv/q+hNO3jfY4HazhZDa0MalgNPo\n"
                                      "Swr/VNRnkck40x2ovFb989J7yl++zTrnIrax9XRH1V0cNu+Kj7OMwZ2RRfbNv5JB\n"
                                      "dOZPvkfqyIKFmbQgYbtD66rHuzNOfJpzqr/WVLO57/zzW8245NKG2B6B0oXkei/K\n"
                                      "qDY0DAbHR3i3EOj1NPtVI1FC/xX8R9BREaid458bqoHJKuInrGcBjaUI9Cvymv8T\n"
                                      "bstUgD6NPbJR4Sm6vrLeUqzjWZP3t1+Z6DjXmnpR2vvhMU/FWb//21p/88o=", 1);
    assert(!ret);

    nc_thread_destroy();
    return NULL;
}

static void *
tls_endpt_add_trusted_cert_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_tls_endpt_add_trusted_cert("quaternary", "MIIDgzCCAmugAwIBAgIJAL+y0WMRGax0MA0GCSqGSIb3DQEBBQUAMFgxCzAJBgNV\n"
                                               "BAYTAkFVMRMwEQYDVQQIDApTb21lLVN0YXRlMSEwHwYDVQQKDBhJbnRlcm5ldCBX\n"
                                               "aWRnaXRzIFB0eSBMdGQxETAPBgNVBAMMCGNsaWVudGNhMB4XDTE2MDExMTEyMTAx\n"
                                               "OVoXDTE4MTAzMTEyMTAxOVowWDELMAkGA1UEBhMCQVUxEzARBgNVBAgMClNvbWUt\n"
                                               "U3RhdGUxITAfBgNVBAoMGEludGVybmV0IFdpZGdpdHMgUHR5IEx0ZDERMA8GA1UE\n"
                                               "AwwIY2xpZW50Y2EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCw7Eyq\n"
                                               "5T5tX6tAv5DHHfWNuaD/a3gVIBlGRWMAXkFWWJEa3o6leIjKxoDnL6tcBWNVJ+Gw\n"
                                               "32MHerpHY6o5czsEHQ2XsOgodyFqe5cvx0kjQbjYQqnIMrslcdvSYuNe/ItqFP/w\n"
                                               "uxb6kQbCYnCQKd/qhdhfoXjIHcnXpZzMCPKQ/uqls7LANJymtQkAuzydlf3+UqoG\n"
                                               "4oo04GXK1Dc0A12cgCXxf+kWx7x34ctx2VEvDsJzw6LiZm8czOWjMFcuqqm/+kla\n"
                                               "N3+6O7Z1kZlft/KNSrOYtc45xKNoSVrdVwFLkxipVDfOql6/DmWfE8iVmlX3QflO\n"
                                               "u3+fzZZQpR5jYzUNAgMBAAGjUDBOMB0GA1UdDgQWBBTjBbQJ6p/mjnjBWXLgXXXW\n"
                                               "a3ieoTAfBgNVHSMEGDAWgBTjBbQJ6p/mjnjBWXLgXXXWa3ieoTAMBgNVHRMEBTAD\n"
                                               "AQH/MA0GCSqGSIb3DQEBBQUAA4IBAQAZr9b0YTaDV5XZr/QQPP1pvHkN3Ezbm9F4\n"
                                               "MiYe4e0QnM9JtjNLDKq1dDnqVDQ/BYdupWWh0398tObFACssWkm4aubPG7LVh5Ck\n"
                                               "O8I8i/GHiXYLmYT22hslWe5dFvidUICkTXoj1h5X2vwfBrNTI1+gnVXXw842xCvU\n"
                                               "sgq28vGMSXLSYKBNaP/llXNmqW35oLs6CwVuiCL7Go0IDIOmiXN2bssb87hZSw3B\n"
                                               "6iwU78wYshJUGZjLaK9PuMvFYJLFWSAePA2Yb+aEv80wMbX1oANSryU7Uf5BJk8V\n"
                                               "kO3mlRDh2b1/5Gb5xA2vU2z3ReHdPNy6qSx0Mk4XJvQw9FsVHZ13");
    assert(!ret);

    nc_thread_destroy();
    return NULL;
}

static void *
tls_endpt_set_trusted_ca_paths_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_tls_endpt_set_trusted_ca_paths("quaternary", TESTS_DIR"/data/serverca.pem", "data");
    assert(!ret);

    nc_thread_destroy();
    return NULL;
}

static void *
tls_endpt_clear_certs_thread(void *arg)
{
    (void)arg;

    pthread_barrier_wait(&barrier);

    nc_server_tls_endpt_clear_certs("quaternary");

    return NULL;
}

static void *
tls_endpt_set_crl_paths_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_tls_endpt_set_crl_paths("quaternary", NULL, "data");
    assert(!ret);

    nc_thread_destroy();
    return NULL;
}

static void *
tls_endpt_clear_crls_thread(void *arg)
{
    (void)arg;

    pthread_barrier_wait(&barrier);

    nc_server_tls_endpt_clear_crls("quaternary");

    return NULL;
}

static void *
tls_endpt_add_ctn_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_tls_endpt_add_ctn("main", 0, "02:F0:F1:F2:F3:F4:F5:F6:F7:F8:F9:10:11:12:EE:FF:A0:A1:A2:A3",
                                      NC_TLS_CTN_SAN_IP_ADDRESS, NULL);
    assert(!ret);

    return NULL;
}

static void *
tls_endpt_del_ctn_thread(void *arg)
{
    (void)arg;
    int ret;

    pthread_barrier_wait(&barrier);

    ret = nc_server_tls_endpt_del_ctn("main", -1, NULL, NC_TLS_CTN_SAN_ANY, NULL);
    assert(!ret);

    return NULL;
}

static void *
tls_client_thread(void *arg)
{
    (void)arg;
    int ret;
    struct nc_session *session;

    ret = nc_client_tls_set_cert_key_paths(TESTS_DIR"/data/client.crt", TESTS_DIR"/data/client.key");
    assert(!ret);
    ret = nc_client_tls_set_trusted_ca_paths(NULL, "data");
    assert(!ret);

    session = nc_connect_tls("127.0.0.1", 6501, NULL);
    assert(session);

    nc_session_free(session);

    nc_client_tls_destroy_opts();

    nc_thread_destroy();
    return NULL;
}

pid_t
fork_tls_client(void)
{
    pid_t client_pid;

    if (!(client_pid = fork())) {
        /* cleanup */
        //nc_server_destroy();
        //ly_ctx_destroy(ctx, NULL);
        pthread_barrier_destroy(&barrier);

        tls_client_thread(NULL);

        teardown_lib();
        exit(0);
    }

    return client_pid;
}

pthread_t
thread_tls_client(void)
{
    pthread_t client_tid;

    pthread_create(&client_tid, NULL, tls_client_thread, NULL);

    return client_tid;
}

#endif /* ENABLE_TLS */

static void *(*thread_funcs[])(void *) = {
#if defined(ENABLE_SSH) || defined(ENABLE_TLS)
    server_thread,
#endif
#ifdef ENABLE_SSH
    ssh_add_endpt_thread,
    ssh_endpt_set_port_thread,
    ssh_del_endpt_thread,
    ssh_endpt_set_hostkey_thread,
    ssh_endpt_set_banner_thread,
    ssh_endpt_set_auth_methods_thread,
    ssh_endpt_set_auth_attempts_thread,
    ssh_endpt_set_auth_timeout_thread,
    ssh_endpt_add_authkey_thread,
    ssh_endpt_del_authkey_thread,
#endif
#ifdef ENABLE_TLS
    tls_add_endpt_thread,
    tls_endpt_set_port_thread,
    tls_del_endpt_thread,
    tls_endpt_set_cert_thread,
    tls_endpt_set_key_thread,
    tls_endpt_add_trusted_cert_thread,
    tls_endpt_set_trusted_ca_paths_thread,
    tls_endpt_clear_certs_thread,
    tls_endpt_set_crl_paths_thread,
    tls_endpt_clear_crls_thread,
    tls_endpt_add_ctn_thread,
    tls_endpt_del_ctn_thread,
#endif
};

const int thread_count = sizeof thread_funcs / sizeof *thread_funcs;

static void
clients_start_cleanup(void)
{
    //static pid_t pids[2] = {0, 0};
    static pthread_t tids[2] = {0, 0};

#if defined(ENABLE_SSH) && defined(ENABLE_TLS)
    /*if (pids[0] && pids[1]) {
        waitpid(pids[0], NULL, 0);
        waitpid(pids[1], NULL, 0);
        return;
    }
    pids[0] = fork_ssh_client();
    pids[1] = fork_tls_client();*/

    if (tids[0] && tids[1]) {
        pthread_join(tids[0], NULL);
        pthread_join(tids[1], NULL);
        return;
    }
    tids[0] = thread_ssh_client();
    tids[1] = thread_tls_client();
#elif defined(ENABLE_SSH)
    /*if (pids[0]) {
        waitpid(pids[0], NULL, 0);
        return;
    }
    pids[0] = fork_ssh_client();*/

    if (tids[0]) {
        pthread_join(tids[0], NULL);
        return;
    }
    tids[0] = thread_ssh_client();
#elif defined(ENABLE_TLS)
    /*if (pids[1]) {
        waitpid(pids[1], NULL, 0);
        return;
    }
    pids[1] = fork_tls_client();*/

    if (tids[1]) {
        pthread_join(tids[1], NULL);
        return;
    }
    tids[1] = thread_tls_client();
#else
    if (!tids[0] && !tids[1]) {
        return;
    }
#endif
}

int
main(void)
{
    struct ly_ctx *ctx;
    int ret, i;
    pthread_t tids[thread_count];

    setup_lib();

    ctx = ly_ctx_new(TESTS_DIR"/../schemas");
    assert(ctx);
    ly_ctx_load_module(ctx, "ietf-netconf", NULL);
    nc_server_init(ctx);

    pthread_barrier_init(&barrier, NULL, thread_count);

#ifdef ENABLE_SSH
    /* do first, so that client can connect on SSH */
    ret = nc_server_ssh_add_endpt_listen("main", "0.0.0.0", 6001);
    assert(!ret);
    ret = nc_server_ssh_endpt_add_authkey("main", TESTS_DIR"/data/key_dsa.pub", "test");
    assert(!ret);
    ret = nc_server_ssh_endpt_set_hostkey("main", TESTS_DIR"/data/key_rsa");
    assert(!ret);

    /* for ssh_endpt_del_authkey */
    ret = nc_server_ssh_endpt_add_authkey("main", TESTS_DIR"/data/key_ecdsa.pub", "test2");
    assert(!ret);

    /* for ssh_del_endpt */
    ret = nc_server_ssh_add_endpt_listen("secondary", "0.0.0.0", 6002);
    assert(!ret);

    /* for ssh_endpt_set_port */
    ret = nc_server_ssh_add_endpt_listen("quaternary", "0.0.0.0", 6004);
    assert(!ret);
#endif

#ifdef ENABLE_TLS
    /* do first, so that client can connect on TLS */
    ret = nc_server_tls_add_endpt_listen("main", "0.0.0.0", 6501);
    assert(!ret);
    ret = nc_server_tls_endpt_set_cert_path("main", TESTS_DIR"/data/server.crt");
    assert(!ret);
    ret = nc_server_tls_endpt_set_key_path("main", TESTS_DIR"/data/server.key");
    assert(!ret);
    ret = nc_server_tls_endpt_add_trusted_cert_path("main", TESTS_DIR"/data/client.crt");
    assert(!ret);
    ret = nc_server_tls_endpt_add_ctn("main", 0, "02:D3:03:0E:77:21:E2:14:1F:E5:75:48:98:6B:FD:8A:63:BB:DE:40:34", NC_TLS_CTN_SPECIFIED, "test");
    assert(!ret);

    /* for tls_del_endpt */
    ret = nc_server_tls_add_endpt_listen("secondary", "0.0.0.0", 6502);
    assert(!ret);

    /* for tls_endpt_set_port */
    ret = nc_server_tls_add_endpt_listen("quaternary", "0.0.0.0", 6504);
    assert(!ret);

    /* for tls_endpt_del_ctn */
    ret = nc_server_tls_endpt_add_ctn("main", 0, "02:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:A0:A1:A2:A3", NC_TLS_CTN_SAN_ANY, NULL);
    assert(!ret);
#endif

    ret = nc_client_schema_searchpath(TESTS_DIR"/../schemas");
    assert(!ret);
    clients_start_cleanup();

    /* threads'n'stuff */
    ret = 0;
    for (i = 0; i < thread_count; ++i) {
        ret += pthread_create(&tids[i], NULL, thread_funcs[i], NULL);
    }
    assert(!ret);

    /* cleanup */
    for (i = 0; i < thread_count; ++i) {
        pthread_join(tids[i], NULL);
    }

    clients_start_cleanup();

    pthread_barrier_destroy(&barrier);

    nc_client_schema_searchpath(NULL);
    nc_server_destroy();
    ly_ctx_destroy(ctx, NULL);

    teardown_lib();

    return 0;
}
