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
#include <termios.h>
#include <sstream>
#include <iostream>
#include <fstream>
#include <vector>
#include "mysql_proxy_api.h"

using namespace std;

static string StrToHex(const string &content)
{
	char buf[content.length()*2+2];
	for(size_t i=0; i<content.length(); i++)
		sprintf(buf+i*2, "%02X", content[i]);
	return string(buf, content.length()*2);
}

static int isallspaces(const char *str)
{
	while(*str && isspace(*str))
		str++;
	return !(*str);
}

void settc()
{
	struct termios new_settings;
	tcgetattr(0,&new_settings);
	new_settings.c_cc[VERASE] = '';
//	new_settings.c_lflag &= ~(ICANON);
	tcsetattr(0,TCSANOW,&new_settings);
}

int ugetchar(void)
{
	char c;
	return (read(0, &c, 1) == 1) ? (unsigned char) c : EOF;
}


int main(int argc, char *argv[])
{
	if(argc!=6)
	{
		printf("usage: %s <ip> <port> <app_name> <app_passwd> <timeout_ms>\n", argv[0]);
		return -1;
	}

	MYSQL my;

	if(mysql_init(&my)==NULL)
	{
		printf("mysql_init() failed: %s\n", mysql_error(&my));
		return -1;
	}

	if(mysql_proxy_connect(&my, argv[1], atoi(argv[2]), argv[3], argv[4], atoi(argv[5]))==NULL)
	{
		printf("mysql_proxy_connect() failed: %s\n", mysql_error(&my));
		return -1;
	}

	char line[10240];
	vector<string> history;

	settc();
	printf("Please input your SQL for query:\n");

	while(1)
	{
		printf(">");fflush(stdout);
		memset(line, 0, sizeof(line));
#if 0
		char *cur = line;
		int key;
		while(1)
		{
			key=ugetchar();
			if(key==EOF)
				return 1;
			if(key=='\n')
				break;
			//printf("%d-%x\n", key, key);
			*cur++ = key;

			if(cur>line+2 && *(cur-2)==0x5b && *(cur-1)>=0x41 && *(cur-1)<=0x43) // up-dn-right-left
			{
				if(*(cur-1)==0x41) // up
				{
				}
				else if(*(cur-1)==0x42) // dn
				{
				}
				else
				{
					cur -= 2;
				}
			}
		}
		*cur = 0;
#else
		if(gets(line)==NULL)
			break;
#endif
		if(isallspaces(line))
			continue;
		if(strcmp(line, "quit")==0||strcmp(line, "quit;")==0||strcmp(line, "exit")==0||strcmp(line, "exit;")==0)
			break;
		printf("input: [%s]\n", line);
		//history.push_back(line);
		int iRet = mysql_query(&my, line);
		if(iRet<0)
		{
			printf("mysql_query() failed: ret=%d, errno=%d:%s\n", iRet, mysql_errno(&my), mysql_error(&my));
			continue;
		}

		MYSQL_RES *res = mysql_use_result(&my);
		if(res==NULL)
		{
			printf("mysql_use_result() failed: %s\n", mysql_error(&my));
			continue;
		}

		printf("mysql_num_fields()  : %lu\n", mysql_num_fields(res));
		printf("mysql_num_rows()    : %lu\n", mysql_num_rows(res));
	//	printf("mysql_fetch_field() : ");
	//	MYSQL_FIELD *field;
	//	while((field=mysql_fetch_field(res)))
	//		printf("%s(%d)\t", field->name, field->name_length);
	//	printf("\n");
		printf("mysql_fetch_fields(): ");
		MYSQL_FIELD *fields = mysql_fetch_fields(res);
		unsigned long fieldnum = mysql_num_fields(res);
		for(unsigned long i=0; i<fieldnum; i++)
		{
			printf("%s(%d)\t", fields[i].name, fields[i].name_length);
		}
		printf("\n");
		printf("mysql_fetch_row()   : \n");
		MYSQL_ROW row;
		while((row=mysql_fetch_row(res)))
		{
			unsigned long *lengths = mysql_fetch_lengths(res);
			unsigned long fieldnum = mysql_num_fields(res);
			for(unsigned long i=0; i<fieldnum; i++)
			{
				if(i==0)
					printf("                      ");
				//printf("%s(%lu)\t", row[i]? row[i]:"NULL", lengths[i]);
				printf("%s\t", row[i]? row[i]:"NULL");
			}
			printf("\n");
		}
		mysql_free_result(res);
	}
	mysql_close(&my);
	return 0;
}


