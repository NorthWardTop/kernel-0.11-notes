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
 �� �� ��  : release
 ��������  : 1.�ͷŸý�����ռ�õ��ڴ���Դ 
                   2.���ռ�õ�ȫ�ֽṹ��ָ������task�ж�Ӧ����һ��
                   3.���̵ĵ����л�
 �������  : struct task_struct * p  
 �������  : ��
 �� �� ֵ  : void
 ���ú���  : 
 ��������  : 
 
 �޸���ʷ      :
  1.��    ��   : 2017��2��4��
    ��    ��   : chandler
    �޸�����   : �����ɺ���

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
 �� �� ��  : send_sig
 ��������  : 1.��һ�����̷����ź�
 �������  : long sig                
             struct task_struct * p  
             int priv                
 �������  : ��
 �� �� ֵ  : static inline int
 ���ú���  : 
 ��������  : 
 
 �޸���ʷ      :
  1.��    ��   : 2017��2��4��
    ��    ��   : chandler
    �޸�����   : �����ɺ���

*****************************************************************************/
static inline int send_sig(long sig,struct task_struct * p,int priv)
{
  //�������ļ��
	if (!p || sig<1 || sig>32)
		return -EINVAL;
  //Linux�ں�����Ҫ�����źŵ�Ȩ��
  //�û�  root group  
	if (priv || (current->euid==p->euid) || suser())
    //��������Կ��� ����ô���ʵ�ָ�һ�����̷����ź�
		p->signal |= (1<<(sig-1));
	else
		return -EPERM;
	return 0;
}

/*****************************************************************************
 �� �� ��  : kill_session
 ��������  : 
                  1.�ر�һ���뵱ǰ������ͬ�Ự�Ľ���
                  2.��Ҫ�رջỰ�Ľ�������һ��SIGHUP���ź�
 �������  : void  
 �������  : ��
 �� �� ֵ  : static void
 ���ú���  : 
 ��������  : 
 
 �޸���ʷ      :
  1.��    ��   : 2017��2��4��
    ��    ��   : chandler
    �޸�����   : �����ɺ���

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

  //pidΪ0ʱ  ��id���ڵ�ǰ����pid����
	if (!pid) while (--p > &FIRST_TASK) {
		if (*p && (*p)->pgrp == current->pid) 
			if ((err=send_sig(sig,*p,1)))
				retval = err;
 //pid����0  ��ָ����pid����
	} else if (pid>0) while (--p > &FIRST_TASK) {
		if (*p && (*p)->pid == pid) 
			if ((err=send_sig(sig,*p,0)))
				retval = err;
   //pid=-1  ��ȫ�����̷����ź�
	} else if (pid == -1) while (--p > &FIRST_TASK) {
		if ((err = send_sig(sig,*p,0)))
			retval = err;
    //pid<-1��ʱ��  ����IDΪPID����ֵ�Ľ��̷����ź�
	} else while (--p > &FIRST_TASK)
		if (*p && (*p)->pgrp == -pid)
			if ((err = send_sig(sig,*p,0)))
				retval = err;
	return retval;
}

/*****************************************************************************
 �� �� ��  : tell_father
 ��������  : 1.��task���ҵ�һ���봫��pid��ͬ��task struct  ���䷢��SIGCHLD�ź�
 �������  : int pid  
 �������  : ��
 �� �� ֵ  : static void
 ���ú���  : 
 ��������  : 
 
 �޸���ʷ      :
  1.��    ��   : 2017��2��4��
    ��    ��   : chandler
    �޸�����   : �����ɺ���

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
          //��task���ҵ�һ���봫��pid��ͬ��task struct 
			task[i]->signal |= (1<<(SIGCHLD-1));
			return;
		}
/* if we don't find any fathers, we just release ourselves */
/* This is not really OK. Must change it to make father 1 */
	printk("BAD BAD - no father found\n\r");
	release(current);
}

/*****************************************************************************
 �� �� ��  : do_exit
 ��������  : �˳�����ϵͳ���ú���
    1.��ս���������ʹ�õ���Դ
    2.���ý���Ϊ����״̬
    3.�����˳�������丸����
    4.�������л�
 �������  : long code  
 �������  : ��
 �� �� ֵ  : int
 ���ú���  : 
 ��������  : 
 
 �޸���ʷ      :
  1.��    ��   : 2016��11��8��
    ��    ��   : chandler
    �޸�����   : �����ɺ���

*****************************************************************************/
int do_exit(long code)
{
	int i;
  //�ͷŴ�����ڴ�
	free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
  //�ͷ����ݶ��ڴ�
	free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
  
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i] && task[i]->father == current->pid) {
			task[i]->father = 1;
			if (task[i]->state == TASK_ZOMBIE)
				/* assumption task[1] is always init */
				(void) send_sig(SIGCHLD, task[1], 1);
		}
    //�رյ�ǰ���̴򿪵������ļ�
	for (i=0 ; i<NR_OPEN ; i++)
		if (current->filp[i])
			sys_close(i);
    //��յ�ǰ���̵�·��
	iput(current->pwd);
	current->pwd=NULL;
  //��յ�ǰ���̵�rootȨ��
	iput(current->root);
	current->root=NULL;
  //���һ��ִ�г����
	iput(current->executable);
	current->executable=NULL;
  //�����ǰ����ռ���˿���̨����ô�����
	if (current->leader && current->tty >= 0)
		tty_table[current->tty].pgrp = 0;
  //�����ǰ����ռ����Э�������������
	if (last_task_used_math == current)
		last_task_used_math = NULL;
  //�����ǰ�����лỰ
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
 �� �� ��  : sys_waitpid
 ��������  : 1.
 �������  : pid_t pid                  
             unsigned long * stat_addr  
             int options                
 �������  : ��
 �� �� ֵ  : int
 ���ú���  : 
 ��������  : 
 
 �޸���ʷ      :
  1.��    ��   : 2017��2��4��
    ��    ��   : chandler
    �޸�����   : �����ɺ���

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
    //�ҵ���ǰ���̵��ӽ���
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
        //ֱ���ͷ�������
				put_fs_long(0x7f,stat_addr);
				return (*p)->pid;
			case TASK_ZOMBIE:
        //�ѵ�ǰ���̵��ӽ��̵��û���ϵͳʱ��ӵ���ǰ������
				current->cutime += (*p)->utime;
				current->cstime += (*p)->stime;
            //�����ӽ���pid �� �˳���
				flag = (*p)->pid;
				code = (*p)->exit_code;
        //�ͷ��ӽ���
				release(*p);
        //�ͷ��ӽ��̵Ĵ����
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


