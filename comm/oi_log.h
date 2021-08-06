
#ifndef _OI_LOG_H
#define _OI_LOG_H

#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* �����¼��־, ��ϸ�Ķ����<������淶>*/
#define _LOG_ERROR_ 	1	//���ش���
#define _LOG_WARNING_ 	2 	//����,�Ƚ����أ���Ҫ�����ֻ�
#define _LOG_NOTICE_ 	3 	//��Ҫע������⣬�����ǷǷ��İ�֮�࣬����Ҫ���͵��ֻ�����Ϊ���ܺܶ�
#define _LOG_INFO_ 		4	//��ˮ
#define _LOG_DEBUG_ 	5	//debug

#define _REMOTE_LOG_DIRTY_	1
#define _REMOTE_LOG_Other_TGTSeed_Fail_	2
#define _REMOTE_LOG_VS_DEBUG_	3
#define _REMOTE_LOG_UNREG_UIN_	4

typedef struct {
	FILE	*pLogFile;
	char	sBaseFileName[80];
	char	sLogFileName[80];
	int	iShiftType;		// 0 -> shift by size,  1 -> shift by LogCount, 2 -> shift by interval, 3 ->shift by day, 4 -> shift by hour, 5 -> shift by min
	int	iMaxLogNum;
	long	lMaxSize;
	long	lMaxCount;
	long	lLogCount;
	time_t	lLastShiftTime;
} LogFile;

// iLogTime : 1 -- ��ȷ���� 2 ---����ȷ��΢��
int Log(LogFile* pstLogFile, int iLogTime, const char* sFormat, ...);
long InitLogFile(LogFile* pstLogFile, char* sLogBaseName, long iShiftType, long iMaxLogNum, long iMAX);

int ShiftFiles(LogFile* pstLogFile);

#ifdef __cplusplus
}
#endif

#endif
