// commands.h
// UC协议命令字和错误码定义
//
// Shaneyu@tencent.com 
//
// 2013-10-23	shaneyu 	Created
//

#ifndef __UC_COMMANDS__
#define __UC_COMMANDS__

#define uint_to_hstr(ival, str, len) \
	do {\
		char s[32]; int i=32, h; uint64_t v=ival; \
		while(v) { h=(v%16); s[--i] = h>9? h-10+'A':h+'0'; v/=16; }\
		str[0]='0'; str[1]='x';\
		if(i==32) {str[2]='0'; str[3]='\0'; } \
		else { memcpy(str+2, s+i, 32-i); str[2+32-i]=0;}\
	} while(0)

namespace uc
{
	enum UcCommand
	{
		// UC cache commands
		CacheReadCommand = 0x4001,
		CacheWriteCommand = 0x4002,
		CacheEraseCommand = 0x4004, // erase data and re-read from UnionSession if needed fill-back

		CacheSyncCommand = 0x4003, // for slaves only, sync-ed from master
		CacheFillbackCommand = 0x4005, // read data from UnionSession and fillback to cache
		CacheFastSyncCommand = 0x4006, // same as sync, but no need to reply

		// Key-Value commands
		CacheReadAllCommand = 0x4007, // read whole key
		CacheWriteAllCommand = 0x4008, // overwrite whole key
		CacheEraseAllCommand = 0x4009, // erase whole key

		// SC commands
		ScSyncNoData = 0x4101, // User-end agent sync to SC server, no data
		ScSyncWithData = 0x4102, // with data

		// US commands
		UsReadCommand = 0x4201, // Read data from data source
	};

	enum UcSyncSubCommand
	{
		CacheSyncFull = 0, // full data
		CacheSyncUpdate = 1, // incremental update
		CacheSyncErase = 2, // incremental delete
	};

	static inline const char *GetCmdName(uint32_t c)
	{
		static __thread char cmd_name[128];
		switch(c)
		{
#define RET_CMDNAME(cmd)	case cmd: uint_to_hstr(cmd,cmd_name,sizeof(cmd_name));  strncat(cmd_name, ":" #cmd, sizeof(cmd_name)); return cmd_name
		// UC cache commands
		RET_CMDNAME(CacheReadCommand);
		RET_CMDNAME(CacheWriteCommand);
		RET_CMDNAME(CacheEraseCommand);

		RET_CMDNAME(CacheSyncCommand);
		RET_CMDNAME(CacheFillbackCommand);
		RET_CMDNAME(CacheFastSyncCommand);

		RET_CMDNAME(CacheReadAllCommand);
		RET_CMDNAME(CacheWriteAllCommand);
		RET_CMDNAME(CacheEraseAllCommand);

		// SC commands
		RET_CMDNAME(ScSyncNoData);
		RET_CMDNAME(ScSyncWithData);

		// US commands
		RET_CMDNAME(UsReadCommand);

		default: uint_to_hstr(c,cmd_name,sizeof(cmd_name)); strncat(cmd_name, ":UnknownCommand", sizeof(cmd_name)); return cmd_name;
		}
	}

	enum UcErrorCode
	{
		Successful = 0,

		// UC common
		UC_BadKey = 1,
		UC_BadRequest = 2,
		UC_SystemError = 3,
		UC_Timeout = 4,

		// Cache layer
		CacheRead_FillbackError = 101,
		CacheRead_FilterFailed = 102,
		// CacheRead follows
		CacheWrite_RequestBodyEmpty = 201,
		// CacheWrite follows
		CacheWrite_WritToNonMaster = 301,
	};

	static inline const char *GetErrorCodeName(uint32_t c)
	{
		static __thread char rc_name[128];
		switch(c)
		{
#define RET_RETCODENAME(rc)	case rc: snprintf(rc_name, sizeof(rc_name), "0x%X:" #rc, rc); return rc_name
		// UC common
		RET_RETCODENAME(Successful);
		RET_RETCODENAME(UC_BadKey);
		RET_RETCODENAME(UC_BadRequest);
		RET_RETCODENAME(UC_SystemError);
		RET_RETCODENAME(UC_Timeout);

		// Cache layer
		RET_RETCODENAME(CacheRead_FillbackError);
		RET_RETCODENAME(CacheRead_FilterFailed);
		RET_RETCODENAME(CacheWrite_RequestBodyEmpty);
		RET_RETCODENAME(CacheWrite_WritToNonMaster);

		default: snprintf(rc_name, sizeof(rc_name), "0x%X:UnknownRetCode", c); return rc_name;
		}
	}

};

#endif

