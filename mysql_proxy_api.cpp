#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <rpc/des_crypt.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <sstream>
#include <iostream>
#include <fstream>
#include "mysql_proxy_api.h"
#include "knv_node.h"

using namespace std;
static int g_errnum;
string g_errmsg;

static inline string tostr(uint64_t i)
{
	char s[64];
	sprintf(s, "%llu", (unsigned long long)i);
	return s;
}

static int connect_in_time(int fd, const struct sockaddr *addr, socklen_t addrlen, int timeout_ms)
{
	if(timeout_ms <=0)
		return connect(fd, addr, addrlen);

	// set socket to nonblock mode
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL)|O_NONBLOCK);

	int ret = connect(fd, addr, addrlen);
	if(ret==0)
		return 0;
	
	if(errno!=EINPROGRESS)
	{
		return -1;
	}

	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	struct timeval tv_timeout;
	tv_timeout.tv_sec = timeout_ms/1000;
	tv_timeout.tv_usec = (timeout_ms%1000)*1000;

	ret = select(fd+1, NULL, &fds, NULL, &tv_timeout);
	if(ret>0)
	{
		if(FD_ISSET(fd, &fds))
		{
			int err = 0;
			socklen_t errlen = sizeof(err);
			if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) == -1)
			{
				return -1;
			}

			if(err)
			{
				errno = err;
				return -1;
			}
			return 0;
		}
		else
		{
			errno = ETIMEDOUT;
			return -1;
		}
	}
	return ret;
}

static int send_in_time(int fd, const char *data, int len, int timeout_ms)
{
	if(timeout_ms<=0)
		return send(fd, data, len, 0);

	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	struct timeval tv_timeout;
	tv_timeout.tv_sec = timeout_ms/1000;
	tv_timeout.tv_usec = (timeout_ms%1000)*1000;

	int ret = select(fd+1, NULL, &fds, NULL, &tv_timeout);
	if(ret>0)
	{
		if(FD_ISSET(fd, &fds))
		{
			return send(fd, data, len, 0);
		}
		else
		{
			ret = 0;
		}
	}
	return ret;
}


static int recv_in_time(int fd, char *buffer, int size, int timeout_ms)
{
	if(timeout_ms<=0)
		return recv(fd, buffer, size, 0);

	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	struct timeval tv_timeout;
	tv_timeout.tv_sec = timeout_ms/1000;
	tv_timeout.tv_usec = (timeout_ms%1000)*1000;

	int ret = select(fd+1, &fds, NULL, NULL, &tv_timeout);
	if(ret>0)
	{
		if(FD_ISSET(fd, &fds))
		{
			return recv(fd, buffer, size, 0);
		}
		else
		{
			ret = 0;
		}
	}
	return ret;
}

static int CreateConnection(const char *ip, int port, int timeout_ms)
{
	struct sockaddr_in stAddr;
	int iListenSocket;
	int iReuseAddr = 1;

	if(port==0)
		port = MYSQL_PROXY_DEFAULT_PORT;

	/* Setup internet address information.  
	This is used with the bind() call */
	memset((char *) &stAddr, 0, sizeof(stAddr));

	stAddr.sin_family = AF_INET;
	stAddr.sin_port = htons(port);
	stAddr.sin_addr.s_addr = inet_addr(ip);

	iListenSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (iListenSocket < 0) {
		g_errnum = CR_SOCKET_CREATE_ERROR;
		g_errmsg ="socket failed: ";
		g_errmsg += strerror(errno);
		return -1;
	}

	setsockopt(iListenSocket, SOL_SOCKET, SO_REUSEADDR, &iReuseAddr, 
		sizeof(iReuseAddr));

	if (connect_in_time(iListenSocket, (struct sockaddr *) &stAddr, sizeof(stAddr), timeout_ms) < 0) {
		g_errnum = CR_CONNECTION_ERROR;
		g_errmsg ="connecting to ";
		g_errmsg += ip;
		g_errmsg += ":";
		g_errmsg += tostr(port);
		g_errmsg += "failed: ";
		g_errmsg += strerror(errno);
		close(iListenSocket);
		return -1;
	}

	return iListenSocket;
}


#define PACKET_START_TOKEN	"@MyPROXY"
#define PACKET_HEADER_LENGTH	16
#define MAX_RESPONSE_PACKET_LENGTH	(1024*1024*500)

/*
 * struct ProxyPacket
 * {
 *	uint64_t start_token; // '@MyPROXY'
 *	uint64_t length; // including start_token
 *	pb_message Request
 *	{
 *		string appname = 1;
 *		uint64 sequence = 2;
 *		uint32 errorno = 3;
 *		string content = 11;
 *	}
 *	Request request;
 * };
 */

struct ProxyPacket
{
	std::string appname;
	std::string appkey;
	uint64_t sequence;
	uint32_t errnum;
	std::string content;
};


/*
 * Turn password into DES key
 */
static void passwd2des(char *pw, char *key)
{
 	int i;

	memset (key, 0, 8);
	for (i = 0; *pw; ++i)
		key[i%8] ^= *pw++ << 1;

	des_setparity (key);
}

#define max_encrypt_length 512

static string Encrypt(const string &key, const string &content)
{
	uint32_t length = content.length();
	string source = string((char*)&length, sizeof(uint32_t)) + content;
	if(source.length()&0x7)
	{
		static char spaces[] = "        ";
		source.append(spaces, 8-source.length()%8);
	}

	char data[max_encrypt_length];
	length = source.length();
	string result;

	while(length > 0)
	{
		int left = 0;
		if(length > max_encrypt_length)
		{
			left = length - max_encrypt_length;
			length = max_encrypt_length;
		}
		memcpy(data, source.data()+result.length(), length);
		int iRet = ecb_crypt((char*)key.c_str(), data, length, DES_ENCRYPT);
		if(DES_FAILED(iRet))
		{
			Attr_API(2977140, 1);
			return string();
		}
		result.append(data, length);
		length = left;
	}
	return result;
}

static string Decrypt(const string &key, const string &content)
{
	char data[max_encrypt_length];
	int length = content.length();
	string result;

	if(content.length() < sizeof(uint32_t))
	{
		Attr_API(2977142, 1);
		return string();
	}

	while(length > 0)
	{
		if(length > max_encrypt_length)
		{
			length = max_encrypt_length;
		}
		memcpy(data, content.data()+result.length(), length);
		int iRet = ecb_crypt((char*)key.c_str(), data, length, DES_DECRYPT);
		if(DES_FAILED(iRet))
		{
			Attr_API(2977141, 1);
			return string();
		}
		result.append(data, length);
		length = content.length() - result.length();
	}
	if(result.length()>sizeof(uint32_t))
	{
		length = *(uint32_t*)result.data();
		if(length+sizeof(uint32_t)<=result.length())
		{
			return result.substr(sizeof(uint32_t), length);
		}
	}
	Attr_API(2977143, 1);
	return string();
}

static int SendRequest(MYSQL *mysql, const char *sql, unsigned long length)
{
	string content = Encrypt(string(mysql->appkey, sizeof(mysql->appkey)), string(sql, length));

	KnvNode *tree = KnvNode::NewTree(1);
	if(tree==NULL)
	{
		mysql->errnum = CR_OUT_OF_MEMORY;
		snprintf(mysql->errmsg, sizeof(mysql->errmsg), 
			"KnvNode::NewTree() failed: %s\n", KnvNode::GetGlobalErrorMsg());
		return -1;
	}
	tree->SetFieldStr(1, strlen(mysql->appname), mysql->appname);
	tree->SetFieldInt(2, mysql->sequence++);
	tree->SetFieldInt(3, 0);
	tree->SetFieldStr(11, content.length(), content.data());

	int len = tree->EvaluateSize();
	char *rsp = new char[PACKET_HEADER_LENGTH + len];
	memcpy(rsp, PACKET_START_TOKEN, 8);
	int ret = tree->Serialize(rsp+PACKET_HEADER_LENGTH, len, false);
	if(ret<0)
	{
		mysql->errnum = CR_OUT_OF_MEMORY;
		snprintf(mysql->errmsg, sizeof(mysql->errmsg), "KnvNode::Serialize() failed: %s\n", tree->GetErrorMsg());
		KnvNode::Delete(tree);
		delete []rsp;
		return -1;
	}
	KnvNode::Delete(tree);
	len += PACKET_HEADER_LENGTH;
	((uint64_t*)rsp)[1] = len;
	int iRet = send_in_time(mysql->sockfd, rsp, len, mysql->timeout_ms);
	delete []rsp;
	if(iRet<0)
	{
		mysql->errnum = CR_SERVER_GONE_ERROR;
		snprintf(mysql->errmsg, sizeof(mysql->errmsg), "Server gone away, errno:%s", strerror(errno));
		return -1;
	}
	return 0;
}

static string StrToHex(const string &content)
{
	char buf[content.length()*2+2];
	for(size_t i=0; i<content.length(); i++)
		sprintf(buf+i*2, "%02X", content[i]);
	return string(buf, content.length()*2);
}

int ProcessResponse(MYSQL *mysql, ProxyPacket &stResponse)
{
	if(stResponse.errnum)
	{
		mysql->errnum = stResponse.errnum;
		snprintf(mysql->errmsg, sizeof(mysql->errmsg), "Server response with error msg: %s.\n", stResponse.content.c_str());
		return -1;
	}
	string rspstr = Decrypt(string(mysql->appkey, sizeof(mysql->appkey)), stResponse.content);
	if(rspstr.length()==0)
	{
		mysql->errnum = CR_MALFORMED_PACKET;
		snprintf(mysql->errmsg, sizeof(mysql->errmsg), "Decrypt rsp failed, rsp(length=%lu):%s.\n", 
			stResponse.content.length(), stResponse.content.length()>20? StrToHex(stResponse.content.substr(0,20)+"..").c_str():StrToHex(stResponse.content).c_str());
		return -2;
	}

	KnvNode *tree = KnvNode::NewFromMessage(rspstr);
	if(mysql->current_query_context)
	{
		KnvNode::Delete((KnvNode*)mysql->current_query_context);
	}
	*(KnvNode**)&mysql->current_query_context = tree;
	return 1;
}

int ParsePacket(MYSQL *mysql, string &reqstr)
{
	char *packet = (char*)reqstr.data();
	char *begpack = packet;
	char *endpack = packet + reqstr.length();

	while(packet!=endpack)
	{
		int skipped = 0;
		while(packet+PACKET_HEADER_LENGTH < endpack && memcmp(packet, PACKET_START_TOKEN, 8))
		{
			skipped ++;
			packet ++;
		}

		if(skipped)
		{
			Attr_API(2977503, 1);
			printf("Skipped %d invalid bytes when searching for start token, start_token is %sfound\n", skipped, (packet+PACKET_HEADER_LENGTH >= endpack)? "NOT ":"");
		}

		if(packet+PACKET_HEADER_LENGTH >= endpack)
			break;

		uint64_t packlen = ((uint64_t*)packet)[1];
		if(packet+packlen > endpack)
		{
			if(packlen > MAX_RESPONSE_PACKET_LENGTH)
			{
				Attr_API(2977504, 1);
				printf("Recv response (length=%d) exceeds hard limit of %d bytes.\n", (int)packlen, MAX_RESPONSE_PACKET_LENGTH);
				packet ++;
				continue;
			}
			break;
		}

		//printf("Recv packet with length %d\n", packlen);
		KnvNode *tree = KnvNode::NewFromMessage(string(packet+PACKET_HEADER_LENGTH, packlen-PACKET_HEADER_LENGTH));
		if(tree==NULL)
		{
			Attr_API(2973361, 1);
			printf("Recv bad packet, KnvNode::NewFromMessage() failed: %s\n", KnvNode::GetGlobalErrorMsg());
		}
		else
		{
			ProxyPacket stPacket;
			stPacket.appname = tree->GetFieldStr(1);
			stPacket.sequence = tree->GetFieldInt(2);
			stPacket.errnum = tree->GetFieldInt(3);
			stPacket.content = tree->GetFieldStr(11);
			KnvNode::Delete(tree);
			return ProcessResponse(mysql, stPacket);
		}
		packet += packlen;
	}
	if(packet!=begpack)
	{
		if(packet!=endpack)
			reqstr = string(packet, endpack - packet);
		else
			reqstr.clear();
	}
	return 0;
}


extern "C" MYSQL *mysql_proxy_init(MYSQL *mysql)
{
	if(mysql==NULL)
	{
		mysql = new MYSQL;
		memset(mysql, 0, sizeof(MYSQL));
		mysql->is_inner_created = true;
	}
	else
	{
		memset(mysql, 0, sizeof(MYSQL));
		mysql->is_inner_created = false;
	}
	mysql->sockfd = -1;
	return mysql;
}

extern "C" void mysql_proxy_close(MYSQL *mysql)
{
	if(mysql==NULL)
		return;

	close(mysql->sockfd);

	if(mysql->is_inner_created)
	{
		delete mysql;
	}
}

extern "C" unsigned int mysql_proxy_errno(MYSQL *mysql)
{
	return mysql? mysql->errnum : g_errnum;
}

extern "C" const char *mysql_proxy_error(MYSQL *mysql)
{
	return mysql? mysql->errmsg : g_errmsg.c_str();
}

extern "C" MYSQL *mysql_proxy_connect(MYSQL *mysql, const char *host, int port, const char *appname, const char *apppwd, int timeout)
{
	MYSQL *my = mysql;
	if(my==NULL)
		my = mysql_init(NULL);

	my->sockfd = CreateConnection(host, port, timeout);
	if(my->sockfd < 0)
	{
		if(mysql)
		{
			mysql->errnum = g_errnum;
			strncpy(mysql->errmsg, g_errmsg.c_str(), sizeof(mysql->errmsg)-1);
			mysql->errmsg[sizeof(mysql->errmsg)-1] = 0;
		}
		else
		{
			mysql_close(my);
		}
		return NULL;
	}

	passwd2des((char*)apppwd, my->appkey);
	strncpy(my->appname, appname, sizeof(my->appname)-1);
	my->sequence = 1;
	my->timeout_ms = timeout;

	string k;
	k.assign(my->appkey, 8);

	return my;
}

extern "C" int mysql_proxy_query(MYSQL *mysql, const char *stmt_str)
{
	return mysql_proxy_real_query(mysql, stmt_str, strlen(stmt_str));
}

extern "C" int mysql_proxy_real_query(MYSQL *mysql, const char *stmt_str, unsigned long length)
{
	int iRet = SendRequest(mysql, stmt_str, length);
	if(iRet<0)
		return iRet-100;

	if(mysql->current_query_context)
	{
		KnvNode::Delete((KnvNode*)mysql->current_query_context);
		mysql->current_query_context = NULL;
	}

	int len;
	string rspstr;
	char buffer[65536];
	int timeout = mysql->timeout_ms;

	while((len=recv_in_time(mysql->sockfd, buffer, sizeof(buffer), timeout)) > 0)
	{
		rspstr.append(buffer, len);
		if((iRet=ParsePacket(mysql, rspstr))!=0)
		{
			if(iRet > 0) // success
				return 0;
			else // error occurs
				return iRet-200;
		}
	}

	mysql->errnum = CR_SERVER_GONE_ERROR;
	if(len<0)
		snprintf(mysql->errmsg, sizeof(mysql->errmsg), "Server gone away, errno:%s", strerror(errno));
	else if(timeout)
		snprintf(mysql->errmsg, sizeof(mysql->errmsg), "Recv response timeout");
	else
		snprintf(mysql->errmsg, sizeof(mysql->errmsg), "Peer has shutdown");
	return -300;
}


struct MYSQL_PROXY_RES
{
	KnvNode *datatree;
	KnvNode *current_row; // iterator for mysql_fetch_row()
	KnvNode *current_field; // iterator for mysql_fetch_field()

	// tmp buffer for mysql_fetch_row()
	unsigned long current_row_field_num;
	MYSQL_ROW current_row_fields;

	// tmp buffer for mysql_fetch_lengths()
	unsigned long *current_row_lengths;

	// tmp buffer for mysql_fetch_fields()
	MYSQL_FIELD *all_fields;

	MYSQL_FIELD current_field_val; // tmp buffer for mysql_fetch_field()
};

extern "C" MYSQL_RES *mysql_proxy_store_result(MYSQL *mysql)
{
	if(mysql->current_query_context)
	{
	//	((KnvNode*)mysql->current_query_context)->Print("----");
		MYSQL_PROXY_RES *res = new MYSQL_PROXY_RES;
		memset(res, 0, sizeof(MYSQL_PROXY_RES));
		res->datatree = (KnvNode*)mysql->current_query_context;
		mysql->current_query_context = NULL;
		return res;
	}
	else
	{
		mysql->errnum = CR_NO_DATA;
		snprintf(mysql->errmsg, sizeof(mysql->errmsg), "No record data available");
		return NULL;
	}
}

static void mysql_free_result_resource(MYSQL_PROXY_RES *result, bool free_row = true, bool free_lengths = true, bool free_fields = true)
{
	if(result)
	{
		if(result->current_row_fields && free_row)
		{
			for(unsigned long i=0; i<result->current_row_field_num; i++)
			{
				if(result->current_row_fields[i])
					delete [](result->current_row_fields[i]);
			}
			delete []result->current_row_fields;
			result->current_row_field_num = 0;
			result->current_row_fields = NULL;
		}
		if(result->current_row_lengths && free_lengths)
		{
			delete []result->current_row_lengths;
			result->current_row_lengths = NULL;
		}
		if(result->all_fields && free_fields)
		{
			delete []result->all_fields;
			result->all_fields = NULL;
		}
	}
}

extern "C" void mysql_proxy_free_result(MYSQL_RES *res)
{
	MYSQL_PROXY_RES *result = (MYSQL_PROXY_RES *)res;
	if(result)
	{
		mysql_free_result_resource(result);
		if(result->datatree)
		{
			KnvNode::Delete(result->datatree);
		}
		delete result;
	}
}


/*
 * message ReqResult
 * {
 * 	string key = 1;        // query sql statement
 * 	uint32 time = 2;       // query time
 * 	uint32 num_rows = 3;   // mysql_num_rows()
 * 	uint32 num_fields = 4; // mysql_num_fields()
 * 
 * 	message Fields
 * 	{
 * 		string key = 1; // "fields"
 * 		message Field
 * 		{
 * 			string name = 1;
 * 
 * 			// future fied info can be added below
 * 		};
 * 		repeated Field field = 11;
 * 	};
 * 	Fields fields = 11;
 * 
 * 	message Row
 * 	{
 * 		string key = 1; // "row <i>" i = {0,1,2...}
 * 		optional string field_0 = 11; // field_0
 * 		optional string field_1 = 12; // field_1
 * 		// ... ...
 * 	};
 *
 * 	// for KNV to work more efficently, we split data rows into sets
 *	// each set contains up to 10000 rows of data
 * 	message Sets
 * 	{
 * 		string key = 1; // "set_<i>" i= {0,1,2...}
 * 		repeated Row row = 11; // set_0, row 0-9999, set_1, row 10000-19999, ...
 * 	};
 * 
 * 	repeated Sets sets = 12;
 * };
 */

#define KNV_FIELDLIST_TAG       	11
#define KNV_FIELDLIST_FIELDS_TAG	11
#define KNV_SETS_TAG            	12
#define KNV_ROWS_TAG                	11
#define KNV_ROWS_FIELD_START_TAG	11
#define KNV_ROW_NUM_TAG         	3
#define KNV_FIELD_NUM_TAG       	4

extern "C" my_ulonglong mysql_proxy_num_rows(MYSQL_RES *res)
{
	MYSQL_PROXY_RES *result = (MYSQL_PROXY_RES *)res;
	if(result && result->datatree)
		return result->datatree->GetFieldInt(KNV_ROW_NUM_TAG);
	return 0;
}

static MYSQL_ROW FetchRow(MYSQL_PROXY_RES *result)
{
	if(result->current_row)
	{
		// free row
		mysql_free_result_resource(result, true, false, false);

		result->current_row_field_num = mysql_num_fields(result);
		if(result->current_row_field_num==0)
			return NULL;

		result->current_row_fields = new char *[result->current_row_field_num];
		for(unsigned int i=0; i<result->current_row_field_num; i++)
		{
			KnvNode *n = result->current_row->GetField(KNV_ROWS_FIELD_START_TAG+i);
			if(n)
			{
				string val = n->GetStrVal();
				result->current_row_fields[i] = new char[val.length()+1];
				memcpy(result->current_row_fields[i], val.data(), val.length());
				result->current_row_fields[i][val.length()] = 0;
			}
			else
			{
				result->current_row_fields[i] = NULL;
			}
		}
		return result->current_row_fields;
	}
	return NULL;
}

extern "C" MYSQL_ROW mysql_proxy_fetch_row(MYSQL_RES *res)
{
	MYSQL_PROXY_RES *result = (MYSQL_PROXY_RES *)res;
	if(result && result->datatree)
	{
		if(result->current_row==NULL)
		{
			KnvNode *set_tree = result->datatree->GetFirstChild();
			while(set_tree)
			{
				if(set_tree->GetTag()!=KNV_SETS_TAG)
				{
					set_tree = set_tree->GetSibling();
					continue;
				}
				result->current_row = set_tree->GetFirstChild();
				if(result->current_row==NULL)
				{
					Attr_API(2981673, 1); // bug: set has no row
					set_tree = set_tree->GetSibling();
					continue;
				}
				break;
			}
		}
		else
		{
			KnvNode *next_row = result->current_row->GetSibling();
			if(next_row==NULL)
			{
				//
				// find in next set
				//
				KnvNode *set_tree = result->current_row->GetParent();
				result->current_row = NULL;
	
				if(set_tree)
				{
					KnvNode *next_set_tree = set_tree->GetSibling();
					set_tree->Remove(); // remove from data tree, should not be used any more

					while(next_set_tree)
					{
						if(next_set_tree->GetTag()!=KNV_SETS_TAG)
						{
							next_set_tree = next_set_tree->GetSibling();
							continue;
						}

						result->current_row = next_set_tree->GetFirstChild();
						if(result->current_row==NULL)
						{
							Attr_API(2981673, 1); // bug: set has no row
							next_set_tree = next_set_tree->GetSibling();
							continue;
						}
						break;
					}
				}
				else
				{
					Attr_API(2981672, 1); // bug: row_tree has no parent of set_tree
				}
			}
			else
			{
				result->current_row = next_row;
			}
		}

		return FetchRow(result);
	}
	return NULL;
}

extern "C" unsigned int mysql_proxy_num_fields(MYSQL_RES *res)
{
	MYSQL_PROXY_RES *result = (MYSQL_PROXY_RES *)res;
	if(result && result->datatree)
		return result->datatree->GetFieldInt(KNV_FIELD_NUM_TAG);
	return 0;
}

static MYSQL_FIELD *FetchField(MYSQL_PROXY_RES *result)
{
	if(result->current_field)
	{
		string name = result->current_field->GetFieldStr(1);
		result->current_field_val.name_length = name.length();
		strncpy(result->current_field_val.name, name.c_str(), sizeof(result->current_field_val.name));
		result->current_field_val.name[sizeof(result->current_field_val.name)-1] = 0;
		return &result->current_field_val;
	}
	return NULL;
}

extern "C" MYSQL_FIELD *mysql_proxy_fetch_field(MYSQL_RES *res)
{
	MYSQL_PROXY_RES *result = (MYSQL_PROXY_RES *)res;
	if(result && result->datatree)
	{
		if(result->current_field==NULL)
		{
			KnvNode *fieldtree = result->datatree->GetField(KNV_FIELDLIST_TAG);
			if(fieldtree == NULL)
			{
				return NULL;
			}
			result->current_field = fieldtree->GetFirstChild();
		}
		else
		{
			result->current_field = result->current_field->GetSibling();
		}
		return FetchField(result);
	}
	return NULL;
}

extern "C" MYSQL_FIELD *mysql_proxy_fetch_fields(MYSQL_RES *res)
{
	MYSQL_PROXY_RES *result = (MYSQL_PROXY_RES *)res;
	if(result)
	{
		MYSQL_FIELD *field;

		unsigned long fieldnum = mysql_num_fields(result);
		if(fieldnum <=0)
			return NULL;

		mysql_free_result_resource(result, false, false, true);
		result->all_fields = new MYSQL_FIELD[fieldnum];
		unsigned long i = 0;
		result->current_field = NULL;
		while((field=mysql_fetch_field(result)))
		{
			result->all_fields[i++] = *field;
		}
		return result->all_fields;
	}
	return NULL;
}

extern "C" unsigned long *mysql_proxy_fetch_lengths(MYSQL_RES *res)
{
	MYSQL_PROXY_RES *result = (MYSQL_PROXY_RES *)res;
	if(result && result->current_row)
	{
		if(result->current_row_lengths)
		{
			delete []result->current_row_lengths;
			result->current_row_lengths = NULL;
		}
		unsigned int num_fields = mysql_num_fields(result);
		if(num_fields <= 0)
		{
			return NULL;
		}

		result->current_row_lengths = new unsigned long[num_fields];
		for(unsigned int i=0; i<num_fields; i++)
		{
			string s = result->current_row->GetFieldStr(KNV_ROWS_FIELD_START_TAG+i);
			result->current_row_lengths[i] = s.length();
		}
		return result->current_row_lengths;
	}
	return NULL;
}


