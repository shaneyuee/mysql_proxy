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
#define MIN_INTERVAL 1    //��������ʱ������С 1��

static 	struct  timeval  preTimeArr[MAXPIDNUM];	       //������һ��������ʱ��
static  int              chldFlagArr[MAXPIDNUM] = {0}; //�ĸ��ӽ��̷�����SIG_CHLD�źţ�������
static 	pid_t            pidArray[1024] = {0}; 
static  unsigned         gChildNum      =  0;   

static void NormalQuit(int iSigNo)
{
	 switch(iSigNo)
	 {
		 case SIGQUIT:
		 case SIGTERM:
			exit(0); //�������յ�SIGQUIT,SIGTERM ��ִ�м���ӽ��̽��������Լ��˳���	
			break;
		 default:
			break;
	 }	 
}

static void DealSigChld(int iSigNo)
{
	int nr = 0;
	Attr_API(433894 ,1); //�ӽ���������Ҫ�ϱ�
	do
	{
		pid_t chidPid = waitpid(-1, NULL, WNOHANG);
		if( -1 == chidPid || 0 == chidPid )
			break;
		nr ++;
	
		unsigned j = 0;	
		for(j=0; j<gChildNum; ++j)
		{
			if(pidArray[j] == chidPid )	 //���ݹ��˵��ӽ��̵�PID�ҵ����Ӧ��λ�ô�����
			{
				chldFlagArr[j] = 1;
				break;
			}
		}
		if( j == gChildNum )
		{
			Attr_API(433299, 1); //�Ҳ������˵��ӽ��̵�PID
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


	gettimeofday(&startTime, NULL);		//�����ʱ����Ϊÿ���ӽ���������ʱ��

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
			pidArray[i] =  pid ; // �洢�ӽ��̵� PID 
			preTimeArr[i] = startTime;
		}
		else if ( -1 == pid) // create child process failure
		{
			pidArray[i] = 0;
			Attr_API(433295, 1);
		}
	}
	printf("iSucNum=%d\n",iSucNum);
	if(iSucNum < childNum )  //fork���ɵ��ӽ���С��Ԥ�ڣ�������killall�ӽ��̣�Ȼ���˳�
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
			if( 1 == chldFlagArr[i] )  //�ҵ�һ�����˵��ӽ���
			{
				if( curTime.tv_sec - preTimeArr[i].tv_sec >= MIN_INTERVAL	) //����ʱ�������������������������ӽ���
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
						chldFlagArr[i] = 0;    //�����Ҫ�����ı��
						Attr_API(433297, 1);
						pidArray[i] = resPid ;
						preTimeArr[i] = curTime; //�����ӽ�������ʱ��
					
					}
					if( -1 == resPid )   //����ʧ�ܣ��ȵ��´�������
					{
						Attr_API(433298, 1);
					}
				}
				else   //���ʱ��̫�̣��ӽ��̶�ʱ���ڹ��ˣ���Ϊ���쳣
				{
					Attr_API(436169,1);
				}
			}
		}	
	}	
	return 0;
}
