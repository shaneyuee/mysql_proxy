#ifndef _FORK_CHILDREN_INCLUDE_
#define _FORK_CHILDREN_INCLUDE_

#ifdef __cplusplus
extern "C"{
#endif
/*
 * return :
 *       返回<0， 代表函数失败,只有父进程才会返回;
 *       -1 ,进程个数太多，目前代码支持1024个子进程;
 *       -2 ,创建子进程时，没有全部创建成功。父进程会杀死已经创建好的子进程;
 *       返回值>=0, 代表分配给子进程的编号,只有子进程返回;
 * 	   	        分配给子进程的进程编号，从0开始递增到childNum-1；
 * parameter:
 *    unsigned childNum:   要生的子进程的个数。
 * 
 * 注意事项：
 *     函数执行完毕后，有1个父进程， childNum个子进程; 
 *     父进程只负责监控子进程，如果子进程死了，重新拉起，给拉起的子进程分配和原来相同的进程编号;
 *     子进程负责处理业务逻辑;
 *     ForkChildren之后，不要再用daemonInit,不然会导致创建的子进程挂了，父进程会拉起子进程，然后反复下去，形成循环;
 *     对于同一个ID的子进程，如果子进程挂的时间距离上一次启动时间间隔<N秒，父进程不会立即拉起，会等到时间间隔满足条件，然后再拉起。
 *
*/

int ForkChildren(unsigned childNum);

#ifdef __cplusplus
}
#endif

#endif
