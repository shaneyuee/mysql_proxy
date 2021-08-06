#include <stdio.h>
#include <stdlib.h>
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
#include "knv_node.h"

using namespace std;

static int CreateSockAddr(int socket_type, const char* sIP, int iPort)
{
	struct sockaddr_in stAddr;
	int iListenSocket;
	int iReuseAddr = 1;

	/* Setup internet address information.  
	This is used with the bind() call */
	memset((char *) &stAddr, 0, sizeof(stAddr));

	stAddr.sin_family = AF_INET;
	stAddr.sin_port = htons(iPort);
	stAddr.sin_addr.s_addr = inet_addr(sIP);

	iListenSocket = socket(AF_INET, socket_type, 0);
	if (iListenSocket < 0) {
		perror("socket");
		return -1;
	}

	setsockopt(iListenSocket, SOL_SOCKET, SO_REUSEADDR, &iReuseAddr, 
		sizeof(iReuseAddr));

	if (connect(iListenSocket, (struct sockaddr *) &stAddr, 
		sizeof(stAddr)) < 0) {
		perror("connect");
		close(iListenSocket);
		return -1;
	}

	return iListenSocket;
}

static int CreateListenSock(const char *ip, int port)
{
	int iSock = CreateSockAddr(SOCK_STREAM, ip, port);
	if(iSock < 0)
		return -1;

	return iSock;
}


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
	std::string appname;
	std::string appkey;
	uint64_t sequence;
	uint32_t errnum;
	std::string content;
};

void SendPacket(int sockfd, const ProxyPacket &stPacket);


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
	return string();
}

static void SendRequest(int sockfd, ProxyPacket &stRequest)
{
	stRequest.content = Encrypt(stRequest.appkey, stRequest.content);

	KnvNode *tree = KnvNode::NewTree(1);
	if(tree==NULL)
	{
		printf("KnvNode::NewTree() failed: %s\n", KnvNode::GetGlobalErrorMsg());
		return;
	}
	tree->SetFieldStr(1, stRequest.appname.length(), stRequest.appname.data());
	tree->SetFieldInt(2, stRequest.sequence);
	tree->SetFieldInt(3, stRequest.errnum);
	tree->SetFieldStr(11, stRequest.content.length(), stRequest.content.data());

	int len = tree->EvaluateSize();
	char *rsp = new char[PACKET_HEADER_LENGTH + len];
	memcpy(rsp, PACKET_START_TOKEN, 8);
	int ret = tree->Serialize(rsp+PACKET_HEADER_LENGTH, len, false);
	if(ret<0)
	{
		Attr_API(1, 1);
		printf("KnvNode::Serialize() failed: %s\n", tree->GetErrorMsg());
		KnvNode::Delete(tree);
		delete []rsp;
		return;
	}
	KnvNode::Delete(tree);
	len += PACKET_HEADER_LENGTH;
	((uint64_t*)rsp)[1] = len;
	send(sockfd, rsp, len, 0);
	delete []rsp;
}

static string StrToHex(const string &content)
{
	char buf[content.length()*2+2];
	for(size_t i=0; i<content.length(); i++)
		sprintf(buf+i*2, "%02X", content[i]);
	return string(buf, content.length()*2);
}

void ProcessResponse(int sockfd, ProxyPacket &stRequest, ProxyPacket &stResponse)
{
	if(stResponse.errnum)
	{
		printf("Response error msg: %s.\n", stResponse.content.c_str());
		return;
	}
	string rspstr = Decrypt(stRequest.appkey, stResponse.content);
	if(rspstr.length()==0)
	{
		printf("Decrypt rsp failed, rsp_hex=%s\n", StrToHex(stResponse.content).c_str());
		return;
	}

	KnvNode *tree = KnvNode::NewFromMessage(rspstr);
	printf("Response:\n");
	tree->Print("    ");
	KnvNode::Delete(tree);
}

int ParsePacket(int sockfd, ProxyPacket &stRequest, string &reqstr)
{
	char *packet = (char*)reqstr.data();
	char *begpack = packet;
	char *endpack = packet + reqstr.length();

	while(packet!=endpack)
	{
		while(memcmp(packet, PACKET_START_TOKEN, 8) && packet!=endpack)
		{
			packet ++;
		}
		if(endpack <= packet+PACKET_HEADER_LENGTH)
			break;

		uint64_t packlen = ((uint64_t*)packet)[1];
		if(endpack < packet+packlen)
		{
			if(packlen > MAX_REQUEST_PACKET_LENGTH)
			{
				packet ++;
				continue;
			}
			break;
		}

		KnvNode *tree = KnvNode::NewFromMessage(string(packet+PACKET_HEADER_LENGTH, packlen-PACKET_HEADER_LENGTH));
		if(tree==NULL)
		{
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
			ProcessResponse(sockfd, stRequest, stPacket);
			return 1;
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

int main(int argc, char *argv[])
{
	if(argc!=5)
	{
		printf("usage: %s <ip> <port> <app_name> <app_passwd>\n", argv[0]);
		return -1;
	}
	int sockfd = CreateListenSock(argv[1], atoi(argv[2]));
	if (sockfd < 0)
	{
		printf("CreateListenSock Fail: %d:%s\n", errno, strerror(errno));
		exit(1);
	}

	char key[8];
	passwd2des(argv[4], key);

	ProxyPacket stRequest;
	stRequest.appname = argv[3];
	stRequest.appkey.assign(key, 8);
	stRequest.sequence = 1;
	stRequest.errnum = 0;

	printf("appname=%s, appkey=%s\n", stRequest.appname.c_str(), stRequest.appkey.c_str());

	char line[10240];
	char buffer[65536*2];

	printf("Please input your SQL for query:\n");

	while(1)
	{
		memset(line, 0, sizeof(line));
		if(gets(line)==NULL)
			break;
		stRequest.content = line;

		SendRequest(sockfd, stRequest);

		int len;
		string rspstr;
		while((len=recv(sockfd, buffer, sizeof(buffer), 0)) > 0)
		{
			rspstr.append(buffer, len);
			if(ParsePacket(sockfd, stRequest, rspstr) > 0)
				break;
		}
		stRequest.sequence ++;
	}
	close(sockfd);
	return 0;
}


