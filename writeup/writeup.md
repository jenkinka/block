Project 3
=========
TODO: our names here.

Plan
----
-We based our code off of ...
-- Felt this was a good jumping off point
-- also felt that ...
-added in crypto stuff
-talk about advantages
-talk about dis-ad..

Implementation
--------------
-implemented as a kernel module
-registers itself as a block device, adds it as a disk using
 add_disk().
-special module parameters, including stuff for sectors,
 private key etc

-creates a block_device_operations struct
- n functions:
- list

***Ramdisk Implementation***
The driver exposes itself as a block device, which reads and 
writes to a block of memory, allocated when it is initialized.
We have three different request modes for our file system:
 * RM_NO_QUEUE: This 
 * RM_FULL: 
 * RM_SIMPLE:

***Crypto Implementation***
- how it works

Implementation Details
----------------------

***osu_ramdisk_init()***
-registers  (using register_blkdev().)
-also creates crypto, initializes using crypto_alloc (aes)
-then allocates the ramdisks. each one is 

***osu_ramdisk_exit()***
-this frees each block device (vfree()s data), then 
calls unregister_blkdev(), crypto_free_cipher(), and kfree()
in order to clean up.

***setup_device()***
In this function, we setup a single device
(e.g. /dev/osuramdiska)
- we create a osu_ramdisk_device instance 
- (more explanation ..)

***osu_ramdisk_getgeo()***
We basically have to lie here about what our device looks like, 
because it does not physically exist.
- we get the size in (units) from the block_device param passed in
we then (make up something plausible to put as the hd geometry)
and then we set the cylinders relative to the size. 
- we need to do this, in order to implement partitioning, so we 
  can put files on this

***osu_ramdisk_invalidate()***
uhhh what does this one do?

***osu_ramdisk_revalidate()***
uhh this one too

***osu_ramdisk_media_changed()***

***osu_ramdisk_release()***

***osu_ramdisk_open()***

***osu_ramdisk_make_request()***
This function takes a  request from the request queue and 
processes it using osu_ramdisk_bio_transfer().

***osu_ramdisk_full_request()***

***osu_ramdisk_transfer_request()***
Enumerates over each request in a bio 


***osu_ramdisk_bio_transfer()***
Iterates over each bio_vec in a given bio struct, and transfers
them using osu_ramdisk_transfer().

***osu_ramdisk_request()***
Handles simple requests. Transfers each request in the queue by
enumerating over it using blk_fetch_request(), 
and calling osu_ramdisk_transfer() on each request.

***osu_ramdisk_transfer()***


Testing
-------

-We can test basic functionality by doing reads and writes to the filesys.
-We can test the encryption by printk-ing the encry

Source Code
===========
<include source here>
