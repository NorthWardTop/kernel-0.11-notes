/*
 *  linux/fs/buffer.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'buffer.c' implements the buffer-cache functions. Race-conditions have
 * been avoided by NEVER letting a interrupt change a buffer (except for the
 * data, of course), but instead letting the caller do it. NOTE! As interrupts
 * can wake up a caller, some cli-sti sequences are needed to check for
 * sleep-on-calls. These should be extremely quick, though (I hope).
 */

/*
 * NOTE! There is one discordant note here: checking floppies for
 * disk change. This is where it fits best, I think, as it should
 * invalidate changed floppy-disk-caches.
 */

#include <stdarg.h>
 
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/io.h>

extern int end;
extern void put_super(int);
extern void invalidate_inodes(int);

struct buffer_head * start_buffer = (struct buffer_head *) &end;
struct buffer_head * hash_table[NR_HASH];
static struct buffer_head * free_list;
static struct task_struct * buffer_wait = NULL;
int NR_BUFFERS = 0;
file
static inline void wait_on_buffer(struct buffer_head * bh)
{
  //关闭系统中断
	cli();
	while (bh->b_lock)
		sleep_on(&bh->b_wait);
  //开启系统中断
	sti();
}

int sys_sync(void)
{
	int i;
	struct buffer_head * bh;

	sync_inodes();		/* write out inodes into buffers */
	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		wait_on_buffer(bh);
		if (bh->b_dirt)
			ll_rw_block(WRITE,bh);		//快设备读写函数通用接口
	}
	return 0;
}

/*****************************************************************************
 函 数 名  : sync_dev
 功能描述  : 同步当前指定的存储设备
 输入参数  : int dev  
 输出参数  : 无
 返 回 值  : int
 调用函数  : 
 被调函数  : 
 
 修改历史      :
  1.日    期   : 2017年2月8日
    作    者   : chandler
    修改内容   : 新生成函数

*****************************************************************************/
int sync_dev(int dev)
{
	int i;
	struct buffer_head * bh;
  //遍历整个高速缓冲区中的所有的buffer
	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
    //找到当前制定设备所占用的所有buffer
		if (bh->b_dev != dev)
			continue;
    //等待高速缓冲区被释放
		wait_on_buffer(bh);
    //判断是否为当前指定设备所使用的高速缓冲区 ，并且是否为脏的 如果是就进行同步
		if (bh->b_dev == dev && bh->b_dirt)
			ll_rw_block(WRITE,bh);			//底层的块设备读写函数
	}
	sync_inodes();
	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)
			continue;
		wait_on_buffer(bh);
		if (bh->b_dev == dev && bh->b_dirt)
			ll_rw_block(WRITE,bh);
	}
	return 0;
}

/*****************************************************************************
 函 数 名  : invalidate_buffers
 功能描述  : 无效当前设备对应的所有buffer
 输入参数  : int dev  
 输出参数  : 无
 返 回 值  : void inline
 调用函数  : 
 被调函数  : 
 
 修改历史      :
  1.日    期   : 2017年2月8日
    作    者   : chandler
    修改内容   : 新生成函数

*****************************************************************************/
void inline invalidate_buffers(int dev)
{
	int i;
	struct buffer_head * bh;
  //遍历整个高速缓冲区中的所有的buffer
	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)
			continue;
		wait_on_buffer(bh);
		if (bh->b_dev == dev)
			bh->b_uptodate = bh->b_dirt = 0;
	}
}

/*
 * This routine checks whether a floppy has been changed, and
 * invalidates all buffer-cache-entries in that case. This
 * is a relatively slow routine, so we have to try to minimize using
 * it. Thus it is called only upon a 'mount' or 'open'. This
 * is the best way of combining speed and utility, I think.
 * People changing diskettes in the middle of an operation deserve
 * to loose :-)
 *
 * NOTE! Although currently this is only for floppies, the idea is
 * that any additional removable block-device will use this routine,
 * and that mount/open needn't know that floppies/whatever are
 * special.
 */
void check_disk_change(int dev)
{
	int i;

	if (MAJOR(dev) != 2)
		return;
	if (!floppy_change(dev & 0x03))
		return;
	for (i=0 ; i<NR_SUPER ; i++)
		if (super_block[i].s_dev == dev)
			put_super(super_block[i].s_dev);
	invalidate_inodes(dev);
	invalidate_buffers(dev);
}


//高速缓冲的管理
//对于空闲高速缓冲区的双向链表管理方法
//对于占用高速缓冲区的散列表管理方法

//散列项的计算方法
#define _hashfn(dev,block) (((unsigned)(dev^block))%NR_HASH)
//从散列表中取出对应散列项的指针
#define hash(dev,block) hash_table[_hashfn(dev,block)]


static inline void remove_from_queues(struct buffer_head * bh)
{
/* remove from hash-queue */
//删除散列项对应的链表中的节点
	if (bh->b_next)
		bh->b_next->b_prev = bh->b_prev;
	if (bh->b_prev)
		bh->b_prev->b_next = bh->b_next;
  //如果要删除的节点为散列项指针，就把散列项移动
	if (hash(bh->b_dev,bh->b_blocknr) == bh)
		hash(bh->b_dev,bh->b_blocknr) = bh->b_next;
/* remove from free list */
	if (!(bh->b_prev_free) || !(bh->b_next_free))
		panic("Free block list corrupted");
  //删除节点中某一个节点
	bh->b_prev_free->b_next_free = bh->b_next_free;
	bh->b_next_free->b_prev_free = bh->b_prev_free;
  //如果要删除的节点为尾部节点，就把链表尾部指针前移
	if (free_list == bh)
		free_list = bh->b_next_free;
}

/*****************************************************************************
 函 数 名  : insert_into_queues
 功能描述  : 向管理空闲缓冲区的双向链表中增加一个链表节点
 输入参数  : struct buffer_head * bh   要增加的节点
 输出参数  : 无
 返 回 值  : static inline void
 调用函数  : 
 被调函数  : 
 
 修改历史      :
  1.日    期   : 2017年2月8日
    作    者   : chandler
    修改内容   : 新生成函数

*****************************************************************************/
static inline void insert_into_queues(struct buffer_head * bh)
{
/* put at end of free list */
//free list为当前双向链表的尾节点
	bh->b_next_free = free_list;
	bh->b_prev_free = free_list->b_prev_free;
	free_list->b_prev_free->b_next_free = bh;
	free_list->b_prev_free = bh;
/* put the buffer in new hash-queue if it has a device */
	bh->b_prev = NULL;
	bh->b_next = NULL;
  //如果当前缓冲区没有被分配设备 缓冲区空闲 直接返回，如果缓冲区不空闲 就把缓冲区加入管理占用缓冲区的
  //哈希表中
	if (!bh->b_dev)
		return;
  //把当前缓冲区加入散列表中的函数
	bh->b_next = hash(bh->b_dev,bh->b_blocknr);
	hash(bh->b_dev,bh->b_blocknr) = bh;
	bh->b_next->b_prev = bh;
}

//通过dev 和 逻辑块号进行buffer的查找
static struct buffer_head * find_buffer(int dev, int block)
{		
	struct buffer_head * tmp;
//找到对应的散列项指针，在散列项对应的链表中进行对应节点的查找
	for (tmp = hash(dev,block); tmp != NULL  ; tmp = tmp->b_next)
		if (tmp->b_dev==dev && tmp->b_blocknr==block)
			return tmp;
	return NULL;
}

/*
 * Why like this, I hear you say... The reason is race-conditions.
 * As we don't lock buffers (unless we are readint them, that is),
 * something might happen to it while we sleep (ie a read-error
 * will force it bad). This shouldn't really happen currently, but
 * the code is ready.
 */
 //从散列表中找到一个bh 是find buffer的上层函数
struct buffer_head * get_hash_table(int dev, int block)
{
	struct buffer_head * bh;

	for (;;) {
		if (!(bh=find_buffer(dev,block)))
			return NULL;
		bh->b_count++;
		wait_on_buffer(bh);
		if (bh->b_dev == dev && bh->b_blocknr == block)
			return bh;
		bh->b_count--;
	}
}

/*
 * Ok, this is getblk, and it isn't very clear, again to hinder
 * race-conditions. Most of the code is seldom used, (ie repeating),
 * so it should be much more efficient than it looks.
 *
 * The algoritm is changed: hopefully better, and an elusive bug removed.
 */
#define BADNESS(bh) (((bh)->b_dirt<<1)+(bh)->b_lock)

/*****************************************************************************
 函 数 名  : getblk
 功能描述  : 获得一个逻辑块的高速缓冲区指针 1.如果当前逻辑块已有高速缓冲区，
             那么从散列表中找到并直接返回。2.如果没有对应的高速缓冲区 则分配
             一个新的高速缓冲区并加入到散列表中 最后返回
 输入参数  : int dev    
             int block  
 输出参数  : 无
 返 回 值  : struct buffer_head *
 调用函数  : 
 被调函数  : 
 
 修改历史      :
  1.日    期   : 2017年2月10日
    作    者   : chandler
    修改内容   : 新生成函数

*****************************************************************************/
struct buffer_head * getblk(int dev,int block)
{
	struct buffer_head * tmp, * bh;  // 定义一个buffer_head 结构体指针

repeat:
  //找到对应设备的对应块的高速缓冲区
  //如果当前找的缓冲区存在于散列表中那么就直接进行返回
	if ((bh = get_hash_table(dev,block)))
		return bh;
  //如果当前块还不对应一个高速缓冲区
	tmp = free_list;
  //从空闲缓冲区双向链表中找到一个优势值较大的高速缓冲区
	do {
		if (tmp->b_count)
			continue;
    //BADNESS是一个优势值
		if (!bh || BADNESS(tmp)<BADNESS(bh)) {
			bh = tmp;
			if (!BADNESS(tmp))
				break;
		}
/* and repeat until we find something good */
	} while ((tmp = tmp->b_next_free) != free_list);
  //如果没有找到，对当前进程进行休眠
	if (!bh) {
		sleep_on(&buffer_wait);
		goto repeat;
	}
  //等待被找到的空闲缓冲区不被再次使用
	wait_on_buffer(bh);
	if (bh->b_count)			//确保在等待过程中找到的高速缓冲区没有被使用
		goto repeat;

  //如果当前找到的高速缓冲区是脏的，进行高速缓冲区的同步
	while (bh->b_dirt) {	//	如果该块是有残余数据的则进行回写同步
		sync_dev(bh->b_dev);
		wait_on_buffer(bh);
		if (bh->b_count)
			goto repeat;
	}
  
/* NOTE!! While we slept waiting for this block, somebody else might */
/* already have added "this" block to the cache. check it */
//如果当前找到的高速缓冲区存在于管理已占用缓冲的散列表中，就回去重新查找
	if (find_buffer(dev,block))
		goto repeat;
/* OK, FINALLY we know that this buffer is the only one of it's kind, */
/* and that it's unused (b_count=0), unlocked (b_lock=0), and clean */
//OOP第二步
//设置当前找到的高速缓冲区
	bh->b_count=1;
	bh->b_dirt=0;
	bh->b_uptodate=0;
  //OOP的第三步 
  //把从空闲缓冲区中找到的高速缓冲区加入到散列表中
	remove_from_queues(bh);			//删除节点从HASH和双向链表
	bh->b_dev=dev;
	bh->b_blocknr=block;
	insert_into_queues(bh);				//添加节点从HASH和双向链表
	//返回对应的高速缓冲区
	return bh;
}

/*****************************************************************************
 函 数 名  : brelse
 功能描述  : 释放当前进程对于一块高速缓冲区的占用权
 输入参数  : struct buffer_head * buf  
 输出参数  : 无
 返 回 值  : void
 调用函数  : 
 被调函数  : 
 
 修改历史      :
  1.日    期   : 2017年2月10日
    作    者   : chandler
    修改内容   : 新生成函数

*****************************************************************************/
void brelse(struct buffer_head * buf)
{
	if (!buf)
		return;
	wait_on_buffer(buf);
	if (!(buf->b_count--))
		panic("Trying to free free buffer");
  //唤醒等待高速缓冲区资源的进程队列
	wake_up(&buffer_wait);
}

/*
 * bread() reads a specified block and returns the buffer that contains
 * it. It returns NULL if the block was unreadable.
 */
struct buffer_head * bread(int dev,int block)
{
  //找一个buffer_head
	struct buffer_head * bh;
	if (!(bh=getblk(dev,block)))
		panic("bread: getblk returned NULL\n");
  //如果对应高速缓冲区已经被更新，直接返回
	if (bh->b_uptodate)
		return bh;
  //从块设备中读取数据到对应的高速缓冲区
	ll_rw_block(READ,bh);
	wait_on_buffer(bh);
	if (bh->b_uptodate)
    //返回读取数据后的bh
		return bh;
	brelse(bh);
	return NULL;
}

#define COPYBLK(from,to) \
__asm__("cld\n\t" \
	"rep\n\t" \
	"movsl\n\t" \
	::"c" (BLOCK_SIZE/4),"S" (from),"D" (to) \
	)

/*
 * bread_page reads four buffers into memory at the desired address. It's
 * a function of its own, as there is some speed to be got by reading them
 * all at the same time, not waiting for one to be read, and then another
 * etc.
 */
 //4K的块设备读写
void bread_page(unsigned long address,int dev,int b[4])
{
	struct buffer_head * bh[4];
	int i;

	for (i=0 ; i<4 ; i++)
		if (b[i]) {
			if ((bh[i] = getblk(dev,b[i])))
				if (!bh[i]->b_uptodate)
					ll_rw_block(READ,bh[i]);
		} else
			bh[i] = NULL;
	for (i=0 ; i<4 ; i++,address += BLOCK_SIZE)
		if (bh[i]) {
			wait_on_buffer(bh[i]);
			if (bh[i]->b_uptodate)
				COPYBLK((unsigned long) bh[i]->b_data,address);
			brelse(bh[i]);
		}
}

/*
 * Ok, breada can be used as bread, but additionally to mark other
 * blocks for reading as well. End the argument list with a negative
 * number.
 */
struct buffer_head * breada(int dev,int first, ...)
{
	va_list args;
	struct buffer_head * bh, *tmp;

	va_start(args,first);
	if (!(bh=getblk(dev,first)))
		panic("bread: getblk returned NULL\n");
	if (!bh->b_uptodate)
		ll_rw_block(READ,bh);
	while ((first=va_arg(args,int))>=0) {
		tmp=getblk(dev,first);
		if (tmp) {
			if (!tmp->b_uptodate)
				ll_rw_block(READA,bh);
			tmp->b_count--;
		}
	}
	va_end(args);
	wait_on_buffer(bh);
	if (bh->b_uptodate)
		return bh;
	brelse(bh);
	return (NULL);
}


void buffer_init(long buffer_end)
{
	struct buffer_head * h = start_buffer;
	void * b;
	int i;

	if (buffer_end == 1<<20)
		b = (void *) (640*1024);
	else
		b = (void *) buffer_end;
  //清空所有buffer head的设置值
	while ( (b -= BLOCK_SIZE) >= ((void *) (h+1)) ) {
		h->b_dev = 0;
		h->b_dirt = 0;
		h->b_count = 0;
		h->b_lock = 0;
		h->b_uptodate = 0;
		h->b_wait = NULL;
		h->b_next = NULL;
		h->b_prev = NULL;
		h->b_data = (char *) b;
		h->b_prev_free = h-1;
		h->b_next_free = h+1;
		h++;
		NR_BUFFERS++;
		if (b == (void *) 0x100000)
			b = (void *) 0xA0000;
	}
	h--;
  //把所有的缓冲区都添加到空闲缓冲区中
	free_list = start_buffer;
	free_list->b_prev_free = h;
	h->b_next_free = free_list;
  //清空散列表中的缓冲区
	for (i=0;i<NR_HASH;i++)
		hash_table[i]=NULL;
}	
