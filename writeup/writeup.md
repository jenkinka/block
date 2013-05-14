Project 3
=========
Jordan Bayles
Corey Eckelman
Kai Jenkins-Rathburn
Jennifer Wolfe

Plan
----
-We based our code off of the sbull.c driver from Linux Driver 
Development 3rd Edition. 
-- Felt this was a good jumping off point
-- also good introduction to how it works
-- the necessary changes between 2.6 and 3.04, as well as the 
-- changes involved in impl crypto would present a challenge.
- We implemented crypto using the ..
-Divided work evenly, between test writing, writeup and coding.
--Formulated plan.

Implementation
--------------
 The driver itself is implemented as a kernel module. It register
 itself as a block device, adding itself 
 In each devices setup function it adds itself as a disk. 
 It stores
 some specific module parameters including the amount of devices
 to store and the request mode. 

 The block_device_operations struct implements .getgeo, as well
 as many operations related to acquiring handles or changing media
 in the drive.

***Ramdisk Implementation***
The driver exposes itself as a block device, which reads and 
writes to a block of memory, allocated when it is initialized.
We have three different request modes for our file system:
 * RM_NO_QUEUE: This implements requests using no queue, and 
   blk_queue_make_request().
 * RM_FULL: This implements requests using blk_init_queue() and
   a request function that groups requests.
 * RM_SIMPLE: This implements requests using blk_init_queue() and 
   a function that does not group requests.

***Crypto Implementation***
In the devices setup function we 

Implementation Details
----------------------
***osu_ramdisk_init()***
This function registers the ramdisk using register_blkdev(), 
initializes crypto stuff, and allocates teh ramdisks using 
setup_device().

***osu_ramdisk_exit()***
This function frees each block device (vfree()s data), then 
calls unregister_blkdev(), crypto_free_cipher(), and kfree()
in order to clean up.

***setup_device()***
In this function we set up a single device, creating an
osu_ramdisk_device instance (e.g. /dev/osuramdiska). We 
first zero the memory, and set up its size. Then we register
the correct request function for the device using either 
blk_init_queue(), or blk_queue_make_request().

***osu_ramdisk_getgeo()***
We make up what our device looks like, 
because it does not physically exist, getting
the size in (units) from the block_device param passed in
and then we set the cylinders relative to the size. 

***osu_ramdisk_make_request()***
This function takes a  request from the request queue and 
processes it using osu_ramdisk_bio_transfer().

***osu_ramdisk_full_request()***
Groups requests, and handles them similar to osu_ramdisk_request().

***osu_ramdisk_transfer_request()***
Enumerates over each request in a bio struct, and calls
osu_ramdisk_bio_transfer() on each. Then, returns the number
of sectors written.

***osu_ramdisk_bio_transfer()***
Iterates over each bio_vec in a given bio struct, and transfers
them using osu_ramdisk_transfer().

***osu_ramdisk_request()***
Handles simple requests. Transfers each request in the queue by
enumerating over it using blk_fetch_request(), 
and calling osu_ramdisk_transfer() on each request.

***osu_ramdisk_transfer()***
Transfers data from a buffer to the disk, encrypting it, or 
transfers it from the ramdisk to a buffer, decrypting it. 

Testing
-------

-We can test basic functionality by doing reads and writes to the filesys.
-We can test the encryption by printk-ing the encry

Source Code
===========
<include test script here>
