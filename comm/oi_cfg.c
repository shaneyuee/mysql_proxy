#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include "oi_cfg.h"

#define MAX_CONFIG_LINE_LEN 1023
#define US		0x1f

#define xstrcpy(dst, src) memmove((dst), (src), strlen(src)+1)

static char* get_val(char* desc, char* src)
{
char *descp=desc, *srcp=src;
int mtime=0, space=0;

	while ( mtime!=2 && *srcp != '\0' ) {
		switch(*srcp) {
			case ' ':
			case '\t':
			case '\0':
			case '\n':
			case US:
				space=1;
				srcp++;
				break;
			default:
				if (space||srcp==src) mtime++;
				space=0;
				if ( mtime==2 ) break;
				*descp=*srcp;
				descp++;
				srcp++;
		}
	}
	*descp='\0';
	xstrcpy(src, srcp);
	return desc;
}

static void InitDefault(va_list ap)
{
	char *sParam, *sVal, *sDefault;
	double *pdVal, dDefault;
	long *plVal, lDefault;
	int iType, *piVal, iDefault;
	long lSize;
	unsigned long *pulVal, ulDefault;
	unsigned int *puiVal, uiDefault;
	
	sParam = va_arg(ap, char *);
	while (sParam != NULL)
	{
		iType = va_arg(ap, int);
		switch(iType)
		{
			case CFG_LINE:
				sVal = va_arg(ap, char *);
				sDefault = va_arg(ap, char *);
				lSize = va_arg(ap, int);
				strncpy(sVal, sDefault, (int)lSize-1);
				sVal[lSize-1] = 0;
				break;
			case CFG_STRING:
				sVal = va_arg(ap, char *);
				sDefault = va_arg(ap, char *);
				lSize = va_arg(ap, int);
				strncpy(sVal, sDefault, (int)lSize-1);
				sVal[lSize-1] = 0;
				break;
			case CFG_LONG:
				plVal = va_arg(ap, long *);
				lDefault = va_arg(ap, long);
				*plVal = lDefault;
				break;
			case CFG_INT:
				piVal = va_arg(ap, int *);
				iDefault = va_arg(ap, int);
				*piVal = iDefault;
				break;
			case CFG_ULONG:
				pulVal = va_arg(ap, unsigned long *);
				ulDefault = va_arg(ap, unsigned long);
				*pulVal = ulDefault;
				break;
			case CFG_UINT:
				puiVal = va_arg(ap, unsigned int *);
				uiDefault = va_arg(ap, unsigned int);
				*puiVal = uiDefault;
				break;
			case CFG_DOUBLE:
				pdVal = va_arg(ap, double *);
				dDefault = va_arg(ap, double);
				*pdVal = dDefault;
				break;
		}
		sParam = va_arg(ap, char *);
	}
}

static void SetVal(va_list ap, char *sP, char *sV)
{
	char *sParam, *sVal = NULL, *sDefault;
	double *pdVal = NULL, dDefault;
	long *plVal = NULL, lDefault;
	int iType, *piVal = NULL, iDefault;
	long lSize = 0;
	char sLine[MAX_CONFIG_LINE_LEN+1], sLine1[MAX_CONFIG_LINE_LEN+1];
	unsigned long *pulVal = NULL, ulDefault;
	unsigned int *puiVal = NULL, uiDefault;
	
	strcpy(sLine, sV);
	strcpy(sLine1, sV);
	get_val(sV, sLine1);
	sParam = va_arg(ap, char *);
	while (sParam != NULL)
	{
		iType = va_arg(ap, int);
		switch(iType)
		{
			case CFG_LINE:
				sVal = va_arg(ap, char *);
				sDefault = va_arg(ap, char *);
				lSize = va_arg(ap, int);
				if (strcmp(sP, sParam) == 0) {
					strncpy(sVal, sLine, (int)lSize-1);
					sVal[lSize-1] = 0;
				}
				break;
			case CFG_STRING:
				sVal = va_arg(ap, char *);
				sDefault = va_arg(ap, char *);
				lSize = va_arg(ap, int);
				break;
			case CFG_LONG:
				plVal = va_arg(ap, long *);
				lDefault = va_arg(ap, long);
				/*
				if (strcmp(sP, sParam) == 0)
				{
					*plVal = atol(sV);
				}
				*/
				break;
			case CFG_INT:
				piVal = va_arg(ap, int *);
				iDefault = va_arg(ap, int);
				/*
				if (strcmp(sP, sParam) == 0)
				{
					*piVal = iDefault;
				}
				*/
				break;
			case CFG_ULONG:
				pulVal = va_arg(ap, unsigned long *);
				ulDefault = va_arg(ap, unsigned long);
				break;
			case CFG_UINT:
				puiVal = va_arg(ap, unsigned int *);
				uiDefault = va_arg(ap, unsigned int);
				break;
			case CFG_DOUBLE:
				pdVal = va_arg(ap, double *);
				dDefault = va_arg(ap, double);
				*pdVal = dDefault;
				break;
		}

		if (strcmp(sP, sParam) == 0)
		{
			switch(iType)
			{
				case CFG_STRING:
					strncpy(sVal, sV, (int)lSize-1);
					sVal[lSize-1] = 0;
					break;
				case CFG_LONG:
					*plVal = strtol(sV, NULL, 0);
					break;
				case CFG_ULONG:
					*pulVal = strtoul(sV, NULL, 0);
					break;
				case CFG_UINT:
					*puiVal = strtoul(sV, NULL, 0);
					break;
				case CFG_INT:
					*piVal = strtol(sV, NULL, 0);
					break;
				case CFG_DOUBLE:
					*pdVal = atof(sV);
					break;
			}
			return;
		}

		sParam = va_arg(ap, char *);
	}
}

static int GetParamVal(char *sLine, char *sParam, char *sVal)
{

	get_val(sParam, sLine);
	strcpy(sVal, sLine);
	
	if (sParam[0] == '#')
		return 1;
		
	return 0;
}

void TLib_Cfg_GetConfig(char *sConfigFilePath, ...)
{
	FILE *pstFile;
	char sLine[MAX_CONFIG_LINE_LEN+1], sParam[MAX_CONFIG_LINE_LEN+1], sVal[MAX_CONFIG_LINE_LEN+1];
	va_list ap;
	
	va_start(ap, sConfigFilePath);
	InitDefault(ap);
	va_end(ap);

	if ((pstFile = fopen(sConfigFilePath, "r")) == NULL)
	{
		// printf("Can't open Config file '%s', ignore.\n", sConfigFilePath);
		return;
	}

	while (1)
	{
		if(fgets(sLine, sizeof(sLine), pstFile) == NULL)
		{
			break; 
		}
		
		if (GetParamVal(sLine, sParam, sVal) == 0)
		{
			va_start(ap, sConfigFilePath);
			SetVal(ap, sParam, sVal);
			va_end(ap);
		}
	}	
	fclose(pstFile);
	
}

