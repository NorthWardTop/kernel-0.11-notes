/*
 *  linux/kernel/exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <asm/segment.h>

int sys_pause(void);
int sys_close(int fd);

/*****************************************************************************
 函 数 名  : release
 功能描述  : 1.释放该进程所占用的内存资源 
                   2.清空占用的全局结构体指针数组task中对应的那一项
                   3.进程的调度切换
 输入参数  : struct task_struct * p  
 输出参数  : 无
 返 回 值  : void
 调用函数  : 
 被调函数  : 
 
 修改历史      :
  1.日    期   : 2017年2月4日
    作    者   : chandler
    修改内容   : 新生成函数

*****************************************************************************/
void release(struct task_struct * p)
{
	int i;

	if (!p)
		return;
	for (i=1 ; i<NR_TASKS ; i++)
		if (task[i]==p) {
			task[i]=NULL;
			free_page((long)p);
			schedule();
			return;
		}
	panic("trying to release non-existent task");
}

/*****************************************************************************
 函 数 名  : send_sig
 功能描述  : 1.给一个进程发送信号
 输入参数  : long sig                
             struct task_struct * p  
             int priv                
 输出参数  : 无
 返 回 值  : static inline int
 调用函数  : 
 被调函数  : 
 
 修改历史      :
  1.日    期   : 2017年2月4日
    作    者   : chandler
    修改内容   : 新生成函数

*****************************************************************************/
static inline int send_sig(long sig,struct task_struct * p,int priv)
{
  //做参数的检查
	if (!p || sig<1 || sig>32)
		return -EINVAL;
  //Linux内核中想要发送信号的权限
  //用户  root group  
	if (priv || (current->euid==p->euid) || suser())
    //从这里可以看出 如何用代码实现给一个进程发送信号
		p->signal |= (1<<(sig-1));
	else
		return -EPERM;
	return 0;
}

/*****************************************************************************
 函 数 名  : kill_session
 功能描述  : 
                  1.关闭一个与当前进程相同会话的进程
                  2.给要关闭会话的进程设置一个SIGHUP的信号
 输入参数  : void  
 输出参数  : 无
 返 回 值  : static void
 调用函数  : 
 被调函数  : 
 
 修改历史      :
  1.日    期   : 2017年2月4日
    作    者   : chandler
    修改内容   : 新生成函数

*****************************************************************************/
static void kill_session(void)
{
	struct task_struct **p = NR_TASKS + task;
	
	while (--p > &FIRST_TASK) {
		if (*p && (*p)->session == current->session)
			(*p)->signal |= 1<<(SIGHUP-1);
	}
}

/*
 * XXX need to check permissions needed to send signals to process
 * groups, etc. etc.  kill() permissions semantics are tricky!
 */
int sys_kill(int pid,int sig)
{
	struct task_struct **p = NR_TASKS + task;
	int err, retval = 0;

  //pid为0时  组id等于当前进程pid发送
	if (!pid) while (--p > &FIRST_TASK) {
		if (*p && (*p)->pgrp == current->pid) 
			if ((err=send_sig(sig,*p,1)))
				retval = err;
 //pid大于0  给指定的pid发送
	} else if (pid>0) while (--p > &FIRST_TASK) {
		if (*p && (*p)->pid == pid) 
			if ((err=send_sig(sig,*p,0)))
				retval = err;
   //pid=-1  给全部进程发送信号
	} else if (pid == -1) while (--p > &FIRST_TASK) {
		if ((err = send_sig(sig,*p,0)))
			retval = err;
    //pid<-1的时候  给组ID为PID绝对值的进程发送信号
	} else while (--p > &FIRST_TASK)
		if (*p && (*p)->pgrp == -pid)
			if ((err = send_sig(sig,*p,0)))
				retval = err;
	return retval;
}

/*****************************************************************************
 函 数 名  : tell_father
 功能描述  : 1.在task中找到一项与传入pid相同的task struct  给其发送SIGCHLD信号
 输入参数  : int pid  
 输出参数  : 无
 返 回 值  : static void
 调用函数  : 
 被调函数  : 
 
 修改历史      :
  1.日    期   : 2017年2月4日
    作    者   : chandler
    修改内容   : 新生成函数

*****************************************************************************/
static void tell_father(int pid)
{
	int i;

	if (pid)
		for (i=0;i<NR_TASKS;i++) {
			  if (!task[i])
				continue;
			if (task[i]->pid != pid)
				continue;
          //在task中找到一项与传入pid相同的task struct 
			task[i]->signal |= (1<<(SIGCHLD-1));
			return;
		}
/* if we don't find any fathers, we just release ourselves */
/* This is not really OK. Must change it to make father 1 */
	printk("BAD BAD - no father found\n\r");
	release(current);
}

/*****************************************************************************
 函 数 名  : do_exit
 功能描述  : 退出进程系统调用函数
    1.清空进程运行中使用的资源
    2.设置进程为僵死状态
    3.设置退出码告诉其父进程
    4.做进程切换
 输入参数  : long code  
 输出参数  : 无
 返 回 值  : int
 调用函数  : 
 被调函数  : 
 
 修改历史      :
  1.日    期   : 2016年11月8日
    作    者   : chandler
    修改内容   : 新生成函数

*****************************************************************************/
int do_exit(long code)
{
	int i;
  //释放代码段内存
	free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
  //释放数据段内存
	free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
  
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i] && task[i]->father == current->pid) {
			task[i]->father = 1;
			if (task[i]->state == TASK_ZOMBIE)
				/* assumption task[1] is always init */
				(void) send_sig(SIGCHLD, task[1], 1);
		}
    //关闭当前进程打开的所有文件
	for (i=0 ; i<NR_OPEN ; i++)
		if (current->filp[i])
			sys_close(i);
    //清空当前进程的路径
	iput(current->pwd);
	current->pwd=NULL;
  //清空当前进程的root权限
	iput(current->root);
	current->root=NULL;
  //清空一个执行程序表
	iput(current->executable);
	current->executable=NULL;
  //如果当前进程占用了控制台，那么就清空
	if (current->leader && current->tty >= 0)
		tty_table[current->tty].pgrp = 0;
  //如果当前进程占用了协处理器，则清空
	if (last_task_used_math == current)
		last_task_used_math = NULL;
  //如果当前进程有会话
	if (current->leader)
		kill_session();
	current->state = TASK_ZOMBIE;
	current->exit_code = code;
	tell_father(current->father);
	schedule();
	return (-1);	/* just to suppress warnings */
}

int sys_exit(int error_code)
{
	return do_exit((error_code&0xff)<<8);
}


/*****************************************************************************
 函 数 名  : sys_waitpid
 功能描述  : 1.
 输入参数  : pid_t pid                  
             unsigned long * stat_addr  
             int options                
 输出参数  : 无
 返 回 值  : int
 调用函数  : 
 被调函数  : 
 
 修改历史      :
  1.日    期   : 2017年2月4日
    作    者   : chandler
    修改内容   : 新生成函数

*****************************************************************************/
int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options)
{
	int flag, code;
	struct task_struct ** p;

	verify_area(stat_addr,4);
repeat:
	flag=0;
	for(p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p || *p == current)
			continue;
    //找到当前进程的子进程
		if ((*p)->father != current->pid)
			continue;
    
		if (pid>0) {
			if ((*p)->pid != pid)
				continue;
		} else if (!pid) {
			if ((*p)->pgrp != current->pgrp)
				continue;
		} else if (pid != -1) {
			if ((*p)->pgrp != -pid)
				continue;
		}
		switch ((*p)->state) {
			case TASK_STOPPED:
				if (!(options & WUNTRACED))
					continue;
        //直接释放其代码段
				put_fs_long(0x7f,stat_addr);
				return (*p)->pid;
			case TASK_ZOMBIE:
        //把当前进程的子进程的用户和系统时间加到当前进程上
				current->cutime += (*p)->utime;
				current->cstime += (*p)->stime;
            //保存子进程pid 和 退出码
				flag = (*p)->pid;
				code = (*p)->exit_code;
        //释放子进程
				release(*p);
        //释放子进程的代码段
				put_fs_long(code,stat_addr);
				return flag;
			default:
				flag=1;
				continue;
		}
	}
	if (flag) {
		if (options & WNOHANG)
			return 0;
		current->state=TASK_INTERRUPTIBLE;
		schedule();
		if (!(current->signal &= ~(1<<(SIGCHLD-1))))
			goto repeat;
		else
			return -EINTR;
	}
	return -ECHILD;
}


