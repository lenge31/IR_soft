/*
 * by wzp
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <mt-plat/mt_gpio.h>
#include <mach/gpio_const.h>

#define PRINT_PREFIX "<IRTX_SOFT>"

// pin
static struct pinctrl *irtx_soft_pinctrl = NULL;
static struct pinctrl_state *irtx_soft_tx0 = NULL;
static struct pinctrl_state *irtx_soft_tx1 = NULL;

extern int mt_set_gpio_out_base(unsigned long pin, unsigned long output);

/************* IR NEC protocol **********/
// 38Khz--26.315789474us; here 26us--38.461538462Khz, 8us/26us==0.307692308 1/4~1/3.
static void one_cycle_send(void) // take 26us
{
	//pinctrl_select_state(irtx_soft_pinctrl, irtx_soft_tx1);
	mt_set_gpio_out_base(GPIO7, GPIO_OUT_ONE);
	udelay(8);
	//pinctrl_select_state(irtx_soft_pinctrl, irtx_soft_tx0);
	mt_set_gpio_out_base(GPIO7, GPIO_OUT_ZERO);
	udelay(18);
}

// 22*26=572us
static void pulse_560us(void)
{
	int i = 21;
	
	while(i--) one_cycle_send();
}

// 173*26=4.498ms
static void pulse_4_5ms(void)
{
	int i = 173;
	
	while(i--) one_cycle_send();
}

// 346*26=8.966ms
static void pulse_9ms(void)
{
	int i = 346;
	
	while(i--) one_cycle_send();
}

static void set_0(void)
{
	//pinctrl_select_state(irtx_soft_pinctrl, irtx_soft_tx0);
	mt_set_gpio_out_base(GPIO7, GPIO_OUT_ZERO);
}
#if 0
static void set_1(void)
{
	//pinctrl_select_state(irtx_soft_pinctrl, irtx_soft_tx1);
	mt_set_gpio_out_base(GPIO7, GPIO_OUT_ONE);
}

static void send_data(unsigned char addr, unsigned char commond)
{
	int i=0;
	
	pulse_9ms(); // AGC pulse
	
	// 4.5ms
	udelay(2000);
	udelay(2000);
	udelay(500);
	
	i=8;		// addr
	while(i)
	{
		pulse_560us();
		if(addr&(0x1<<(8-i))) udelay(1685);	// 1
		else udelay(560); // 0
		
		i--;
	}
	
	i=8;		// addr repeat
	while(i)
	{
		pulse_560us();
		if(addr&(0x1<<(8-i))) udelay(1685);	// 1
		else udelay(560); // 0
		
		i--;
	}
	
	i=8;		// commond
	while(i)
	{
		pulse_560us();
		if(commond&(0x1<<(8-i))) udelay(1685);	// 1
		else udelay(560); // 0
		
		i--;
	}
	
	i=8;		// commond repeat
	while(i)
	{
		pulse_560us();
		if(commond&(0x1<<(8-i))) udelay(1685);	// 1
		else udelay(560); // 0
		
		i--;
	}
}
#endif
/************* IR NEC protocol end*******/

/************* char dev file ************/
#define IRTX_SOFT_CHARDEV_NAME "irtx_soft_cdev"
#define IRTX_SOFT_CLASS_NAME "irtx_soft_class"
#define IRTX_SOFT_DEVICE_NAME "irtx"
static struct cdev *irtx_soft_cdev = NULL;
static dev_t irtx_soft_dev_t = 0;
static struct device *irtx_soft_device = NULL;
static struct class *irtx_soft_class = NULL;

static int irtx_soft_cdev_open(struct inode *inode, struct file *file)
{
	pr_info(PRINT_PREFIX"irtx_soft_cdev_open inode=0x%p, file=0x%p.\n", inode, file);
	return 0;
}

static ssize_t irtx_soft_cdev_read(struct file *file, char __user *buf, size_t size, loff_t *off)
{
	pr_info(PRINT_PREFIX"irtx_soft_cdev_read tmpBuf=%s, size=%d, *off=%d.\n", buf, size, (int)*off);
	return 0;
}

static ssize_t irtx_soft_cdev_write(struct file *file, const char __user *buf, size_t size, loff_t *off)
{
	char *tmpBuf;
	int ret=0, level=1, us_level=0, i,j;
	
	tmpBuf = kmalloc(size, 0);
	if(IS_ERR(tmpBuf))
	{
		pr_err(PRINT_PREFIX"irtx_soft_cdev_write kmalloc failed(%ld), size=%d.\n", PTR_ERR(tmpBuf), size);
		return -1;
	}
	ret = copy_from_user(tmpBuf, buf, size);
	if (ret)
	{
		pr_err(PRINT_PREFIX"copy_from_user failed(0x%p).\n", buf);
		return -1;
	}
	pr_info(PRINT_PREFIX"irtx_soft_cdev_write size=%d, *off=%d.\n", size, (int)*off);
	/*printk("tmpBuf:");
	for(i=0,j=0; i<size; i++)
	{
		if(j<=i/32)
		{
			printk("\n"PRINT_PREFIX"%d:", i);
			j++;
		}
		printk("%02x ", tmpBuf[i]);
	}*/
	
	for(i=0; i<size; )
	{
		us_level=8;
		level=tmpBuf[i];
		j=i+1;
		while(tmpBuf[j] == level) {us_level+=8; j++;}
		
		if(level<0xff) level=0;
		
		if(level==0) {if(us_level>8) udelay(us_level);}
		else if(us_level>510&&us_level<610) pulse_560us();
		else if(us_level>4450&&us_level<4550) pulse_4_5ms();
		else if(us_level>8950&&us_level<9050) pulse_9ms();
		else
		{
			pr_info(PRINT_PREFIX"error:level=%d, us_level=%d.\n", level, us_level);
		}
		
		i = j;
	}

	set_0();
	
	return size;
}

int irtx_soft_cdev_close(struct inode *inode, struct file *file)
{
	pr_info(PRINT_PREFIX"irtx_soft_cdev_close inode=0x%p, file=0x%p.\n", inode, file);
	return 0;
}

long irtx_soft_cdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	pr_info(PRINT_PREFIX"irtx_soft_cdev_ioctl file=0x%p, cmd=0x%x, arg=%ld,0x%lx(%d).\n", file, cmd, arg, arg, *(unsigned int*)arg);
	return 0;
}

static struct file_operations irtx_soft_cdev_fops =
{
	.owner = THIS_MODULE,
	.open = irtx_soft_cdev_open,
	.read = irtx_soft_cdev_read,
	.write = irtx_soft_cdev_write,
	.release = irtx_soft_cdev_close,
	.unlocked_ioctl = irtx_soft_cdev_ioctl
};

static int irtx_soft_chardev_init(void)
{
	int ret;
	
	ret = alloc_chrdev_region(&irtx_soft_dev_t, 0, 1, IRTX_SOFT_CHARDEV_NAME);
	if (ret)
	{
		pr_err(PRINT_PREFIX"alloc_chrdev_region failed(%d).\n", ret);
		goto exit;
	}
	pr_info(PRINT_PREFIX"major=%d, minor=%d.\n", MAJOR(irtx_soft_dev_t), MINOR(irtx_soft_dev_t));
	
	irtx_soft_cdev = cdev_alloc();
	if (IS_ERR(irtx_soft_cdev))
	{
		pr_err(PRINT_PREFIX"cdev_alloc failed(%ld).\n", PTR_ERR(irtx_soft_cdev));
		goto exit;
	}
	
	cdev_init(irtx_soft_cdev, &irtx_soft_cdev_fops);
	ret = cdev_add(irtx_soft_cdev, irtx_soft_dev_t, 1);
	if (ret)
	{
		pr_err(PRINT_PREFIX"cdev_add failed(%d).\n", ret);
		goto exit;
	}
	
	irtx_soft_class = class_create(THIS_MODULE, IRTX_SOFT_CLASS_NAME);
	if (IS_ERR(irtx_soft_class))
	{
		pr_err(PRINT_PREFIX"class_create failed(%ld).\n", PTR_ERR(irtx_soft_class));
		goto exit;
	}
	
	irtx_soft_device = device_create(irtx_soft_class, NULL, irtx_soft_dev_t, NULL, IRTX_SOFT_DEVICE_NAME);
	if (IS_ERR(irtx_soft_device))
	{
		pr_err(PRINT_PREFIX"device_create failed(%ld).\n", PTR_ERR(irtx_soft_device));
		goto exit;
	}

	return 0;
exit: // release resources
	return -1;
}
/************* char dev file end ********/

int irtx_soft_probe(struct platform_device * pdevice)
{
	pr_info(PRINT_PREFIX"irtx_soft_probe pdevice=0x%p.\n", pdevice);
	
	irtx_soft_pinctrl = devm_pinctrl_get(&pdevice->dev);
	if (IS_ERR(irtx_soft_pinctrl))
	{
		pr_err(PRINT_PREFIX"Cannot find pinctrl(%ld).\n", PTR_ERR(irtx_soft_pinctrl));
		return PTR_ERR(irtx_soft_pinctrl);
	}
	
	irtx_soft_tx0 = pinctrl_lookup_state(irtx_soft_pinctrl, "irtx_soft_tx0");
	if (IS_ERR(irtx_soft_tx0))
	{
		pr_err(PRINT_PREFIX"Cannot find pin irtx_soft_tx0(%ld).\n", PTR_ERR(irtx_soft_tx0));
		return PTR_ERR(irtx_soft_tx0);
	}
	
	irtx_soft_tx1 = pinctrl_lookup_state(irtx_soft_pinctrl, "irtx_soft_tx1");
	if (IS_ERR(irtx_soft_tx1))
	{
		pr_err(PRINT_PREFIX"Cannot find pin irtx_soft_tx1(%ld).\n", PTR_ERR(irtx_soft_tx1));
		return PTR_ERR(irtx_soft_tx1);
	}
	
	irtx_soft_chardev_init();
	
	return 0;
}

static struct of_device_id of_irtx_soft_ids[] = 
{
	{.compatible = "vsun,irtx_soft"},
	{}
};

static struct platform_driver irtx_soft_driver =
{
	.probe = irtx_soft_probe,
	.driver = 
	{
		.name = "irtx_soft_driver",
		.of_match_table = of_irtx_soft_ids
	}
};

static int irtx_soft_init(void)
{
	platform_driver_register(&irtx_soft_driver);
	
	return 0;
}

static void irtx_soft_exit(void)
{
	
	return;
}

late_initcall(irtx_soft_init);
module_exit(irtx_soft_exit);
