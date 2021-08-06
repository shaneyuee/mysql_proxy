#ifndef _TLIB_CFG_H_
#define _TLIB_CFG_H_

#define CFG_STRING	(int)1
#define CFG_INT		(int)2
#define CFG_LONG		(int)3
#define CFG_DOUBLE	(int)4
#define CFG_LINE		(int)5
#define CFG_UINT	(int)6
#define CFG_ULONG	(int)7

#ifdef __cplusplus
extern "C"
{
#endif

void TLib_Cfg_GetConfig(char *sConfigFilePath, ...);

#ifdef __cplusplus
}
#endif

#endif
