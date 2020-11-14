#include <linux/init.h>
#include <linux/module.h>
#include <linux/blk-mq.h>

MODULE_LICENSE("GPL");

/* blkdev.h */
#ifndef SECTOR_SHIFT
#define SECTOR_SHIFT 9
#endif

/* blkdev.h */
#ifndef SECTOR_SIZE
#define SECTOR_SIZE 512
#endif

/* initialize major */
static int major = 0;
static int minor = 0;

/* testing block device name (like /dev/sda) */
static const char* name = "test_blkdev";
static int name_size = 11;

/* initial structure for testing block device */
static struct block_dev *test_blkdev = NULL;

/* a structure that contains information for the kernel about the device */
/* (device capacity, block device data buffer size, device pointer, etc.) */
struct block_dev {
	/* caparity of the device (size in bytes) */
	sector_t capacity;

	/* data buffer for block storage (u8 - 8 bytes) */
	u8 * data;

	/* gendisk structure */
	struct gendisk *gd;

	/* request queue which handles the list of pending operations for device */
	struct request_queue *queue;

	/* tag set */
	struct blk_mq_tag_set tag_set;
};

/* open block device*/
static int blkdev_open(struct block_device *device, fmode_t fmode)
{
	printk("%s: opened\n", name);
	return 0;
}

/* release block device */
static void blkdev_release(struct gendisk *gd, fmode_t fmode)
{
	printk("%s: released\n", name);
}

/* processing requests for each segment */
static int rq_processing(struct request *rq, unsigned int *bytes)
{
	struct bio_vec v;
	struct req_iterator i;
	struct block_dev *dev = rq->q->queuedata;
	loff_t pos = blk_rq_pos(rq) << SECTOR_SHIFT;

	rq_for_each_segment(v, rq, i)
		{
			unsigned long b_len = v.bv_len;
			void* b_buf = page_address(v.bv_page) + v.bv_offset;

			if (rq_data_dir(rq) == WRITE) {
				memcpy(dev->data + pos, b_buf, b_len);
			} else {
				memcpy(b_buf, dev->data + pos, b_len);
			}

			pos += b_len;
			*bytes += b_len;
		}

	return 0;
}

/* queue processing */
static blk_status_t q_processing(struct blk_mq_hw_ctx *hw_ctx,
				 const struct blk_mq_queue_data* blkdev)
{
	unsigned int bytes = 0;
	blk_status_t blk_sts = BLK_STS_OK;
	struct request *rq = blkdev->rq;
	blk_mq_start_request(rq);
	if (rq_processing(rq, &bytes) != 0)
		blk_sts = BLK_STS_IOERR;
	blk_mq_end_request(rq, blk_sts);

	return blk_sts;
}

/* the device operations (open, release, ioctl, media_changed, and revalidate_disk) for this device */
static struct block_device_operations blkdev_operations = {
	.open = blkdev_open,
	.owner = THIS_MODULE,
	.release = blkdev_release
};

static struct blk_mq_ops mq_ops = {
	.queue_rq = q_processing,
};

/* initializing a block device */
static int __init test_blkdev_init(void)
{
	/*
	 * STEP 1: register and configure block device
	 */

	/* register */
	major = register_blkdev(major, name);

	/* allocate */
	test_blkdev = kmalloc(sizeof (struct block_dev), GFP_KERNEL);

	/* set capacity */
	test_blkdev->capacity = (SECTOR_SIZE * PAGE_SIZE) >> SECTOR_SHIFT;

	/* set buffer data for capacity */
	test_blkdev->data = kmalloc(test_blkdev->capacity << SECTOR_SHIFT, GFP_KERNEL);

	/* initialize queue */
	test_blkdev->queue = blk_mq_init_sq_queue(&test_blkdev->tag_set,
						  &mq_ops, 128,
						  BLK_MQ_F_SHOULD_MERGE);

	/* set initial structure of block device as queue data */
	test_blkdev->queue->queuedata = test_blkdev;

	/*
	 * STEP 2: gendisk configuration to create device in /dev
	 */

	/* allocate with number of partitions */
	test_blkdev->gd = alloc_disk(1);

	/* flags controlling the management of this device */
	/* GENHD_FL_NO_PART_SCAN - partition scanning is disabled */
	test_blkdev->gd->flags = GENHD_FL_NO_PART_SCAN;

	/* the major number of this device; either a static major assigned to a specific driver, or one that was obtained dynamically from register_blkdev()  */
	test_blkdev->gd->major = major;

	/* the first minor device number corresponding to this disk. this number will be determined by how  driver divides up its minor number space */
	test_blkdev->gd->first_minor = minor;

	/* fops - device operations for thisdevice */
	test_blkdev->gd->fops = &blkdev_operations;

	/* queue - request queue, which handles the list of pending operations for this device */
	test_blkdev->gd->queue = test_blkdev->queue;

	/* 'private_data' field is reserved for the driver; the rest of the block subsystem will not touch it. Usually it holds a pointer to a driver-specific data structure describing this device. */
	test_blkdev->gd->private_data = test_blkdev;

	/* set device name in /dev using constants */
	strncpy(test_blkdev->gd->disk_name, name, name_size);

	/* set device capacity */
	set_capacity(test_blkdev->gd, test_blkdev->capacity);

	/*
	 * STEP 3: add disk to /dev
	 */

	/* add gendisk */
	add_disk(test_blkdev->gd);
	printk(KERN_INFO "%s: added", test_blkdev->gd->disk_name);

	return 0;
}

/* remove block device */
static void __exit test_blkdev_exit(void)
{
	/* remove disk from the system. no more operations will be sent to this device */
	del_gendisk(test_blkdev->gd);

	/* free gendisk structure, as long as no other part of the kernel retains a reference to it */
	put_disk(test_blkdev->gd);
	blk_cleanup_queue(test_blkdev->queue);
	kfree(test_blkdev->data);
	unregister_blkdev(major, name);
	kfree(test_blkdev);
}

module_init(test_blkdev_init);
module_exit(test_blkdev_exit);
