// storage.h
// UC数据存储模块提供的API
//
// Shaneyu@tencent.com 
//
// 2013-08-28	shaneyu 	Created
// 2013-10-12	shaneyu		Support Key-N-Value
//

#ifndef __UC_STORAGE__
#define __UC_STORAGE__

#include <string>
#include <map>
#include "knv_node.h"

#define DEFAUTL_HT_SHM_KEY	0x00443377
#define MAX_HT_SHM_NUM		16

using namespace std;

typedef uint64_t StIterator;

// This is an  encapsulation for KnvNode
class UcStorage
{
private:
	knv_key_t key;
	bool auto_delete; // delete tree on destruction
	KnvNode *tree;
	string errmsg;
	time_t tAccessTime;
	int storage_idx;

	static int lock_fd;
	static int ShmLock(int iUnlocking, string &errmsg, bool trylock);

public:
	UcStorage(int fd, const knv_key_t &k): key(k), auto_delete(true), tree(NULL), errmsg(), tAccessTime(), storage_idx(fd){}
	UcStorage(int fd): key(), auto_delete(true), tree(NULL), errmsg(), tAccessTime(), storage_idx(fd){}
	UcStorage(int fd, KnvNode &tr, bool auto_del = false): key(tr.GetKey()), auto_delete(auto_del), tree(&tr), errmsg(), tAccessTime(), storage_idx(fd) {}
	~UcStorage() { Delete(); }
	void Delete() { if(auto_delete && tree) KnvNode::Delete(tree); tree = NULL; key.FreeDynData(); }

	KnvNode *GetDataTree() { return tree; }
	const knv_key_t &GetKey() const { return key; }
	time_t GetAccessedTime() { return tAccessTime; }
	int GetFd() const { return storage_idx; }

	// for multi process/thread writing
	int LockShm() {	return ShmLock(0, errmsg, false); }
	int TryLockShm() { return ShmLock(0, errmsg, true); }
	int UnlockShm() { return ShmLock(1, errmsg, false); }

	// Call me when the current thread exits, in multi-thread mode
	static void ThreadExit();

	int Attach(KnvNode &tr, bool auto_del = true)
	{
		Delete(); tree = &tr; key = tr.GetKey(); auto_delete = auto_del; return 0;
	}
	KnvNode *Detach()
	{
		if(auto_delete) { auto_delete = false; return tree; } return NULL; // don't own the tree
	}

	// init storage,
	// 注意：Init接口只能在启动的时候有主线程调用，不允许子线程调用
	//   shmkey         -- 使用共享内存的 key
	//   shmsz_percent  -- 使用共享内存占总内存的百分比
	//   avg_blocknum   -- 平均每个key需要的数据块数(用于控制索引块和数据块的占比)
	//   block_size     -- 数据块的大小
	//   use_access_seq -- 是否跟进读Seq进行淘汰
	// 返回：
	//   < 0 -- 表示失败，请调用 GetInitErrorMsg() 获取失败原因
	//   >=0 -- 成功，返回用于操作 storage 的句柄，构造 UcStorage 时传入
	static int InitWrite(uint64_t shmkey=DEFAUTL_HT_SHM_KEY, uint8_t shmsz_percent=5, uint32_t max_keylen=8, uint16_t row_num=20, uint8_t avg_blocknum=2, uint32_t block_size=128, bool use_access_seq=false, bool print_info = false);
	static int InitRead(uint64_t shmkey=DEFAUTL_HT_SHM_KEY, bool print_info = false);

	static const string &GetInitErrorMsg();
	static void Destroy(int fd);

	// Read from Cache
	int Read(bool own_buf = true); // own_buf - true:use allocated buffer, false: use static buffer
	bool HasData() { return tree && tree->IsValid(); }

	// 写入
	int Write(bool auto_remove = true); // 写入Cache，auto_remove: 空间不足时是否自动淘汰旧数据
	int Erase(); // 从Cache删除，删除成功返回1，没有删除返回0，失败返回<0

	// 调试
	const string &GetErrorMsg() const { return errmsg; }
	static void PrintInfo(int fd);
	static void ReportInfo(int fd);
	int FixHtBadNode();
	float GetHtUsage(int fd);
	float GetLtUsage(int fd);
};

#endif

