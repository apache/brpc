/* Copyright (c) 2000, 2019, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   Without limiting anything contained in the foregoing, this file,
   which is part of C Driver for MySQL (Connector/C), is also subject to the
   Universal FOSS Exception, version 1.0, a copy of which can be found at
   http://oss.oracle.com/licenses/universal-foss-exception.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/**
  @file include/mysql_com.h
  Common definition between mysql server & client.
*/

#ifndef _mysql_com_h
#define _mysql_com_h

/**
  @addtogroup group_cs_com_refresh_flags
  @{
*/

#define REFRESH_GRANT 1    /**< Refresh grant tables, FLUSH PRIVILEGES */
#define REFRESH_LOG 2      /**< Start on new log file, FLUSH LOGS */
#define REFRESH_TABLES 4   /**< close all tables, FLUSH TABLES */
#define REFRESH_HOSTS 8    /**< Flush host cache, FLUSH HOSTS */
#define REFRESH_STATUS 16  /**< Flush status variables, FLUSH STATUS */
#define REFRESH_THREADS 32 /**< Flush thread cache */
#define REFRESH_SLAVE                         \
  64 /**< Reset master info and restart slave \
        thread, RESET SLAVE */
#define REFRESH_MASTER                                                 \
  128                            /**< Remove all bin logs in the index \
                                    and truncate the index, RESET MASTER */
#define REFRESH_ERROR_LOG 256    /**< Rotate only the erorr log */
#define REFRESH_ENGINE_LOG 512   /**< Flush all storage engine logs */
#define REFRESH_BINARY_LOG 1024  /**< Flush the binary log */
#define REFRESH_RELAY_LOG 2048   /**< Flush the relay log */
#define REFRESH_GENERAL_LOG 4096 /**< Flush the general log */
#define REFRESH_SLOW_LOG 8192    /**< Flush the slow query log */
#define REFRESH_READ_LOCK 16384  /**< Lock tables for read. */
/**
  Wait for an impending flush before closing the tables.

  @sa REFRESH_READ_LOCK, handle_reload_request, close_cached_tables
*/
#define REFRESH_FAST 32768
#define REFRESH_USER_RESOURCES 0x80000L   /** FLISH RESOUCES. @sa ::reset_mqh */
#define REFRESH_FOR_EXPORT 0x100000L      /** FLUSH TABLES ... FOR EXPORT */
#define REFRESH_OPTIMIZER_COSTS 0x200000L /** FLUSH OPTIMIZER_COSTS */
#define REFRESH_PERSIST 0x400000L         /** RESET PERSIST */

/** @}*/

/**
   @defgroup group_cs_capabilities_flags Capabilities Flags
   @ingroup group_cs

   @brief Values for the capabilities flag bitmask used by the MySQL protocol

   Currently need to fit into 32 bits.

   Each bit represents an optional feature of the protocol.

   Both the client and the server are sending these.

   The intersection of the two determines whast optional parts of the
   protocol will be used.
*/

/**
  @addtogroup group_cs_capabilities_flags
  @{
*/

/**
  Use the improved version of Old Password Authentication.

  Not used.

  @note Assumed to be set since 4.1.1.
*/
#define CLIENT_LONG_PASSWORD 1
/**
  Send found rows instead of affected rows in @ref
  page_protocol_basic_eof_packet
*/
#define CLIENT_FOUND_ROWS 2
/**
  @brief Get all column flags

  Longer flags in Protocol::ColumnDefinition320.

  @todo Reference Protocol::ColumnDefinition320

  Server
  ------

  Supports longer flags.

  Client
  ------

  Expects longer flags.
*/
#define CLIENT_LONG_FLAG 4
/**
  Database (schema) name can be specified on connect in Handshake Response
  Packet.

  @todo Reference Handshake Response Packet.

  Server
  ------

  Supports schema-name in Handshake Response Packet.

  Client
  ------

  Handshake Response Packet contains a schema-name.

  @sa send_client_reply_packet()
*/
#define CLIENT_CONNECT_WITH_DB 8
#define CLIENT_NO_SCHEMA 16 /**< Don't allow database.table.column */
/**
  Compression protocol supported.

  @todo Reference Compression

  Server
  ------

  Supports compression.

  Client
  ------

  Switches to Compression compressed protocol after successful authentication.
*/
#define CLIENT_COMPRESS 32
/**
  Special handling of ODBC behavior.

  @note No special behavior since 3.22.
*/
#define CLIENT_ODBC 64
/**
  Can use LOAD DATA LOCAL.

  Server
  ------

  Enables the LOCAL INFILE request of LOAD DATA|XML.

  Client
  ------

  Will handle LOCAL INFILE request.
*/
#define CLIENT_LOCAL_FILES 128
/**
  Ignore spaces before '('

  Server
  ------

  Parser can ignore spaces before '('.

  Client
  ------

  Let the parser ignore spaces before '('.
*/
#define CLIENT_IGNORE_SPACE 256
/**
  New 4.1 protocol

  @todo Reference the new 4.1 protocol

  Server
  ------

  Supports the 4.1 protocol.

  Client
  ------

  Uses the 4.1 protocol.

  @note this value was CLIENT_CHANGE_USER in 3.22, unused in 4.0
*/
#define CLIENT_PROTOCOL_41 512
/**
  This is an interactive client

  Use @ref System_variables::net_wait_timeout
  versus @ref System_variables::net_interactive_timeout.

  Server
  ------

  Supports interactive and noninteractive clients.

  Client
  ------

  Client is interactive.

  @sa mysql_real_connect()
*/
#define CLIENT_INTERACTIVE 1024
/**
  Use SSL encryption for the session

  @todo Reference SSL

  Server
  ------

  Supports SSL

  Client
  ------

  Switch to SSL after sending the capability-flags.
*/
#define CLIENT_SSL 2048
/**
  Client only flag. Not used.

  Client
  ------

  Do not issue SIGPIPE if network failures occur (libmysqlclient only).

  @sa mysql_real_connect()
*/
#define CLIENT_IGNORE_SIGPIPE 4096
/**
  Client knows about transactions

  Server
  ------

  Can send status flags in @ref page_protocol_basic_ok_packet /
  @ref page_protocol_basic_eof_packet.

  Client
  ------

  Expects status flags in @ref page_protocol_basic_ok_packet /
  @ref page_protocol_basic_eof_packet.

  @note This flag is optional in 3.23, but always set by the server since 4.0.
  @sa send_server_handshake_packet(), parse_client_handshake_packet(),
  net_send_ok(), net_send_eof()
*/
#define CLIENT_TRANSACTIONS 8192
#define CLIENT_RESERVED 16384 /**< DEPRECATED: Old flag for 4.1 protocol  */
#define CLIENT_RESERVED2                                 \
  32768 /**< DEPRECATED: Old flag for 4.1 authentication \
           CLIENT_SECURE_CONNECTION */
/**
  Enable/disable multi-stmt support

  Also sets @ref CLIENT_MULTI_RESULTS. Currently not checked anywhere.

  Server
  ------

  Can handle multiple statements per COM_QUERY and COM_STMT_PREPARE.

  Client
  -------

  May send multiple statements per COM_QUERY and COM_STMT_PREPARE.

  @note Was named ::CLIENT_MULTI_QUERIES in 4.1.0, renamed later.

  Requires
  --------

  ::CLIENT_PROTOCOL_41

  @todo Reference COM_QUERY and COM_STMT_PREPARE
*/
#define CLIENT_MULTI_STATEMENTS (1UL << 16)
/**
  Enable/disable multi-results

  Server
  ------

  Can send multiple resultsets for COM_QUERY.
  Error if the server needs to send them and client
  does not support them.

  Client
  -------

  Can handle multiple resultsets for COM_QUERY.

  Requires
  --------

  ::CLIENT_PROTOCOL_41

  @sa mysql_execute_command(), sp_head::MULTI_RESULTS
*/
#define CLIENT_MULTI_RESULTS (1UL << 17)
/**
  Multi-results and OUT parameters in PS-protocol.

  Server
  ------

  Can send multiple resultsets for COM_STMT_EXECUTE.

  Client
  ------

  Can handle multiple resultsets for COM_STMT_EXECUTE.

  Requires
  --------

  ::CLIENT_PROTOCOL_41

  @todo Reference COM_STMT_EXECUTE and PS-protocol

  @sa Protocol_binary::send_out_parameters
*/
#define CLIENT_PS_MULTI_RESULTS (1UL << 18)

/**
  Client supports plugin authentication

  Server
  ------

  Sends extra data in Initial Handshake Packet and supports the pluggable
  authentication protocol.

  Client
  ------

  Supports authentication plugins.

  Requires
  --------

  ::CLIENT_PROTOCOL_41

  @todo Reference plugin authentication, Initial Handshake Packet,
  Authentication plugins

  @sa send_change_user_packet(), send_client_reply_packet(), run_plugin_auth(),
  parse_com_change_user_packet(), parse_client_handshake_packet()
*/
#define CLIENT_PLUGIN_AUTH (1UL << 19)
/**
  Client supports connection attributes

  Server
  ------

  Permits connection attributes in Protocol::HandshakeResponse41.

  Client
  ------

  Sends connection attributes in Protocol::HandshakeResponse41.

  @todo Reference Protocol::HandshakeResponse41

  @sa send_client_connect_attrs(), read_client_connect_attrs()
*/
#define CLIENT_CONNECT_ATTRS (1UL << 20)

/**
  Enable authentication response packet to be larger than 255 bytes.

  When the ability to change default plugin require that the initial password
  field in the Protocol::HandshakeResponse41 paclet can be of arbitrary size.
  However, the 4.1 client-server protocol limits the length of the
  auth-data-field sent from client to server to 255 bytes.
  The solution is to change the type of the field to a true length encoded
  string and indicate the protocol change
  with this client capability flag.

  Server
  ------

  Understands length-encoded integer for auth response data in
  Protocol::HandshakeResponse41.

  Client
  ------

  Length of auth response data in Protocol::HandshakeResponse41
  is a length-encoded integer.

  @todo Reference Protocol::HandshakeResponse41

  @note The flag was introduced in 5.6.6, but had the wrong value.

  @sa send_client_reply_packet(), parse_client_handshake_packet(),
  get_56_lenc_string(), get_41_lenc_string()
*/
#define CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA (1UL << 21)

/**
  Don't close the connection for a user account with expired password.

  Server
  ------

  Announces support for expired password extension.

  Client
  ------

  Can handle expired passwords.

  @todo Reference expired password

  @sa MYSQL_OPT_CAN_HANDLE_EXPIRED_PASSWORDS, disconnect_on_expired_password
  ACL_USER::password_expired, check_password_lifetime(), acl_authenticate()
*/
#define CLIENT_CAN_HANDLE_EXPIRED_PASSWORDS (1UL << 22)

/**
  Capable of handling server state change information. Its a hint to the
  server to include the state change information in
  @ref page_protocol_basic_ok_packet.

  Server
  ------
  Can set ::SERVER_SESSION_STATE_CHANGED in the ::SERVER_STATUS_flags_enum
  and send @ref sect_protocol_basic_ok_packet_sessinfo in a
  @ref page_protocol_basic_ok_packet.

  Client
  ------

  Expects the server to send @ref sect_protocol_basic_ok_packet_sessinfo in
  a @ref page_protocol_basic_ok_packet.

  @sa enum_session_state_type, read_ok_ex(), net_send_ok(), Session_tracker,
  State_tracker
*/
#define CLIENT_SESSION_TRACK (1UL << 23)
/**
  Client no longer needs @ref page_protocol_basic_eof_packet and will
  use @ref page_protocol_basic_ok_packet instead.
  @sa net_send_ok()

  Server
  ------

  Can send OK after a Text Resultset.

  Client
  ------

  Expects an @ref page_protocol_basic_ok_packet (instead of
  @ref page_protocol_basic_eof_packet) after the resultset rows of a
  Text Resultset.

  Background
  ----------

  To support ::CLIENT_SESSION_TRACK, additional information must be sent after
  all successful commands. Although the @ref page_protocol_basic_ok_packet is
  extensible, the @ref page_protocol_basic_eof_packet is not due to the overlap
  of its bytes with the content of the Text Resultset Row.

  Therefore, the @ref page_protocol_basic_eof_packet in the
  Text Resultset is replaced with an @ref page_protocol_basic_ok_packet.
  @ref page_protocol_basic_eof_packet is deprecated as of MySQL 5.7.5.

  @todo Reference Text Resultset

  @sa cli_safe_read_with_ok(), read_ok_ex(), net_send_ok(), net_send_eof()
*/
#define CLIENT_DEPRECATE_EOF (1UL << 24)

/**
  Verify server certificate.

  Client only flag.

  @deprecated in favor of --ssl-mode.
*/
#define CLIENT_SSL_VERIFY_SERVER_CERT (1UL << 30)

/**
  The client can handle optional metadata information in the resultset.
*/
#define CLIENT_OPTIONAL_RESULTSET_METADATA (1UL << 25)

/**
  Compression protocol extended to support zstd compression method

  This capability flag is used to send zstd compression level between
  client and server provided both client and server are enabled with
  this flag.

  Server
  ------
  Server sets this flag when global variable protocol-compression-algorithms
  has zstd in its list of supported values.

  Client
  ------
  Client sets this flag when it is configured to use zstd compression method.

*/
#define CLIENT_ZSTD_COMPRESSION_ALGORITHM (1UL << 26)

/**
  This flag will be reserved to extend the 32bit capabilities structure to
  64bits.
*/
#define CLIENT_CAPABILITY_EXTENSION (1UL << 29)
/**
  Don't reset the options after an unsuccessful connect

  Client only flag.

  Typically passed via ::mysql_real_connect() 's client_flag parameter.

  @sa mysql_real_connect()
*/
#define CLIENT_REMEMBER_OPTIONS (1UL << 31)
/** @}*/

/** a compatibility alias for CLIENT_COMPRESS */
#define CAN_CLIENT_COMPRESS CLIENT_COMPRESS

/** Gather all possible capabilites (flags) supported by the server */
#define CLIENT_ALL_FLAGS                                                       \
  (CLIENT_LONG_PASSWORD | CLIENT_FOUND_ROWS | CLIENT_LONG_FLAG |               \
   CLIENT_CONNECT_WITH_DB | CLIENT_NO_SCHEMA | CLIENT_COMPRESS | CLIENT_ODBC | \
   CLIENT_LOCAL_FILES | CLIENT_IGNORE_SPACE | CLIENT_PROTOCOL_41 |             \
   CLIENT_INTERACTIVE | CLIENT_SSL | CLIENT_IGNORE_SIGPIPE |                   \
   CLIENT_TRANSACTIONS | CLIENT_RESERVED | CLIENT_RESERVED2 |                  \
   CLIENT_MULTI_STATEMENTS | CLIENT_MULTI_RESULTS | CLIENT_PS_MULTI_RESULTS |  \
   CLIENT_SSL_VERIFY_SERVER_CERT | CLIENT_REMEMBER_OPTIONS |                   \
   CLIENT_PLUGIN_AUTH | CLIENT_CONNECT_ATTRS |                                 \
   CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA |                                     \
   CLIENT_CAN_HANDLE_EXPIRED_PASSWORDS | CLIENT_SESSION_TRACK |                \
   CLIENT_DEPRECATE_EOF | CLIENT_OPTIONAL_RESULTSET_METADATA |                 \
   CLIENT_ZSTD_COMPRESSION_ALGORITHM)

/**
  Switch off from ::CLIENT_ALL_FLAGS the flags that are optional and
  depending on build flags.
  If any of the optional flags is supported by the build it will be switched
  on before sending to the client during the connection handshake.
*/
#define CLIENT_BASIC_FLAGS                                          \
  (CLIENT_ALL_FLAGS &                                               \
   ~(CLIENT_SSL | CLIENT_COMPRESS | CLIENT_SSL_VERIFY_SERVER_CERT | \
     CLIENT_ZSTD_COMPRESSION_ALGORITHM))

/** The status flags are a bit-field */
enum SERVER_STATUS_flags_enum {
  /**
    Is raised when a multi-statement transaction
    has been started, either explicitly, by means
    of BEGIN or COMMIT AND CHAIN, or
    implicitly, by the first transactional
    statement, when autocommit=off.
  */
  SERVER_STATUS_IN_TRANS = 1,
  SERVER_STATUS_AUTOCOMMIT = 2,   /**< Server in auto_commit mode */
  SERVER_MORE_RESULTS_EXISTS = 8, /**< Multi query - next query exists */
  SERVER_QUERY_NO_GOOD_INDEX_USED = 16,
  SERVER_QUERY_NO_INDEX_USED = 32,
  /**
    The server was able to fulfill the clients request and opened a
    read-only non-scrollable cursor for a query. This flag comes
    in reply to COM_STMT_EXECUTE and COM_STMT_FETCH commands.
    Used by Binary Protocol Resultset to signal that COM_STMT_FETCH
    must be used to fetch the row-data.
    @todo Refify "Binary Protocol Resultset" and "COM_STMT_FETCH".
  */
  SERVER_STATUS_CURSOR_EXISTS = 64,
  /**
    This flag is sent when a read-only cursor is exhausted, in reply to
    COM_STMT_FETCH command.
  */
  SERVER_STATUS_LAST_ROW_SENT = 128,
  SERVER_STATUS_DB_DROPPED = 256, /**< A database was dropped */
  SERVER_STATUS_NO_BACKSLASH_ESCAPES = 512,
  /**
    Sent to the client if after a prepared statement reprepare
    we discovered that the new statement returns a different
    number of result set columns.
  */
  SERVER_STATUS_METADATA_CHANGED = 1024,
  SERVER_QUERY_WAS_SLOW = 2048,
  /**
    To mark ResultSet containing output parameter values.
  */
  SERVER_PS_OUT_PARAMS = 4096,

  /**
    Set at the same time as SERVER_STATUS_IN_TRANS if the started
    multi-statement transaction is a read-only transaction. Cleared
    when the transaction commits or aborts. Since this flag is sent
    to clients in OK and EOF packets, the flag indicates the
    transaction status at the end of command execution.
  */
  SERVER_STATUS_IN_TRANS_READONLY = 8192,

  /**
    This status flag, when on, implies that one of the state information has
    changed on the server because of the execution of the last statement.
  */
  SERVER_SESSION_STATE_CHANGED = (1UL << 14)
};

/**
  Server status flags that must be cleared when starting
  execution of a new SQL statement.
  Flags from this set are only added to the
  current server status by the execution engine, but
  never removed -- the execution engine expects them
  to disappear automagically by the next command.
*/
#define SERVER_STATUS_CLEAR_SET                                   \
  (SERVER_QUERY_NO_GOOD_INDEX_USED | SERVER_QUERY_NO_INDEX_USED | \
   SERVER_MORE_RESULTS_EXISTS | SERVER_STATUS_METADATA_CHANGED |  \
   SERVER_QUERY_WAS_SLOW | SERVER_STATUS_DB_DROPPED |             \
   SERVER_STATUS_CURSOR_EXISTS | SERVER_STATUS_LAST_ROW_SENT |    \
   SERVER_SESSION_STATE_CHANGED)

/** Max length of a error message. Should be kept in sync with ::ERRMSGSIZE. */
#define MYSQL_ERRMSG_SIZE 512
#define NET_READ_TIMEOUT 30          /**< Timeout on read */
#define NET_WRITE_TIMEOUT 60         /**< Timeout on write */
#define NET_WAIT_TIMEOUT 8 * 60 * 60 /**< Wait for new query */


/**
  @addtogroup group_cs_backward_compatibility Backward compatibility
  @ingroup group_cs
  @{
*/
#define CLIENT_MULTI_QUERIES CLIENT_MULTI_STATEMENTS
#define FIELD_TYPE_DECIMAL MYSQL_TYPE_DECIMAL
#define FIELD_TYPE_NEWDECIMAL MYSQL_TYPE_NEWDECIMAL
#define FIELD_TYPE_TINY MYSQL_TYPE_TINY
#define FIELD_TYPE_SHORT MYSQL_TYPE_SHORT
#define FIELD_TYPE_LONG MYSQL_TYPE_LONG
#define FIELD_TYPE_FLOAT MYSQL_TYPE_FLOAT
#define FIELD_TYPE_DOUBLE MYSQL_TYPE_DOUBLE
#define FIELD_TYPE_NULL MYSQL_TYPE_NULL
#define FIELD_TYPE_TIMESTAMP MYSQL_TYPE_TIMESTAMP
#define FIELD_TYPE_LONGLONG MYSQL_TYPE_LONGLONG
#define FIELD_TYPE_INT24 MYSQL_TYPE_INT24
#define FIELD_TYPE_DATE MYSQL_TYPE_DATE
#define FIELD_TYPE_TIME MYSQL_TYPE_TIME
#define FIELD_TYPE_DATETIME MYSQL_TYPE_DATETIME
#define FIELD_TYPE_YEAR MYSQL_TYPE_YEAR
#define FIELD_TYPE_NEWDATE MYSQL_TYPE_NEWDATE
#define FIELD_TYPE_ENUM MYSQL_TYPE_ENUM
#define FIELD_TYPE_SET MYSQL_TYPE_SET
#define FIELD_TYPE_TINY_BLOB MYSQL_TYPE_TINY_BLOB
#define FIELD_TYPE_MEDIUM_BLOB MYSQL_TYPE_MEDIUM_BLOB
#define FIELD_TYPE_LONG_BLOB MYSQL_TYPE_LONG_BLOB
#define FIELD_TYPE_BLOB MYSQL_TYPE_BLOB
#define FIELD_TYPE_VAR_STRING MYSQL_TYPE_VAR_STRING
#define FIELD_TYPE_STRING MYSQL_TYPE_STRING
#define FIELD_TYPE_CHAR MYSQL_TYPE_TINY
#define FIELD_TYPE_INTERVAL MYSQL_TYPE_ENUM
#define FIELD_TYPE_GEOMETRY MYSQL_TYPE_GEOMETRY
#define FIELD_TYPE_BIT MYSQL_TYPE_BIT
/** @}*/

/**
  @addtogroup group_cs_shutdown_kill_constants Shutdown/kill enums and constants
  @ingroup group_cs

  @sa THD::is_killable
  @{
*/
#define MYSQL_SHUTDOWN_KILLABLE_CONNECT (unsigned char)(1 << 0)
#define MYSQL_SHUTDOWN_KILLABLE_TRANS (unsigned char)(1 << 1)
#define MYSQL_SHUTDOWN_KILLABLE_LOCK_TABLE (unsigned char)(1 << 2)
#define MYSQL_SHUTDOWN_KILLABLE_UPDATE (unsigned char)(1 << 3)

/**
  We want levels to be in growing order of hardness (because we use number
  comparisons).

  @note ::SHUTDOWN_DEFAULT does not respect the growing property, but it's ok.
*/
enum mysql_enum_shutdown_level {
  SHUTDOWN_DEFAULT = 0,
  /** Wait for existing connections to finish */
  SHUTDOWN_WAIT_CONNECTIONS = MYSQL_SHUTDOWN_KILLABLE_CONNECT,
  /** Wait for existing transactons to finish */
  SHUTDOWN_WAIT_TRANSACTIONS = MYSQL_SHUTDOWN_KILLABLE_TRANS,
  /** Wait for existing updates to finish (=> no partial MyISAM update) */
  SHUTDOWN_WAIT_UPDATES = MYSQL_SHUTDOWN_KILLABLE_UPDATE,
  /** Flush InnoDB buffers and other storage engines' buffers*/
  SHUTDOWN_WAIT_ALL_BUFFERS = (MYSQL_SHUTDOWN_KILLABLE_UPDATE << 1),
  /** Don't flush InnoDB buffers, flush other storage engines' buffers*/
  SHUTDOWN_WAIT_CRITICAL_BUFFERS = (MYSQL_SHUTDOWN_KILLABLE_UPDATE << 1) + 1,
  /** Query level of the KILL command */
  KILL_QUERY = 254,
  /** Connection level of the KILL command */
  KILL_CONNECTION = 255
};
/** @}*/

enum enum_resultset_metadata {
  /** No metadata will be sent. */
  RESULTSET_METADATA_NONE = 0,
  /** The server will send all metadata. */
  RESULTSET_METADATA_FULL = 1
};

enum enum_cursor_type {
  CURSOR_TYPE_NO_CURSOR = 0,
  CURSOR_TYPE_READ_ONLY = 1,
  CURSOR_TYPE_FOR_UPDATE = 2,
  CURSOR_TYPE_SCROLLABLE = 4
};

/** options for ::mysql_options() */
enum enum_mysql_set_option {
  MYSQL_OPTION_MULTI_STATEMENTS_ON,
  MYSQL_OPTION_MULTI_STATEMENTS_OFF
};

/**
  Type of state change information that the server can include in the Ok
  packet.

  @note
    - session_state_type shouldn't go past 255 (i.e. 1-byte boundary).
    - Modify the definition of ::SESSION_TRACK_END when a new member is added.
*/
enum enum_session_state_type {
  SESSION_TRACK_SYSTEM_VARIABLES, /**< Session system variables */
  SESSION_TRACK_SCHEMA,           /**< Current schema */
  SESSION_TRACK_STATE_CHANGE,     /**< track session state changes */
  SESSION_TRACK_GTIDS,            /**< See also: session_track_gtids */
  SESSION_TRACK_TRANSACTION_CHARACTERISTICS, /**< Transaction chistics */
  SESSION_TRACK_TRANSACTION_STATE            /**< Transaction state */
};

/** start of ::enum_session_state_type */
#define SESSION_TRACK_BEGIN SESSION_TRACK_SYSTEM_VARIABLES

/** End of ::enum_session_state_type */
#define SESSION_TRACK_END SESSION_TRACK_TRANSACTION_STATE

/** is T a valid session state type */
#define IS_SESSION_STATE_TYPE(T) \
  (((int)(T) >= SESSION_TRACK_BEGIN) && ((T) <= SESSION_TRACK_END))

#define net_new_transaction(net) ((net)->pkt_nr = 0)

/**
  @addtogroup group_cs_compresson_constants Constants when using compression
  @ingroup group_cs
  @{
*/
#define NET_HEADER_SIZE 4  /**< standard header size */
#define COMP_HEADER_SIZE 3 /**< compression header extra size */
/** @}*/

#define NULL_LENGTH ((unsigned long)~0) /**< For ::net_store_length() */
#define MYSQL_STMT_HEADER 4
#define MYSQL_LONG_DATA_HEADER 6
#endif
