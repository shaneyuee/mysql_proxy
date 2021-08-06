#include<sys/types.h>
#include<sys/wait.h>
#include<unistd.h>
#include<stdio.h>
#include<stdlib.h>
#include<signal.h>
#include<sys/time.h>
#include"fork_children.h"

#include"Attr_API.h"

#define MAXPIDNUM  1024
#define MIN_INTERVAL 1    //两次重启时间间隔最小 1秒

static 	struct  timeval  preTimeArr[MAXPIDNUM];	       //进程上一次启动的时间
static  int              chldFlagArr[MAXPIDNUM] = {0}; //哪个子进程发送了SIG_CHLD信号，则标记下
static 	pid_t            pidArray[1024] = {0}; 
static  unsigned         gChildNum      =  0;   

static void NormalQuit(int iSigNo)
{
	 switch(iSigNo)
	 {
		 case SIGQUIT:
		 case SIGTERM:
			exit(0); //父进程收到SIGQUIT,SIGTERM 则不执行监控子进程进行秒起，自己退出。	
			break;
		 default:
			break;
	 }	 
}

static void DealSigChld(int iSigNo)
{
	int nr = 0;
	Attr_API(433894 ,1); //子进程死了需要上报
	do
	{
		pid_t chidPid = waitpid(-1, NULL, WNOHANG);
		if( -1 == chidPid || 0 == chidPid )
			break;
		nr ++;
	
		unsigned j = 0;	
		for(j=0; j<gChildNum; ++j)
		{
			if(pidArray[j] == chidPid )	 //根据挂了的子进程的PID找到其对应的位置打个标记
			{
				chldFlagArr[j] = 1;
				break;
			}
		}
		if( j == gChildNum )
		{
			Attr_API(433299, 1); //找不到挂了的子进程的PID
		}
	}
	while(1);

	if(nr==0)
		Attr_API(433296,1);
}

int ForkChildren( unsigned  childNum )
{
	unsigned  i = 0;
	pid_t pid = 0;
	unsigned iSucNum = 0;
	
	struct  timeval startTime ;

	struct  sigaction newAction;
	struct  sigaction oldQuitAction;
	struct  sigaction oldTermAction;
	struct  sigaction newChldAction;
	struct  sigaction oldChldAction;
	struct  sigaction tmpChldAction;

	newAction.sa_handler =  NormalQuit;
	newAction.sa_flags  =0;
	sigemptyset(&newAction.sa_mask);

	newChldAction.sa_handler =DealSigChld;
	newChldAction.sa_flags   = 0;
	sigemptyset(&newChldAction.sa_mask);

	tmpChldAction.sa_handler = SIG_IGN;
	tmpChldAction.sa_flags = 0;
	sigemptyset(&tmpChldAction.sa_mask);

	sigaction(SIGQUIT, &newAction, &oldQuitAction);
	sigaction(SIGTERM, &newAction, &oldTermAction);
	sigaction(SIGCHLD, &tmpChldAction, &oldChldAction);
	if( childNum > sizeof(pidArray)/sizeof(pid_t) )
	{
		printf("Error!! childNum is larger than sizeof pidArray \n");
		return -1;
	}
	gChildNum = childNum;


	gettimeofday(&startTime, NULL);		//将这个时间作为每个子进程启动的时间

	for( i=0; i < childNum ; ++i )
	{
		pid  = fork();	
		if(0 == pid ) // is Child Process
		{
			sigaction(SIGQUIT, &oldQuitAction, NULL );
			sigaction(SIGTERM, &oldTermAction, NULL );
			sigaction(SIGCHLD, &oldChldAction, NULL );
			return i;
		}
		else if( pid > 0 ) // is Parent Process
		{
			iSucNum ++;
			pidArray[i] =  pid ; // 存储子进程的 PID 
			preTimeArr[i] = startTime;
		}
		else if ( -1 == pid) // create child process failure
		{
			pidArray[i] = 0;
			Attr_API(433295, 1);
		}
	}
	printf("iSucNum=%d\n",iSucNum);
	if(iSucNum < childNum )  //fork生成的子进程小于预期，父进程killall子进程，然后退出
	{
		for(i =0; i < childNum; ++i )			
		{
			if( pidArray[i] != 0 ) kill(pidArray[i], SIGKILL);
		}	
		Attr_API(436167, 1);
		return -2;
	}


	sigaction(SIGCHLD, &newChldAction, NULL);
	while(1)
	{
		sleep(1);
		struct timeval curTime ;
		gettimeofday(&curTime, NULL); 
		for(i = 0; i< childNum; ++i ) 
		{
			if( 1 == chldFlagArr[i] )  //找到一个挂了的子进程
			{
				if( curTime.tv_sec - preTimeArr[i].tv_sec >= MIN_INTERVAL	) //两次时间满足条件，父进程则拉起子进程
				{
					pid_t resPid = fork();
					if( 0 == resPid)  //childProcess
					{
						sigaction(SIGCHLD, &oldChldAction, NULL );
						sigaction(SIGQUIT, &oldQuitAction, NULL );
						sigaction(SIGTERM, &oldTermAction, NULL );
						return i;
					}
					if( resPid > 0 )  //parentProcess
					{
						chldFlagArr[i] = 0;    //清空需要重启的标记
						Attr_API(433297, 1);
						pidArray[i] = resPid ;
						preTimeArr[i] = curTime; //更新子进程重启时间
					
					}
					if( -1 == resPid )   //重启失败，等到下次再重启
					{
						Attr_API(433298, 1);
					}
				}
				else   //间隔时间太短，子进程短时间内挂了，认为是异常
				{
					Attr_API(436169,1);
				}
			}
		}	
	}	
	return 0;
}
