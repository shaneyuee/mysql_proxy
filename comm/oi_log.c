#include "oi_log.h"
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>

typedef struct 
{
	uint16_t  wLength;
	uint16_t  wVersion; 
	uint16_t  wCommand;
	uint32_t  dwCmdSequence;
	char  acReserved[4];
} Log_PkgHead; //发包到log服务器 

#define CLI_VERSION_2 2
#define LOG_CNETER_WRITE_LOG_CMD 0x1
#define LOG_MODEL_DB	9

static int g_Send_Log_Level=_LOG_NOTICE_;	//log开关
//static LogFile* pstLogFileShm=NULL;

static char *CompactDateStr(time_t mytime)
{
	static char s[30];
	struct tm *curr;
	
	curr = localtime(&mytime);
	sprintf(s, "%04d%02d%02d", 
				curr->tm_year+1900, curr->tm_mon+1, curr->tm_mday);
	return s;
}

static char *DateTimeStr(uint32_t *mytime)
{
	static char s[50];
	struct tm curr;
	time_t mt = *mytime;
	
	curr = *localtime(&mt);

	if (curr.tm_year > 50)
	{
		sprintf(s, "%04d-%02d-%02d %02d:%02d:%02d", 
					curr.tm_year+1900, curr.tm_mon+1, curr.tm_mday,
					curr.tm_hour, curr.tm_min, curr.tm_sec);
	}
	else
	{
		sprintf(s, "%04d-%02d-%02d %02d:%02d:%02d", 
					curr.tm_year+2000, curr.tm_mon+1, curr.tm_mday,
					curr.tm_hour, curr.tm_min, curr.tm_sec);
	}
				
	return s;
}

static char *CurrDateTimeStr(void)
{
	time_t	iCurTime;

	time(&iCurTime);
	return DateTimeStr((uint32_t*)&iCurTime);
}

int ShiftFiles(LogFile* pstLogFile)
{
	struct stat stStat;
	char sLogFileName[300];
	char sNewLogFileName[300];
	int i;
	struct tm stLogTm, stShiftTm;
	time_t ltime;

	sprintf(sLogFileName,"%s.log", pstLogFile->sBaseFileName);

	if(pstLogFile->iShiftType!=6 && stat(sLogFileName, &stStat) < 0) return -1;
	switch (pstLogFile->iShiftType) {
		case 0:
			if (stStat.st_size < pstLogFile->lMaxSize) return 0;
			break;
		case 2:
			if (stStat.st_mtime - pstLogFile->lLastShiftTime < pstLogFile->lMaxCount) return 0;
			break;
		case 3:
			if (pstLogFile->lLastShiftTime - stStat.st_mtime > 86400) break;
			memcpy(&stLogTm, localtime((time_t*)&stStat.st_mtime), sizeof(stLogTm));
			memcpy(&stShiftTm, localtime(&pstLogFile->lLastShiftTime), sizeof(stShiftTm));
			if (stLogTm.tm_mday == stShiftTm.tm_mday) return 0;
			break;
		case 4:
			if (pstLogFile->lLastShiftTime - stStat.st_mtime > 3600) break;
			memcpy(&stLogTm, localtime((time_t*)&stStat.st_mtime), sizeof(stLogTm));
			memcpy(&stShiftTm, localtime(&pstLogFile->lLastShiftTime), sizeof(stShiftTm));
			if (stLogTm.tm_hour == stShiftTm.tm_hour) return 0;
			break;
		case 5:
			if (pstLogFile->lLastShiftTime - stStat.st_mtime > 60) break;
			memcpy(&stLogTm, localtime((time_t*)&stStat.st_mtime), sizeof(stLogTm));
			memcpy(&stShiftTm, localtime(&pstLogFile->lLastShiftTime), sizeof(stShiftTm));
			if (stLogTm.tm_min == stShiftTm.tm_min) return 0;
			break;
		case 6:
			time(&ltime);
			sprintf(pstLogFile->sLogFileName,"%s%s.log", pstLogFile->sBaseFileName, CompactDateStr(ltime));
			
			if (ltime - pstLogFile->lLastShiftTime > 86400)			
			{
				sprintf(sLogFileName,"%s%s.log", pstLogFile->sBaseFileName, CompactDateStr(ltime-pstLogFile->iMaxLogNum*86400));
				unlink(sLogFileName);
				time(&pstLogFile->lLastShiftTime);
			}

			return 0;
		default:
			if (pstLogFile->lLogCount < pstLogFile->lMaxCount) return 0;
			pstLogFile->lLogCount = 0;
	}

	// fclose(pstLogFile->pLogFile);

	for(i = pstLogFile->iMaxLogNum-2; i >= 0; i--)
	{
		if (i == 0)
			sprintf(sLogFileName,"%s.log", pstLogFile->sBaseFileName);
		else
			sprintf(sLogFileName,"%s%d.log", pstLogFile->sBaseFileName, i);
			
		if (access(sLogFileName, F_OK) == 0)
		{
			sprintf(sNewLogFileName,"%s%d.log", pstLogFile->sBaseFileName, i+1);
			if (rename(sLogFileName,sNewLogFileName) < 0 )
			{
				return -1;
			}
		}
	}
	// if ((pstLogFile->pLogFile = fopen(sLogFileName, "a+")) == NULL) return -1;
	time(&pstLogFile->lLastShiftTime);
	return 0;
}

long InitLogFile(LogFile* pstLogFile, char* sLogBaseName, long iShiftType, long iMaxLogNum, long iMAX)
{

	memset(pstLogFile, 0, sizeof(LogFile));
	strncat(pstLogFile->sLogFileName, sLogBaseName, sizeof(pstLogFile->sLogFileName) - 10);
	strcat(pstLogFile->sLogFileName, ".log");

	// if ((pstLogFile->pLogFile = fopen(sLogFileName, "a+")) == NULL) return -1;
	strncpy(pstLogFile->sBaseFileName, sLogBaseName, sizeof(pstLogFile->sBaseFileName) - 1);
	pstLogFile->iShiftType = iShiftType;
	pstLogFile->iMaxLogNum = iMaxLogNum;
	pstLogFile->lMaxSize = iMAX;
	pstLogFile->lMaxCount = iMAX;
	pstLogFile->lLogCount = iMAX;
	time(&pstLogFile->lLastShiftTime);

	return ShiftFiles(pstLogFile);
}

int Log(LogFile* pstLogFile, int iLogTime, const char* sFormat, ...)
{
va_list ap;
struct timeval stLogTv;

	if ((pstLogFile->pLogFile = fopen(pstLogFile->sLogFileName, "a+")) == NULL) return -1;
	va_start(ap, sFormat);
	if (iLogTime == 1) {
		fprintf(pstLogFile->pLogFile, "[%s] ", CurrDateTimeStr());
	}
	else if (iLogTime == 2) {
		gettimeofday(&stLogTv, NULL);
		fprintf(pstLogFile->pLogFile, "[%s.%.6u] ", DateTimeStr((uint32_t*)&(stLogTv.tv_sec)), (uint32_t)stLogTv.tv_usec);
	}
	vfprintf(pstLogFile->pLogFile, sFormat, ap);
	fprintf(pstLogFile->pLogFile, "\n");
	va_end(ap);
	pstLogFile->lLogCount++;
	fclose(pstLogFile->pLogFile);
	return ShiftFiles(pstLogFile);
}


