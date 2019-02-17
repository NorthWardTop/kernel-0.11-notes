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
  //�ر�ϵͳ�ж�
	cli();
	while (bh->b_lock)
		sleep_on(&bh->b_wait);
  //����ϵͳ�ж�
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
			ll_rw_block(WRITE,bh);		//���豸��д����ͨ�ýӿ�
	}
	return 0;
}

/*****************************************************************************
 �� �� ��  : sync_dev
 ��������  : ͬ����ǰָ���Ĵ洢�豸
 �������  : int dev  
 �������  : ��
 �� �� ֵ  : int
 ���ú���  : 
 ��������  : 
 
 �޸���ʷ      :
  1.��    ��   : 2017��2��8��
    ��    ��   : chandler
    �޸�����   : �����ɺ���

*****************************************************************************/
int sync_dev(int dev)
{
	int i;
	struct buffer_head * bh;
  //�����������ٻ������е����е�buffer
	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
    //�ҵ���ǰ�ƶ��豸��ռ�õ�����buffer
		if (bh->b_dev != dev)
			continue;
    //�ȴ����ٻ��������ͷ�
		wait_on_buffer(bh);
    //�ж��Ƿ�Ϊ��ǰָ���豸��ʹ�õĸ��ٻ����� �������Ƿ�Ϊ��� ����Ǿͽ���ͬ��
		if (bh->b_dev == dev && bh->b_dirt)
			ll_rw_block(WRITE,bh);			//�ײ�Ŀ��豸��д����
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
 �� �� ��  : invalidate_buffers
 ��������  : ��Ч��ǰ�豸��Ӧ������buffer
 �������  : int dev  
 �������  : ��
 �� �� ֵ  : void inline
 ���ú���  : 
 ��������  : 
 
 �޸���ʷ      :
  1.��    ��   : 2017��2��8��
    ��    ��   : chandler
    �޸�����   : �����ɺ���

*****************************************************************************/
void inline invalidate_buffers(int dev)
{
	int i;
	struct buffer_head * bh;
  //�����������ٻ������е����е�buffer
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


//���ٻ���Ĺ���
//���ڿ��и��ٻ�������˫�����������
//����ռ�ø��ٻ�������ɢ�б������

//ɢ����ļ��㷽��
#define _hashfn(dev,block) (((unsigned)(dev^block))%NR_HASH)
//��ɢ�б���ȡ����Ӧɢ�����ָ��
#define hash(dev,block) hash_table[_hashfn(dev,block)]


static inline void remove_from_queues(struct buffer_head * bh)
{
/* remove from hash-queue */
//ɾ��ɢ�����Ӧ�������еĽڵ�
	if (bh->b_next)
		bh->b_next->b_prev = bh->b_prev;
	if (bh->b_prev)
		bh->b_prev->b_next = bh->b_next;
  //���Ҫɾ���Ľڵ�Ϊɢ����ָ�룬�Ͱ�ɢ�����ƶ�
	if (hash(bh->b_dev,bh->b_blocknr) == bh)
		hash(bh->b_dev,bh->b_blocknr) = bh->b_next;
/* remove from free list */
	if (!(bh->b_prev_free) || !(bh->b_next_free))
		panic("Free block list corrupted");
  //ɾ���ڵ���ĳһ���ڵ�
	bh->b_prev_free->b_next_free = bh->b_next_free;
	bh->b_next_free->b_prev_free = bh->b_prev_free;
  //���Ҫɾ���Ľڵ�Ϊβ���ڵ㣬�Ͱ�����β��ָ��ǰ��
	if (free_list == bh)
		free_list = bh->b_next_free;
}

/*****************************************************************************
 �� �� ��  : insert_into_queues
 ��������  : �������л�������˫������������һ������ڵ�
 �������  : struct buffer_head * bh   Ҫ���ӵĽڵ�
 �������  : ��
 �� �� ֵ  : static inline void
 ���ú���  : 
 ��������  : 
 
 �޸���ʷ      :
  1.��    ��   : 2017��2��8��
    ��    ��   : chandler
    �޸�����   : �����ɺ���

*****************************************************************************/
static inline void insert_into_queues(struct buffer_head * bh)
{
/* put at end of free list */
//free listΪ��ǰ˫�������β�ڵ�
	bh->b_next_free = free_list;
	bh->b_prev_free = free_list->b_prev_free;
	free_list->b_prev_free->b_next_free = bh;
	free_list->b_prev_free = bh;
/* put the buffer in new hash-queue if it has a device */
	bh->b_prev = NULL;
	bh->b_next = NULL;
  //�����ǰ������û�б������豸 ���������� ֱ�ӷ��أ���������������� �Ͱѻ������������ռ�û�������
  //��ϣ����
	if (!bh->b_dev)
		return;
  //�ѵ�ǰ����������ɢ�б��еĺ���
	bh->b_next = hash(bh->b_dev,bh->b_blocknr);
	hash(bh->b_dev,bh->b_blocknr) = bh;
	bh->b_next->b_prev = bh;
}

//ͨ��dev �� �߼���Ž���buffer�Ĳ���
static struct buffer_head * find_buffer(int dev, int block)
{		
	struct buffer_head * tmp;
//�ҵ���Ӧ��ɢ����ָ�룬��ɢ�����Ӧ�������н��ж�Ӧ�ڵ�Ĳ���
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
 //��ɢ�б����ҵ�һ��bh ��find buffer���ϲ㺯��
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
 �� �� ��  : getblk
 ��������  : ���һ���߼���ĸ��ٻ�����ָ�� 1.�����ǰ�߼������и��ٻ�������
             ��ô��ɢ�б����ҵ���ֱ�ӷ��ء�2.���û�ж�Ӧ�ĸ��ٻ����� �����
             һ���µĸ��ٻ����������뵽ɢ�б��� ��󷵻�
 �������  : int dev    
             int block  
 �������  : ��
 �� �� ֵ  : struct buffer_head *
 ���ú���  : 
 ��������  : 
 
 �޸���ʷ      :
  1.��    ��   : 2017��2��10��
    ��    ��   : chandler
    �޸�����   : �����ɺ���

*****************************************************************************/
struct buffer_head * getblk(int dev,int block)
{
	struct buffer_head * tmp, * bh;  // ����һ��buffer_head �ṹ��ָ��

repeat:
  //�ҵ���Ӧ�豸�Ķ�Ӧ��ĸ��ٻ�����
  //�����ǰ�ҵĻ�����������ɢ�б�����ô��ֱ�ӽ��з���
	if ((bh = get_hash_table(dev,block)))
		return bh;
  //�����ǰ�黹����Ӧһ�����ٻ�����
	tmp = free_list;
  //�ӿ��л�����˫���������ҵ�һ������ֵ�ϴ�ĸ��ٻ�����
	do {
		if (tmp->b_count)
			continue;
    //BADNESS��һ������ֵ
		if (!bh || BADNESS(tmp)<BADNESS(bh)) {
			bh = tmp;
			if (!BADNESS(tmp))
				break;
		}
/* and repeat until we find something good */
	} while ((tmp = tmp->b_next_free) != free_list);
  //���û���ҵ����Ե�ǰ���̽�������
	if (!bh) {
		sleep_on(&buffer_wait);
		goto repeat;
	}
  //�ȴ����ҵ��Ŀ��л����������ٴ�ʹ��
	wait_on_buffer(bh);
	if (bh->b_count)			//ȷ���ڵȴ��������ҵ��ĸ��ٻ�����û�б�ʹ��
		goto repeat;

  //�����ǰ�ҵ��ĸ��ٻ���������ģ����и��ٻ�������ͬ��
	while (bh->b_dirt) {	//	����ÿ����в������ݵ�����л�дͬ��
		sync_dev(bh->b_dev);
		wait_on_buffer(bh);
		if (bh->b_count)
			goto repeat;
	}
  
/* NOTE!! While we slept waiting for this block, somebody else might */
/* already have added "this" block to the cache. check it */
//�����ǰ�ҵ��ĸ��ٻ����������ڹ�����ռ�û����ɢ�б��У��ͻ�ȥ���²���
	if (find_buffer(dev,block))
		goto repeat;
/* OK, FINALLY we know that this buffer is the only one of it's kind, */
/* and that it's unused (b_count=0), unlocked (b_lock=0), and clean */
//OOP�ڶ���
//���õ�ǰ�ҵ��ĸ��ٻ�����
	bh->b_count=1;
	bh->b_dirt=0;
	bh->b_uptodate=0;
  //OOP�ĵ����� 
  //�Ѵӿ��л��������ҵ��ĸ��ٻ��������뵽ɢ�б���
	remove_from_queues(bh);			//ɾ���ڵ��HASH��˫������
	bh->b_dev=dev;
	bh->b_blocknr=block;
	insert_into_queues(bh);				//��ӽڵ��HASH��˫������
	//���ض�Ӧ�ĸ��ٻ�����
	return bh;
}

/*****************************************************************************
 �� �� ��  : brelse
 ��������  : �ͷŵ�ǰ���̶���һ����ٻ�������ռ��Ȩ
 �������  : struct buffer_head * buf  
 �������  : ��
 �� �� ֵ  : void
 ���ú���  : 
 ��������  : 
 
 �޸���ʷ      :
  1.��    ��   : 2017��2��10��
    ��    ��   : chandler
    �޸�����   : �����ɺ���

*****************************************************************************/
void brelse(struct buffer_head * buf)
{
	if (!buf)
		return;
	wait_on_buffer(buf);
	if (!(buf->b_count--))
		panic("Trying to free free buffer");
  //���ѵȴ����ٻ�������Դ�Ľ��̶���
	wake_up(&buffer_wait);
}

/*
 * bread() reads a specified block and returns the buffer that contains
 * it. It returns NULL if the block was unreadable.
 */
struct buffer_head * bread(int dev,int block)
{
  //��һ��buffer_head
	struct buffer_head * bh;
	if (!(bh=getblk(dev,block)))
		panic("bread: getblk returned NULL\n");
  //�����Ӧ���ٻ������Ѿ������£�ֱ�ӷ���
	if (bh->b_uptodate)
		return bh;
  //�ӿ��豸�ж�ȡ���ݵ���Ӧ�ĸ��ٻ�����
	ll_rw_block(READ,bh);
	wait_on_buffer(bh);
	if (bh->b_uptodate)
    //���ض�ȡ���ݺ��bh
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
 //4K�Ŀ��豸��д
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
  //�������buffer head������ֵ
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
  //�����еĻ���������ӵ����л�������
	free_list = start_buffer;
	free_list->b_prev_free = h;
	h->b_next_free = free_list;
  //���ɢ�б��еĻ�����
	for (i=0;i<NR_HASH;i++)
		hash_table[i]=NULL;
}	
