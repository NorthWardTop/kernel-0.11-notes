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
      //д�����
			write_inode(inode);
	}
}
//block��Ӧ��Ϊ��ĸ���
//���ص�ǰ�ļ�inode�ڵ��ж�Ӧ�ĵ�block��Ŀ�ţ����creatΪ1 ��ô������鲻���ڵ�ʱ�� �ͷ����µĿ�
static int _bmap(struct m_inode * inode,int block,int create)
{
	struct buffer_head * bh;
	int i;
//���������
	if (block<0)
		panic("_bmap: block<0");
	if (block >= 7+512+512*512)
		panic("_bmap: block>big");
  //�����Ŵ���ֱ�ӿ����
	if (block<7) {
    //���������־Ϊ1 ����ֱ�ӿ鲻���ڣ��ʹ����µ��߼��飬���߼������i_zone��Ӧ������
		if (create && !inode->i_zone[block])
    //�����µ��߼���
			if ((inode->i_zone[block]=new_block(inode->i_dev))) {
				inode->i_ctime=CURRENT_TIME; 
				inode->i_dirt=1;
			}
		return inode->i_zone[block];
	}
  
	//�ļ�ռ�õĿ�Ŵ���7K�ģ���Ҫ�õ�һ�μ�ӿ��
	block -= 7;
	if (block<512) {
	//���������洢һ�μ�ӿ�Ŀ�
		if (create && !inode->i_zone[7])
			if ((inode->i_zone[7]=new_block(inode->i_dev))) {
				  inode->i_dirt=1;
				  inode->i_ctime=CURRENT_TIME;
			}
		if (!inode->i_zone[7])
			return 0;
    //��һ����ӿ���仺����
		if (!(bh = bread(inode->i_dev,inode->i_zone[7])))
			return 0;
		i = ((unsigned short *) (bh->b_data))[block];
		if (create && !i)
      //�����µĿ飬���Ҹ��µĿ��Ӧ��blockλ�ø�ֵ
			if ((i=new_block(inode->i_dev))) {
        //д��һ����ӿ�ĸ��ٻ�������
				((unsigned short *) (bh->b_data))[block]=i;
				bh->b_dirt=1;
			}
      //���ٻ�������ͬ���ش���
		brelse(bh);
      //���ط���õĿ��
		return i;
	}
//�Զ�����ӿ�Ĵ���
  block -= 512;
	if (create && !inode->i_zone[8])
    //���������ӿ�
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
//����������־�Ķ�ȡ���
//open("path",mode,flags)
//flags  append�ɸ��ӵ�
//creat
int bmap(struct m_inode * inode,int block)
{
	return _bmap(inode,block,0);
}
//���Դ���
int create_block(struct m_inode * inode, int block)
{
	return _bmap(inode,block,1);
}

// �ͷ�һ��inode�ڵ�		
void iput(struct m_inode * inode)
{
	if (!inode)
		return;
	wait_on_inode(inode);
  //û�н������ã���ô�����ͷ�һ���յ�inode�ڵ�
	if (!inode->i_count)
		panic("iput: trying to free free inode");
  //�����ǰΪһ��pipe�ܵ�
	if (inode->i_pipe) {
		wake_up(&inode->i_wait);
    //�����ü������٣����ǻ���Ϊ0�Ļ� ���ͷ��� ��Ϊ���н���������
    //�ӽ���close(listenfd) 
		if (--inode->i_count)
			return;
    //�����ü������ٺ�Ϊ0
    //�ͷ�pipe��ռ�õ��ڴ�
		free_page(inode->i_size);
		inode->i_count=0;
		inode->i_dirt=0;
		inode->i_pipe=0;
		return;
	}
  //�����ַ��豸Ҳ���ǿ��豸
	if (!inode->i_dev) {
		inode->i_count--;
		return;
	}
  //���Ϊ���豸
	if (S_ISBLK(inode->i_mode)) {
    //ͬ�����豸
		sync_dev(inode->i_zone[0]);
		wait_on_inode(inode);
	}
repeat:
    //��ǰ��ֹһ��������ʹ�ø�inode�ڵ�
    //��ô���ü����Լ��ٺ󷵻�
	if (inode->i_count>1) {
		inode->i_count--;
		return;
	}
  //�����ļ�
	if (!inode->i_nlinks) {
		truncate(inode);
		free_inode(inode);
		return;
	}
  //���inode���޸�
  
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
			// ��inode�ڵ��������ҵ�һ��û�б�Ӧ�� �����޸ĵĿղ�
	do {
		inode = NULL;

    //��ȫ�ֵ�inode_table���ҵ�һ��û�б����� û�б����� û�б��޸ĵ�inode�ڵ�
		for (i = NR_INODE; i ; i--) {
			if (++last_inode >= inode_table + NR_INODE)
				last_inode = inode_table;
      //��inode_table�е������ü���Ϊ0
			if (!last_inode->i_count) {
				inode = last_inode;
				if (!inode->i_dirt && !inode->i_lock)
					break;
			}
		}
		//���û���ҵ�inode�Ŀղ����ӡ��ǰȫ����inode
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
  //����ҵ���inode�ڵ�
	memset(inode,0,sizeof(*inode));
  //���õ�ǰ�������ø�inode�ڵ�
	inode->i_count = 1;
	return inode;
}


struct m_inode * get_pipe_inode(void)
{
	struct m_inode * inode;

	if (!(inode = get_empty_inode()))
		return NULL;
// Ϊ��ǰinode�ڵ����һ���ڴ�ҳ����ΪPIPE
	if (!(inode->i_size=get_free_page())) {
		inode->i_count = 0;
		return NULL;
	}
	// �ֶ� ��д
	inode->i_count = 2;	/* sum of readers/writers */
	//��ʼ��PIPE��ͷβ
	PIPE_HEAD(*inode) = PIPE_TAIL(*inode) = 0;
	//��PIPE��־
	inode->i_pipe = 1;
	return inode;
}
//���һ��inode�ڵ�
// dev �豸��  nr inode�ڵ��
struct m_inode * iget(int dev,int nr)
{
  //1. ����һ��inode�ڵ�
  //2. �������inode�ڵ�
  //3. ��ȫ�ֵ�inode_table��������úõ�inode�ڵ�
	struct m_inode * inode, * empty;

	if (!dev)
		panic("iget with dev==0");
	empty = get_empty_inode();
	inode = inode_table;
  //����inode table�� �����е�dev����inode��ƥ��ĵ�
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

// read inode��������inode�ڵ���Ϣ �ǲ�����inode�ڴ涯̬��Ϣ��
static void read_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;
	int block;

	lock_inode(inode);
	if (!(sb=get_super(inode->i_dev)))
		panic("trying to read inode without dev");
  //INODES_PER_BLOCKÿһ������ռ�õ�inode�Ĵ�С
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
	//���㵱ǰinode�ڵ���߼����
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
