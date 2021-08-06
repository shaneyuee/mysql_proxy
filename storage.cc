// storage.cc
// UC数据存储模块实现
//
// Shaneyu@tencent.com 
//
// 2013-08-28	shaneyu 	Created
// 2013-10-12	shaneyu		Support Key-N-Value
//

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <map>

#include "storage.h"

#include "ht/multi_hash_table.h"
static MultiHashTable mht[MAX_HT_SHM_NUM];


#define DB_LOCK_FILE    "/tmp/.mysql_proxy_shm_lock"

int UcStorage::lock_fd = -1;

int UcStorage::ShmLock(int iUnlocking, string &errmsg, bool trylock)
{
	if(lock_fd <= 0)
		lock_fd = open(DB_LOCK_FILE, O_CREAT, 666);
	if(lock_fd < 0)
	{
		errmsg = "open lock file "DB_LOCK_FILE" failed: " + string(strerror(errno));
		return -1;
	}
	int flag = iUnlocking? LOCK_UN:LOCK_EX;
	if(trylock && iUnlocking==0)
		flag |= LOCK_NB;
	int ret = flock(lock_fd, flag);
	if(ret < 0)
	{
		errmsg = string(iUnlocking? "Unlock":"Lock") + " file "DB_LOCK_FILE" failed: " + string(strerror(errno));
		return -2;
	}
	return 0;
}

void UcStorage::PrintInfo(int idx)
{
	mht[idx].ReportInfo();
}
void UcStorage::ReportInfo(int idx)
{
	mht[idx].ReportInfo();
}
int UcStorage::FixHtBadNode()
{
	return mht[storage_idx].EraseBadNode(key.GetData(), key.GetLength());
}

float UcStorage::GetHtUsage(int idx)
{
	return mht[idx].GetHtUsage();
}

float UcStorage::GetLtUsage(int idx)
{
	return mht[idx].GetLtUsage();
}

#define SMALL_BUFFER_SIZE	(10*1024*1024)
#define HUGE_BUFFER_SIZE	HASH_MAX_DATA_LENGTH

static __thread struct
{
	char *small_buffer;
	char *huge_buffer;
}mht_thread_data = { NULL, NULL };

void UcStorage::ThreadExit()
{
	if(mht_thread_data.small_buffer)
	{
		free(mht_thread_data.small_buffer);
		mht_thread_data.small_buffer = NULL;
	}
	if(mht_thread_data.huge_buffer)
	{
		free(mht_thread_data.huge_buffer);
		mht_thread_data.huge_buffer = NULL;
	}
}

// Read from LinkTable into buffer
// FIXME: need to update access_time each time a key is read from HashTable
// access_time keeps previous read time
static int ReadCache(UcStorage &st, time_t &access_time, char *(&data), string &errmsg)
{
	int ret, l = SMALL_BUFFER_SIZE;
	if(mht_thread_data.small_buffer==NULL)
	{
		mht_thread_data.small_buffer = (char*)malloc(SMALL_BUFFER_SIZE);
		if(mht_thread_data.small_buffer==NULL)
		{
			// running out of memory
			errmsg = "out of memory";
			return -1;
		}
	}
	const knv_key_t &k = st.GetKey();
	if((ret=mht[st.GetFd()].GetData(k.GetData(), k.GetLength(), mht_thread_data.small_buffer, l, true, &access_time))<0)
	{
		if(ret==-22) // small buffer not large enough, use huge buffer
		{
			l = HUGE_BUFFER_SIZE;
			if(mht_thread_data.huge_buffer==NULL)
			{
				mht_thread_data.huge_buffer = (char*)malloc(HUGE_BUFFER_SIZE);
				if(mht_thread_data.huge_buffer==NULL)
				{
					// running out of memory
					errmsg = "out of memory";
					return -1;
				}
			}
			ret = mht[st.GetFd()].GetData(k.GetData(), k.GetLength(), mht_thread_data.huge_buffer, l, true, &access_time);
			if(ret<0)
			{
				free(mht_thread_data.huge_buffer);
				mht_thread_data.huge_buffer = NULL;
				errmsg = "HashTable read failed: ";
				errmsg += mht[st.GetFd()].GetErrorMsg();
				return ret;
			}
			else
			{
				data = mht_thread_data.huge_buffer;
			}
		}
		else
		{
			errmsg = "HashTable read failed: ";
			errmsg += mht[st.GetFd()].GetErrorMsg();
			return ret;
		}
	}
	else
	{
		data = mht_thread_data.small_buffer;
	}
	return l;
}

int UcStorage::Read(bool own_buf)
{
	char *data;
	int ret = ReadCache(*this, tAccessTime, data, errmsg);
	if(ret==0)
	{
		errmsg = "No such key";
		return -1;
	}
	if(ret<0)
	{
		return -2;
	}

	Delete();

	tree = KnvNode::New(data, ret, own_buf);
	if(tree==NULL)
	{
		char sdata[64], *pdata = sdata;
		int i;
		snprintf(sdata, sizeof(sdata), "len=%d, data=", ret);
		pdata = sdata+strlen(sdata);
		for(i=0; i<ret && i<10; i++)
		{
			if(i) *pdata++ = ' ';
			sprintf(pdata, "%02X", (unsigned char)(data[i]));
			pdata += strlen(pdata);
		}
		if(i<ret) strcat(pdata, "...");
		errmsg = "Construct KnvNode from data(";
		errmsg += sdata;
		errmsg += ") failed: "; errmsg += KnvNode::GetGlobalErrorMsg();
		return -3;
	}
	auto_delete = true;

	return 0;
}

void UcStorage::Destroy(int idx)
{
	mht[idx].Destroy();
}

int UcStorage::Write(bool auto_remove)
{
	if(tree==NULL || !tree->IsValid())
	{
		errmsg = "Knv tree is not initialized";
		return -1;
	}

	string s;
	int ret = tree->Serialize(s);
	if(ret!=0)
	{
		errmsg = tree->GetErrorMsg();
		return -2;
	}

	const knv_key_t &k = GetKey();
	if((ret=mht[storage_idx].SetData(k.GetData(), k.GetLength(), s.data(), s.length(), auto_remove))<0)
	{
		errmsg = "MultiHashTable set failed: "; errmsg += mht[storage_idx].GetErrorMsg();
		return ret;
	}
	return 0;
}

int UcStorage::Erase()
{
	int ret;
	const knv_key_t &k = GetKey();
	if((ret=mht[storage_idx].EraseData(k.GetData(), k.GetLength()))<0)
	{
		errmsg = "MultiHashTable erase failed: "; errmsg += mht[storage_idx].GetErrorMsg();
		return ret;
	}
	return ret;
}

//awk '/MemFree/{print $2}' /proc/meminfo

static ull64_t GetTotalMemory()
{
	static char sMem[128] = {0};
	const char *fpath = "/tmp/total_mem.txt";

	char cmd[256];
	sprintf(cmd, "awk '/MemTotal/{print $2}' /proc/meminfo > %s 2>/dev/null", fpath);

	// if the main process has called DaemonInit(), system() will not be able to
	// attain the result of shell command
	system(cmd);

	int fd = open(fpath, O_RDONLY);
	if(fd >= 0)
	{
		read(fd, sMem, sizeof(sMem)-1);
		close(fd);
	}

	remove(fpath);

	return strtoull(sMem, NULL, 10)*1024;
}

static ostringstream g_errmsg;
static int create_idx = 0;

int UcStorage::InitWrite(uint64_t shmkey, uint8_t shmsz_percent, uint32_t max_keylen, uint16_t row_num, uint8_t avg_blocknum, uint32_t block_size, bool use_access_seq, bool print_info)
{
	int idx = create_idx++;
	if(idx>=MAX_HT_SHM_NUM)
	{
		g_errmsg << "Too many FDs, please adjust MAX_HT_SHM_NUM if necessary";
		return -1;
	}

	MhtInitParam param;
	memset(&param, 0, sizeof(param));
	param.ddwShmKey = shmkey;
	param.ddwBufferSize = ((GetTotalMemory()*shmsz_percent/100 + 2048*1024-1)/(2048*1024));
	param.ddwBufferSize = param.ddwBufferSize*(1024*2048);
	if(print_info)
		cout << "memsize="<<param.ddwBufferSize<<endl;
	param.wMaxKeyLen = max_keylen;
	param.wRowNum = row_num;
	param.dwIndexRatio = avg_blocknum? (100/avg_blocknum) : 150; // more index nodes could improve mht performance
	if(param.dwIndexRatio==0) param.dwIndexRatio = 1;
	param.ddwBlockSize = block_size;
	param.bUseHugePage = false;
	param.bUseAccessSeq = use_access_seq;

	int ret = mht[idx].CreateFromShm(param);
	if(ret)
	{
		g_errmsg << "Mht Create shm failed: " << mht[idx].GetErrorMsg();
		return -4;
	}
	if(print_info)
		mht[idx].PrintInfo();
	return idx;
}

int UcStorage::InitRead(uint64_t shmkey, bool print_info)
{
	int idx = create_idx++;
	if(idx>=MAX_HT_SHM_NUM)
	{
		g_errmsg << "Too many FDs, please adjust MAX_HT_SHM_NUM if necessary";
		return -1;
	}

	int ret;
	if((ret=mht[idx].InitFromShm(shmkey))<0)
	{
		g_errmsg << "Mht init failed: " << ret << ":" << mht[idx].GetErrorMsg();
		return ret;
	}
	if(print_info)
		mht[idx].PrintInfo();
	return 0;
}

const string &UcStorage::GetInitErrorMsg()
{
	static string s;
	s = g_errmsg.str();
	return s;
}



