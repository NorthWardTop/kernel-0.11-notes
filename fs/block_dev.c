/*
 *  linux/fs/block_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>


int block_write(int dev, long * pos, char * buf, int count)
{
 	// 根据所读取的位置拿出读取起始地址的块号
	int block = *pos >> BLOCK_SIZE_BITS;
	// 根据所读取的位置 拿出当前逻辑块内的偏移地址
	int offset = *pos & (BLOCK_SIZE-1);
	int chars;
	int written = 0;
	struct buffer_head * bh;
	register char * p;

	while (count>0) {
		//该块还能够写入剩余大小
		chars = BLOCK_SIZE - offset;
		if (chars > count)
			chars=count;
		if (chars == BLOCK_SIZE)
			bh = getblk(dev,block);
		else
			bh = breada(dev,block,block+1,block+2,-1);
		block++;
		if (!bh)
			return written?written:-EIO;
		p = offset + bh->b_data;
		offset = 0;
		*pos += chars;
		written += chars;
		count -= chars;
		while (chars-->0)
			*(p++) = get_fs_byte(buf++);
		// 下面两段调用了ll_rw_block
		bh->b_dirt = 1;
		brelse(bh);
	}
	return written;
}

int block_read(int dev, unsigned long * pos, char * buf, int count)
{
	int block = *pos >> BLOCK_SIZE_BITS;
	int offset = *pos & (BLOCK_SIZE-1);
	int chars;
	int read = 0;
	struct buffer_head * bh;
	register char * p;

	while (count>0) {
		chars = BLOCK_SIZE-offset;
		if (chars > count)
			chars = count;
		if (!(bh = breada(dev,block,block+1,block+2,-1)))
			return read?read:-EIO;
		block++;
		p = offset + bh->b_data;
		offset = 0;
		*pos += chars;
		read += chars;
		count -= chars;
		while (chars-->0)
			put_fs_byte(*(p++),buf++);
		brelse(bh);
	}
	return read;
}
