/*
 *  linux/fs/super.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * super.c contains code to handle the super-block tables.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include <errno.h>
#include <sys/stat.h>

int sync_dev(int dev);
void wait_for_keypress(void);

/* set_bit uses setb, as gas doesn't recognize setc */
//test_bit
#define set_bit(bitnr,addr) ({ \
register int __res ; \
__asm__("bt %2,%3;setb %%al":"=a" (__res):"a" (0),"r" (bitnr),"m" (*(addr))); \
__res; })
//全局的已挂载硬盘的超级块数组
struct super_block super_block[NR_SUPER];
/* this is initialized in init/main.c */
int ROOT_DEV = 0;

static void lock_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sb->s_lock = 1;
  //mutex spin 
	sti();
}

static void free_super(struct super_block * sb)
{
	cli();
	sb->s_lock = 0;
	wake_up(&(sb->s_wait));
	sti();
}

static void wait_on_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sti();
}

//通过当前的块设备的设备号来获取一个硬盘的超级块
struct super_block * get_super(int dev)
{
	struct super_block * s;

	if (!dev)
		return NULL;
	s = 0+super_block;
  //遍历超级块数组
	while (s < NR_SUPER+super_block)
		if (s->s_dev == dev) {
			wait_on_super(s);
			if (s->s_dev == dev)
				return s;
			s = 0+super_block;
		} else
			s++;
	return NULL;
}
//释放一个超级块  unmount
void put_super(int dev)
{
	struct super_block * sb;
	/* struct m_inode * inode;*/
	int i;
     //判断当前要放置的设备是否为Linux的根文件设备
	if (dev == ROOT_DEV) {
		printk("root diskette changed: prepare for armageddon\n\r");
		return;
	}
  //找到要释放的超级块
	if (!(sb = get_super(dev)))
		return;
 //判断当前指定的设备已经被挂载
 //当前要释放超级块的硬盘一定是不能被挂载的
	if (sb->s_imount) {
		printk("Mounted disk changed - tssk, tssk\n\r");
		return;
	}
  //锁定要释放的超级块，不让其他进程进行使用
	lock_super(sb);
  //释放工作
	sb->s_dev = 0;
  //做inode节点位图 逻辑块位图的缓冲区释放  会回写硬盘
	for(i=0;i<I_MAP_SLOTS;i++)
		brelse(sb->s_imap[i]);
	for(i=0;i<Z_MAP_SLOTS;i++)
		brelse(sb->s_zmap[i]);
  //调用释放超级块
	free_super(sb);
  // 还没有从全局的super_block数组中清空当前释放的超级块
	return;
}

//
static struct super_block * read_super  (int dev)
{
	struct super_block * s;
	struct buffer_head * bh;
	int i,block;

	if (!dev)
		return NULL;
	check_disk_change(dev);
  //获取超级块
	if ((s = get_super(dev)))
		return s;

  
    //如果没找到 就创建这个超级块
	//找对应super_block 数组中的空槽
	for (s = 0+super_block ;; s++) {
		if (s >= NR_SUPER+super_block)
			return NULL;
		if (!s->s_dev)
			break;
	}

	//初始化超级块在内存中的一些动态配置项
	s->s_dev = dev;
	s->s_isup = NULL;
	s->s_imount = NULL;
	s->s_time = 0;
	s->s_rd_only = 0;
	s->s_dirt = 0;
	lock_super(s);
  //从硬盘的超级块中读取到高速缓冲区中
	if (!(bh = bread(dev,1))) {
		s->s_dev=0;
		free_super(s);
		return NULL;
	}
  //根据读取的只存在硬盘的超级块数据 来进行内存超级块数据的构建
	//设置设备中固有的超级块配置参数
	*((struct d_super_block *) s) =
		*((struct d_super_block *) bh->b_data);
  //memcpy
	brelse(bh);
	// 检测文件系统的ID号，如果不支持该文件系统则释放资源并返回
	if (s->s_magic != SUPER_MAGIC) {
		s->s_dev = 0;
		free_super(s);
		return NULL;
	}
  //清空对应的inode 逻辑块位图
	for (i=0;i<I_MAP_SLOTS;i++)
		s->s_imap[i] = NULL;
	for (i=0;i<Z_MAP_SLOTS;i++)
		s->s_zmap[i] = NULL;
  //磁盘中超级块对应的块号为1  inode结点位图对应的为2
	block=2;
	//根据取出的超级块信息 分配该设备文件系统的i节点位图
	for (i=0 ; i < s->s_imap_blocks ; i++)
		if ((s->s_imap[i]=bread(dev,block)))
			block++;
		else
			break;
	for (i=0 ; i < s->s_zmap_blocks ; i++)
		if ((s->s_zmap[i]=bread(dev,block)))
			block++;
		else
			break;
	//出错就还原一切
	if (block != 2+s->s_imap_blocks+s->s_zmap_blocks) {
		for(i=0;i<I_MAP_SLOTS;i++)
			brelse(s->s_imap[i]);
		for(i=0;i<Z_MAP_SLOTS;i++)
			brelse(s->s_zmap[i]);
		s->s_dev=0;
		free_super(s);
		return NULL;
	}
  //磁盘中的第一个inode节点 和  第一个逻辑块是不使用的
	s->s_imap[0]->b_data[0] |= 1;
	s->s_zmap[0]->b_data[0] |= 1;
	free_super(s);
	return s;
}

int sys_umount(char * dev_name)
{
	struct m_inode * inode;
	struct super_block * sb;
	int dev;

	if (!(inode=namei(dev_name)))
		return -ENOENT;
	// inode节点中第一个直接块号位其设备号
	dev = inode->i_zone[0];
	//判断当前是否为块设备
	if (!S_ISBLK(inode->i_mode)) {
		iput(inode);
		return -ENOTBLK;
	}
  //如果为块设备
	iput(inode);
	if (dev==ROOT_DEV)
		return -EBUSY;
	//如果没有被挂接或者读取失败则返回错误
	if (!(sb=get_super(dev)) || !(sb->s_imount))
		return -ENOENT;
	//如果该文件系统的挂接节点的挂接标志位空，则返回错误
	if (!sb->s_imount->i_mount)
		printk("Mounted inode has i_mount=0\n");
	//检索系统的I节点表，如果有进程正在使用该设备上的文件，则返回忙错误
	for (inode=inode_table+0 ; inode<inode_table+NR_INODE ; inode++)
		if (inode->i_dev==dev && inode->i_count)
				return -EBUSY;
    //清空挂载属性
	sb->s_imount->i_mount=0;
    //释放挂在结点
	iput(sb->s_imount);
	sb->s_imount = NULL;
	iput(sb->s_isup);
	sb->s_isup = NULL;
	//释放块 并同步设备
	put_super(dev);
	sync_dev(dev);
	return 0;
}

//如何挂载一个硬盘设备
//挂载名
//挂载路径名称
//读写属性
int sys_mount(char * dev_name, char * dir_name, int rw_flag)
{
	struct m_inode * dev_i, * dir_i;
	struct super_block * sb;
	int dev;
    //通过路径来检索一个inode结点
	if (!(dev_i=namei(dev_name)))
		return -ENOENT;
  //通过inode节点拿到一个设备号
	dev = dev_i->i_zone[0];
  
	if (!S_ISBLK(dev_i->i_mode)) {
		iput(dev_i);
		return -EPERM;
	}
	iput(dev_i);
	if (!(dir_i=namei(dir_name)))
		return -ENOENT;
	if (dir_i->i_count != 1 || dir_i->i_num == ROOT_INO) {
		iput(dir_i);
		return -EBUSY;
	}
	if (!S_ISDIR(dir_i->i_mode)) {
		iput(dir_i);
		return -EPERM;
	}
	if (!(sb=read_super(dev))) {
		iput(dir_i);
		return -EBUSY;
	}
	if (sb->s_imount) {
		iput(dir_i);
		return -EBUSY;
	}
	if (dir_i->i_mount) {
		iput(dir_i);
		return -EPERM;
	}
  //设置当前的挂载路径
	sb->s_imount=dir_i;
	dir_i->i_mount=1;
  //设置超级块被修改
	dir_i->i_dirt=1;		/* NOTE! we don't iput(dir_i) */
	return 0;			/* we do that in umount */
}

void mount_root(void)
{
	int i,free;
	struct super_block * p;
	struct m_inode * mi;

	if (32 != sizeof (struct d_inode))
		panic("bad i-node size");
	// 文件表数组的初始化
	for(i=0;i<NR_FILE;i++)
		file_table[i].f_count=0;
	//验证根文件系统是否存在软盘上
	if (MAJOR(ROOT_DEV) == 2) {
		printk("Insert root floppy and press ENTER");
		wait_for_keypress();
	}
	//初始化超级块数组元素
	for(p = &super_block[0] ; p < &super_block[NR_SUPER] ; p++) {
		p->s_dev = 0;
		p->s_lock = 0;
		p->s_wait = NULL;
	}
	if (!(p=read_super(ROOT_DEV)))
		panic("Unable to mount root");
	if (!(mi=iget(ROOT_DEV,ROOT_INO)))
		panic("Unable to read root i-node");
	//给根文件系统inode节点引用数+3
	mi->i_count += 3 ;	/* NOTE! it is logically used 4 times, not 1 */
	p->s_isup = p->s_imount = mi;
	current->pwd = mi;
	current->root = mi;
	free=0;
	i=p->s_nzones;
	// 打印 当前文件系统的inode节点和逻辑块的空闲个数
	while (-- i >= 0)
		//监测逻辑块位图的空闲个数
		if (!set_bit(i&8191,p->s_zmap[i>>13]->b_data))
			free++;
	printk("%d/%d free blocks\n\r",free,p->s_nzones);
	free=0;
	i=p->s_ninodes+1;
	while (-- i >= 0)
		if (!set_bit(i&8191,p->s_imap[i>>13]->b_data))
			free++;
	printk("%d/%d free inodes\n\r",free,p->s_ninodes);
}
