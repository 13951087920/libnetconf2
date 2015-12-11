/**
 * \file messages.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief libnetconf2's public functions and structures of NETCONF messages.
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

#ifndef NC_MESSAGES_H_
#define NC_MESSAGES_H_

#include <stdint.h>

typedef enum {
    NC_RPC_GENERIC,     /**< user-defined generic RPC. */

    /* ietf-netconf */
    NC_RPC_GETCONFIG,   /**< \<get-config\> RPC. */
    NC_RPC_EDIT,        /**< \<edit-config\> RPC. */
    NC_RPC_COPY,        /**< \<copy-config\> RPC. */
    NC_RPC_DELETE,      /**< \<delete-config\> RPC. */
    NC_RPC_LOCK,        /**< \<lock\> RPC. */
    NC_RPC_UNLOCK,      /**< \<unlock\> RPC. */
    NC_RPC_GET,         /**< \<get\> RPC. */
    /* NC_RPC_CLOSE is not defined since sending \<close-session\> is done by nc_session_free() */
    NC_RPC_KILL,        /**< \<kill-session\> RPC. */
    NC_RPC_COMMIT,      /**< \<commit\> RPC. */
    NC_RPC_DISCARD,     /**< \<discard-changes\> RPC. */
    NC_RPC_CANCEL,      /**< \<cancel-commit\> RPC. */
    NC_RPC_VALIDATE,    /**< \<validate\> RPC. */

    /* ietf-netconf-monitoring */
    NC_RPC_GETSCHEMA,   /**< \<get-schema\> RPC. */

    /* notifications */
    NC_RPC_SUBSCRIBE    /**< \<create-subscription\> RPC. */
} NC_RPC_TYPE;

typedef enum {
    NC_REPLY_ERROR,
    NC_REPLY_OK,
    NC_REPLY_DATA,
    NC_REPLY_NOTIF
} NC_REPLY_TYPE;

typedef enum {
    NC_RPC_EDIT_DFLTOP_UNKNOWN = 0,
    NC_RPC_EDIT_DFLTOP_MERGE,
    NC_RPC_EDIT_DFLTOP_REPLACE,
    NC_RPC_EDIT_DFLTOP_NONE
} NC_RPC_EDIT_DFLTOP;

typedef enum {
    NC_RPC_EDIT_TESTOPT_UNKNOWN = 0,
    NC_RPC_EDIT_TESTOPT_TESTSET,
    NC_RPC_EDIT_TESTOPT_SET,
    NC_RPC_EDIT_TESTOPT_TEST
} NC_RPC_EDIT_TESTOPT;

typedef enum {
    NC_RPC_EDIT_ERROPT_UNKNOWN = 0,
    NC_RPC_EDIT_ERROPT_STOP,
    NC_RPC_EDIT_ERROPT_CONTINUE,
    NC_RPC_EDIT_ERROPT_ROLLBACK
} NC_RPC_EDIT_ERROPT;

typedef enum {
    NC_WD_UNKNOWN = 0,
    NC_WD_ALL,
    NC_WD_ALL_TAG,
    NC_WD_TRIM,
    NC_WD_EXPLICIT
} NC_WD_MODE;

typedef enum {
    NC_RPC_PARAMTYPE_CONST,
    NC_RPC_PARAMTYPE_FREE,
    NC_RPC_PARAMTYPE_DUP_AND_FREE
} NC_RPC_PARAMTYPE;

/**
 * @brief NETCONF error structure representation
 */
struct nc_err {
    /**
     * @brief \<error-type\>, error layer where the error occurred.
     */
    const char *type;
    /**
     * @brief \<error-tag\>.
     */
    const char *tag;
    /**
     * @brief \<error-severity\>.
     */
    const char *severity;
    /**
     * @brief \<error-app-tag\>, the data-model-specific or implementation-specific error condition, if one exists.
     */
    const char *apptag;
    /**
     * @brief \<error-path\>, XPATH expression identifying the element with the error.
     */
    const char *path;
    /**
     * @brief \<error-message\>, Human-readable description of the error.
     */
    const char *message;
    const char *message_lang;

    /* <error-info> */

    /**
     * @brief \<session-id\>, session ID of the session holding the requested lock.
     */
    const char *sid;
    /**
     * @brief \<bad-attr\>, the name of the data-model-specific XML attribute that caused the error.
     */
    const char **attr;
    int attr_count;
    /**
     * @brief \<bad-element\>, the name of the data-model-specific XML element that caused the error.
     */
    const char **elem;
    int elem_count;
    /**
     * @brief \<bad-namespace\>, the name of the unexpected XML namespace that caused the error.
     */
    const char **ns;
    int ns_count;
    /**
     * @brief Remaining non-standard elements.
     */
    struct lyxml_elem **other;
    int other_count;
};

struct nc_reply {
    NC_REPLY_TYPE type;
};

struct nc_reply_error {
    NC_REPLY_TYPE type;      /**< NC_REPLY_ERROR */
    struct ly_ctx *ctx;
    struct nc_err *err;      /**< errors, any of the values inside can be NULL */
    int err_count;
};

struct nc_reply_data {
    NC_REPLY_TYPE type;      /**< NC_REPLY_DATA */
    struct lyd_node *data;   /**< libyang data tree */
};

struct nc_notif {
    NC_REPLY_TYPE type;      /**< NC_REPLY_NOTIF */
    struct lyd_node *tree;   /**< libyang data tree of the message */
};

/**
 * @brief NETCONF RPC object
 */
struct nc_rpc;

/**
 * @brief Get the type of the RPC
 *
 * @param[in] rpc RPC to check the type of.
 * @return Type of \p rpc.
 */
NC_RPC_TYPE nc_rpc_get_type(const struct nc_rpc *rpc);

/**
 * @brief Create a generic NETCONF RPC
 *
 * Note that created object can be sent via any NETCONF session that shares the context
 * of the \p data.
 *
 * @param[in] data NETCONF RPC data as a data tree.
 * @param[in] paramtype How to further manage data parameters.
 * @return Created RPC object to send via a NETCONF session or NULL in case of (memory allocation) error.
 */
struct nc_rpc *nc_rpc_generic(const struct lyd_node *data, NC_RPC_PARAMTYPE paramtype);

/**
 * @brief Create a generic NETCONF RPC from an XML string
 *
 * Note that functions to create any RPC object do not check validity of the provided
 * parameters. It is checked later while sending the RPC via a specific NETCONF session
 * (nc_send_rpc()) since the NETCONF capabilities of the session are needed for such a
 * check. Created object can be sent via any NETCONF session which supports all the
 * needed NETCONF capabilities for the RPC.
 *
 * @param[in] xml_str NETCONF RPC data as an XML string.
 * @param[in] paramtype How to further manage data parameters.
 * @return Created RPC object to send via a NETCONF session or NULL in case of (memory allocation) error.
 */
struct nc_rpc *nc_rpc_generic_xml(const char *xml_str, NC_RPC_PARAMTYPE paramtype);

/**
 * @brief Create NETCONF RPC \<get-config\>
 *
 * Note that functions to create any RPC object do not check validity of the provided
 * parameters. It is checked later while sending the RPC via a specific NETCONF session
 * (nc_send_rpc()) since the NETCONF capabilities of the session are needed for such a
 * check. Created object can be sent via any NETCONF session which supports all the
 * needed NETCONF capabilities for the RPC.
 *
 * @param[in] source Source datastore being queried.
 * @param[in] filter Optional filter data, an XML subtree or XPath expression.
 * @param[in] wd_mode Optional with-defaults capability mode.
 * @param[in] paramtype How to further manage data parameters.
 * @return Created RPC object to send via a NETCONF session or NULL in case of (memory allocation) error.
 */
struct nc_rpc *nc_rpc_getconfig(NC_DATASTORE source, const char *filter, NC_WD_MODE wd_mode,
                                NC_RPC_PARAMTYPE paramtype);

/**
 * @brief Create NETCONF RPC \<edit-config\>
 *
 * Note that functions to create any RPC object do not check validity of the provided
 * parameters. It is checked later while sending the RPC via a specific NETCONF session
 * (nc_send_rpc()) since the NETCONF capabilities of the session are needed for such a
 * check. Created object can be sent via any NETCONF session which supports all the
 * needed NETCONF capabilities for the RPC.
 *
 * @param[in] target Target datastore being edited.
 * @param[in] default_op Optional default operation.
 * @param[in] test_opt Optional test option.
 * @param[in] error_opt Optional error option.
 * @param[in] edit_content Config or URL where the config to perform is to be found.
 * @param[in] paramtype How to further manage data parameters.
 * @return Created RPC object to send via a NETCONF session or NULL in case of (memory allocation) error.
 */
struct nc_rpc *nc_rpc_edit(NC_DATASTORE target, NC_RPC_EDIT_DFLTOP default_op, NC_RPC_EDIT_TESTOPT test_opt,
                           NC_RPC_EDIT_ERROPT error_opt, const char *edit_content, NC_RPC_PARAMTYPE paramtype);

/**
 * @brief Create NETCONF RPC \<copy-config\>
 *
 * Note that functions to create any RPC object do not check validity of the provided
 * parameters. It is checked later while sending the RPC via a specific NETCONF session
 * (nc_send_rpc()) since the NETCONF capabilities of the session are needed for such a
 * check. Created object can be sent via any NETCONF session which supports all the
 * needed NETCONF capabilities for the RPC.
 *
 * @param[in] target Target datastore.
 * @param[in] url_trg Used instead \p target if the target is an URL.
 * @param[in] source Source datastore.
 * @param[in] url_or_config_src Used instead \p source if the source is an URL or a config.
 * @param[in] wd_mode Optional with-defaults capability mode.
 * @param[in] paramtype How to further manage data parameters.
 * @return Created RPC object to send via a NETCONF session or NULL in case of (memory allocation) error.
 */
struct nc_rpc *nc_rpc_copy(NC_DATASTORE target, const char *url_trg, NC_DATASTORE source,
                           const char *url_or_config_src, NC_WD_MODE wd_mode, NC_RPC_PARAMTYPE paramtype);

/**
 * @brief Create NETCONF RPC \<delete-config\>
 *
 * Note that functions to create any RPC object do not check validity of the provided
 * parameters. It is checked later while sending the RPC via a specific NETCONF session
 * (nc_send_rpc()) since the NETCONF capabilities of the session are needed for such a
 * check. Created object can be sent via any NETCONF session which supports all the
 * needed NETCONF capabilities for the RPC.
 *
 * @param[in] target Target datastore to delete.
 * @param[in] url Used instead \p target if the target is an URL.
 * @param[in] paramtype How to further manage data parameters.
 * @return Created RPC object to send via a NETCONF session or NULL in case of (memory allocation) error.
 */
struct nc_rpc *nc_rpc_delete(NC_DATASTORE target, const char *url, NC_RPC_PARAMTYPE paramtype);

/**
 * @brief Create NETCONF RPC \<lock\>
 *
 * Note that functions to create any RPC object do not check validity of the provided
 * parameters. It is checked later while sending the RPC via a specific NETCONF session
 * (nc_send_rpc()) since the NETCONF capabilities of the session are needed for such a
 * check. Created object can be sent via any NETCONF session which supports all the
 * needed NETCONF capabilities for the RPC.
 *
 * @param[in] target Target datastore of the operation.
 * @return Created RPC object to send via a NETCONF session or NULL in case of (memory allocation) error.
 */
struct nc_rpc *nc_rpc_lock(NC_DATASTORE target);

/**
 * @brief Create NETCONF RPC \<unlock\>
 *
 * Note that functions to create any RPC object do not check validity of the provided
 * parameters. It is checked later while sending the RPC via a specific NETCONF session
 * (nc_send_rpc()) since the NETCONF capabilities of the session are needed for such a
 * check. Created object can be sent via any NETCONF session which supports all the
 * needed NETCONF capabilities for the RPC.
 *
 * @param[in] target Target datastore of the operation.
 * @return Created RPC object to send via a NETCONF session or NULL in case of (memory allocation) error.
 */
struct nc_rpc *nc_rpc_unlock(NC_DATASTORE target);

/**
 * @brief Create NETCONF RPC \<get\>
 *
 * Note that functions to create any RPC object do not check validity of the provided
 * parameters. It is checked later while sending the RPC via a specific NETCONF session
 * (nc_send_rpc()) since the NETCONF capabilities of the session are needed for such a
 * check. Created object can be sent via any NETCONF session which supports all the
 * needed NETCONF capabilities for the RPC.
 *
 * @param[in] filter Optional filter data, an XML subtree or XPath expression.
 * @param[in] wd_mode Optional with-defaults capability mode.
 * @param[in] paramtype How to further manage data parameters.
 * @return Created RPC object to send via a NETCONF session or NULL in case of (memory allocation) error.
 */
struct nc_rpc *nc_rpc_get(const char *filter, NC_WD_MODE wd_mode, NC_RPC_PARAMTYPE paramtype);

/**
 * @brief Create NETCONF RPC \<kill-session\>
 *
 * Note that functions to create any RPC object do not check validity of the provided
 * parameters. It is checked later while sending the RPC via a specific NETCONF session
 * (nc_send_rpc()) since the NETCONF capabilities of the session are needed for such a
 * check. Created object can be sent via any NETCONF session which supports all the
 * needed NETCONF capabilities for the RPC.
 *
 * @param[in] session_id Session ID of the session to kill.
 * @return Created RPC object to send via a NETCONF session or NULL in case of (memory allocation) error.
 */
struct nc_rpc *nc_rpc_kill(uint32_t session_id);

/**
 * @brief Create NETCONF RPC \<commit\>
 *
 * Note that functions to create any RPC object do not check validity of the provided
 * parameters. It is checked later while sending the RPC via a specific NETCONF session
 * (nc_send_rpc()) since the NETCONF capabilities of the session are needed for such a
 * check. Created object can be sent via any NETCONF session which supports all the
 * needed NETCONF capabilities for the RPC.
 *
 * @param[in] confirmed Whether the commit is to be confirmed.
 * @param[in] confirm_timeout Optional confirm timeout.
 * @param[in] persist Optional identification string of a new persistent confirmed commit.
 * @param[in] persist_id Optional identification string of a persistent confirmed commit to be commited.
 * @param[in] paramtype How to further manage data parameters.
 * @return Created RPC object to send via a NETCONF session or NULL in case of (memory allocation) error.
 */
struct nc_rpc *nc_rpc_commit(int confirmed, uint32_t confirm_timeout, const char *persist, const char *persist_id,
                             NC_RPC_PARAMTYPE paramtype);

/**
 * @brief Create NETCONF RPC \<discard-changes\>
 *
 * Note that functions to create any RPC object do not check validity of the provided
 * parameters. It is checked later while sending the RPC via a specific NETCONF session
 * (nc_send_rpc()) since the NETCONF capabilities of the session are needed for such a
 * check. Created object can be sent via any NETCONF session which supports all the
 * needed NETCONF capabilities for the RPC.
 *
 * @return Created RPC object to send via a NETCONF session or NULL in case of (memory allocation) error.
 */
struct nc_rpc *nc_rpc_discard(void);

/**
 * @brief Create NETCONF RPC \<cancel-commit\>
 *
 * Note that functions to create any RPC object do not check validity of the provided
 * parameters. It is checked later while sending the RPC via a specific NETCONF session
 * (nc_send_rpc()) since the NETCONF capabilities of the session are needed for such a
 * check. Created object can be sent via any NETCONF session which supports all the
 * needed NETCONF capabilities for the RPC.
 *
 * @param[in] persist_id Optional identification string of a persistent confirmed commit.
 * @param[in] paramtype How to further manage data parameters.
 * @return Created RPC object to send via a NETCONF session or NULL in case of (memory allocation) error.
 */
struct nc_rpc *nc_rpc_cancel(const char *persist_id, NC_RPC_PARAMTYPE paramtype);

/**
 * @brief Create NETCONF RPC \<validate\>
 *
 * Note that functions to create any RPC object do not check validity of the provided
 * parameters. It is checked later while sending the RPC via a specific NETCONF session
 * (nc_send_rpc()) since the NETCONF capabilities of the session are needed for such a
 * check. Created object can be sent via any NETCONF session which supports all the
 * needed NETCONF capabilities for the RPC.
 *
 * @param[in] source Source datastore being validated.
 * @param[in] url_or_config Usedn instead \p source if the source is an URL or a config.
 * @param[in] paramtype How to further manage data parameters.
 * @return Created RPC object to send via a NETCONF session or NULL in case of (memory allocation) error.
 */
struct nc_rpc *nc_rpc_validate(NC_DATASTORE source, const char *url_or_config, NC_RPC_PARAMTYPE paramtype);

/**
 * @brief Create NETCONF RPC \<get-schema\>
 *
 * Note that functions to create any RPC object do not check validity of the provided
 * parameters. It is checked later while sending the RPC via a specific NETCONF session
 * (nc_send_rpc()) since the NETCONF capabilities of the session are needed for such a
 * check. Created object can be sent via any NETCONF session which supports all the
 * needed NETCONF capabilities for the RPC.
 *
 * @param[in] identifier Requested model identifier.
 * @param[in] version Optional model version, either YANG version (1.0/1.1) or revision date.
 * @param[in] format Optional format of the model (default is YANG).
 * @param[in] paramtype How to further manage data parameters.
 * @return Created RPC object to send via a NETCONF session or NULL in case of (memory allocation) error.
 */
struct nc_rpc *nc_rpc_getschema(const char *identifier, const char *version, const char *format, NC_RPC_PARAMTYPE paramtype);

/**
 * @brief Create NETCONF RPC \<create-subscription\>
 *
 * Note that functions to create any RPC object do not check validity of the provided
 * parameters. It is checked later while sending the RPC via a specific NETCONF session
 * (nc_send_rpc()) since the NETCONF capabilities of the session are needed for such a
 * check. Created object can be sent via any NETCONF session which supports all the
 * needed NETCONF capabilities for the RPC.
 *
 * @param[in] stream_name Optional name of a NETCONF stream to subscribe to.
 * @param[in] filter Optional filter data, an XML subtree or XPath expression.
 * @param[in] start_time Optional YANG datetime identifying the start of the subscription.
 * @param[in] stop_time Optional YANG datetime identifying the end of the subscription.
 * @param[in] paramtype How to further manage data parameters.
 * @return Created RPC object to send via a NETCONF session or NULL in case of (memory allocation) error.
 */
struct nc_rpc *nc_rpc_subscribe(const char *stream_name, const char *filter, const char *start_time,
								const char *stop_time, NC_RPC_PARAMTYPE paramtype);

/**
 * @brief Free the NETCONF RPC object.
 * @param[in] rpc Object to free.
 */
void nc_rpc_free(struct nc_rpc *rpc);

/**
 * @brief Free the NETCONF RPC reply object.
 * @param[in] rpc Object to free.
 */
void nc_reply_free(struct nc_reply *reply);

/**
 * @brief Free the NETCONF Notification object.
 * @param[in] rpc Object to free.
 */
void nc_notif_free(struct nc_notif *notif);

#endif /* NC_MESSAGES_H_ */
