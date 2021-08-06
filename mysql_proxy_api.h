#ifndef __MYSQL_PROXY_API__
#define __MYSQL_PROXY_API__

#include <stdint.h>
#include <stdio.h>
#include <errno.h>

#define MYSQL_VERSION_ID 40000

#ifdef __cplusplus
extern "C"
{
#endif

// Error number copied from mysql/errmsg.h
#define CR_ERROR_FIRST  	2000 /*Copy first error nr.*/
#define CR_UNKNOWN_ERROR	2000
#define CR_SOCKET_CREATE_ERROR	2001
#define CR_CONNECTION_ERROR	2002
#define CR_CONN_HOST_ERROR	2003
#define CR_IPSOCK_ERROR		2004
#define CR_UNKNOWN_HOST		2005
#define CR_SERVER_GONE_ERROR	2006
#define CR_OUT_OF_MEMORY	2008
#define CR_SERVER_LOST   	2013
#define CR_MALFORMED_PACKET	2027
#define CR_NO_DATA       	2051

#define MYSQL_PROXY_DEFAULT_PORT	34765

typedef uint64_t my_ulonglong;
typedef int my_bool;

struct MYSQL_PROXY
{
	int sockfd;
	my_bool is_inner_created;

	int errnum;
	char errmsg[1024];

	char appname[128];
	char appkey[8];
	uint64_t sequence;
	int timeout_ms;

	void *current_query_context;
};
typedef struct MYSQL_PROXY MYSQL;


typedef void MYSQL_RES;
typedef char **MYSQL_ROW;

//FIXME: currently we only support name in field info
//but it is very easy to add more fields as necessary, please contact the author to add other fields needed
struct MYSQL_PROXY_FIELD
{
	char name[256];
	int name_length;
};
typedef struct MYSQL_PROXY_FIELD MYSQL_FIELD;

MYSQL *mysql_proxy_init(MYSQL *mysql);
static inline MYSQL *mysql_init(MYSQL *mysql) { return mysql_proxy_init(mysql); }

void mysql_proxy_close(MYSQL *mysql);
static inline void mysql_close(MYSQL *mysql) { mysql_proxy_close(mysql); }

unsigned int mysql_proxy_errno(MYSQL *mysql);
static inline unsigned int mysql_errno(MYSQL *mysql) { return mysql_proxy_errno(mysql); }

const char *mysql_proxy_error(MYSQL *mysql);
static inline const char *mysql_error(MYSQL *mysql) { return mysql_proxy_error(mysql); }

// Please replace mysql_real_connect with this one
// if server_port is 0, use default port 34765
// timeout_ms can be set to 0 to disable timeout mechanism
MYSQL *mysql_proxy_connect(MYSQL *mysql, const char *server_ip, int server_port, const char *app_name, const char *app_passwd, int timeout_ms);
static inline MYSQL *mysql_real_connect(MYSQL *mysql, const char *host, const char *user, const char *passwd, const char *db, unsigned int port, const char *unix_socket, unsigned long client_flag)
{
	// Is the user still use the compat api to access mysql_proxy, the user SHOULD keep in mind that:
	// 1) Some parameters have changed their meanings:
	//       host   -- proxy server ip
	//       user   -- proxy configured app_name
	//       passwd -- proxy configured app_passwd
	// 2) Some parameters are no longer valid:
	//       db     -- not used, real connected db is configured in proxy_server side
	//       unix_socket -- not supported
	//       client_flag -- not supported
	// 3) Proxy API supports timeout_ms parameter, which this compat api do not allow passing, this parameter will be set to 0
	return mysql_proxy_connect(mysql, host, port, user, passwd, 0);
}

int mysql_proxy_query(MYSQL *mysql, const char *stmt_str);
static inline int mysql_query(MYSQL *mysql, const char *stmt_str) { return mysql_proxy_query(mysql, stmt_str); }

int mysql_proxy_real_query(MYSQL *mysql, const char *stmt_str, unsigned long length);
static inline int mysql_real_query(MYSQL *mysql, const char *stmt_str, unsigned long length) { return mysql_proxy_real_query(mysql, stmt_str, length); }

MYSQL_RES *mysql_proxy_store_result(MYSQL *mysql);
static inline MYSQL_RES *mysql_store_result(MYSQL *mysql) { return mysql_proxy_store_result(mysql); }
static inline MYSQL_RES *mysql_use_result(MYSQL *mysql) { return mysql_store_result(mysql); }

void mysql_proxy_free_result(MYSQL_RES *result);
static inline void mysql_free_result(MYSQL_RES *result) { mysql_proxy_free_result(result); }

my_ulonglong mysql_proxy_num_rows(MYSQL_RES *result);
static inline my_ulonglong mysql_num_rows(MYSQL_RES *result) { return mysql_proxy_num_rows(result); }

MYSQL_ROW mysql_proxy_fetch_row(MYSQL_RES *result);
static inline MYSQL_ROW mysql_fetch_row(MYSQL_RES *result) { return mysql_proxy_fetch_row(result); }

unsigned int mysql_proxy_num_fields(MYSQL_RES *result);
static inline unsigned int mysql_num_fields(MYSQL_RES *result) { return mysql_proxy_num_fields(result); }

MYSQL_FIELD *mysql_proxy_fetch_field(MYSQL_RES *result);
static inline MYSQL_FIELD *mysql_fetch_field(MYSQL_RES *result) { return mysql_proxy_fetch_field(result); }

MYSQL_FIELD *mysql_proxy_fetch_fields(MYSQL_RES *result);
static inline MYSQL_FIELD *mysql_fetch_fields(MYSQL_RES *result) { return mysql_proxy_fetch_fields(result); }

unsigned long *mysql_proxy_fetch_lengths(MYSQL_RES *result);
static inline unsigned long *mysql_fetch_lengths(MYSQL_RES *result) { return mysql_proxy_fetch_lengths(result); }

static inline int mysql_set_character_set(MYSQL *mysql, const char *name)
{
	mysql->errnum = ENOSYS;
	snprintf(mysql->errmsg, sizeof(mysql->errmsg), "Please set MYSQL_CHARSET in proxy server config file.");
	return -1;
}

static inline my_ulonglong mysql_affected_rows(MYSQL *mysql)
{
	mysql->errnum = ENOSYS;
	snprintf(mysql->errmsg, sizeof(mysql->errmsg), "This API is not supported by proxy server.");
	return (my_ulonglong)-1;
}

static inline my_ulonglong mysql_insert_id(MYSQL *mysql)
{
	mysql->errnum = ENOSYS;
	snprintf(mysql->errmsg, sizeof(mysql->errmsg), "This API is not supported by proxy server.");
	return (my_ulonglong)-1;
}

#ifdef __cplusplus
}
#endif

#endif //#ifndef __MYSQL_PROXY_API__

