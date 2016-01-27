/**
 * \file session_client_ssh.c
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \author Michal Vasko <mvasko@cesnet.cz>
 * \brief libnetconf2 - SSH specific client session transport functions
 *
 * This source is compiled only with libssh.
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

#define _GNU_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>

#ifdef ENABLE_DNSSEC
#   include <validator/validator.h>
#   include <validator/resolver.h>
#   include <validator/validator-compat.h>
#endif

#include <libssh/libssh.h>
#include <libyang/libyang.h>

#include "libnetconf.h"

static struct nc_client_ssh_opts ssh_opts = {
    .auth_pref = {{NC_SSH_AUTH_INTERACTIVE, 3}, {NC_SSH_AUTH_PASSWORD, 2}, {NC_SSH_AUTH_PUBLICKEY, 1}}
};

static struct nc_client_ssh_opts ssh_ch_opts = {
    .auth_pref = {{NC_SSH_AUTH_INTERACTIVE, 1}, {NC_SSH_AUTH_PASSWORD, 2}, {NC_SSH_AUTH_PUBLICKEY, 3}}
};

API void
nc_client_ssh_destroy(void)
{
    int i;

    for (i = 0; i < ssh_opts.key_count; ++i) {
        free(ssh_opts.keys[i].pubkey_path);
        free(ssh_opts.keys[i].privkey_path);
    }

    free(ssh_opts.keys);
    ssh_opts.keys = NULL;
    ssh_opts.key_count = 0;
}

static char *
sshauth_password(const char *username, const char *hostname)
{
    char *buf, *newbuf;
    int buflen = 1024, len = 0;
    char c = 0;
    struct termios newterm, oldterm;
    FILE *tty;

    if (!(tty = fopen("/dev/tty", "r+"))) {
        ERR("Unable to open the current terminal (%s).", strerror(errno));
        return NULL;
    }

    if (tcgetattr(fileno(tty), &oldterm)) {
        ERR("Unable to get terminal settings (%s).", strerror(errno));
        fclose(tty);
        return NULL;
    }

    fprintf(tty, "%s@%s password: ", username, hostname);
    fflush(tty);

    /* system("stty -echo"); */
    newterm = oldterm;
    newterm.c_lflag &= ~ECHO;
    newterm.c_lflag &= ~ICANON;
    tcflush(fileno(tty), TCIFLUSH);
    if (tcsetattr(fileno(tty), TCSANOW, &newterm)) {
        ERR("Unable to change terminal settings for hiding password (%s).", strerror(errno));
        fclose(tty);
        return NULL;
    }

    buf = malloc(buflen * sizeof *buf);
    if (!buf) {
        ERRMEM;
        fclose(tty);
        return NULL;
    }

    while ((fread(&c, 1, 1, tty) == 1) && (c != '\n')) {
        if (len >= buflen - 1) {
            buflen *= 2;
            newbuf = realloc(buf, buflen * sizeof *newbuf);
            if (!newbuf) {
                ERRMEM;

                /* remove content of the buffer */
                memset(buf, 0, len);
                free(buf);

                /* restore terminal settings */
                if (tcsetattr(fileno(tty), TCSANOW, &oldterm) != 0) {
                    ERR("Unable to restore terminal settings (%s).", strerror(errno));
                }
                fclose(tty);
                return NULL;
            } else {
                buf = newbuf;
            }
        }
        buf[len++] = c;
    }
    buf[len++] = 0; /* terminating null byte */

    /* system ("stty echo"); */
    if (tcsetattr(fileno(tty), TCSANOW, &oldterm)) {
        ERR("Unable to restore terminal settings (%s).", strerror(errno));
        /*
         * terminal probably still hides input characters, but we have password
         * and anyway we are unable to set terminal to the previous state, so
         * just continue
         */
    }
    fprintf(tty, "\n");

    fclose(tty);
    return buf;
}

static char *
sshauth_interactive(const char *auth_name, const char *instruction, const char *prompt, int echo)
{
    unsigned int buflen = 8, response_len;
    char c = 0;
    struct termios newterm, oldterm;
    char *newtext, *response;
    FILE *tty;

    if (!(tty = fopen("/dev/tty", "r+"))) {
        ERR("Unable to open the current terminal (%s).", strerror(errno));
        return NULL;
    }

    if (tcgetattr(fileno(tty), &oldterm) != 0) {
        ERR("Unable to get terminal settings (%s).", strerror(errno));
        fclose(tty);
        return NULL;
    }

    if (auth_name && (!fwrite(auth_name, sizeof(char), strlen(auth_name), tty)
            || !fwrite("\n", sizeof(char), 1, tty))) {
        ERR("Writing the auth method name into stdout failed.");
        fclose(tty);
        return NULL;
    }

    if (instruction && (!fwrite(instruction, sizeof(char), strlen(instruction), tty)
            || !fwrite("\n", sizeof(char), 1, tty))) {
        ERR("Writing the instruction into stdout failed.");
        fclose(tty);
        return NULL;
    }

    if (!fwrite(prompt, sizeof(char), strlen(prompt), tty)) {
        ERR("Writing the authentication prompt into stdout failed.");
        fclose(tty);
        return NULL;
    }
    fflush(tty);
    if (!echo) {
        /* system("stty -echo"); */
        newterm = oldterm;
        newterm.c_lflag &= ~ECHO;
        tcflush(fileno(tty), TCIFLUSH);
        if (tcsetattr(fileno(tty), TCSANOW, &newterm)) {
            ERR("Unable to change terminal settings for hiding password (%s).", strerror(errno));
            fclose(tty);
            return NULL;
        }
    }

    response = malloc(buflen * sizeof *response);
    response_len = 0;
    if (!response) {
        ERRMEM;
        /* restore terminal settings */
        if (tcsetattr(fileno(tty), TCSANOW, &oldterm)) {
            ERR("Unable to restore terminal settings (%s).", strerror(errno));
        }
        fclose(tty);
        return NULL;
    }

    while ((fread(&c, 1, 1, tty) == 1) && (c != '\n')) {
        if (response_len >= buflen - 1) {
            buflen *= 2;
            newtext = realloc(response, buflen * sizeof *newtext);
            if (!newtext) {
                ERRMEM;
                free(response);

                /* restore terminal settings */
                if (tcsetattr(fileno(tty), TCSANOW, &oldterm)) {
                    ERR("Unable to restore terminal settings (%s).", strerror(errno));
                }
                fclose(tty);
                return NULL;
            } else {
                response = newtext;
            }
        }
        response[response_len++] = c;
    }
    /* terminating null byte */
    response[response_len++] = '\0';

    /* system ("stty echo"); */
    if (tcsetattr(fileno(tty), TCSANOW, &oldterm)) {
        ERR("Unable to restore terminal settings (%s).", strerror(errno));
        /*
         * terminal probably still hides input characters, but we have password
         * and anyway we are unable to set terminal to the previous state, so
         * just continue
         */
    }

    fprintf(tty, "\n");
    fclose(tty);
    return response;
}

static char *
sshauth_passphrase(const char* privkey_path)
{
    char c, *buf, *newbuf;
    int buflen = 1024, len = 0;
    struct termios newterm, oldterm;
    FILE *tty;

    buf = malloc(buflen * sizeof *buf);
    if (!buf) {
        ERRMEM;
        return NULL;
    }

    if (!(tty = fopen("/dev/tty", "r+"))) {
        ERR("Unable to open the current terminal (%s).", strerror(errno));
        goto fail;
    }

    if (tcgetattr(fileno(tty), &oldterm)) {
        ERR("Unable to get terminal settings (%s).", strerror(errno));
        goto fail;
    }

    fprintf(tty, "Enter passphrase for the key '%s':", privkey_path);
    fflush(tty);

    /* system("stty -echo"); */
    newterm = oldterm;
    newterm.c_lflag &= ~ECHO;
    newterm.c_lflag &= ~ICANON;
    tcflush(fileno(tty), TCIFLUSH);
    if (tcsetattr(fileno(tty), TCSANOW, &newterm)) {
        ERR("Unable to change terminal settings for hiding password (%s).", strerror(errno));
        goto fail;
    }

    while ((fread(&c, 1, 1, tty) == 1) && (c != '\n')) {
        if (len >= buflen - 1) {
            buflen *= 2;
            newbuf = realloc(buf, buflen * sizeof *newbuf);
            if (!newbuf) {
                ERRMEM;
                /* restore terminal settings */
                if (tcsetattr(fileno(tty), TCSANOW, &oldterm)) {
                    ERR("Unable to restore terminal settings (%s).", strerror(errno));
                }
                goto fail;
            }
            buf = newbuf;
        }
        buf[len++] = (char)c;
    }
    buf[len++] = 0; /* terminating null byte */

    /* system ("stty echo"); */
    if (tcsetattr(fileno(tty), TCSANOW, &oldterm)) {
        ERR("Unable to restore terminal settings (%s).", strerror(errno));
        /*
         * terminal probably still hides input characters, but we have password
         * and anyway we are unable to set terminal to the previous state, so
         * just continue
         */
    }
    fprintf(tty, "\n");

    fclose(tty);
    return buf;

fail:
    free(buf);
    if (tty) {
        fclose(tty);
    }
    return NULL;
}

/* TODO define this switch */
#ifdef ENABLE_DNSSEC

/* return 0 (DNSSEC + key valid), 1 (unsecure DNS + key valid), 2 (key not found or an error) */
/* type - 1 (RSA), 2 (DSA), 3 (ECDSA); alg - 1 (SHA1), 2 (SHA-256) */
static int
sshauth_hostkey_hash_dnssec_check(const char *hostname, const char *sha1hash, int type, int alg) {
    ns_msg handle;
    ns_rr rr;
    val_status_t val_status;
    const unsigned char* rdata;
    unsigned char buf[4096];
    int buf_len = 4096;
    int ret = 0, i, j, len;

    /* class 1 - internet, type 44 - SSHFP */
    len = val_res_query(NULL, hostname, 1, 44, buf, buf_len, &val_status);

    if ((len < 0) || !val_istrusted(val_status)) {
        ret = 2;
        goto finish;
    }

    if (ns_initparse(buf, len, &handle) < 0) {
        ERR("Failed to initialize DNSSEC response parser.");
        ret = 2;
        goto finish;
    }

    if ((i = libsres_msg_getflag(handle, ns_f_rcode))) {
        ERR("DNSSEC query returned %d.", i);
        ret = 2;
        goto finish;
    }

    if (!libsres_msg_getflag(handle, ns_f_ad)) {
        /* response not secured by DNSSEC */
        ret = 1;
    }

    /* query section */
    if (ns_parserr(&handle, ns_s_qd, 0, &rr)) {
        ERROR("DNSSEC query section parser fail.");
        ret = 2;
        goto finish;
    }

    if (strcmp(hostname, ns_rr_name(rr)) || (ns_rr_type(rr) != 44) || (ns_rr_class(rr) != 1)) {
        ERROR("DNSSEC query in the answer does not match the original query.");
        ret = 2;
        goto finish;
    }

    /* answer section */
    i = 0;
    while (!ns_parserr(&handle, ns_s_an, i, &rr)) {
        if (ns_rr_type(rr) != 44) {
            ++i;
            continue;
        }

        rdata = ns_rr_rdata(rr);
        if (rdata[0] != type) {
            ++i;
            continue;
        }
        if (rdata[1] != alg) {
            ++i;
            continue;
        }

        /* we found the correct SSHFP entry */
        rdata += 2;
        for (j = 0; j < 20; ++j) {
            if (rdata[j] != (unsigned char)sha1hash[j]) {
                ret = 2;
                goto finish;
            }
        }

        /* server fingerprint is supported by a DNS entry,
        * we have already determined if DNSSEC was used or not
        */
        goto finish;
    }

    /* no match */
    ret = 2;

finish:
    val_free_validator_state();
    return ret;
}

#endif /* ENABLE_DNSSEC */

static int
sshauth_hostkey_check(const char *hostname, ssh_session session)
{
    char *hexa;
    int c, state, ret;
    ssh_key srv_pubkey;
    unsigned char *hash_sha1 = NULL;
    size_t hlen;
    enum ssh_keytypes_e srv_pubkey_type;
    char answer[5];

    state = ssh_is_server_known(session);

    ret = ssh_get_publickey(session, &srv_pubkey);
    if (ret < 0) {
        ERR("Unable to get server public key.");
        return -1;
    }

    srv_pubkey_type = ssh_key_type(srv_pubkey);
    ret = ssh_get_publickey_hash(srv_pubkey, SSH_PUBLICKEY_HASH_SHA1, &hash_sha1, &hlen);
    ssh_key_free(srv_pubkey);
    if (ret < 0) {
        ERR("Failed to calculate SHA1 hash of the server public key.");
        return -1;
    }

    hexa = ssh_get_hexa(hash_sha1, hlen);

    switch (state) {
    case SSH_SERVER_KNOWN_OK:
        break; /* ok */

    case SSH_SERVER_KNOWN_CHANGED:
        ERR("Remote host key changed, the connection will be terminated!");
        goto fail;

    case SSH_SERVER_FOUND_OTHER:
        WRN("Remote host key is not known, but a key of another type for this host is known. Continue with caution.");
        goto hostkey_not_known;

    case SSH_SERVER_FILE_NOT_FOUND:
        WRN("Could not find the known hosts file.");
        goto hostkey_not_known;

    case SSH_SERVER_NOT_KNOWN:
hostkey_not_known:
#ifdef ENABLE_DNSSEC
        if ((srv_pubkey_type != SSH_KEYTYPE_UNKNOWN) || (srv_pubkey_type != SSH_KEYTYPE_RSA1)) {
            if (srv_pubkey_type == SSH_KEYTYPE_DSS) {
                ret = callback_ssh_hostkey_hash_dnssec_check(hostname, hash_sha1, 2, 1);
            } else if (srv_pubkey_type == SSH_KEYTYPE_RSA) {
                ret = callback_ssh_hostkey_hash_dnssec_check(hostname, hash_sha1, 1, 1);
            } else if (srv_pubkey_type == SSH_KEYTYPE_ECDSA) {
                ret = callback_ssh_hostkey_hash_dnssec_check(hostname, hash_sha1, 3, 1);
            }

            /* DNSSEC SSHFP check successful, that's enough */
            if (!ret) {
                DBG("DNSSEC SSHFP check successful");
                ssh_write_knownhost(session);
                ssh_clean_pubkey_hash(&hash_sha1);
                ssh_string_free_char(hexa);
                return 0;
            }
        }
#endif

        /* try to get result from user */
        fprintf(stdout, "The authenticity of the host \'%s\' cannot be established.\n", hostname);
        fprintf(stdout, "%s key fingerprint is %s.\n", ssh_key_type_to_char(srv_pubkey_type), hexa);

#ifdef ENABLE_DNSSEC
        if (ret == 2) {
            fprintf(stdout, "No matching host key fingerprint found in DNS.\n");
        } else if (ret == 1) {
            fprintf(stdout, "Matching host key fingerprint found in DNS.\n");
        }
#endif

        fprintf(stdout, "Are you sure you want to continue connecting (yes/no)? ");

        do {
            if (fscanf(stdin, "%4s", answer) == EOF) {
                ERR("fscanf() failed (%s).", strerror(errno));
                goto fail;
            }
            while (((c = getchar()) != EOF) && (c != '\n'));

            fflush(stdin);
            if (!strcmp("yes", answer)) {
                /* store the key into the host file */
                ret = ssh_write_knownhost(session);
                if (ret < 0) {
                    WRN("Adding the known host %s failed (%s).", hostname, ssh_get_error(session));
                }
            } else if (!strcmp("no", answer)) {
                goto fail;
            } else {
                fprintf(stdout, "Please type 'yes' or 'no': ");
            }
        } while (strcmp(answer, "yes") && strcmp(answer, "no"));

        break;

    case SSH_SERVER_ERROR:
        ssh_clean_pubkey_hash(&hash_sha1);
        fprintf(stderr,"%s",ssh_get_error(session));
        return -1;
    }

    ssh_clean_pubkey_hash(&hash_sha1);
    ssh_string_free_char(hexa);
    return 0;

fail:
    ssh_clean_pubkey_hash(&hash_sha1);
    ssh_string_free_char(hexa);
    return -1;
}

static int
_nc_client_ssh_add_keypair(const char *pub_key, const char *priv_key, struct nc_client_ssh_opts *opts)
{
    int i;
    FILE *key;
    char line[128];

    if (!pub_key || !priv_key) {
        ERRARG;
        return -1;
    }

    for (i = 0; i < opts->key_count; ++i) {
        if (!strcmp(opts->keys[i].pubkey_path, pub_key) || !strcmp(opts->keys[i].privkey_path, priv_key)) {
            if (strcmp(opts->keys[i].pubkey_path, pub_key)) {
                WRN("Private key \"%s\" found with another public key \"%s\".",
                    priv_key, opts->keys[i].pubkey_path);
                continue;
            } else if (strcmp(opts->keys[i].privkey_path, priv_key)) {
                WRN("Public key \"%s\" found with another private key \"%s\".",
                    pub_key, opts->keys[i].privkey_path);
                continue;
            }

            ERR("SSH key pair already set.");
            return -1;
        }
    }

    /* add the keys */
    ++opts->key_count;
    opts->keys = realloc(opts->keys, opts->key_count * sizeof *opts->keys);
    opts->keys[opts->key_count - 1].pubkey_path = strdup(pub_key);
    opts->keys[opts->key_count - 1].privkey_path = strdup(priv_key);
    opts->keys[opts->key_count - 1].privkey_crypt = 0;

    /* check encryption */
    if ((key = fopen(priv_key, "r"))) {
        /* 1st line - key type */
        if (!fgets(line, sizeof line, key)) {
            fclose(key);
            ERR("fgets() on %s failed.", priv_key);
            return -1;
        }
        /* 2nd line - encryption information or key */
        if (!fgets(line, sizeof line, key)) {
            fclose(key);
            ERR("fgets() on %s failed.", priv_key);
            return -1;
        }
        fclose(key);
        if (strcasestr(line, "encrypted")) {
            opts->keys[opts->key_count - 1].privkey_crypt = 1;
        }
    }

    return 0;
}

API int
nc_client_ssh_add_keypair(const char *pub_key, const char *priv_key)
{
    return _nc_client_ssh_add_keypair(pub_key, priv_key, &ssh_opts);
}

API int
nc_client_ssh_ch_add_keypair(const char *pub_key, const char *priv_key)
{
    return _nc_client_ssh_add_keypair(pub_key, priv_key, &ssh_ch_opts);
}

static int
_nc_client_ssh_del_keypair(int idx, struct nc_client_ssh_opts *opts)
{
    if (idx >= opts->key_count) {
        ERRARG;
        return -1;
    }

    free(opts->keys[idx].pubkey_path);
    free(opts->keys[idx].privkey_path);

    --opts->key_count;

    memcpy(opts->keys + idx, opts->keys + opts->key_count, sizeof *opts->keys);
    opts->keys = realloc(opts->keys, opts->key_count * sizeof *opts->keys);

    return 0;
}

API int
nc_client_ssh_del_keypair(int idx)
{
    return _nc_client_ssh_del_keypair(idx, &ssh_opts);
}

API int
nc_client_ssh_ch_del_keypair(int idx)
{
    return _nc_client_ssh_del_keypair(idx, &ssh_ch_opts);
}

static int
_nc_client_ssh_get_keypair_count(struct nc_client_ssh_opts *opts)
{
    return opts->key_count;
}

API int
nc_client_ssh_get_keypair_count(void)
{
    return _nc_client_ssh_get_keypair_count(&ssh_opts);
}

API int
nc_client_ssh_ch_get_keypair_count(void)
{
    return _nc_client_ssh_get_keypair_count(&ssh_ch_opts);
}

static int
_nc_client_ssh_get_keypair(int idx, const char **pub_key, const char **priv_key, struct nc_client_ssh_opts *opts)
{
    if ((idx >= opts->key_count) || (!pub_key && !priv_key)) {
        ERRARG;
        return -1;
    }

    if (pub_key) {
        *pub_key = opts->keys[idx].pubkey_path;
    }
    if (priv_key) {
        *priv_key = opts->keys[idx].privkey_path;
    }

    return 0;
}

API int
nc_client_ssh_get_keypair(int idx, const char **pub_key, const char **priv_key)
{
    return _nc_client_ssh_get_keypair(idx, pub_key, priv_key, &ssh_opts);
}

API int
nc_client_ssh_ch_get_keypair(int idx, const char **pub_key, const char **priv_key)
{
    return _nc_client_ssh_get_keypair(idx, pub_key, priv_key, &ssh_ch_opts);
}

static void
_nc_client_ssh_set_auth_pref(NC_SSH_AUTH_TYPE auth_type, int16_t pref, struct nc_client_ssh_opts *opts)
{
    if (pref < 0) {
        pref = -1;
    }

    if (auth_type == NC_SSH_AUTH_INTERACTIVE) {
        opts->auth_pref[0].value = pref;
    } else if (auth_type == NC_SSH_AUTH_PASSWORD) {
        opts->auth_pref[1].value = pref;
    } else if (auth_type == NC_SSH_AUTH_PUBLICKEY) {
        opts->auth_pref[2].value = pref;
    }
}

API void
nc_client_ssh_set_auth_pref(NC_SSH_AUTH_TYPE auth_type, int16_t pref)
{
    _nc_client_ssh_set_auth_pref(auth_type, pref, &ssh_opts);
}

API void
nc_client_ssh_ch_set_auth_pref(NC_SSH_AUTH_TYPE auth_type, int16_t pref)
{
    _nc_client_ssh_set_auth_pref(auth_type, pref, &ssh_ch_opts);
}

static int16_t
_nc_client_ssh_get_auth_pref(NC_SSH_AUTH_TYPE auth_type, struct nc_client_ssh_opts *opts)
{
    int16_t pref = 0;

    if (auth_type == NC_SSH_AUTH_INTERACTIVE) {
        pref = opts->auth_pref[0].value;
    } else if (auth_type == NC_SSH_AUTH_PASSWORD) {
        pref = opts->auth_pref[1].value;
    } else if (auth_type == NC_SSH_AUTH_PUBLICKEY) {
        pref = opts->auth_pref[2].value;
    }

    return pref;
}

API int16_t
nc_client_ssh_get_auth_pref(NC_SSH_AUTH_TYPE auth_type)
{
    return _nc_client_ssh_get_auth_pref(auth_type, &ssh_opts);
}

API int16_t
nc_client_ssh_ch_get_auth_pref(NC_SSH_AUTH_TYPE auth_type)
{
    return _nc_client_ssh_get_auth_pref(auth_type, &ssh_ch_opts);
}

static int
_nc_client_ssh_set_username(const char *username, struct nc_client_ssh_opts *opts)
{
    if (opts->username) {
        free(opts->username);
    }
    if (username) {
        opts->username = strdup(username);
        if (!opts->username) {
            ERRMEM;
            return -1;
        }
    } else {
        opts->username = NULL;
    }

    return 0;
}

API int
nc_client_ssh_set_username(const char *username)
{
    return _nc_client_ssh_set_username(username, &ssh_opts);
}

API int
nc_client_ssh_ch_set_username(const char *username)
{
    return _nc_client_ssh_set_username(username, &ssh_ch_opts);
}

API int
nc_client_ssh_ch_add_bind_listen(const char *address, uint16_t port)
{
    return nc_client_ch_add_bind_listen(address, port, NC_TI_LIBSSH);
}

API int
nc_client_ssh_ch_del_bind(const char *address, uint16_t port)
{
    return nc_client_ch_del_bind(address, port, NC_TI_LIBSSH);
}

/* Establish a secure SSH connection, authenticate, and create a channel with the 'netconf' subsystem.
 * Host, port, username, and a connected socket is expected to be set.
 */
static int
connect_ssh_session_netconf(struct nc_session *session)
{
    int j, ret_auth, userauthlist;
    NC_SSH_AUTH_TYPE auth;
    short int pref;
    const char* prompt;
    char *s, *answer, echo;
    ssh_key pubkey, privkey;
    ssh_session ssh_sess;

    ssh_sess = session->ti.libssh.session;

    if (ssh_connect(ssh_sess) != SSH_OK) {
        ERR("Starting the SSH session failed (%s)", ssh_get_error(ssh_sess));
        DBG("Error code %d.", ssh_get_error_code(ssh_sess));
        return -1;
    }

    if (sshauth_hostkey_check(session->host, ssh_sess)) {
        ERR("Checking the host key failed.");
        return -1;
    }

    if ((ret_auth = ssh_userauth_none(ssh_sess, NULL)) == SSH_AUTH_ERROR) {
        ERR("Authentication failed (%s).", ssh_get_error(ssh_sess));
        return -1;
    }

    /* check what authentication methods are available */
    userauthlist = ssh_userauth_list(ssh_sess, NULL);

    /* remove those disabled */
    if (ssh_opts.auth_pref[0].value < 0) {
        VRB("Interactive SSH authentication method was disabled.");
        userauthlist &= ~SSH_AUTH_METHOD_INTERACTIVE;
    }
    if (ssh_opts.auth_pref[1].value < 0) {
        VRB("Password SSH authentication method was disabled.");
        userauthlist &= ~SSH_AUTH_METHOD_PASSWORD;
    }
    if (ssh_opts.auth_pref[2].value < 0) {
        VRB("Publickey SSH authentication method was disabled.");
        userauthlist &= ~SSH_AUTH_METHOD_PUBLICKEY;
    }

    while (ret_auth != SSH_AUTH_SUCCESS) {
        auth = 0;
        pref = 0;
        if (userauthlist & SSH_AUTH_METHOD_INTERACTIVE) {
            auth = NC_SSH_AUTH_INTERACTIVE;
            pref = ssh_opts.auth_pref[0].value;
        }
        if ((userauthlist & SSH_AUTH_METHOD_PASSWORD) && (ssh_opts.auth_pref[1].value > pref)) {
            auth = NC_SSH_AUTH_PASSWORD;
            pref = ssh_opts.auth_pref[1].value;
        }
        if ((userauthlist & SSH_AUTH_METHOD_PUBLICKEY) && (ssh_opts.auth_pref[2].value > pref)) {
            auth = NC_SSH_AUTH_PUBLICKEY;
        }

        if (!auth) {
            ERR("Unable to authenticate to the remote server (no supported authentication methods left).");
            break;
        }

        /* found common authentication method */
        switch (auth) {
        case NC_SSH_AUTH_PASSWORD:
            userauthlist &= ~SSH_AUTH_METHOD_PASSWORD;

            VRB("Password authentication (host \"%s\", user \"%s\").", session->host, session->username);
            s = sshauth_password(session->username, session->host);
            if ((ret_auth = ssh_userauth_password(ssh_sess, session->username, s)) != SSH_AUTH_SUCCESS) {
                memset(s, 0, strlen(s));
                VRB("Authentication failed (%s).", ssh_get_error(ssh_sess));
            }
            free(s);
            break;
        case NC_SSH_AUTH_INTERACTIVE:
            userauthlist &= ~SSH_AUTH_METHOD_INTERACTIVE;

            VRB("Keyboard-interactive authentication.");
            while ((ret_auth = ssh_userauth_kbdint(ssh_sess, NULL, NULL)) == SSH_AUTH_INFO) {
                for (j = 0; j < ssh_userauth_kbdint_getnprompts(ssh_sess); ++j) {
                    prompt = ssh_userauth_kbdint_getprompt(ssh_sess, j, &echo);
                    if (prompt == NULL) {
                        break;
                    }
                    answer = sshauth_interactive(ssh_userauth_kbdint_getname(ssh_sess),
                                                 ssh_userauth_kbdint_getinstruction(ssh_sess),
                                                 prompt, echo);
                    if (ssh_userauth_kbdint_setanswer(ssh_sess, j, answer) < 0) {
                        free(answer);
                        break;
                    }
                    free(answer);
                }
            }

            if (ret_auth == SSH_AUTH_ERROR) {
                VRB("Authentication failed (%s).", ssh_get_error(ssh_sess));
            }

            break;
        case NC_SSH_AUTH_PUBLICKEY:
            userauthlist &= ~SSH_AUTH_METHOD_PUBLICKEY;

            VRB("Publickey athentication.");

            /* if publickeys path not provided, we cannot continue */
            if (!ssh_opts.key_count) {
                VRB("No key pair specified.");
                break;
            }

            for (j = 0; j < ssh_opts.key_count; j++) {
                VRB("Trying to authenticate using %spair \"%s\" \"%s\".",
                     ssh_opts.keys[j].privkey_crypt ? "password-protected " : "", ssh_opts.keys[j].privkey_path,
                     ssh_opts.keys[j].pubkey_path);

                if (ssh_pki_import_pubkey_file(ssh_opts.keys[j].pubkey_path, &pubkey) != SSH_OK) {
                    WRN("Failed to import the key \"%s\".", ssh_opts.keys[j].pubkey_path);
                    continue;
                }
                ret_auth = ssh_userauth_try_publickey(ssh_sess, NULL, pubkey);
                if ((ret_auth == SSH_AUTH_DENIED) || (ret_auth == SSH_AUTH_PARTIAL)) {
                    ssh_key_free(pubkey);
                    continue;
                }
                if (ret_auth == SSH_AUTH_ERROR) {
                    ERR("Authentication failed (%s).", ssh_get_error(ssh_sess));
                    ssh_key_free(pubkey);
                    break;
                }

                if (ssh_opts.keys[j].privkey_crypt) {
                    s = sshauth_passphrase(ssh_opts.keys[j].privkey_path);
                } else {
                    s = NULL;
                }

                if (ssh_pki_import_privkey_file(ssh_opts.keys[j].privkey_path, s, NULL, NULL, &privkey) != SSH_OK) {
                    WRN("Failed to import the key \"%s\".", ssh_opts.keys[j].privkey_path);
                    if (s) {
                        memset(s, 0, strlen(s));
                        free(s);
                    }
                    ssh_key_free(pubkey);
                    continue;
                }

                if (s) {
                    memset(s, 0, strlen(s));
                    free(s);
                }

                ret_auth = ssh_userauth_publickey(ssh_sess, NULL, privkey);
                ssh_key_free(pubkey);
                ssh_key_free(privkey);

                if (ret_auth == SSH_AUTH_ERROR) {
                    ERR("Authentication failed (%s).", ssh_get_error(ssh_sess));
                }
                if (ret_auth == SSH_AUTH_SUCCESS) {
                    break;
                }
            }
            break;
        }
    }

    /* check a state of authentication */
    if (ret_auth != SSH_AUTH_SUCCESS) {
        return -1;
    }

    /* open a channel */
    session->ti.libssh.channel = ssh_channel_new(ssh_sess);
    if (ssh_channel_open_session(session->ti.libssh.channel) != SSH_OK) {
        ssh_channel_free(session->ti.libssh.channel);
        session->ti.libssh.channel = NULL;
        ERR("Opening an SSH channel failed (%s).", ssh_get_error(ssh_sess));
        return -1;
    }

    /* execute the NETCONF subsystem on the channel */
    if (ssh_channel_request_subsystem(session->ti.libssh.channel, "netconf") != SSH_OK) {
        ssh_channel_free(session->ti.libssh.channel);
        session->ti.libssh.channel = NULL;
        ERR("Starting the \"netconf\" SSH subsystem failed (%s).", ssh_get_error(ssh_sess));
        return -1;
    }

    return EXIT_SUCCESS;
}

API struct nc_session *
nc_connect_ssh(const char *host, uint16_t port, struct ly_ctx *ctx)
{
    const int timeout = NC_SSH_TIMEOUT;
    int sock;
    char *username;
    struct passwd *pw;
    struct nc_session *session = NULL;

    /* process parameters */
    if (!host || strisempty(host)) {
        host = "localhost";
    }

    if (!port) {
        port = NC_PORT_SSH;
    }

    if (!ssh_opts.username) {
        pw = getpwuid(getuid());
        if (!pw) {
            ERR("Unknown username for the SSH connection (%s).", strerror(errno));
            return NULL;
        } else {
            username = pw->pw_name;
        }
    } else {
        username = ssh_opts.username;
    }

    /* prepare session structure */
    session = calloc(1, sizeof *session);
    if (!session) {
        ERRMEM;
        return NULL;
    }
    session->status = NC_STATUS_STARTING;
    session->side = NC_CLIENT;

    /* transport lock */
    session->ti_lock = malloc(sizeof *session->ti_lock);
    if (!session->ti_lock) {
        ERRMEM;
        goto fail;
    }
    pthread_mutex_init(session->ti_lock, NULL);

    /* other transport-specific data */
    session->ti_type = NC_TI_LIBSSH;
    session->ti.libssh.session = ssh_new();
    if (!session->ti.libssh.session) {
        ERR("Unable to initialize SSH session.");
        goto fail;
    }

    /* set some basic SSH session options */
    ssh_options_set(session->ti.libssh.session, SSH_OPTIONS_HOST, host);
    ssh_options_set(session->ti.libssh.session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(session->ti.libssh.session, SSH_OPTIONS_USER, username);
    ssh_options_set(session->ti.libssh.session, SSH_OPTIONS_TIMEOUT, &timeout);
    if (ssh_options_set(session->ti.libssh.session, SSH_OPTIONS_HOSTKEYS,
                        "ssh-ed25519,ecdsa-sha2-nistp521,ecdsa-sha2-nistp384,"
                        "ecdsa-sha2-nistp256,ssh-rsa,ssh-dss,ssh-rsa1")) {
        /* ecdsa is probably not supported... */
        ssh_options_set(session->ti.libssh.session, SSH_OPTIONS_HOSTKEYS, "ssh-ed25519,ssh-rsa,ssh-dss,ssh-rsa1");
    }

    /* create and assign communication socket */
    sock = nc_sock_connect(host, port);
    if (sock == -1) {
        goto fail;
    }
    ssh_options_set(session->ti.libssh.session, SSH_OPTIONS_FD, &sock);

    /* temporarily, for session connection */
    session->host = host;
    session->username = username;
    if (connect_ssh_session_netconf(session)) {
        goto fail;
    }

    /* assign context (dicionary needed for handshake) */
    if (!ctx) {
        ctx = ly_ctx_new(SCHEMAS_DIR);
    } else {
        session->flags |= NC_SESSION_SHAREDCTX;
    }
    session->ctx = ctx;

    /* NETCONF handshake */
    if (nc_handshake(session)) {
        goto fail;
    }
    session->status = NC_STATUS_RUNNING;

    if (nc_ctx_check_and_fill(session) == -1) {
        goto fail;
    }

    /* store information into the dictionary */
    session->host = lydict_insert(ctx, host, 0);
    session->port = port;
    session->username = lydict_insert(ctx, username, 0);

    return session;

fail:
    nc_session_free(session);
    return NULL;
}

API struct nc_session *
nc_connect_libssh(ssh_session ssh_session, struct ly_ctx *ctx)
{
    char *host = NULL, *username = NULL;
    unsigned short port = 0;
    int sock;
    struct passwd *pw;
    struct nc_session *session = NULL;

    if (!ssh_session) {
        ERRARG;
        return NULL;
    }

    /* prepare session structure */
    session = calloc(1, sizeof *session);
    if (!session) {
        ERRMEM;
        return NULL;
    }
    session->status = NC_STATUS_STARTING;
    session->side = NC_CLIENT;

    /* transport lock */
    session->ti_lock = malloc(sizeof *session->ti_lock);
    if (!session->ti_lock) {
        ERRMEM;
        goto fail;
    }
    pthread_mutex_init(session->ti_lock, NULL);

    session->ti_type = NC_TI_LIBSSH;
    session->ti.libssh.session = ssh_session;

    /* was port set? */
    ssh_options_get_port(ssh_session, (unsigned int *)&port);

    if (ssh_options_get(ssh_session, SSH_OPTIONS_HOST, &host) != SSH_OK) {
        /*
         * There is no file descriptor (detected based on the host, there is no way to check
         * the SSH_OPTIONS_FD directly :/), we need to create it. (TCP/IP layer)
         */

        /* remember host */
        host = strdup("localhost");
        ssh_options_set(session->ti.libssh.session, SSH_OPTIONS_HOST, host);

        /* create and connect socket */
        sock = nc_sock_connect(host, port);
        if (sock == -1) {
            goto fail;
        }
        ssh_options_set(session->ti.libssh.session, SSH_OPTIONS_FD, &sock);
    }

    /* was username set? */
    ssh_options_get(ssh_session, SSH_OPTIONS_USER, &username);

    if (!ssh_is_connected(ssh_session)) {
        /*
         * We are connected, but not SSH authenticated. (Transport layer)
         */

        /* remember username */
        if (!username) {
            if (!ssh_opts.username) {
                pw = getpwuid(getuid());
                if (!pw) {
                    ERR("Unknown username for the SSH connection (%s).", strerror(errno));
                    goto fail;
                }
                username = strdup(pw->pw_name);
            } else {
                username = strdup(ssh_opts.username);
            }
            ssh_options_set(session->ti.libssh.session, SSH_OPTIONS_USER, username);
        }

        /* authenticate SSH session */
        session->host = host;
        session->username = username;
        if (connect_ssh_session_netconf(session)) {
            goto fail;
        }
    }

    /*
     * SSH session is established, create NETCONF session. (Application layer)
     */

    /* assign context (dicionary needed for handshake) */
    if (!ctx) {
        ctx = ly_ctx_new(SCHEMAS_DIR);
    } else {
        session->flags |= NC_SESSION_SHAREDCTX;
    }
    session->ctx = ctx;

    /* NETCONF handshake */
    if (nc_handshake(session)) {
        goto fail;
    }
    session->status = NC_STATUS_RUNNING;

    if (nc_ctx_check_and_fill(session) == -1) {
        goto fail;
    }

    /* store information into the dictionary */
    if (host) {
        session->host = lydict_insert_zc(ctx, host);
    }
    if (port) {
        session->port = port;
    }
    if (username) {
        session->username = lydict_insert_zc(ctx, username);
    }

    return session;

fail:
    nc_session_free(session);
    return NULL;
}

API struct nc_session *
nc_connect_ssh_channel(struct nc_session *session, struct ly_ctx *ctx)
{
    struct nc_session *new_session, *ptr;

    if (!session) {
        ERRARG;
        return NULL;
    }

    /* prepare session structure */
    new_session = calloc(1, sizeof *new_session);
    if (!new_session) {
        ERRMEM;
        return NULL;
    }
    new_session->status = NC_STATUS_STARTING;
    new_session->side = NC_CLIENT;

    /* share some parameters including the session lock */
    new_session->ti_type = NC_TI_LIBSSH;
    new_session->ti_lock = session->ti_lock;
    new_session->ti.libssh.session = session->ti.libssh.session;

    /* create the channel safely */
    pthread_mutex_lock(new_session->ti_lock);

    /* open a channel */
    new_session->ti.libssh.channel = ssh_channel_new(new_session->ti.libssh.session);
    if (ssh_channel_open_session(new_session->ti.libssh.channel) != SSH_OK) {
        ERR("Opening an SSH channel failed (%s).", ssh_get_error(new_session->ti.libssh.session));
        goto fail;
    }
    /* execute the NETCONF subsystem on the channel */
    if (ssh_channel_request_subsystem(new_session->ti.libssh.channel, "netconf") != SSH_OK) {
        ERR("Starting the \"netconf\" SSH subsystem failed (%s).", ssh_get_error(new_session->ti.libssh.session));
        goto fail;
    }

    /* assign context (dicionary needed for handshake) */
    if (!ctx) {
        ctx = ly_ctx_new(SCHEMAS_DIR);
    } else {
        new_session->flags |= NC_SESSION_SHAREDCTX;
    }
    new_session->ctx = ctx;

    /* NETCONF handshake */
    if (nc_handshake(new_session)) {
        goto fail;
    }
    new_session->status = NC_STATUS_RUNNING;

    pthread_mutex_unlock(new_session->ti_lock);

    if (nc_ctx_check_and_fill(new_session) == -1) {
        goto fail;
    }

    /* store information into session and the dictionary */
    new_session->host = lydict_insert(ctx, session->host, 0);
    new_session->port = session->port;
    new_session->username = lydict_insert(ctx, session->username, 0);

    /* append to the session ring list */
    if (!session->ti.libssh.next) {
        session->ti.libssh.next = new_session;
        new_session->ti.libssh.next = session;
    } else {
        ptr = session->ti.libssh.next;
        session->ti.libssh.next = new_session;
        new_session->ti.libssh.next = ptr;
    }

    return new_session;

fail:
    nc_session_free(new_session);
    return NULL;
}

struct nc_session *
nc_accept_callhome_sock_ssh(int sock, const char *host, uint16_t port, struct ly_ctx *ctx)
{
    const int ssh_timeout = NC_SSH_TIMEOUT;
    struct passwd *pw;
    ssh_session sess;

    sess = ssh_new();
    if (!sess) {
        ERR("Unable to initialize an SSH session.");
        close(sock);
        return NULL;
    }

    ssh_options_set(sess, SSH_OPTIONS_FD, &sock);
    ssh_options_set(sess, SSH_OPTIONS_HOST, host);
    ssh_options_set(sess, SSH_OPTIONS_PORT, &port);
    ssh_options_set(sess, SSH_OPTIONS_TIMEOUT, &ssh_timeout);
    if (!ssh_ch_opts.username) {
        pw = getpwuid(getuid());
        if (!pw) {
            ERR("Unknown username for the SSH connection (%s).", strerror(errno));
            return NULL;
        }
        ssh_options_set(sess, SSH_OPTIONS_USER, pw->pw_name);
    } else {
        ssh_options_set(sess, SSH_OPTIONS_USER, ssh_ch_opts.username);
    }
    if (ssh_options_set(sess, SSH_OPTIONS_HOSTKEYS,
                        "ssh-ed25519,ecdsa-sha2-nistp521,ecdsa-sha2-nistp384,"
                        "ecdsa-sha2-nistp256,ssh-rsa,ssh-dss,ssh-rsa1")) {
        /* ecdsa is probably not supported... */
        ssh_options_set(sess, SSH_OPTIONS_HOSTKEYS, "ssh-ed25519,ssh-rsa,ssh-dss,ssh-rsa1");
    }

    return nc_connect_libssh(sess, ctx);
}
