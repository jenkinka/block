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

enum {
	RM_SIMPLE = 0,		/* The extra-simple request function */
	RM_FULL = 1,		/* The full-blown version */
	RM_NOQUEUE = 2,		/* Use make_request */
};


#define OSU_RAMDISK_MINORS 16
#define MINOR_SHIFT 4
#define DEVNUM(kdevnum) (MINOR(kdev_t_to_nr(kdevnum)) >> MINOR_SHIFT
#define OSU_CIPHER "aes"
#define OSU_DEV_NAME "osuramdisk"

#define KERNEL_SECTOR_SIZE 512
#define INVALIDATE_DELAY 30*HZ


struct osu_ramdisk_dev {
	int size;
	u8 *data;
	short users;
	short media_change;
	spinlock_t lock;
	struct request_queue *queue;
	struct gendisk *gd;
	struct timer_list timer;
};



static int osurd_major = 0;
module_param(osurd_major, int, 0);
MODULE_PARM_DESC(major_num, "Major number, kernel can allocate");
static int hardsect_size = 512;
module_param(hardsect_size, int, 0);
MODULE_PARM_DESC(hardsect_size, "Sector size");
static int nsectors = 1024;
module_param(nsectors, int, 0);
MODULE_PARM_DESC(nsectors, "Number of sectors");
static int ndevices = 4;
module_param(ndevices, int, 0);
MODULE_PARM_DESC(ndevices, "Number of devices");
static int request_mode = RM_SIMPLE;
module_param(request_mode, int, 0);
MODULE_PARM_DESC(request_mode, "Request mode");
static int encrypt = 1;
module_param(encrypt, int, 0);
MODULE_PARM_DESC(encrypt, "Encryption enabled");

static char *key = "defaultCRYPTOk3y1s31337!!!!";
module_param(key, charp, 0000);
MODULE_PARM_DESC(key, "Encryption key");
static struct osu_ramdisk_dev *devices = NULL;
struct crypto_cipher *cipher = NULL;

static void
osu_ramdisk_transfer(struct osu_ramdisk_dev *dev, unsigned long sector,
	       unsigned long nsect, char *buffer, int write)
{
	unsigned long offset = sector *KERNEL_SECTOR_SIZE;
	unsigned long nbytes = nsect *KERNEL_SECTOR_SIZE;
	int i;
	if ((offset + nbytes) > dev->size) {
		printk(KERN_NOTICE "Beyond-end write (%ld %ld)\n", offset,
		       nbytes);
		return;
	}

	crypto_cipher_clear_flags(cipher, ~0);
	crypto_cipher_setkey(cipher, key, strlen(key));

	if (write){
		printk("(OSU_RAMDISK) Writing to ram disk\n");

        if (encrypt) {
    		for (i = 0; i < nbytes; i += crypto_cipher_blocksize(cipher)) {
    			memset(dev->data + offset + i, 0,
    			       crypto_cipher_blocksize(cipher));
    			crypto_cipher_encrypt_one(cipher, dev->data + offset + i,
    						  buffer + i);
    		}
        } else {
			memcpy(dev->data+offset, buffer, nbytes);
        }

	} else {
		printk("(OSU_RAMDISK) Reading from ram disk\n");
        if (encrypt) {
		    for (i = 0; i < nbytes; i += crypto_cipher_blocksize(cipher)) {
		    	crypto_cipher_decrypt_one(cipher, buffer + i,
		    				  dev->data + offset + i);
		    }
        } else {
            memcpy(buffer, dev->data + offset, nbytes);
        }
	}
}

static void
osu_ramdisk_request(struct request_queue *q)
{
	struct request *req;

	req = blk_fetch_request(q);
	while (req != NULL) {
		struct osu_ramdisk_dev *dev = req->rq_disk->private_data;
		if (req->cmd_type != REQ_TYPE_FS) {
			printk(KERN_NOTICE " (OSU_RAMDISK) Skip non-fs request\n");
			__blk_end_request_all(req, -EIO);
			continue;
		}
		osu_ramdisk_transfer(dev, blk_rq_pos(req),
			       blk_rq_cur_sectors(req), req->buffer,
			       rq_data_dir(req));

		if (!(__blk_end_request_cur(req, 0))) {
			req = blk_fetch_request(q);
		}
	}
}

static int
osu_ramdisk_xfer_bio(struct osu_ramdisk_dev *dev, struct bio *bio)
{
	int i;
	struct bio_vec *bvec;
	sector_t sector = bio->bi_sector;


	bio_for_each_segment(bvec, bio, i) {
		char *buffer = __bio_kmap_atomic(bio, i, KM_USER0);
		osu_ramdisk_transfer(dev, sector, bio_cur_bytes(bio) >> 9,
			        buffer, bio_data_dir(bio) == WRITE);
		sector += bio_cur_bytes(bio) >> 9;
		__bio_kunmap_atomic(bio, KM_USER0);
	}
	return 0;
}

static int
osu_ramdisk_xfer_request(struct osu_ramdisk_dev *dev, struct request *req)
{
	struct bio *bio;
	int nsect = 0;

	__rq_for_each_bio(bio, req) {
		osu_ramdisk_xfer_bio(dev, bio);
		nsect += bio->bi_size / KERNEL_SECTOR_SIZE;
	}
	return nsect;
}

static void
osu_ramdisk_full_request(struct request_queue *q)
{
	struct request *req;
	int sectors_xferred;
	struct osu_ramdisk_dev *dev = q->queuedata;

	req = blk_fetch_request(q);
	while (req != NULL) {
		if (req->cmd_type != REQ_TYPE_FS) {
			printk(KERN_NOTICE "(OSU_RAMDISK) Skip non-fs request\n");
			__blk_end_request_all(req, -EIO);
			continue;
		}
		sectors_xferred = osu_ramdisk_xfer_request(dev, req);
		if (!__blk_end_request_cur(req, 0)) {
			blk_fetch_request(q);
		}
	}
}


static int
osu_ramdisk_make_request(struct request_queue *q, struct bio *bio)
{
	struct osu_ramdisk_dev *dev = q->queuedata;
	int status;

	status = osu_ramdisk_xfer_bio(dev, bio);
	bio_endio(bio, status);
	return 0;
}

static int
osu_ramdisk_open(struct block_device *device, fmode_t mode)
{
	struct osu_ramdisk_dev *dev = device->bd_disk->private_data;

	del_timer_sync(&dev->timer);
	/* filp->private_data = dev; */
	spin_lock(&dev->lock);
	if (!dev->users)
		check_disk_change(device);
	dev->users++;
	spin_unlock(&dev->lock);
	return 0;
}

static int
osu_ramdisk_release(struct gendisk *disk, fmode_t mode)
{
	struct osu_ramdisk_dev *dev = disk->private_data;

	spin_lock(&dev->lock);
	dev->users--;

	if (!dev->users) {
		dev->timer.expires = jiffies + INVALIDATE_DELAY;
		add_timer(&dev->timer);
	}
	spin_unlock(&dev->lock);

	return 0;
}

int
osu_ramdisk_media_changed(struct gendisk *gd)
{
	struct osu_ramdisk_dev *dev = gd->private_data;

	return dev->media_change;
}

int
osu_ramdisk_revalidate(struct gendisk *gd)
{
	struct osu_ramdisk_dev *dev = gd->private_data;

	if (dev->media_change) {
		dev->media_change = 0;
		memset(dev->data, 0, dev->size);
	}
	return 0;
}

void
osu_ramdisk_invalidate(unsigned long ldev)
{
	struct osu_ramdisk_dev *dev = (struct osu_ramdisk_dev *) ldev;

	spin_lock(&dev->lock);
	if (dev->users || !dev->data)
		printk(KERN_WARNING "(OSU_RAMDISK) timer sanity check failed\n");
	else
		dev->media_change = 1;
	spin_unlock(&dev->lock);
}

static int
osu_ramdisk_getgeo(struct block_device *device, struct hd_geometry *geo)
{
	struct osu_ramdisk_dev *dev = device->bd_disk->private_data;
	long size = dev->size * (hardsect_size / KERNEL_SECTOR_SIZE);

	geo->cylinders = (size & ~0x3f) >> 6;
	geo->heads = 4;
	geo->sectors = 16;
	geo->start = 0;
	return 0;
}


static struct block_device_operations osu_ramdisk_ops = {
	.owner = THIS_MODULE,
	.open = osu_ramdisk_open,
	.release = osu_ramdisk_release,
	.media_changed = osu_ramdisk_media_changed,
	.revalidate_disk = osu_ramdisk_revalidate,
	.getgeo = osu_ramdisk_getgeo
};

static void
setup_device(struct osu_ramdisk_dev *dev, int which)
{

	memset(dev, 0, sizeof (struct osu_ramdisk_dev));
	dev->size = nsectors * hardsect_size;
	dev->data = vmalloc(dev->size);
	if (dev->data == NULL) {
		printk(KERN_NOTICE "vmalloc failure.\n");
		return;
	}

	spin_lock_init(&dev->lock);
	init_timer(&dev->timer);
	dev->timer.data = (unsigned long) dev;
	dev->timer.function = osu_ramdisk_invalidate;

	switch (request_mode) {
	case RM_NOQUEUE:
		dev->queue = blk_alloc_queue(GFP_KERNEL);
		if (dev->queue == NULL)
			goto out_vfree;
		blk_queue_make_request(dev->queue, osu_ramdisk_make_request);
		break;
	case RM_FULL:
		dev->queue = blk_init_queue(osu_ramdisk_full_request, &dev->lock);
		if (dev->queue == NULL)
			goto out_vfree;
		break;
	default:
		printk(KERN_NOTICE
		       "Bad request mode %d, using simple\n", request_mode);
		/* fall into.. */
	case RM_SIMPLE:
		dev->queue = blk_init_queue(osu_ramdisk_request, &dev->lock);
		if (dev->queue == NULL)
			goto out_vfree;
		break;
	}
	blk_queue_logical_block_size(dev->queue, hardsect_size);
	dev->queue->queuedata = dev;
	dev->gd = alloc_disk(OSU_RAMDISK_MINORS);
	if (!dev->gd) {
		printk(KERN_NOTICE "alloc_disk failure\n");
		goto out_vfree;
	}
	dev->gd->major = osu_ramdisk_major;
	dev->gd->first_minor = which * OSU_RAMDISK_MINORS;
	dev->gd->fops = &osu_ramdisk_ops;
	dev->gd->queue = dev->queue;
	dev->gd->private_data = dev;

	snprintf(dev->gd->disk_name, 32, "osu_ramdisk%c", which + 'a');
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
	if (encrypt){
		cipher = crypto_alloc_cipher(OSU_CIPHER, 0, CRYPTO_ALG_ASYNC);
		if (IS_ERR(cipher)) {
			printk(KERN_ERR "osu_ramdisk_init: Failed to load cipher\n");
			return PTR_ERR(cipher);
		}
	}		
		
	osu_ramdisk_major = register_blkdev(osu_ramdisk_major, OSU_DEV_NAME);
	if (osu_ramdisk_major <= 0) {
		printk(KERN_WARNING "osu_ramdisk: unable to get major number\n");
		return -EBUSY;
	}
	devices = kmalloc(ndevices * sizeof (struct osu_ramdisk_dev), GFP_KERNEL);
	if (devices == NULL)
		goto out_unregister;

	for (i = 0; i < ndevices; i++)
		setup_device(devices + i, i);

		return 0;
      out_unregister:
	unregister_blkdev(osu_ramdisk_major, OSU_DEV_NAME);
	return -ENOMEM;
}

static void
osu_ramdisk_exit(void)
{
	int i;
	for (i = 0; i < ndevices; i++) {
		struct osu_ramdisk_dev *dev = devices + i;
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
	unregister_blkdev(osu_ramdisk_major, OSU_DEV_NAME);
	crypto_free_cipher(cipher);
	kfree(devices);
}

module_init(osu_ramdisk_init);
module_exit(osu_ramdisk_exit);
