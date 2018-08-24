/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2010 NorthScale, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

/*! \mainpage libvbucket
 *
 * \section intro_sec Introduction
 *
 * libvbucket helps you understand and make use of vbuckets for
 * scaling kv services.
 *
 * \section docs_sec API Documentation
 *
 * Jump right into <a href="modules.html">the modules docs</a> to get started.
 */

/**
 * VBucket Utility Library.
 *
 * \defgroup CD Creation and Destruction
 * \defgroup cfg Config Access
 * \defgroup cfgcmp Config Comparison
 * \defgroup vb VBucket Access
 * \defgroup err Error handling
 */

#ifndef LIBVBUCKET_VBUCKET_H
#define LIBVBUCKET_VBUCKET_H 1

#include <stddef.h>
#include "butil/third_party/libvbucket/visibility.h"

#ifdef __cplusplus
extern "C" {
namespace butil {
#endif

    struct vbucket_config_st;

    /**
     * Opaque config representation.
     */
    typedef struct vbucket_config_st* VBUCKET_CONFIG_HANDLE;

    /**
     * Type of distribution used to map keys to servers. It is possible to
     * select algorithm using "locator" key in config.
     */
    typedef enum {
        VBUCKET_DISTRIBUTION_VBUCKET = 0,
        VBUCKET_DISTRIBUTION_KETAMA = 1
    } VBUCKET_DISTRIBUTION_TYPE;

    /**
     * \addtogroup cfgcmp
     * @{
     */

    /**
     * Difference between two vbucket configs.
     */
    typedef struct {
        /**
         * NULL-terminated list of server names that were added.
         */
        char **servers_added;
        /**
         * NULL-terminated list of server names that were removed.
         */
        char **servers_removed;
        /**
         * Number of vbuckets that changed.  -1 if the total number changed
         */
        int n_vb_changes;
        /**
         * non-null if the sequence of servers changed.
         */
        int sequence_changed;
    } VBUCKET_CONFIG_DIFF;

    /**
     * @}
     */

    /**
     * \addtogroup CD
     *  @{
     */

    /**
     * Create a new vbucket config handle
     * @return handle or NULL if there is no more memory
     */
    LIBVBUCKET_PUBLIC_API
    VBUCKET_CONFIG_HANDLE vbucket_config_create(void);

    typedef enum {
        LIBVBUCKET_SOURCE_FILE,
        LIBVBUCKET_SOURCE_MEMORY
    } vbucket_source_t;

    /**
     * Parse a vbucket configuration
     * @param handle the vbucket config handle to store the result
     * @param data_source what kind of datasource to parse
     * @param data A zero terminated string representing the data to parse.
     *             For LIBVBUCKET_SOURCE_FILE this is the file to parse,
     *             for LIBVBUCKET_SOURCE_MEMORY it is the actual JSON body.
     * @return 0 for success, the appropriate error code otherwise
     */
    LIBVBUCKET_PUBLIC_API
    int vbucket_config_parse(VBUCKET_CONFIG_HANDLE handle,
                             vbucket_source_t data_source,
                             const char *data);

    /**
     * Parse a vbucket configuration and substitute local address if needed
     * @param handle the vbucket config handle to store the result
     * @param data_source what kind of datasource to parse
     * @param data A zero terminated string representing the data to parse.
     *             For LIBVBUCKET_SOURCE_FILE this is the file to parse,
     *             for LIBVBUCKET_SOURCE_MEMORY it is the actual JSON body.
     * @param peername a string, representing address of local peer
     *                 (usually 127.0.0.1)
     * @return 0 for success, the appropriate error code otherwise
     */
    LIBVBUCKET_PUBLIC_API
    int vbucket_config_parse2(VBUCKET_CONFIG_HANDLE handle,
                              vbucket_source_t data_source,
                              const char *data,
                              const char *peername);

    LIBVBUCKET_PUBLIC_API
    const char *vbucket_get_error_message(VBUCKET_CONFIG_HANDLE handle);


    /**
     * Create an instance of vbucket config from a file.
     *
     * @param filename the vbucket config to parse
     */
    LIBVBUCKET_PUBLIC_API
    VBUCKET_CONFIG_HANDLE vbucket_config_parse_file(const char *filename);

    /**
     * Create an instance of vbucket config from a string.
     *
     * @param data a vbucket config string.
     */
    LIBVBUCKET_PUBLIC_API
    VBUCKET_CONFIG_HANDLE vbucket_config_parse_string(const char *data);

    /**
     * Destroy a vbucket config.
     *
     * @param h the vbucket config handle
     */
    LIBVBUCKET_PUBLIC_API
    void vbucket_config_destroy(VBUCKET_CONFIG_HANDLE h);

    /**
     * @}
     */

    /**
     * \addtogroup err
     *  @{
     */

    /**
     * Get the most recent vbucket error.
     *
     * This is currently not threadsafe.
     */
    LIBVBUCKET_PUBLIC_API
    const char *vbucket_get_error(void);

    /**
     * Tell libvbucket it told you the wrong server ID.
     *
     * This will cause libvbucket to do whatever is necessary to try
     * to figure out a better answer.
     *
     * @param h the vbucket config handle.
     * @param vbucket the vbucket ID
     * @param wrongserver the incorrect server ID
     *
     * @return the correct server ID
     */
    LIBVBUCKET_PUBLIC_API
    int vbucket_found_incorrect_master(VBUCKET_CONFIG_HANDLE h,
                                       int vbucket,
                                       int wrongserver);

    /**
     * @}
     */

    /**
     * \addtogroup cfg
     * @{
     */

    /**
     * Get the number of replicas configured for each vbucket.
     */
    LIBVBUCKET_PUBLIC_API
    int vbucket_config_get_num_replicas(VBUCKET_CONFIG_HANDLE h);

    /**
     * Get the total number of vbuckets;
     */
    LIBVBUCKET_PUBLIC_API
    int vbucket_config_get_num_vbuckets(VBUCKET_CONFIG_HANDLE h);

    /**
     * Get the total number of known servers.
     */
    LIBVBUCKET_PUBLIC_API
    int vbucket_config_get_num_servers(VBUCKET_CONFIG_HANDLE h);

    /**
     * Get the optional SASL user.
     *
     * @return a string or NULL.
     */
    LIBVBUCKET_PUBLIC_API
    const char *vbucket_config_get_user(VBUCKET_CONFIG_HANDLE h);

    /**
     * Get the optional SASL password.
     *
     * @return a string or NULL.
     */
    LIBVBUCKET_PUBLIC_API
    const char *vbucket_config_get_password(VBUCKET_CONFIG_HANDLE h);

    /**
     * Get the server at the given index.
     *
     * @return a string in the form of hostname:port
     */
    LIBVBUCKET_PUBLIC_API
    const char *vbucket_config_get_server(VBUCKET_CONFIG_HANDLE h, int i);

    /**
     * Get the CouchDB API endpoint at the given index.
     *
     * @return a string or NULL.
     */
    LIBVBUCKET_PUBLIC_API
    const char *vbucket_config_get_couch_api_base(VBUCKET_CONFIG_HANDLE vb, int i);

    /**
     * Get the REST API endpoint at the given index.
     *
     * @return a string or NULL.
     */
    LIBVBUCKET_PUBLIC_API
    const char *vbucket_config_get_rest_api_server(VBUCKET_CONFIG_HANDLE vb, int i);

    /**
     * Check if the server was used for configuration
     *
     * @return non-zero for configuration node
     */
    LIBVBUCKET_PUBLIC_API
    int vbucket_config_is_config_node(VBUCKET_CONFIG_HANDLE h, int i);

    /**
     * Get the distribution type. Currently can be or "vbucket" (for
     * eventually persisted nodes) either "ketama" (for plain memcached
     * nodes).
     *
     * @return a member of VBUCKET_DISTRIBUTION_TYPE enum.
     */
    LIBVBUCKET_PUBLIC_API
    VBUCKET_DISTRIBUTION_TYPE vbucket_config_get_distribution_type(VBUCKET_CONFIG_HANDLE vb);
    /**
     * @}
     */

    /**
     * \addtogroup vb
     * @{
     */


    /**
     * Map given key to server index. It is aware about current distribution
     * type.
     *
     * @param h the vbucket config
     * @param key pointer to the beginning of the key
     * @param nkey the size of the key
     * @param vbucket_id the vbucket identifier when vbucket distribution is
     *                   used or zero otherwise.
     * @param server_idx the server index
     *
     * @return zero on success
     */
    LIBVBUCKET_PUBLIC_API
    int vbucket_map(VBUCKET_CONFIG_HANDLE h, const void *key, size_t nkey,
                    int *vbucket_id, int *server_idx);

    /**
     * Get the vbucket number for the given key.
     *
     * @param h the vbucket config
     * @param key pointer to the beginning of the key
     * @param nkey the size of the key
     *
     * @return a key
     */
    LIBVBUCKET_PUBLIC_API
    int vbucket_get_vbucket_by_key(VBUCKET_CONFIG_HANDLE h,
                                   const void *key, size_t nkey);

    /**
     * Get the master server for the given vbucket.
     *
     * @param h the vbucket config
     * @param id the vbucket identifier
     *
     * @return the server index
     */
    LIBVBUCKET_PUBLIC_API
    int vbucket_get_master(VBUCKET_CONFIG_HANDLE h, int id);

    /**
     * Get a given replica for a vbucket.
     *
     * @param h the vbucket config
     * @param id the vbucket id
     * @param n the replica number
     *
     * @return the server ID
     */
    LIBVBUCKET_PUBLIC_API
    int vbucket_get_replica(VBUCKET_CONFIG_HANDLE h, int id, int n);

    /**
     * @}
     */

    /**
     * \addtogroup cfgcmp
     * @{
     */

    /**
     * Compare two vbucket handles.
     *
     * @param from the source vbucket config
     * @param to the destination vbucket config
     *
     * @return what changed between the "from" config and the "to" config
     */
    LIBVBUCKET_PUBLIC_API
    VBUCKET_CONFIG_DIFF* vbucket_compare(VBUCKET_CONFIG_HANDLE from,
                                         VBUCKET_CONFIG_HANDLE to);

    /**
     * Free a vbucket diff.
     *
     * @param diff the diff to free
     */
    LIBVBUCKET_PUBLIC_API
    void vbucket_free_diff(VBUCKET_CONFIG_DIFF *diff);

    /**
     * @}
     */

#ifdef __cplusplus
}  // namespace butil
}
#endif

#endif
