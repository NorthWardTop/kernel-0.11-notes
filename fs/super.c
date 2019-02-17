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
//ȫ�ֵ��ѹ���Ӳ�̵ĳ���������
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

//ͨ����ǰ�Ŀ��豸���豸������ȡһ��Ӳ�̵ĳ�����
struct super_block * get_super(int dev)
{
	struct super_block * s;

	if (!dev)
		return NULL;
	s = 0+super_block;
  //��������������
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
//�ͷ�һ��������  unmount
void put_super(int dev)
{
	struct super_block * sb;
	/* struct m_inode * inode;*/
	int i;
     //�жϵ�ǰҪ���õ��豸�Ƿ�ΪLinux�ĸ��ļ��豸
	if (dev == ROOT_DEV) {
		printk("root diskette changed: prepare for armageddon\n\r");
		return;
	}
  //�ҵ�Ҫ�ͷŵĳ�����
	if (!(sb = get_super(dev)))
		return;
 //�жϵ�ǰָ�����豸�Ѿ�������
 //��ǰҪ�ͷų������Ӳ��һ���ǲ��ܱ����ص�
	if (sb->s_imount) {
		printk("Mounted disk changed - tssk, tssk\n\r");
		return;
	}
  //����Ҫ�ͷŵĳ����飬�����������̽���ʹ��
	lock_super(sb);
  //�ͷŹ���
	sb->s_dev = 0;
  //��inode�ڵ�λͼ �߼���λͼ�Ļ������ͷ�  ���дӲ��
	for(i=0;i<I_MAP_SLOTS;i++)
		brelse(sb->s_imap[i]);
	for(i=0;i<Z_MAP_SLOTS;i++)
		brelse(sb->s_zmap[i]);
  //�����ͷų�����
	free_super(sb);
  // ��û�д�ȫ�ֵ�super_block��������յ�ǰ�ͷŵĳ�����
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
  //��ȡ������
	if ((s = get_super(dev)))
		return s;

  
    //���û�ҵ� �ʹ������������
	//�Ҷ�Ӧsuper_block �����еĿղ�
	for (s = 0+super_block ;; s++) {
		if (s >= NR_SUPER+super_block)
			return NULL;
		if (!s->s_dev)
			break;
	}

	//��ʼ�����������ڴ��е�һЩ��̬������
	s->s_dev = dev;
	s->s_isup = NULL;
	s->s_imount = NULL;
	s->s_time = 0;
	s->s_rd_only = 0;
	s->s_dirt = 0;
	lock_super(s);
  //��Ӳ�̵ĳ������ж�ȡ�����ٻ�������
	if (!(bh = bread(dev,1))) {
		s->s_dev=0;
		free_super(s);
		return NULL;
	}
  //���ݶ�ȡ��ֻ����Ӳ�̵ĳ��������� �������ڴ泬�������ݵĹ���
	//�����豸�й��еĳ��������ò���
	*((struct d_super_block *) s) =
		*((struct d_super_block *) bh->b_data);
  //memcpy
	brelse(bh);
	// ����ļ�ϵͳ��ID�ţ������֧�ָ��ļ�ϵͳ���ͷ���Դ������
	if (s->s_magic != SUPER_MAGIC) {
		s->s_dev = 0;
		free_super(s);
		return NULL;
	}
  //��ն�Ӧ��inode �߼���λͼ
	for (i=0;i<I_MAP_SLOTS;i++)
		s->s_imap[i] = NULL;
	for (i=0;i<Z_MAP_SLOTS;i++)
		s->s_zmap[i] = NULL;
  //�����г������Ӧ�Ŀ��Ϊ1  inode���λͼ��Ӧ��Ϊ2
	block=2;
	//����ȡ���ĳ�������Ϣ ������豸�ļ�ϵͳ��i�ڵ�λͼ
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
	//����ͻ�ԭһ��
	if (block != 2+s->s_imap_blocks+s->s_zmap_blocks) {
		for(i=0;i<I_MAP_SLOTS;i++)
			brelse(s->s_imap[i]);
		for(i=0;i<Z_MAP_SLOTS;i++)
			brelse(s->s_zmap[i]);
		s->s_dev=0;
		free_super(s);
		return NULL;
	}
  //�����еĵ�һ��inode�ڵ� ��  ��һ���߼����ǲ�ʹ�õ�
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
	// inode�ڵ��е�һ��ֱ�ӿ��λ���豸��
	dev = inode->i_zone[0];
	//�жϵ�ǰ�Ƿ�Ϊ���豸
	if (!S_ISBLK(inode->i_mode)) {
		iput(inode);
		return -ENOTBLK;
	}
  //���Ϊ���豸
	iput(inode);
	if (dev==ROOT_DEV)
		return -EBUSY;
	//���û�б��ҽӻ��߶�ȡʧ���򷵻ش���
	if (!(sb=get_super(dev)) || !(sb->s_imount))
		return -ENOENT;
	//������ļ�ϵͳ�Ĺҽӽڵ�Ĺҽӱ�־λ�գ��򷵻ش���
	if (!sb->s_imount->i_mount)
		printk("Mounted inode has i_mount=0\n");
	//����ϵͳ��I�ڵ������н�������ʹ�ø��豸�ϵ��ļ����򷵻�æ����
	for (inode=inode_table+0 ; inode<inode_table+NR_INODE ; inode++)
		if (inode->i_dev==dev && inode->i_count)
				return -EBUSY;
    //��չ�������
	sb->s_imount->i_mount=0;
    //�ͷŹ��ڽ��
	iput(sb->s_imount);
	sb->s_imount = NULL;
	iput(sb->s_isup);
	sb->s_isup = NULL;
	//�ͷſ� ��ͬ���豸
	put_super(dev);
	sync_dev(dev);
	return 0;
}

//��ι���һ��Ӳ���豸
//������
//����·������
//��д����
int sys_mount(char * dev_name, char * dir_name, int rw_flag)
{
	struct m_inode * dev_i, * dir_i;
	struct super_block * sb;
	int dev;
    //ͨ��·��������һ��inode���
	if (!(dev_i=namei(dev_name)))
		return -ENOENT;
  //ͨ��inode�ڵ��õ�һ���豸��
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
  //���õ�ǰ�Ĺ���·��
	sb->s_imount=dir_i;
	dir_i->i_mount=1;
  //���ó����鱻�޸�
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
	// �ļ�������ĳ�ʼ��
	for(i=0;i<NR_FILE;i++)
		file_table[i].f_count=0;
	//��֤���ļ�ϵͳ�Ƿ����������
	if (MAJOR(ROOT_DEV) == 2) {
		printk("Insert root floppy and press ENTER");
		wait_for_keypress();
	}
	//��ʼ������������Ԫ��
	for(p = &super_block[0] ; p < &super_block[NR_SUPER] ; p++) {
		p->s_dev = 0;
		p->s_lock = 0;
		p->s_wait = NULL;
	}
	if (!(p=read_super(ROOT_DEV)))
		panic("Unable to mount root");
	if (!(mi=iget(ROOT_DEV,ROOT_INO)))
		panic("Unable to read root i-node");
	//�����ļ�ϵͳinode�ڵ�������+3
	mi->i_count += 3 ;	/* NOTE! it is logically used 4 times, not 1 */
	p->s_isup = p->s_imount = mi;
	current->pwd = mi;
	current->root = mi;
	free=0;
	i=p->s_nzones;
	// ��ӡ ��ǰ�ļ�ϵͳ��inode�ڵ���߼���Ŀ��и���
	while (-- i >= 0)
		//����߼���λͼ�Ŀ��и���
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
