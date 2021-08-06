#include <stdlib.h>
#include <mysql.h>
#include <mysql/errmsg.h>
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
#include <queue>
#include "oi_log.h"
#include "oi_cfg.h"
#include "Attr_API.h"
#include "atomic_counter_array.h"
#include "storage.h"
#include "fork_children.h"

#define MYSQL_PROXY_DEFAULT_PORT	34765
#define MYSQL_PROXY_DEFAULT_SPORT	"34765"

//error
#define LOG_ERROR(fmt, args...) do { if (stConfig.iLogLevel >= _LOG_ERROR_) Log(&(stConfig.stLog), 2, "[ERR][PID_%d]%s:%d(%s):" fmt, stConfig.pid, __FILE__, __LINE__, __FUNCTION__ , ## args);} while (0)
//warning
#define LOG_WARNING(fmt, args...) do { if (stConfig.iLogLevel >= _LOG_WARNING_) Log(&(stConfig.stLog), 2, "[WARNING][PID_%d]%s:%d(%s):" fmt, stConfig.pid, __FILE__, __LINE__, __FUNCTION__ , ## args);} while (0)
//info
#define LOG_INFO(fmt, args...) do { if (stConfig.iLogLevel >= _LOG_INFO_) Log(&(stConfig.stLog), 2, "[INFO][PID_%d]%s:%d(%s):" fmt, stConfig.pid, __FILE__, __LINE__, __FUNCTION__ , ## args);} while (0)
//debug
#define LOG_DEBUG(fmt, args...) do { if (stConfig.iLogLevel >= _LOG_DEBUG_) Log(&(stConfig.stLog), 2, "[DEBUG][PID_%d]%s:%d(%s):" fmt, stConfig.pid, __FILE__, __LINE__, __FUNCTION__ , ## args);} while (0)

static struct CONFIG
{
	char sDBIP[30] ;
	char sDBUser[30];
	char sDbPwd[20];
	char sDBName[20];
	int iLogLevel;
	LogFile stLog;
	char sLogFilePath[256];
	char sServerIP[128];
	char sServerPort[32];
	char sCharset[32];

	time_t lCurrentTime;
	time_t lLastGetTime;
	time_t lForkTime;
	atomic_counter  stSendBytesCounter;
	int iSendBytesLimit;
	int iLoopWaitTime;
	int iMinInterval;
	int iEnaTraficCtrl;
	int iShmFd;
	int iShmKey;
	int iShmMaxKeyLen;
	int iCacheValidSeconds;
	pid_t pid;

	map<string, string> mapAppKeys; // app name to key mappings
} stConfig;

template<typename T> class ConcurrentQueue : public std::queue<T>
{
public:
	ConcurrentQueue<T>():queue<T>()
	{
		pthread_mutex_init(&mutex, NULL);
	}

	~ConcurrentQueue<T>()
	{
		pthread_mutex_destroy(&mutex);
	}

	void Put(T &t)
	{
		pthread_mutex_lock(&mutex);
		queue<T>::push(t);
		pthread_mutex_unlock(&mutex);
	}

	bool Get(T &t)
	{
		bool result = false;
		pthread_mutex_lock(&mutex);
		if(queue<T>::size())
		{
			t = queue<T>::front();
			queue<T>::pop();
			result = true;
		}
		pthread_mutex_unlock(&mutex);
		return result;
	}

private:
	pthread_mutex_t mutex;
};

ConcurrentQueue<MYSQL *> vstMySQLPool;
string sMySQLError;

#define PACKET_START_TOKEN	"@MyPROXY"
#define PACKET_HEADER_LENGTH	16
#define MAX_REQUEST_PACKET_LENGTH	(4096+PACKET_HEADER_LENGTH)

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
	string appname;
	string appkey;
	uint64_t sequence;
	uint32_t errnum;
	string content;
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


static string StrToHex(const string &content)
{
	char buf[content.length()*2+2];
	for(size_t i=0; i<content.length(); i++)
		sprintf(buf+i*2, "%02X", content[i]);
	return string(buf, content.length()*2);
}

static string HexToStr(const string &hex)
{
	size_t newlen = hex.length()/2;
	char buf[newlen];
	for(size_t i=0; i<newlen; i++)
		buf[i] = strtoul(hex.substr(i*2, 2).c_str(), NULL, 16);
	return string(buf, newlen);
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

	if(length < sizeof(uint32_t))
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
	LOG_ERROR("Decrypt successful, but length (%d) is incorrect.", result.length());
	return string();
}

static void SendResponse(int sockfd, ProxyPacket &stRequest, int errornum, const string &content)
{
	ProxyPacket stResponse = stRequest;
	stResponse.errnum = errornum;
	if(errornum==0)
	{
		stResponse.content = Encrypt(stResponse.appkey, content);
		if(stResponse.content.length()==0 && content.length())
		{
			stResponse.errnum = 2;
			stResponse.content = "Encryption failed";
		}
	}
	else
	{
		stResponse.content = content;
	}

	KnvNode *tree = KnvNode::NewTree(1);
	if(tree==NULL)
	{
		Attr_API(2973314, 1);
		LOG_ERROR("KnvNode::NewTree() failed: %s", KnvNode::GetGlobalErrorMsg());
		return;
	}
	tree->SetFieldStr(1, stResponse.appname.length(), stResponse.appname.data());
	tree->SetFieldInt(2, stResponse.sequence);
	tree->SetFieldInt(3, stResponse.errnum);
	tree->SetFieldStr(11, stResponse.content.length(), stResponse.content.data());

	int len = tree->EvaluateSize();
	char *rsp = new char[PACKET_HEADER_LENGTH + len];
	memcpy(rsp, PACKET_START_TOKEN, 8);
	int ret = tree->Serialize(rsp+PACKET_HEADER_LENGTH, len, false);
	if(ret<0)
	{
		Attr_API(2973316, 1);
		LOG_ERROR("KnvNode::Serialize() failed: %s", tree->GetErrorMsg());
		KnvNode::Delete(tree);
		delete []rsp;
		return;
	}
	KnvNode::Delete(tree);
	len += PACKET_HEADER_LENGTH;
	((uint64_t*)rsp)[1] = len;
	atomic_counter_array_add_current_slot(&(stConfig.stSendBytesCounter), (uint32_t)len);
	LOG_DEBUG("Sending response, packet length %d, original content length %d, encrypted content length %d.", len, content.length(), stResponse.content.length());
	if(send(sockfd, rsp, len, 0)<0)
	{
		Attr_API(2973317, 1);
		LOG_ERROR("Send response failed: errno=%d:%s", errno, strerror(errno));
	}
	delete []rsp;
}

static MYSQL *InitMySQL()
{
	//connect to db
	MYSQL *mysql = mysql_init(NULL);
	if(mysql==NULL)
	{
		sMySQLError = "mysql_init failed: ";
		sMySQLError += mysql_error(NULL);
		printf("%s\n", sMySQLError.c_str());
		return NULL;
	}

	if (mysql_real_connect(mysql, stConfig.sDBIP, stConfig.sDBUser, stConfig.sDbPwd, stConfig.sDBName, 0, NULL, 0) == NULL) 
	{
		sMySQLError = "mysql_real_connect failed: ";
		sMySQLError += mysql_error(mysql);
		printf("%s\n", sMySQLError.c_str());
		mysql_close(mysql);
		return NULL;
	}

	if(stConfig.sCharset[0])
		mysql_set_character_set(mysql, stConfig.sCharset);
	return mysql;
}



static MYSQL *AllocMySQL()
{
	Attr_API(2974016, 1);
	MYSQL *mysql = NULL;
	if(vstMySQLPool.Get(mysql)) // use recycled fd
	{
		// report fd_list size
		int iNum = 0;
		Attr_API_Get(2973318, &iNum);
		if(vstMySQLPool.size()>iNum)
			Attr_API_Set(2973318, vstMySQLPool.size());
		if(mysql)
			return mysql;
	}
	return InitMySQL();
}

static int FreeMySQL(MYSQL *mysql)
{
	Attr_API(2974017, 1);
	if(mysql)
	{
		vstMySQLPool.Put(mysql);
	}
}

static int RealFreeMySQL(MYSQL *mysql)
{
	Attr_API(2974018, 1);
	mysql_close(mysql);
}

static int CreateSockAddr(int socket_type, char* sIP, char* sPort, int* iListener)
{
	struct sockaddr_in stAddr;
	int iListenSocket;
	int iReuseAddr = 1;
	int iPort = atoi(sPort);
	if(iPort==0)
		iPort = MYSQL_PROXY_DEFAULT_PORT;

	/* Setup internet address information.  
	This is used with the bind() call */
	memset((char *) &stAddr, 0, sizeof(stAddr));

	stAddr.sin_family = AF_INET;
	stAddr.sin_port = htons(atoi(sPort));
	stAddr.sin_addr.s_addr = inet_addr(sIP);

	iListenSocket = socket(AF_INET, socket_type, 0);
	if (iListenSocket < 0) {
		perror("socket");
		return -1;
	}

	if (iListener != NULL)
		*iListener = iListenSocket;

	setsockopt(iListenSocket, SOL_SOCKET, SO_REUSEADDR, &iReuseAddr, 
		sizeof(iReuseAddr));

	if (bind(iListenSocket, (struct sockaddr *) &stAddr, 
		sizeof(stAddr)) < 0) {
		perror("bind");
		close(iListenSocket);
		return -1;
	}

	if (socket_type == SOCK_STREAM) {
		listen(iListenSocket, 5); /* Queue up to five connections before
		having them automatically rejected. */
	}
	return iListenSocket;
}

int SetNBlock(int iSock)
{
	int iFlags;
	iFlags = fcntl(iSock, F_GETFL, 0);
	iFlags |= O_NONBLOCK;
	iFlags |= O_NDELAY;
	fcntl(iSock, F_SETFL, iFlags);
	return 0;
}

static int CreateListenSock()
{
	int iSock = CreateSockAddr(SOCK_STREAM, stConfig.sServerIP, stConfig.sServerPort, NULL);
	if(iSock < 0)
		return -1;

	int iOn = 1;
	int iRet = setsockopt(iSock, SOL_SOCKET, SO_KEEPALIVE, (char *)&iOn, sizeof(iOn));
	if(iRet == -1)
	{
		perror("setsockopt");
		close(iSock);
		return -1;
	}

	SetNBlock(iSock);
	return iSock;
}

int LoadKeyFile(const char *sKeyFile)
{
	try {
		printf("Loading key mapping from file %s.\n", sKeyFile);

		ifstream ifs(sKeyFile, std::ifstream::in);
		if(ifs.is_open())
		{
			char line[10240];
			while(1)
			{
				line[0] = 0;
				ifs.getline(line, sizeof(line));
				if(line[0])
				{
					char *pstr = line;
					while(isspace(*pstr))
						pstr ++;
					if(*pstr=='#')
					{
						continue;
					}
					string appname, appkey;
					while(*pstr && !isspace(*pstr) && *pstr!='#')
					{
						appname.append(string(pstr, 1));
						pstr ++;
					}
					if(*pstr==0 || *pstr=='#')
					{
						printf("Warning: app name %s has no key!\n", appname.c_str());
					}
					else
					{
						while(isspace(*pstr))
							pstr ++;
						while(*pstr && !isspace(*pstr) && *pstr!='#')
						{
							appkey.append(string(pstr, 1));
							pstr ++;
						}
					}
					printf("Got one app: %s - %s.\n", appname.c_str(), appkey.c_str());
					char key[8];
					passwd2des((char*)appkey.c_str(), key);
					stConfig.mapAppKeys[appname].assign(key, 8);
				}
				else
				{
					if(!ifs.good())
						break;
				}
			}
			return stConfig.mapAppKeys.size();
		}
		else
		{
			printf("Failed to open faile %s\n", sKeyFile);
			return -1;
		}
	}
	catch (ios_base::failure &f)
	{
		printf("Failed to open faile %s: %s\n", sKeyFile, f.what());
		return -1;
	}

}

int ProxyInit(int argc, char *argv[])
{
	char sConfFilePath[255]={0};

	if(argc > 2) 
	{
		printf("Usage: %s Config_file\n", argv[0]);
		return -1;
	}
	else if(argc == 2) 
	{
		strncpy(sConfFilePath, argv[1], sizeof(sConfFilePath)-1);
	} 
	else 
	{
		strncpy(sConfFilePath, "/home/oicq/mysql_proxy/conf/mysql_proxy_server.conf", sizeof(sConfFilePath)-1);
	}
	stConfig.iShmKey = 0x447755;
	int iShmPercentage = 20;
	int iShmBlockSize = 4096;
	stConfig.iShmMaxKeyLen = 128;
	int iChildNum = 1;
	stConfig.iCacheValidSeconds = 60;
	char sKeyFile[256];

	TLib_Cfg_GetConfig(sConfFilePath, 
	        "LOGFILE", CFG_STRING, stConfig.sLogFilePath, "/home/oicq/mysql_proxy/log/mysql_proxy_server", sizeof(stConfig.sLogFilePath),
	        "KEYFILE", CFG_STRING, sKeyFile, "/home/oicq/mysql_proxy/conf/key_file.txt", sizeof(sKeyFile),
	        "LOG_LEVEL", CFG_INT, &(stConfig.iLogLevel),5,
	        "SERVER_IP", CFG_STRING, stConfig.sServerIP, "0.0.0.0", sizeof(stConfig.sServerIP),
	        "SERVER_PORT", CFG_STRING, stConfig.sServerPort, MYSQL_PROXY_DEFAULT_SPORT,  sizeof(stConfig.sServerPort),
	        "MYSQL_HOST", CFG_STRING, stConfig.sDBIP, "localhost", sizeof(stConfig.sDBIP),
	        "MYSQL_USER", CFG_STRING, stConfig.sDBUser, "root", sizeof(stConfig.sDBUser),
	        "MYSQL_PASSWD", CFG_STRING, stConfig.sDbPwd, "", sizeof(stConfig.sDbPwd),
	        "MYSQL_DB", CFG_STRING, stConfig.sDBName, "mysql_test", sizeof(stConfig.sDBName),
	        "MYSQL_CHARSET", CFG_STRING, stConfig.sCharset, "utf8", sizeof(stConfig.sCharset),
	        "SEND_BYTES_LIMIT", CFG_INT, &(stConfig.iSendBytesLimit), 300000000,  //默认一秒可以发送300MByte配置
	        "MIN_INTERVAL",CFG_INT,&(stConfig.iMinInterval), 10,                //流量计算的最小时间间隔,1/10秒为单位
	        "ENA_TRAFIC_CTRL",CFG_INT,&(stConfig.iEnaTraficCtrl), 1,    //是否开启流量控制,默认开启
		"SHM_KEY", CFG_INT, &stConfig.iShmKey, stConfig.iShmKey,                   //Shm缓存的key
		"SHM_PERCENT", CFG_INT, &iShmPercentage, iShmPercentage, //Shm缓存占用系统内存的百分比
		"SHM_BLOCKSIZE", CFG_INT, &iShmBlockSize, iShmBlockSize, //Shm缓存分配的块大小
		"SHM_MAX_KEYLEN", CFG_INT, &stConfig.iShmMaxKeyLen, stConfig.iShmMaxKeyLen, //Shm哈希表存储的最大key长度
		"CHILD_NUM", CFG_INT, &iChildNum, iChildNum, //工作进程个数
		"CACHE_VALID_SECONDS", CFG_INT, &stConfig.iCacheValidSeconds, stConfig.iCacheValidSeconds, //Cache在shm中的有效时间
	        (void*)NULL
	        );

	if(LoadKeyFile(sKeyFile)<0)
		return -1;
	if(stConfig.mapAppKeys.size()==0)
	{
		printf("Key file is empty: %s, the server can not continue without app/key configs.\n", sKeyFile);
		return -1;
	}
	if(stConfig.iShmMaxKeyLen<128)
	{
		printf("SHM_MAX_KEYLEN of %d is too small, forced to %d.\n", stConfig.iShmMaxKeyLen, 128);
		stConfig.iShmMaxKeyLen = 128;
	}

	//SEND_BYTES_LIMIT做一下限制,免得配错了影响服务
	if(stConfig.iSendBytesLimit  < 10000000)
		stConfig.iSendBytesLimit = 10000000;
	if(stConfig.iSendBytesLimit > 500000000)
	    stConfig.iSendBytesLimit = 500000000;

	if(stConfig.iMinInterval > 50)  //计算流量的最小时间间隔是10ms,最大时间间隔是1分钟
		stConfig.iMinInterval = 50;

	printf("SHM_KEY:0x%X\n", (uint32_t)stConfig.iShmKey);
	printf("LOG_LEVEL:%d\n", stConfig.iLogLevel);
	printf("SEND_BYTES_LIMIT:%d\n", stConfig.iSendBytesLimit);
	printf("MIN_INTERVAL:%d\n", stConfig.iMinInterval);
	printf("SHM_PERCENT:%d\n", iShmPercentage);

	InitLogFile(&stConfig.stLog, stConfig.sLogFilePath, 0, 5, 5000000);

	stConfig.iShmFd = UcStorage::InitWrite(stConfig.iShmKey, iShmPercentage, stConfig.iShmMaxKeyLen, 20, 1, iShmBlockSize);
	if(stConfig.iShmFd<0)
	{
		printf("Shm init failed: %s\n", UcStorage::GetInitErrorMsg().c_str());
		return -1;
	}

	if (atomic_counter_array_init(&(stConfig.stSendBytesCounter), stConfig.iShmKey+1, 0) != 0)
	{
		printf("atomic_counter_init() failed!\n");
		return -1;
	}
	stConfig.stSendBytesCounter.ptr->cIdx = 0;

	int iListenFd = CreateListenSock();
	if (iListenFd < 0)
	{
		printf("CreateListenSock Fail: %d:%s\n", errno, strerror(errno));
		return -1;
	}

	//connect to db
	MYSQL *pstMySQL = InitMySQL();
	if(pstMySQL==NULL)
		return -1;

	if(iChildNum>1)
	{
		RealFreeMySQL(pstMySQL);
	}
	else // single child-mode, can be reuse
	{
		FreeMySQL(pstMySQL);
	}

	int iRet = ForkChildren(iChildNum);
	if(iRet<0)
	{
		printf("Fork children failed.\n");
		return -1;
	}

	stConfig.pid = getpid();

	return iListenFd;
}

static uint64_t TimeToUsec(const struct timeval *pval)
{
	uint64_t ret;
	if (pval == NULL) return 0;

	ret = pval->tv_sec;
	ret = ret * 1000000;
	ret += pval->tv_usec;
	return ret;
}

static uint64_t SubTime(struct timeval tv1, struct timeval tv2)
{
	return TimeToUsec(&tv1) - TimeToUsec(&tv2);
}

static int TrafficCtl(int iUTimePice)
{
	int iRet = 0, i = 0;
	static struct timeval tvTime[6] = {{0,0},{0,0},{0,0},{0,0},{0,0}};
	struct timeval tvNow;
	gettimeofday(&tvNow, NULL);
	uint32_t dwSendBytes = 0;
	uint64_t ullUsec = SubTime(tvNow, tvTime[stConfig.stSendBytesCounter.ptr->cIdx]);
	if(ullUsec >= 5000 * (uint64_t)1000000 * iUTimePice)
	{
		Attr_API(253031,1);
		memset(stConfig.stSendBytesCounter.ptr, 0, sizeof(atomic_counter_t)); //发生时间跳变时可能出现再次初始化，上报
		for(i = 0; i < 6; ++i)
		{
			tvTime[i] = tvNow;
		}
		return 0;
	}
	if(ullUsec > 1 * (uint64_t)100000 * iUTimePice) //与本时间片计数器进入时间超过一个时间片
	{
		//更新索引，更新时间戳
		atomic_counter_array_set_lastest_slot(&(stConfig.stSendBytesCounter), 0);
		stConfig.stSendBytesCounter.ptr->cIdx = (stConfig.stSendBytesCounter.ptr->cIdx + 5) % 6;
		tvTime[stConfig.stSendBytesCounter.ptr->cIdx] = tvNow;
	}
	ullUsec = SubTime(tvNow,tvTime[(stConfig.stSendBytesCounter.ptr->cIdx + 4)%6]); //开始计算流量，此处计算与4个时间片前的计数器时间戳的时间差
	if(ullUsec >= 1 * (uint64_t)100000 * iUTimePice)
	{
		iRet = atomic_counter_array_get_five_slots(&(stConfig.stSendBytesCounter), &dwSendBytes); 
	//	LOG_INFO("PId %d Send Speed:%llu Bytes/s [%u]:[%llu]\n", getpid(), dwSendBytes * (uint64_t)1000000 / ullUsec, dwSendBytes, ullUsec);
	//	int iSpeed = (int) dwSendBytes * (uint64_t)1000000 / ullUsec;
		if(dwSendBytes * (uint64_t)1000000 / ullUsec > (uint64_t)stConfig.iSendBytesLimit)
			return 1;
	}

	return 0;
}

void DoLoop()
{
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

#define max_row_num_for_each_set	10000

static KnvNode *PackResult(MYSQL_RES *result, knv_key_t &reqkey)
{
	KnvNode *tree = KnvNode::NewTree(1, &reqkey);
	if(tree==NULL)
	{
		Attr_API(2973314, 1);
		sMySQLError = "Create KNV tree failed: ";
		sMySQLError += KnvNode::GetGlobalErrorMsg();
		return NULL;
	}
	unsigned int num_rows = mysql_num_rows(result);
	unsigned int num_fields = mysql_num_fields(result);
	tree->SetFieldInt(2, time(NULL));
	tree->SetFieldInt(3, num_rows);
	tree->SetFieldInt(4, num_fields);

	MYSQL_FIELD *field;
	vector<string> fields;

	knv_key_t kfields((char*)"fields", 6);
	KnvNode *fieldstree = tree->InsertSubNode(11, &kfields);
	if(fieldstree==NULL)
	{
		Attr_API(2973315, 1);
		sMySQLError = "Add KNV node failed: ";
		sMySQLError += tree->GetErrorMsg();
		KnvNode::Delete(tree);
		return NULL;
	}

	while((field = mysql_fetch_field(result)))
	{
		knv_key_t kfield(field->name, field->name_length);
		KnvNode *fieldnode = fieldstree->InsertSubNode(11, &kfield);
		if(fieldnode==NULL)
		{
			Attr_API(2973315, 1);
			sMySQLError = "Add KNV node failed: ";
			sMySQLError += tree->GetErrorMsg();
			KnvNode::Delete(tree);
			return NULL;
		}
		fields.push_back(field->name);
	}

	MYSQL_ROW row;

	KnvNode *settree = NULL;

	int r = 0;
	while ((row = mysql_fetch_row(result))) 
	{
		char rowname[64];
		snprintf(rowname, sizeof(rowname), "row %d", r);

		if(settree==NULL)
		{
			char setname[64];
			snprintf(setname, sizeof(setname), "set %d", r/max_row_num_for_each_set);
			knv_key_t kset(setname, strlen(setname));
			settree = tree->InsertSubNode(12, &kset);
			if(settree==NULL)
			{
				Attr_API(2973315, 1);
				sMySQLError = "Add KNV node failed: ";
				sMySQLError += tree->GetErrorMsg();
				KnvNode::Delete(tree);
				return NULL;
			}
		}

		knv_key_t k(rowname, strlen(rowname));
		KnvNode *subtree = settree->InsertSubNode(11, &k);
		if(subtree==NULL)
		{
			Attr_API(2973315, 1);
			sMySQLError = "Add KNV node failed: ";
			sMySQLError += settree->GetErrorMsg();
			KnvNode::Delete(tree);
			return NULL;
		}
		unsigned long *lengths = mysql_fetch_lengths(result);
		for(int i = 0; i < num_fields; i++) 
		{
			if(row[i])
			{
				if(lengths)
					subtree->AddFieldStr(11+i, lengths[i], row[i]);
				else // mysql_fetch_lengths() failed
					subtree->AddFieldStr(11+i, strlen(row[i]), row[i]);
			}
		}
		if(r && (r%max_row_num_for_each_set==0))
		{
			settree->GetValue(); // Fold and delete children objects
			settree = NULL; // start a new set
		}
		r++;
	}

	//printf("done.\n");
	LOG_DEBUG("Rsp data: fields=%d/%d, rows=%d/%d, data_size=%d", num_fields, fields.size(), num_rows, r, tree->EvaluateSize());
	return tree;
}

static inline string GetReqHashKey(const char *reqstr, int length)
{
	if(length<stConfig.iShmMaxKeyLen-32)
		return string(reqstr, length);
	else
		return string(reqstr, stConfig.iShmMaxKeyLen-32);
}

void ProcessRequest(int sockfd, ProxyPacket &stRequest)
{
	string reqstr = Decrypt(stRequest.appkey, stRequest.content);
	if(reqstr.length()==0)
	{
		LOG_INFO("Recv bad request, length=%d.", stRequest.content.length());
		sMySQLError = "Unauthorized request";
		SendResponse(sockfd, stRequest, 1, sMySQLError);
		return;
	}

	LOG_DEBUG("Recv request: %s.", reqstr.c_str());

	// skip spaces
	const char *sqlstr = reqstr.c_str();
	const char *sql = sqlstr;
	while(*sql=='\n'||*sql=='\r'||isspace(*sql))
		sql ++;

	if(strncasecmp(sql, "select", 6) && strncasecmp(sql, "show", 4) && strncasecmp(sql, "desc", 4))
	{
		sMySQLError = "statement not starting with SELECT/SHOW/DESC is not supperted.";
		SendResponse(sockfd, stRequest, 1, sMySQLError);
		LOG_DEBUG("%s", sMySQLError.c_str());
		return;
	}

	int len = reqstr.length() - (sql-sqlstr);
	string keystr = GetReqHashKey(sql, len);
	knv_key_t key((char*)keystr.data(), keystr.length());
	UcStorage st(stConfig.iShmFd, key);

	KnvNode *tree = NULL;
	if(st.Read()>=0)
	{
		Attr_API(2974111, 1);
		LOG_DEBUG("Got data in cache.");
		tree = st.GetDataTree();
		time_t t = tree? tree->GetFieldInt(2) : 0;
		if(t==0 || t+stConfig.iCacheValidSeconds<time(NULL))
		{
			LOG_DEBUG("Data in cache is outdated.");
			Attr_API(2974110, 1);
			tree = NULL;
		}
	}
	else
	{
		LOG_DEBUG("Get data from cache failed: %s", st.GetErrorMsg().c_str());
	}

	int try_num = 0, max_try_num = 10;
	while(tree==NULL)
	{
		Attr_API(2974112, 1);
		LOG_DEBUG("Getting data from DB.\n");
		MYSQL *mysql = AllocMySQL();
		if(mysql==NULL)
		{
			Attr_API(2973330, 1);
			SendResponse(sockfd, stRequest, 1, sMySQLError);
			LOG_ERROR("AllocMySQL failed: %s", sMySQLError.c_str());
			return;
		}

		int iRet = mysql_query(mysql, sql);
		if(iRet<0)
		{
			Attr_API(2973331, 1);
			sMySQLError = "mysql_query failed: ";
			sMySQLError += mysql_error(mysql);
			int err = mysql_errno(mysql);
			LOG_DEBUG("%s, mysql_errno=%d", sMySQLError.c_str(), err);
			if(err>=ER_ERROR_FIRST && err<=ER_ERROR_LAST) // server side error, not relavant to socket
			{
				FreeMySQL(mysql);
			}
			else
			{
				RealFreeMySQL(mysql);
			}
			// if connection is closed by server, try again
			if((err==CR_SERVER_GONE_ERROR || err==CR_SERVER_LOST ||
				err==CR_CONNECTION_ERROR || err==CR_CONN_HOST_ERROR ||
				err==CR_LOCALHOST_CONNECTION || err==CR_TCP_CONNECTION) &&
				try_num < max_try_num)
			{
				try_num ++;
				continue;
			}
			SendResponse(sockfd, stRequest, err, sMySQLError);
			return;
		}

		MYSQL_RES *result = mysql_store_result(mysql);
		if (result == NULL) 
		{
			Attr_API(2973335, 1);
			sMySQLError = "mysql_store_result failed: ";
			sMySQLError += mysql_error(mysql);
			int err = mysql_errno(mysql);
			LOG_DEBUG("%s, mysql_errno=%d", sMySQLError.c_str(), err);
			if(err>=ER_ERROR_FIRST && err<=ER_ERROR_LAST) // client side error, not relavant to socket
			{
				FreeMySQL(mysql);
			}
			else
			{
				RealFreeMySQL(mysql);
			}
			// if connection is closed by server, try again
			if((err==CR_SERVER_GONE_ERROR || err==CR_SERVER_LOST ||
				err==CR_CONNECTION_ERROR || err==CR_CONN_HOST_ERROR ||
				err==CR_LOCALHOST_CONNECTION || err==CR_TCP_CONNECTION) &&
				try_num < max_try_num)
			{
				try_num ++;
				continue;
			}
			SendResponse(sockfd, stRequest, err, sMySQLError);
			return;
		}
		tree = PackResult(result, key);
		mysql_free_result(result);
		FreeMySQL(mysql);

		if (tree == NULL)
		{
			Attr_API(2973336, 1);
			SendResponse(sockfd, stRequest, CR_OUT_OF_MEMORY, sMySQLError);
			LOG_DEBUG("PackResult failed: %s", sMySQLError.c_str());
			return;
		}

		st.Attach(*tree);
		if(st.TryLockShm()==0)
		{
			Attr_API(2973339, 1);
			LOG_DEBUG("Wrote data to cache.");
			if(st.Write(true)) // enable auto_remove
			{
				Attr_API(2973344, 1);
				LOG_ERROR("Write to cache failed: %s", st.GetErrorMsg().c_str());
			}
			st.UnlockShm();
		}
		else
		{
			LOG_WARNING("TrylockShm failed.");
			Attr_API(2973338, 1);
		}
	}
	string rsp = tree->GetStrVal();
	if(rsp.length()==0)
	{
		LOG_ERROR("KNV GetStrVal() failed: %s", tree->GetErrorMsg()? tree->GetErrorMsg() : "NULL");
	}
	LOG_DEBUG("Sending rsp of %lu bytes.", rsp.length());
	SendResponse(sockfd, stRequest, 0, rsp);
}

void ParsePacket(int sockfd, string &reqstr)
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
			LOG_ERROR("Skipped %d invalid bytes when searching for start token, start_token is %sfound.", skipped, (packet+PACKET_HEADER_LENGTH >= endpack)? "NOT ":"");
		}

		if(packet+PACKET_HEADER_LENGTH >= endpack)
			break;

		uint64_t packlen = ((uint64_t*)packet)[1];
		if(endpack < packet+packlen)
		{
			if(packlen > MAX_REQUEST_PACKET_LENGTH)
			{
				Attr_API(2977504, 1);
				LOG_ERROR("Recv response (length=%d) exceeds hard limit of %d bytes.", packlen, MAX_REQUEST_PACKET_LENGTH);
				packet ++;
				continue;
			}
			break;
		}

		KnvNode *tree = KnvNode::NewFromMessage(string(packet+PACKET_HEADER_LENGTH, packlen-PACKET_HEADER_LENGTH));
		if(tree==NULL)
		{
			Attr_API(2973361, 1);
			LOG_ERROR("Recv bad packet, KnvNode::NewFromMessage() failed: %s", KnvNode::GetGlobalErrorMsg());
		}
		else
		{
			ProxyPacket stPacket;
			stPacket.appname = tree->GetFieldStr(1);
			stPacket.sequence = tree->GetFieldInt(2);
			stPacket.errnum = 0;
			stPacket.content = tree->GetFieldStr(11);
			KnvNode::Delete(tree);

			map<string,string>::iterator it = stConfig.mapAppKeys.find(stPacket.appname);
			if(it==stConfig.mapAppKeys.end())
			{
				Attr_API(2973362, 1);
				sMySQLError = "Unregistered appname ";
				sMySQLError += stPacket.appname;
				LOG_ERROR("%s", sMySQLError.c_str());
				SendResponse(sockfd, stPacket, 1, sMySQLError);
			}
			else
			{
				stPacket.appkey = it->second;
				ProcessRequest(sockfd, stPacket);
			}
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
}

extern "C" void *Worker(void *data)
{
	int sockfd = (int)(unsigned long)data;
	string reqstr;
	char buffer[65536];
	int len;

	while((len = recv(sockfd, buffer, sizeof(buffer), 0))>0)
	{
		reqstr.append(buffer, len);
		ParsePacket(sockfd, reqstr);
	}
	if(reqstr.length())
	{
		ParsePacket(sockfd, reqstr);
	}
	close(sockfd);
	UcStorage::ThreadExit();
}


int main(int argc, char *argv[])
{
	int iClose;

	daemon(1, 1);
	stConfig.pid = 0;

	int iRet = ProxyInit(argc, argv);
	if(iRet<0)
		return iRet;
	int iListenFd = iRet;

	LOG_DEBUG("Entering into working loop.\n");

	// handle loop
	while(1)
	{
		iClose = 0;
		if(stConfig.iEnaTraficCtrl)
		{
			if(TrafficCtl(stConfig.iMinInterval))
				iClose = 1;
		}

		DoLoop();

		struct timeval  stTimeVal;
		fd_set Rfds;
		int iMaxFd = 0;

		FD_ZERO(&Rfds);
		FD_SET(iListenFd, &Rfds);
		if (iMaxFd < iListenFd) 
			iMaxFd = iListenFd;

		stTimeVal.tv_sec = 0;
		stTimeVal.tv_usec = 100*1000;
		iRet = select(iMaxFd+1, &Rfds, NULL, NULL, &stTimeVal);
		if(iRet==0)
			continue;
		else if(iRet < 0)
		{
			if(errno == EINTR)
			{
				LOG_DEBUG("select interrupted.\n");
				continue;
			}
			LOG_WARNING("Something wrong in select. errno=%d:%s\n", errno, strerror(errno));
		}

		if (!FD_ISSET(iListenFd, &Rfds)) {
			LOG_DEBUG("select success but not set.\n");
			continue;
		}

		int iNewSocket = accept(iListenFd, NULL, NULL); 
		if (iNewSocket < 0 && errno == EINTR)
		{
			LOG_DEBUG("accept interrupted.\n");
			continue;
		}
		else if (iNewSocket < 0) 
		{
			Attr_API(2973374, 1);
			LOG_WARNING("Something wrong with accept. errno=%d:%s\n", errno, strerror(errno));
			continue;
		}
		if(iClose)
		{
			struct sockaddr_in addr;
			socklen_t len = sizeof(addr);
			getpeername(iNewSocket, (struct sockaddr *)&addr, &len);

			close(iNewSocket);
			Attr_API(2973372,1);
			LOG_INFO("Traffic limit hitten, peer %s:%hu closed!\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
			continue;
		}

		pthread_t thread;
		memset(&thread, 0, sizeof(thread));
		iRet = pthread_create(&thread, NULL, Worker, (void*)(unsigned long)iNewSocket);
		if(iRet < 0)
		{
			close(iNewSocket);
			Attr_API(2973373, 1);
			LOG_INFO("pthread_create failed: %d:%s\n", errno, strerror(errno));
			continue;
		}
	}
	return 0;
}


