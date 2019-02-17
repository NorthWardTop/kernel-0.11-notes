#include <asm/segment.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/type.h>
#include <unistd.h>
#include <string.h>


char gProcBuf[1024];





int proc_read(int dev,char * buf,int count, off_t * pos)
{
    struct task_struct *pTask;
    unsigned int i=0;
    unsigned int iNum=0;
    unsigned int iNumTemp=0;
    if(!(*pos))
    {
        for(i=0;i<1024;i++)
            gProcBuf[i]=0;
        for(i=0;i<NR_TASKS;i++)
        {
            pTask=task[i];
             if(pTask!=NULL)
             {
                  iNum=sprintf(gProcBuf,"%s","pid   state   father   counter  start_time\n");
                  iNumTemp=sprintf(gProcBuf+iNum,"%d\t   %d/t   %d/t   %d/t   %d/n",pTask->pid,pTask->state,pTask->father \
                                                                                                               pTask->counter,pTask->start_time);
                  iNum=iNum+iNumTemp;
             }
          }
        
    }
    if(count>1024)
        count=1024;
    for(i=0;i<count;i++)
    {
          if(gProcBuf[i+(*pos)]=='\0')
                break;
          put_fs_byte(gProcBuf[i+(*pos)],buf+i+(*pos));
    }
    *pos=*pos+i;
      return i;
}


