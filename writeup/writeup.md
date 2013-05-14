Project 3
=========
TODO: our names here.

Plan
----
-We based our code off of ...
-added in crypto stuff
-talk about advantages
-talk about dis-ad..

Implementation
--------------
-implemented as a kernel module
-registers itself as a block device
-special module parameters, including stuff for sectors,
 private key etc

-creates a block_device_operations struct
- n functions:
- list

***Ramdisk Implementation***
The driver exposes itself as a block device, which reads and 
writes to a block of memory, allocated when it is initialized.
The 

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
whats a media change? why can this happen?

***osu_ramdisk_release()***
this releases the disk
what does that mean??

***osu_ramdisk_open()***
what does open do?

***osu_ramdisk_make_request()***
takes a  request from the request queue and processes it
using osu_ramdisk_bio_transfer().

***osu_ramdisk_full_request()***
idk

***osu_ramdisk_transfer_request()***
does a bio request (todo explain)

***osu_ramdisk_bio_transfer()***

***osu_ramdisk_request()***

***osu_ramdisk_transfer()***

Testing
-------

-We can test basic functionality by doing reads and writes to the filesys.
-We can test the encryption by printk-ing the encry

Source Code
===========
<include source here>
