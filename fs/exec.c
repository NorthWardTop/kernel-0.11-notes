/*
 *  linux/fs/exec.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * #!-checking implemented by tytso.
 */

/*
 * Demand-loading implemented 01.12.91 - no need to read anything but
 * the header into memory. The inode of the executable is put into
 * "current->executable", and page faults do the actual loading. Clean.
 *
 * Once more I can proudly say that linux stood up to being changed: it
 * was less than 2 hours work to get demand-loading completely implemented.
 */

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <a.out.h>

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/segment.h>

extern int sys_exit(int exit_code);
extern int sys_close(int fd);

/*
 * MAX_ARG_PAGES defines the number of pages allocated for arguments
 * and envelope for the new program. 32 should suffice, this gives
 * a maximum env+arg of 128kB !
 */
#define MAX_ARG_PAGES 32

/*
 * create_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */
static unsigned long * create_tables(char * p,int argc,int envc)
{
	unsigned long *argv,*envp;
	unsigned long * sp;

	sp = (unsigned long *) (0xfffffffc & (unsigned long) p);
	sp -= envc+1;
	envp = sp;
	sp -= argc+1;
	argv = sp;
	put_fs_long((unsigned long)envp,--sp);
	put_fs_long((unsigned long)argv,--sp);
	put_fs_long((unsigned long)argc,--sp);
	while (argc-->0) {
		put_fs_long((unsigned long) p,argv++);
		while (get_fs_byte(p++)) /* nothing */ ;
	}
	put_fs_long(0,argv);
	while (envc-->0) {
		put_fs_long((unsigned long) p,envp++);
		while (get_fs_byte(p++)) /* nothing */ ;
	}
	put_fs_long(0,envp);
	return sp;
}

/*
 * count() counts the number of arguments/envelopes
 */
 //计算参数表中参数项的个数
static int count(char ** argv)
{
	int i=0;
	char ** tmp;

	if ((tmp = argv))
		while (get_fs_long((unsigned long *) (tmp++)))
			i++;

	return i;
}

/*
 * 'copy_string()' copies argument/envelope strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 *
 * Modified by TYT, 11/24/91 to add the from_kmem argument, which specifies
 * whether the string and the string array are from user or kernel segments:
 * 
 * from_kmem     argv *        argv **
 *    0          user space    user space
 *    1          kernel space  user space
 *    2          kernel space  kernel space
 * 
 * We do this by playing games with the fs segment register.  Since it
 * it is expensive to load a segment register, we try to avoid calling
 * set_fs() unless we absolutely have to.
 */

static unsigned long copy_strings(int argc,char ** argv,unsigned long *page,
		unsigned long p, int from_kmem)
{
	char *tmp, *pag=NULL;
	int len, offset = 0;
	unsigned long old_fs, new_fs;

	if (!p)
		return 0;	/* bullet-proofing */

	new_fs = get_ds();
	old_fs = get_fs();
	if (from_kmem==2)
		// 设置fs  为 ds
		set_fs(new_fs);
	
	while (argc-- > 0) {
		if (from_kmem == 1)
			// 设置fs  为 ds
			set_fs(new_fs);
		//最后一个参数的地址 因argv在内核中所以要设置fs为当前ds值
		if (!(tmp = (char *)get_fs_long(((unsigned long *)argv)+argc)))
			panic("argc is wrong");
		if (from_kmem == 1)
			// 设置fs  为 fs
			set_fs(old_fs);
		//计算当前参数项的参数长度
		len=0;		/* remember zero-padding */
		// 计算每一条参数的字符长度 存在len里面
		do {
			len++;
		} while (get_fs_byte(tmp++));
		//更新
		if (p-len < 0) {	/* this shouldn't happen - 128kB */
			set_fs(old_fs);
			return 0;
		}
		// 预留出当前参数条目的空间，再放入条目
		while (len) {
			--p; --tmp; --len;
			//如果当前为第一个参数
			if (--offset < 0) {
				offset = p % PAGE_SIZE;
				//如果参数实体在内核 则切换FS为当前FS 用户数据段
				if (from_kmem==2)
					set_fs(old_fs);
				//当前制定存放参数的位置是否分配内存 没有分配则进行分配
				if (!(pag = (char *) page[p/PAGE_SIZE]) &&
				    !(pag = (char *) page[p/PAGE_SIZE] =
				      (unsigned long *) get_free_page())) 
					return 0;
				//切换FS为ds
				if (from_kmem==2)
					set_fs(new_fs);

			}
			*(pag + offset) = get_fs_byte(tmp);
		}
	}
	if (from_kmem==2)
		set_fs(old_fs);
	return p;
}

static unsigned long change_ldt(unsigned long text_size,unsigned long * page)
{
	unsigned long code_limit,data_limit,code_base,data_base;
	int i;

	code_limit = text_size+PAGE_SIZE -1;
	code_limit &= 0xFFFFF000;
	data_limit = 0x4000000;
	code_base = get_base(current->ldt[1]);
	data_base = code_base;
	//更新带有新程序长度的代码段和数据段
	set_base(current->ldt[1],code_base);
	set_limit(current->ldt[1],code_limit);
	set_base(current->ldt[2],data_base);
	set_limit(current->ldt[2],data_limit);
/* make sure fs points to the NEW data segment */
	__asm__("pushl $0x17\n\tpop %%fs"::);
	data_base += data_limit;
	for (i=MAX_ARG_PAGES-1 ; i>=0 ; i--) {
		data_base -= PAGE_SIZE;
		//当前已分配的参数内存页放入
		if (page[i])
			put_page(page[i],data_base);
	}
	return data_limit;
}

/*
 * 'do_execve()' executes a new program.
 */
 //  ./helloworld
 // ./run.sh
int do_execve(unsigned long * eip,long tmp,char * filename,
	char ** argv, char ** envp)
{
	struct m_inode * inode;
	struct buffer_head * bh;
	struct exec ex;
	unsigned long page[MAX_ARG_PAGES];
	int i,argc,envc;
	int e_uid, e_gid;
	int retval;
	int sh_bang = 0;
	// 让P的值为参数的最高参数项
	unsigned long p=PAGE_SIZE*MAX_ARG_PAGES-4;

	if ((0xffff & eip[1]) != 0x000f)
		panic("execve called from supervisor mode");
	for (i=0 ; i<MAX_ARG_PAGES ; i++)	/* clear page-table */
		page[i]=0;
	// 1.打开指定启动文件的inode节点
	if (!(inode=namei(filename)))		/* get executables inode */
		return -ENOENT;
	argc = count(argv);
	envc = count(envp);
	
restart_interp:
	// 2. 判断当前执行文件属性和执行权限
	if (!S_ISREG(inode->i_mode)) {	/* must be regular file */
		retval = -EACCES;
		goto exec_error2;
	}
	i = inode->i_mode;
	e_uid = (i & S_ISUID) ? inode->i_uid : current->euid;
	e_gid = (i & S_ISGID) ? inode->i_gid : current->egid;
	// 文件宿主和当前进程匹配
	if (current->euid == inode->i_uid)
		i >>= 6;
	//文件群id和当前进程匹配
	else if (current->egid == inode->i_gid)
		i >>= 3;
	if (!(i & 1) &&
	    !((inode->i_mode & 0111) && suser())) {
		retval = -ENOEXEC;
		goto exec_error2;
	}
	// 3.读取当前执行程序或shell脚本的文件头结构
	if (!(bh = bread(inode->i_dev,inode->i_zone[0]))) {
		retval = -EACCES;
		goto exec_error2;
	}
	ex = *((struct exec *) bh->b_data);	/* read exec-header */
	// 认定为一个shell脚本
	if ((bh->b_data[0] == '#') && (bh->b_data[1] == '!') && (!sh_bang)) {
		/*
		 * This section does the #! interpretation.
		 * Sorta complicated, but hopefully it will work.  -TYT
		 */

		char buf[1023], *cp, *interp, *i_name, *i_arg;
		unsigned long old_fs;

		strncpy(buf, bh->b_data+2, 1022);
		brelse(bh);
		iput(inode);
		buf[1022] = '\0';
		if ((cp = strchr(buf, '\n'))) {
			*cp = '\0';
			for (cp = buf; (*cp == ' ') || (*cp == '\t'); cp++);
		}
		if (!cp || *cp == '\0') {
			retval = -ENOEXEC; /* No interpreter name found */
			goto exec_error1;
		}
		//解析出shell的脚本解析程序名
		interp = i_name = cp;
		i_arg = 0;
		//让iname指向执行文件名
		for ( ; *cp && (*cp != ' ') && (*cp != '\t'); cp++) {
 			if (*cp == '/')
				i_name = cp+1;
		}
		//如果当前cp不等于0后面带着参数，让iarg指向你的参数
		if (*cp) {
			*cp++ = '\0';
			i_arg = cp;
		}
		/*
		 * OK, we've parsed out the interpreter name and
		 * (optional) argument.
		 */
		 // 用sh_bang 作为一个是否为第一次执行shell的状态
		if (sh_bang++ == 0) {
		// 为第一次载入shell 建立拷贝环境变量和参数表
			p = copy_strings(envc, envp, page, p, 0);
		//执行程序名 也算一个参数 在拷贝参数的时候不进行执行名称的拷贝
			p = copy_strings(--argc, argv+1, page, p, 0);
		}
		/*
		 * Splice in (1) the interpreter's name for argv[0]
		 *           (2) (optional) argument to interpreter
		 *           (3) filename of shell script
		 *
		 * This is done in reverse order, because of how the
		 * user environment and arguments are stored.
		 */
		p = copy_strings(1, &filename, page, p, 1);
		argc++;
		if (i_arg) {
			p = copy_strings(1, &i_arg, page, p, 2);
			argc++;
		}
		p = copy_strings(1, &i_name, page, p, 2);
		argc++;
		// 当前参数页 page满了
		if (!p) {
			retval = -ENOMEM;
			goto exec_error1;
		}
		/*
		 * OK, now restart the process with the interpreter's inode.
		 */
		 // 备份fs地址 让给fs变为 ds
		old_fs = get_fs();
		set_fs(get_ds());
		// 执行程序的inode节点
		if (!(inode=namei(interp))) { /* get executables inode */
			set_fs(old_fs);
			retval = -ENOENT;
			goto exec_error1;
		}
		set_fs(old_fs);
		goto restart_interp;
	}
	brelse(bh);
	// 4 根据头文件信息进行当前程序的判定
	if (N_MAGIC(ex) != ZMAGIC || ex.a_trsize || ex.a_drsize ||
		ex.a_text+ex.a_data+ex.a_bss>0x3000000 ||
		inode->i_size < ex.a_text+ex.a_data+ex.a_syms+N_TXTOFF(ex)) {
		retval = -ENOEXEC;
		goto exec_error2;
	}
	// 文件头描述不为1K 则出错
	if (N_TXTOFF(ex) != BLOCK_SIZE) {
		printk("%s: N_TXTOFF != BLOCK_SIZE. See a.out.h.", filename);
		retval = -ENOEXEC;
		goto exec_error2;
	}
// 5. 拷贝执行文件的 环境变量和参数
	if (!sh_bang) {
		
		p = copy_strings(envc,envp,page,p,0);
		p = copy_strings(argc,argv,page,p,0);
		if (!p) {
			retval = -ENOMEM;
			goto exec_error2;
		}
	}
/* OK, This is the point of no return */
// 6.  执行线程时  进程要做的切换和更新
	//更新当前进程的执行程序inode节点
	if (current->executable)
		iput(current->executable);
	current->executable = inode;
	// 清空当前进行的信号处理函数
	for (i=0 ; i<32 ; i++)
		current->sigaction[i].sa_handler = NULL;
	// 关闭原线程指定执行后就关闭的文件并清空其位图
	// file【32 】
	for (i=0 ; i<NR_OPEN ; i++)
		if ((current->close_on_exec>>i)&1)
			sys_close(i);
	current->close_on_exec = 0;
    // 释放原线程的代码段和数据段
	free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
	free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
//  释放原线程的协处理器
	if (last_task_used_math == current)
		last_task_used_math = NULL;
	current->used_math = 0;
	// 设置整个程序的内存映射，并且更新进程的各端信息
	p += change_ldt(ex.a_text,page)-MAX_ARG_PAGES*PAGE_SIZE;
	p = (unsigned long) create_tables((char *)p,argc,envc);
	current->brk = ex.a_bss +
		(current->end_data = ex.a_data +
		(current->end_code = ex.a_text));
	current->start_stack = p & 0xfffff000;
	current->euid = e_uid;
	current->egid = e_gid;
	i = ex.a_text+ex.a_data;
	// 清空bss段
	while (i&0xfff)
		put_fs_byte(0,(char *) (i++));
	eip[0] = ex.a_entry;		/* eip, magic happens :-) */
	eip[3] = p;			/* stack pointer */
	return 0;
exec_error2:
	iput(inode);
exec_error1:
	for (i=0 ; i<MAX_ARG_PAGES ; i++)
		free_page(page[i]);
	return(retval);
}
