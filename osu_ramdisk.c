/*
* Filename: osu_ramdisk.c
*
* Authors: Kai Jenkins-Rathbun,
* Jordan Bayles,
* Corey Eckelman,
* Jennifer Wolfe
*
* Based on examples from Linux Driver Development 3rd Edition. (ch 16)
* By Jonathan Corbet, Alessandro Rubini, Greg Kroah-Hartman.
*
* osu_ramdisk - A ramdisk block device driver for linux-3.0.4. This driver
* uses Crypto to encrypt and decrypt on read and write using
* AES encryption.
*
* Redistributable under the terms of the GNU GPL
*
*/

#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/hdreg.h>
#include <linux/kdev_t.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/bio.h>
#include <linux/crypto.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR
    ("Kai Jenkins-Rathbun, Jordan Bayles, Corey Eckelman, Jennifer Wolfe");
MODULE_DESCRIPTION
    ("Ramdisk device driver that uses crypto for encryption/decription as you read and write");

#define KERNEL_SECTOR_SIZE 512
#define INVALIDATE_DELAY 30*HZ
#define MINORS 16
#define MINOR_SHIFT 4
#define DEVNUM(devnum) (MINOR(kdev_t_to_nr(devnum)) >> MINOR_SHIFT)
#define OSU_DEV_NAME "osuramdisk"

/* Request modes */
enum {
	RM_SIMPLE = 0,	/* simple requst function */
	RM_FULL = 1,	/* The full version */
	RM_NO_QUEUE = 2	/* Use make_request */
};

/* Our device internal structure*/
struct osu_ramdisk_device {
	int size;	/* Device size in sectors */
	u8 *data;	/* The data array */
	short users;	/* How many users */
	short media_change;	/* Flag a media change? */
	spinlock_t lock;	/* For mutual exclusion */
	struct request_queue *queue;	/* The device request queue */
	struct gendisk *gd;	/* The gendisk structure */
	struct timer_list timer;	/* For simulated media changes */
};

static int major_num = 0;	/* kernel will allocate a number */
static int hardsect_size = 512;	/* Sector size */
static int nsectors = 2048;	/* Drive size */
static int ndevices = 4;
static int request_mode = RM_SIMPLE;
static int encrypt = 1;

module_param(major_num, int, 0);
MODULE_PARM_DESC(major_num, "Major number, kernel can allocate");

module_param(hardsect_size, int, 0);
MODULE_PARM_DESC(hardsect_size, "Sector size");

module_param(nsectors, int, 0);
MODULE_PARM_DESC(nsectors, "Number of sectors");

module_param(ndevices, int, 0);
MODULE_PARM_DESC(ndevices, "Number of devices");

module_param(request_mode, int, 0);
MODULE_PARM_DESC(request_mode, "Request mode");

module_param(encrypt, int, 0);
MODULE_PARM_DESC(encrypt, "Encryption added");

/* Crypto key */
static char *key = "aasdfaksdfjDLKFJSDLKFJDf91238493842DFKJSDKLFJDS";
module_param(key, charp, S_IRUGO);
MODULE_PARM_DESC(key, "Encryption key");

static struct osu_ramdisk_device *Device = NULL;
static struct crypto_cipher *cipher = NULL;

/* Handle an I/O request */
static void
osu_ramdisk_transfer(struct osu_ramdisk_device *dev,
	sector_t sector, unsigned long nsect,
	char *buffer, int write)
{
	unsigned long offset = sector * KERNEL_SECTOR_SIZE;
	unsigned long nbytes = nsect * KERNEL_SECTOR_SIZE;
	int i;
	unsigned long length = (unsigned long) strlen(key);	/* keylength */
	crypto_cipher_clear_flags(cipher, ~0);
	crypto_cipher_setkey(cipher, key, length);
	if ((offset + nbytes) > dev->size) {
		printk(KERN_NOTICE "osu_ramdisk: Beyond-end write\n");
		return;
	}

	if (write) {
		printk("Writing to DISK\n");
		if (encrypt)
		{
			memset(dev->data+offset, 0, nbytes);
			for (i = 0; i < nbytes; i += crypto_cipher_blocksize(cipher))
				crypto_cipher_encrypt_one(cipher, dev->data + offset,
				buffer + i);
		} else
			memcpy(dev->data+offset, buffer, nbytes);

	} else {
		printk("Reading from Disk\n");
		if (encrypt){
			for (i = 0; i < nbytes; i += crypto_cipher_blocksize(cipher)) {
				crypto_cipher_decrypt_one(cipher, buffer + i,
							  dev->data + offset + i);
			}
		}else
			memcpy(buffer, dev->data+offset, nbytes);
		
	}
}

/*
* Our Simple request function
*/
static void
osu_ramdisk_request(struct request_queue *q)
{
	struct request *req;

	req = blk_fetch_request(q);
	while (req != NULL) {
		struct osu_ramdisk_device *Device = req->rq_disk->private_data;
		if (req == NULL || (req->cmd_type != REQ_TYPE_FS)) {
			printk(KERN_NOTICE "Skip non-CMD request\n");
			__blk_end_request_all(req, -EIO);
			continue;
		}
		osu_ramdisk_transfer(Device, blk_rq_pos(req),
				     blk_rq_cur_sectors(req), req->buffer,
				     rq_data_dir(req));
		
		if (!(__blk_end_request_cur(req, 0))) {
			req = blk_fetch_request(q);
		}
	}
}

/*
* Our bio transfer function transfers a single bio structure
*/
static int
osu_ramdisk_bio_transfer(struct osu_ramdisk_device *dev, struct bio *pbio)
{
	int i;
	struct bio_vec *bvec;
	sector_t bsector = pbio->bi_sector;

	/* transfer each segment */
	bio_for_each_segment(bvec, pbio, i) {
		char *buff = __bio_kmap_atomic(pbio, i, KM_USER0);
		osu_ramdisk_transfer(dev, bsector, bio_cur_bytes(pbio) >> 9,
		buff, bio_data_dir(pbio) == WRITE);
		bsector += bio_cur_bytes(pbio) >> 9;
		__bio_kunmap_atomic(pbio, KM_USER0);
	}
	return 0;
}

/*
* Transfer a full request
*/
static int
osu_ramdisk_transfer_request(struct osu_ramdisk_device *dev,
struct request *req)
{
	struct bio *pbio;
	int nsect = 0;
	
	__rq_for_each_bio(pbio, req) {
		osu_ramdisk_bio_transfer(dev, pbio);
		nsect += pbio->bi_size / KERNEL_SECTOR_SIZE;
	}
	return nsect;
}

/*
* Request function that "handles clustering".
*/
static void
osu_ramdisk_full_request(struct request_queue *q)
{
	struct request *req;
	int sectors_xferred;
	struct osu_ramdisk_device *dev = q->queuedata;

	req = blk_fetch_request(q);
	while (req != NULL) {
		if (req == NULL || (req->cmd_type != REQ_TYPE_FS)) {
			printk(KERN_NOTICE "Skip non-CMD request\n");
			__blk_end_request_all(req, -EIO);
			continue;
		}
		sectors_xferred = osu_ramdisk_transfer_request(dev, req);
		if (!__blk_end_request_cur(req, 0)) {
			blk_fetch_request(q);
		}
	}
}

/*
* The direct make request version.
*/
static int
osu_ramdisk_make_request(struct request_queue *q, struct bio *pbio)
{
	struct osu_ramdisk_device *dev = q->queuedata;
	int status;

	status = osu_ramdisk_bio_transfer(dev, pbio);
	bio_endio(pbio, status);
	return 0;
}

/*
* Open
*/

static int
osu_ramdisk_open(struct block_device *device, fmode_t mode)
{
	struct osu_ramdisk_device *dev = device->bd_disk->private_data;

	del_timer_sync(&dev->timer);
	/* filp->private_data = dev; */
	spin_lock(&dev->lock);
	if (!dev->users)
		check_disk_change(device);
	dev->users++;
	spin_unlock(&dev->lock);
	
	return 0;
}

/*
* Release
*/
static int
osu_ramdisk_release(struct gendisk *disk, fmode_t mode)
{
	struct osu_ramdisk_device *dev = disk->private_data;

	spin_lock(&dev->lock);
	dev->users--;
	if (!dev->users) {
		dev->timer.expires = jiffies + INVALIDATE_DELAY;
		add_timer(&dev->timer);
	}
	spin_unlock(&dev->lock);

	return 0;
}

/*
* Look for a (simulated) media change.
*/
int
osu_ramdisk_media_changed(struct gendisk *gd)
{
	struct osu_ramdisk_device *dev = gd->private_data;
	return dev->media_change;
}

/*
* Revalidate. WE DO NOT TAKE THE LOCK HERE, for fear of deadlocking
* with open. That needs to be reevaluated.
*/
int
osu_ramdisk_revalidate(struct gendisk *gd)
{
	struct osu_ramdisk_device *dev = gd->private_data;

	if (dev->media_change) {
		dev->media_change = 0;
		memset(dev->data, 0, dev->size);
	}
	return 0;
}

/*
* The "invalidate" function runs out of the device timer; it sets
* a flag to simulate the removal of the media.
*/
void
osu_ramdisk_invalidate(unsigned long ldev)
{
	struct osu_ramdisk_device *dev = (struct osu_ramdisk_device *) ldev;

	spin_lock(&dev->lock);
	if (dev->users || !dev->data)
		printk(KERN_WARNING "osurd: timer sanity check failed\n");
	else
		dev->media_change = 1;
	spin_unlock(&dev->lock);
}

/*
* The HDIO_GETGEO ioctl is handled in blkdev_ioctl(), which
* calls this. We need to implement getgeo, since we can't
* use tools such as fdisk to partition the drive otherwise.
*/
int
osu_ramdisk_getgeo(struct block_device *block_device, struct hd_geometry *geo)
{
	long size;

	struct osu_ramdisk_device *dev = block_device->bd_disk->private_data;
	size = dev->size * (hardsect_size / KERNEL_SECTOR_SIZE);
	geo->cylinders = (size & ~0x3f) >> 6;
	geo->heads = 4;
	geo->sectors = 16;
	geo->start = 0;
	return 0;
}

/* The device operations structure. */
static struct block_device_operations osu_ramdisk_ops = {
	.owner = THIS_MODULE,
	.open = osu_ramdisk_open,
	.release = osu_ramdisk_release,
	.media_changed = osu_ramdisk_media_changed,
	.revalidate_disk = osu_ramdisk_revalidate,
	.getgeo = osu_ramdisk_getgeo
	/*.ioctl not used */
};

/*
* Set up our internal device.
*/
static void
setup_device(struct osu_ramdisk_device *dev, int which)
{
	memset(dev, 0, sizeof (struct osu_ramdisk_device));
	dev->size = nsectors * hardsect_size;
	dev->data = vmalloc(dev->size);
	if (dev->data == NULL) {
		printk(KERN_NOTICE "vmalloc failure.\n");
		return;
	}
	spin_lock_init(&dev->lock);
	/*
	* The timer which "invalidates" the device.
	*/
	init_timer(&dev->timer);
	dev->timer.data = (unsigned long) dev;
	dev->timer.function = osu_ramdisk_invalidate;

	/*
	* The I/O queue, depending on whether we are using our own
	* make_request function or not.
	*/
	switch (request_mode) {
	case RM_NO_QUEUE:
		dev->queue = blk_alloc_queue(GFP_KERNEL);
		if (dev->queue == NULL)
			goto out_vfree;
		blk_queue_make_request(dev->queue, osu_ramdisk_make_request);
		break;
	case RM_FULL:
		dev->queue =
		blk_init_queue(osu_ramdisk_full_request, &dev->lock);
		if (dev->queue == NULL)
			goto out_vfree;
		break;
	default:
		printk(KERN_NOTICE "Bad request mode %d, using simple\n",
		request_mode);
	/* fall into.. */

	case RM_SIMPLE:
		dev->queue = blk_init_queue(osu_ramdisk_request, &dev->lock);
		if (dev->queue == NULL)
			goto out_vfree;
		break;
	}
	blk_queue_logical_block_size(dev->queue, hardsect_size);
	dev->queue->queuedata = dev;

	/*
	* And the gendisk structure.
	*/

	dev->gd = alloc_disk(MINORS);
	if (!dev->gd) {
		printk(KERN_NOTICE "alloc_disk failure\n");
		goto out_vfree;
	}
	dev->gd->major = major_num;
	dev->gd->first_minor = which * MINORS;
	dev->gd->fops = &osu_ramdisk_ops;
	dev->gd->queue = dev->queue;
	dev->gd->private_data = dev;
	snprintf(dev->gd->disk_name, 32, "osuramdisk%c", which + 'a');
	set_capacity(dev->gd, nsectors * (hardsect_size / KERNEL_SECTOR_SIZE));
	add_disk(dev->gd);
	return;

out_vfree:
	if (dev->data)
		vfree(dev->data);
}

static int __init
osu_ramdisk_init(void)
{
	int i;

	printk("osu_ramdisk_init\n");

	if (encrypt){
		cipher = crypto_alloc_cipher("aes", 0, CRYPTO_ALG_ASYNC);
		if (IS_ERR(cipher)) {
			printk(KERN_ERR "osu_ramdisk_init: Failed to load cipher\n");
			return PTR_ERR(cipher);
		}
	}

	/* Register our device */
	major_num = register_blkdev(major_num, OSU_DEV_NAME);

	if (major_num <= 0) {
		printk(KERN_WARNING
			"osu_ramdisk: unable to get major number\n");
		return -(EBUSY);
	}

	/* Allocate the device array and init each of them */

	Device =
	kmalloc(ndevices * sizeof (struct osu_ramdisk_device), GFP_KERNEL);
	if (Device == NULL) {
		printk(KERN_ERR
			"osu_ramdisk_init: Failed to allocate the device\n");
		goto out;
	}
	for (i = 0; i < ndevices; i++) {
		setup_device(Device + i, i);
	}
	return 0;
	out:
		unregister_blkdev(major_num, OSU_DEV_NAME);
		return -ENOMEM;
}

/* osu_ramdisk_exit(void)
*
* Here we unregister our device an free any memory we used.
*/
static void
osu_ramdisk_exit(void)
{
	int i;
	printk("osu_ramdisk_exit\n");
	
	/* unregister our device */
	for (i = 0; i < ndevices; i++) {
		struct osu_ramdisk_device *dev = Device + i;
		del_timer_sync(&dev->timer);
		if (dev->gd) {
			del_gendisk(dev->gd);
			put_disk(dev->gd);
		}
		if (dev->queue)
			blk_cleanup_queue(dev->queue);
		if (dev->data)
			vfree(dev->data);
	}
	unregister_blkdev(major_num, OSU_DEV_NAME);
	
	crypto_free_cipher(cipher);
	
	kfree(Device);
}

module_init(osu_ramdisk_init);
module_exit(osu_ramdisk_exit);
