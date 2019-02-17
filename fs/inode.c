/*
 *  linux/fs/inode.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h> 
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/system.h>

struct m_inode inode_table[NR_INODE]={{0,},};

static void read_inode(struct m_inode * inode);
static void write_inode(struct m_inode * inode);

static inline void wait_on_inode(struct m_inode * inode)
{
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	sti();
}

static inline void lock_inode(struct m_inode * inode)
{
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	inode->i_lock=1;
	sti();
}

static inline void unlock_inode(struct m_inode * inode)
{
	inode->i_lock=0;
	wake_up(&inode->i_wait);
}

void invalidate_inodes(int dev)
{
	int i;
	struct m_inode * inode;

	inode = 0+inode_table;
	for(i=0 ; i<NR_INODE ; i++,inode++) {
		wait_on_inode(inode);
		if (inode->i_dev == dev) {
			if (inode->i_count)
				printk("inode in use on removed disk\n\r");
			inode->i_dev = inode->i_dirt = 0;
		}
	}
}

void sync_inodes(void)
{
	int i;
	struct m_inode * inode;

	inode = 0+inode_table;
	for(i=0 ; i<NR_INODE ; i++,inode++) {
		wait_on_inode(inode);
		if (inode->i_dirt && !inode->i_pipe)
      //写入磁盘
			write_inode(inode);
	}
}
//block对应的为块的个数
//返回当前文件inode节点中对应的第block块的块号，如果creat为1 那么当这个块不存在的时候 就分配新的块
static int _bmap(struct m_inode * inode,int block,int create)
{
	struct buffer_head * bh;
	int i;
//做参数检测
	if (block<0)
		panic("_bmap: block<0");
	if (block >= 7+512+512*512)
		panic("_bmap: block>big");
  //如果快号存在直接块号里
	if (block<7) {
    //如果创建标志为1 并且直接块不存在，就创建新的逻辑块，把逻辑块放入i_zone对应的区域
		if (create && !inode->i_zone[block])
    //分配新的逻辑块
			if ((inode->i_zone[block]=new_block(inode->i_dev))) {
				inode->i_ctime=CURRENT_TIME; 
				inode->i_dirt=1;
			}
		return inode->i_zone[block];
	}
  
	//文件占用的块号大于7K的，就要用到一次间接块号
	block -= 7;
	if (block<512) {
	//创建用来存储一次间接块的块
		if (create && !inode->i_zone[7])
			if ((inode->i_zone[7]=new_block(inode->i_dev))) {
				  inode->i_dirt=1;
				  inode->i_ctime=CURRENT_TIME;
			}
		if (!inode->i_zone[7])
			return 0;
    //给一级间接块分配缓冲区
		if (!(bh = bread(inode->i_dev,inode->i_zone[7])))
			return 0;
		i = ((unsigned short *) (bh->b_data))[block];
		if (create && !i)
      //创建新的块，并且给新的块对应的block位置赋值
			if ((i=new_block(inode->i_dev))) {
        //写到一级间接块的高速缓冲区中
				((unsigned short *) (bh->b_data))[block]=i;
				bh->b_dirt=1;
			}
      //高速缓冲区被同步回磁盘
		brelse(bh);
      //返回分配好的块号
		return i;
	}
//对二级间接块的处理
  block -= 512;
	if (create && !inode->i_zone[8])
    //分配二级间接块
		if ((inode->i_zone[8]=new_block(inode->i_dev))) {
			inode->i_dirt=1;
			inode->i_ctime=CURRENT_TIME;
		}
	if (!inode->i_zone[8])
		return 0;
	if (!(bh=bread(inode->i_dev,inode->i_zone[8])))
		return 0;
	i = ((unsigned short *)bh->b_data)[block>>9];
	if (create && !i)
		if ((i=new_block(inode->i_dev))) {
			((unsigned short *) (bh->b_data))[block>>9]=i;
			bh->b_dirt=1;
		}
	brelse(bh);
	if (!i)
		return 0;
	if (!(bh=bread(inode->i_dev,i)))
		return 0;
	i = ((unsigned short *)bh->b_data)[block&511];
	if (create && !i)
		if ((i=new_block(inode->i_dev))) {
			((unsigned short *) (bh->b_data))[block&511]=i;
			bh->b_dirt=1;
		}
	brelse(bh);
	return i;
}
//不带创建标志的读取块号
//open("path",mode,flags)
//flags  append可附加的
//creat
int bmap(struct m_inode * inode,int block)
{
	return _bmap(inode,block,0);
}
//可以创建
int create_block(struct m_inode * inode, int block)
{
	return _bmap(inode,block,1);
}

// 释放一个inode节点		
void iput(struct m_inode * inode)
{
	if (!inode)
		return;
	wait_on_inode(inode);
  //没有进程引用，那么报错释放一个空的inode节点
	if (!inode->i_count)
		panic("iput: trying to free free inode");
  //如果当前为一个pipe管道
	if (inode->i_pipe) {
		wake_up(&inode->i_wait);
    //当引用计数减少，但是还不为0的话 ，就返回 因为还有进程在引用
    //子进程close(listenfd) 
		if (--inode->i_count)
			return;
    //当引用计数减少后为0
    //释放pipe所占用的内存
		free_page(inode->i_size);
		inode->i_count=0;
		inode->i_dirt=0;
		inode->i_pipe=0;
		return;
	}
  //不是字符设备也不是块设备
	if (!inode->i_dev) {
		inode->i_count--;
		return;
	}
  //如果为块设备
	if (S_ISBLK(inode->i_mode)) {
    //同步块设备
		sync_dev(inode->i_zone[0]);
		wait_on_inode(inode);
	}
repeat:
    //当前不止一个进程在使用该inode节点
    //那么引用计数自减少后返回
	if (inode->i_count>1) {
		inode->i_count--;
		return;
	}
  //链接文件
	if (!inode->i_nlinks) {
		truncate(inode);
		free_inode(inode);
		return;
	}
  //如果inode被修改
  
	if (inode->i_dirt) {
		write_inode(inode);	/* we can sleep - so do again */
		wait_on_inode(inode);
		goto repeat;
	}
	inode->i_count--;
	return;
}

struct m_inode * get_empty_inode(void)
{
	struct m_inode * inode;
	static struct m_inode * last_inode = inode_table;
	int i;
			// 在inode节点数组中找到一个没有被应用 锁定修改的空槽
	do {
		inode = NULL;

    //从全局的inode_table中找到一个没有被引用 没有被锁定 没有被修改的inode节点
		for (i = NR_INODE; i ; i--) {
			if (++last_inode >= inode_table + NR_INODE)
				last_inode = inode_table;
      //在inode_table中的想引用计数为0
			if (!last_inode->i_count) {
				inode = last_inode;
				if (!inode->i_dirt && !inode->i_lock)
					break;
			}
		}
		//如果没有找到inode的空槽则打印当前全部的inode
		if (!inode) {
			for (i=0 ; i<NR_INODE ; i++)
				printk("%04x: %6d\t",inode_table[i].i_dev,
					inode_table[i].i_num);
			panic("No free inodes in mem");
		}
		wait_on_inode(inode);
		while (inode->i_dirt) {
			write_inode(inode);
			wait_on_inode(inode);
		}
	} while (inode->i_count);
  //清空找到的inode节点
	memset(inode,0,sizeof(*inode));
  //设置当前进程引用该inode节点
	inode->i_count = 1;
	return inode;
}


struct m_inode * get_pipe_inode(void)
{
	struct m_inode * inode;

	if (!(inode = get_empty_inode()))
		return NULL;
// 为当前inode节点分配一个内存页，做为PIPE
	if (!(inode->i_size=get_free_page())) {
		inode->i_count = 0;
		return NULL;
	}
	// 又读 又写
	inode->i_count = 2;	/* sum of readers/writers */
	//初始化PIPE的头尾
	PIPE_HEAD(*inode) = PIPE_TAIL(*inode) = 0;
	//置PIPE标志
	inode->i_pipe = 1;
	return inode;
}
//获得一个inode节点
// dev 设备号  nr inode节点号
struct m_inode * iget(int dev,int nr)
{
  //1. 分配一个inode节点
  //2. 设置这个inode节点
  //3. 向全局的inode_table中添加设置好的inode节点
	struct m_inode * inode, * empty;

	if (!dev)
		panic("iget with dev==0");
	empty = get_empty_inode();
	inode = inode_table;
  //遍历inode table表 找其中的dev或者inode号匹配的点
	while (inode < NR_INODE+inode_table) {
		if (inode->i_dev != dev || inode->i_num != nr) {
			inode++;
			continue;
		}
    
		wait_on_inode(inode);
    
		if (inode->i_dev != dev || inode->i_num != nr) {
			inode = inode_table;
			continue;
		}
		inode->i_count++;
		if (inode->i_mount) {
			int i;

			for (i = 0 ; i<NR_SUPER ; i++)
				if (super_block[i].s_imount==inode)
					break;
			if (i >= NR_SUPER) {
				printk("Mounted inode hasn't got sb\n");
				if (empty)
					iput(empty);
				return inode;
			}
			iput(inode);
			dev = super_block[i].s_dev;
			nr = ROOT_INO;
			inode = inode_table;
			continue;
		}
		if (empty)
			iput(empty);
		return inode;
	}
	if (!empty)
		return (NULL);
	inode=empty;
	inode->i_dev = dev;
	inode->i_num = nr;
	read_inode(inode);
	return inode;
}

// read inode读出来的inode节点信息 是不包含inode内存动态信息的
static void read_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;
	int block;

	lock_inode(inode);
	if (!(sb=get_super(inode->i_dev)))
		panic("trying to read inode without dev");
  //INODES_PER_BLOCK每一个块所占用的inode的大小
	block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
		(inode->i_num-1)/INODES_PER_BLOCK;
  
	if (!(bh=bread(inode->i_dev,block)))
		panic("unable to read i-node block");
	*(struct d_inode *)inode =
		((struct d_inode *)bh->b_data)
			[(inode->i_num-1)%INODES_PER_BLOCK];
	brelse(bh);
	unlock_inode(inode);
}

static void write_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;
	int block;

	lock_inode(inode);
	if (!inode->i_dirt || !inode->i_dev) {
		unlock_inode(inode);
		return;
	}
	if (!(sb=get_super(inode->i_dev)))
		panic("trying to write inode without device");
	//计算当前inode节点的逻辑块号
	block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
		(inode->i_num-1)/INODES_PER_BLOCK;
	if (!(bh=bread(inode->i_dev,block)))
		panic("unable to read i-node block");
	((struct d_inode *)bh->b_data)
		[(inode->i_num-1)%INODES_PER_BLOCK] =
			*(struct d_inode *)inode;
	bh->b_dirt=1;
	inode->i_dirt=0;
	brelse(bh);
	unlock_inode(inode);
}
