#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define QSET 1000
#define QUANTUM 4000

static dev_t first; // Global variable for the first device number
//static struct cdev c_dev; // Global variable for the character device structure
static struct class *cl; // Global variable for the device class

struct cs_qset {
	void **data;
	struct cs_qset *next;
};

struct cs_dev {
	struct cs_qset *data;
	int quantum;
	int qset;
	unsigned long size;
	struct mutex mutex;
	struct cdev cdev;
};

struct cs_dev *csdev;

struct cs_qset *cs_follow(struct cs_dev *dev, int item)
{
	struct cs_qset *dptr;
	//int quantum = dev->quantum, qset = dev->qset;
	//int i;
	printk(KERN_INFO "Driver: follow()\n");
	printk(KERN_INFO "Driver: item = %d\n", item);
	
	dptr = dev->data;
	
	printk(KERN_INFO "Driver: dptr = %p\n", dptr);
	
	if (!dptr) {
		dptr = dev->data = kzalloc(sizeof(struct cs_qset), GFP_KERNEL);
		if (dptr == NULL)
			return NULL;
	}
	
	while (item--) {
		if (!dptr->next) {
			dptr->next = kzalloc(sizeof(struct cs_qset), GFP_KERNEL);
			if (dptr->next == NULL)
				return NULL;
		}
				
		dptr = dptr->next;
		//continue;
	}
	
	return dptr;
}

void cs_trim(struct cs_dev *dev)
{
	//if (dev->cs_qset.data != NULL)
	struct cs_qset *dptr, *next;
	int qset = dev->qset;
	int i;
	
	dptr = dev->data;
	
	while(dptr) {
	//for (dptr = dev->data; dptr; dptr = next) {
		if (dptr->data) {
			for (i = 0; i < qset; ++i) {
				kfree(dptr->data[i]);
			}
			
			kfree(dptr->data);
			dptr->data = NULL;
		}
		
		next = dptr->next;
		kfree(dptr);
		dptr = next;
	}
	
	dev->size = 0;
	dev->quantum = QUANTUM;
	dev->qset = QSET;
	dev->data = NULL;
	
}

static int my_open(struct inode *i, struct file *f)
{
	struct cs_dev *dev;
	
	dev = container_of(i->i_cdev, struct cs_dev, cdev);
    f->private_data = dev;
    
    if ( (f->f_flags & O_ACCMODE) == O_WRONLY) {
		cs_trim(dev);
	}
    
    printk(KERN_INFO "Driver: open()\n");
    return 0;
}

loff_t my_llseek(struct file *f, loff_t off, int whence)
{
	struct cs_dev *dev = f->private_data;
	loff_t newpos;
	
	printk(KERN_INFO "Driver: llseek()\n");
	
	switch(whence) {
	case 0:
		newpos = off;
		break;
	case 1:
		newpos = f->f_pos + off;
		break;
	case 2:
		newpos = dev->size + off;
		break;
	default:
		return -EINVAL;
	}
	if (newpos < 0) return -EINVAL;
	f->f_pos = newpos;
	
	printk(KERN_INFO "Driver: llseek end\n");
	printk(KERN_INFO "off = %llu, newpos = %llu\n", off, newpos);
	
	return newpos;
}

static int my_close(struct inode *i, struct file *f)
{
    printk(KERN_INFO "Driver: close()\n");
    return 0;
}
static ssize_t my_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{	
	struct cs_dev *dev = f->private_data;
    struct cs_qset *dptr;
    int qset = dev->qset, quantum = dev->quantum;
    int item_size = qset * quantum;
    int item, spos, qpos, rest;
    
    printk(KERN_INFO "Driver: read()\n");
    printk(KERN_INFO "off = %llu, count = %zu\n", *off, len);
    printk(KERN_INFO "fpos = %llu\n", f->f_pos);
    
    if (*off >= dev->size)
		return 0;
		
	//printk(KERN_INFO "Test1\n");
		
	if (*off + len > dev->size)
		len = dev->size - *off;
		
	//printk(KERN_INFO "Test2\n");
    
    item = (long)*off / item_size;
    rest = (long)*off % item_size;
    
    spos = rest / quantum;
    qpos = rest % quantum;
    
    dptr = cs_follow(dev, item);
    
    if (dptr == NULL)
		return 0;
		
	//printk(KERN_INFO "Test3, %d\n", spos);
	//printk(KERN_INFO "P = %p\n", dptr);
		
	if (dptr == NULL || !dptr->data || ! dptr->data[spos] )
		return 0;
		
	printk(KERN_INFO "Test4\n");
		
	if (len > quantum - qpos)
		len = quantum - qpos;
    
    if (copy_to_user(buf, dptr->data[spos]+qpos, len))
    //if (copy_to_user(buf, dptr->data[], len))
		return -EFAULT;
	
	//printk(KERN_INFO "Test5 off\n");
		
	*off += len; 
	
	//printk(KERN_INFO "off after is %llu, %zu\n", *off, len);
    
    
    printk(KERN_INFO "Driver: read completed");

    
    return len;
}
static ssize_t my_write(struct file *f, const char __user *buf, size_t len,
    loff_t *off)
{
    struct cs_dev *dev = f->private_data;
    struct cs_qset *dptr;
    
    int item, qpos, spos, rest;    
    int quantum = dev->quantum, qset = dev->qset;
    int item_size = qset * quantum;
    
    printk(KERN_INFO "Driver: write()\n");
    printk(KERN_INFO "off = %llu, count = %zu\n", *off, len);
    printk(KERN_INFO "fpos = %llu\n", f->f_pos);
    
    item = (long)*off / item_size;
    rest = (long)*off % item_size;
    
    //printk(KERN_INFO "Item, resr is %d, %d\n", item, rest);
    
    spos = rest / quantum;
    qpos = rest % quantum;
    
    dptr = cs_follow(dev, item);
    
    if (dptr == NULL)
		return -ENOMEM;
		
	if (dptr->data == NULL)
		dptr->data = kzalloc(qset * sizeof(char *), GFP_KERNEL);
	
	if (dptr->data == NULL)
		return -ENOMEM;
		
	if (! dptr->data[spos])
		dptr->data[spos] = kzalloc(quantum, GFP_KERNEL);
	
	if (dptr->data[spos] == NULL)
		return -ENOMEM;
	//printk(KERN_INFO "P = %p\n", dptr);
		
		
	if (len > quantum - qpos)
		len = quantum - qpos;
		
	if (copy_from_user(dptr->data[spos]+qpos, buf, len))
		return -EFAULT;
	
	*off += len;
	
	printk(KERN_INFO "After off = %llu, count = %zu\n", *off, len);
	
	if (dev->size < *off)
		dev->size = *off;
		
	printk(KERN_INFO "Driver: write completed");
	
	return len;
}

static struct file_operations my_fops =
{
    .owner = THIS_MODULE,
    .open = my_open,
    .release = my_close,
    .read = my_read,
    .write = my_write,
    .llseek = my_llseek
};

static int __init csscull_init(void) /* Constructor */
{
    int ret;
    struct device *dev_ret;
    //struct csscull_dev *scdev = &csscull_dev;

    printk(KERN_INFO "SK: csscull registered");
    if ((ret = alloc_chrdev_region(&first, 0, 1, "skdevice")) < 0)
    {
        return ret;
    }
    if (IS_ERR(cl = class_create(THIS_MODULE, "chardrv")))
    {
        unregister_chrdev_region(first, 1);
        return PTR_ERR(cl);
    }
    if (IS_ERR(dev_ret = device_create(cl, NULL, first, NULL, "mynull")))
    {
        class_destroy(cl);
        unregister_chrdev_region(first, 1);
        return PTR_ERR(dev_ret);
    }

    csdev = kzalloc(sizeof(struct cs_dev), GFP_KERNEL);
    
    cdev_init(&csdev->cdev, &my_fops);
    csdev->cdev.owner = THIS_MODULE;
    csdev->cdev.ops = &my_fops;
    csdev->size = 0;
    csdev->qset = QSET;
    csdev->quantum = QUANTUM;
    mutex_init(&csdev->mutex);
    
    if ((ret = cdev_add(&csdev->cdev, first, 1)) < 0)
    {
        // TODO exit
        return ret;
    }
    
    
    
    
    
    return 0;
}

static void __exit csscull_exit(void) /* Destructor */
{
    cs_trim(csdev);
    kfree(csdev);
    cdev_del(&csdev->cdev);
    device_destroy(cl,first);
    class_destroy(cl);
    unregister_chrdev_region(first, 1);
    printk(KERN_INFO "SK: csscull unregistered");
}

module_init(csscull_init);
module_exit(csscull_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SK");
MODULE_DESCRIPTION("First Character Driver");
