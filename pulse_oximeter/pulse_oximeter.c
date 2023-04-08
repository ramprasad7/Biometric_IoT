/*Pulse Oximter Device Driver*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>

/*User Defined Macros for Errors*/
#define SUCCESS 0
#define FAILURE -1

/*GPIO for receiving Interrpt*/
#define MAX30100_INTERRUPT_PIN (21)

/*available i2c bus number*/
#define I2C_BUS (1)
/*GPIO for interrupt*/
#define GPIO_INTERRUPT (21)
/*pulse oximeter address*/
#define MAX30100_OXIMETER_ADDRESS (0x57)
/*our slave device name*/
#define SLAVE_DEVICE_NAME "pulse_oximeter"

/*IOCLT Commands to set mode configurations*/
#define HEART_RATE_MODE _IOW('a', 'b', int)
#define SPO2_MODE _IOW('a', 'a', int)
#define TEMPERATURE_MODE _IOW('a', 'c', int)

/*buffer size*/
#define BUF_SIZE 64

/*driver buffer*/
static char *max30100_buffer;
/*instruction buffer*/
static char instruction_buffer[2];

/*Register Maps*/
// STATUS
#define INT_STATUS_REG_OFFSET 0x00
#define INT_ENABLE_REG_OFFSET 0x01
// FIFO
#define FIFO_WR_PTR_REG_OFFSET 0x02
#define FIFO_OVER_FLOW_REG_OFFSET 0x03
#define FIFO_RD_PTR_REG_OFFSET 0X04
#define FIFO_DATA_REG_OFFSET 0x05
// CONFIGURATION
#define MODE_CONFIG_REG_OFFSET 0x06
#define SPO2_CONFIG_REG_OFFSET 0X07
#define LED_CONFIG_REG_OFFSET 0x09
// TEMPERATURE
#define TEMP_INTEGER_REG_OFFSET 0x16
#define TEMP_FRACTION_REG_OFFSET 0x17
// PART ID
#define REVISION_ID_REG_OFFSET 0xFE
#define PART_ID_REG_OFFSET 0xFF

/*device number*/
static dev_t max30100_dev;
/*cdev structure*/
static struct cdev max30100_cdev;
/*class structure*/
static struct class *max30100_class;
/*I2C adapter structure*/
static struct i2c_adapter *i2c_adapter;
/*I2C client structure*/
static struct i2c_client *i2c_client;

/*irq number*/
static int max30100_irq_num;
/*length from user*/
static int length;

/*global variable to know what to read*/
static int flag = 0;

/*work queue*/
static struct work_struct max30100_workqueue;

/*Workqueue Function*/
static void max30100_workqueue_fn(struct work_struct *work)
{
  int i = 0;
  int ret;
  printk("Work Queue Function Invoked\n");
  if (flag == 0)
  {
    for (i = 0; i < length; i++)
    {
      instruction_buffer[0] = FIFO_DATA_REG_OFFSET;
      ret = i2c_master_send(i2c_client, instruction_buffer, 1);
      if (ret < 0)
      {
        pr_err("failed to send fifo data register\n");
        return;
      }

      ret = i2c_master_recv(i2c_client, max30100_buffer + i, 1);
      if (ret < 0)
      {
        pr_err("failed to read from FIFO\n");
        return;
      }
      printk("Oximeter Data = %d", max30100_buffer[i]);
    }
  }
  else if (flag == 1)
  {
    instruction_buffer[0] = TEMP_INTEGER_REG_OFFSET;
    ret = i2c_master_send(i2c_client, instruction_buffer, 1);
    if (ret < 0)
    {
      pr_err("failed to send temp reg off set\n");
      return;
    }

    ret = i2c_master_recv(i2c_client, max30100_buffer, 1);
    if (ret < 0)
    {
      pr_err("failed to read temp int\n");
      return;
    }
    printk("Int temp = %d\n", max30100_buffer[0]);

    instruction_buffer[0] = TEMP_FRACTION_REG_OFFSET;
    ret = i2c_master_send(i2c_client, instruction_buffer, 1);
    if (ret < 0)
    {
      pr_err("failed to send temp frac reg offset\n");
      return;
    }

    ret = i2c_master_recv(i2c_client, max30100_buffer + 1, 1);
    if (ret < 0)
    {
      pr_err("failed to read temp frac\n");
      return;
    }
    printk("Frac temp = %x\n", max30100_buffer[1]);
  }
}

/*interrupt handler*/
static irqreturn_t max30100_irq_handler(int irq, void *dev_id)
{
  printk("Interrupt Occured...\n");
  schedule_work(&max30100_workqueue);
  return IRQ_HANDLED;
}

/*function to setup configuration*/
static int config_setup(struct i2c_client *i2c_client)
{
  int ret;
  printk("Setting up Configurations..\n");
  instruction_buffer[0] = INT_STATUS_REG_OFFSET;
  ret = i2c_master_send(i2c_client, instruction_buffer, 1);
  if (ret < 0)
  {
    pr_err("failed to send interrput status register offset\n");
    return FAILURE;
  }

  ret = i2c_master_recv(i2c_client, instruction_buffer, 1);
  if (ret < 0)
  {
    pr_err("failed to get interrput status register offset\n");
    return FAILURE;
  }
  /*
    printk("Interrupt status is %x\n", instruction_buffer[0]);
    printk("Interrupt PWR_RDY Status = %d\n", (instruction_buffer[0] >> 0) & 0x1);
    printk("Interrupt B1 Status = %d\n", (instruction_buffer[0] >> 1) & 0x1);
    printk("Interrupt B2 Status = %d\n", (instruction_buffer[0] >> 2) & 0x1);
    printk("Interrupt B3 Status = %d\n", (instruction_buffer[0] >> 3) & 0x1);
    printk("Interrupt SPO2_RDY Status = %d\n", (instruction_buffer[0] >> 4) & 0x1);
    printk("Interrupt HR_RDY Status = %d\n", (instruction_buffer[0] >> 5) & 0x1);
    printk("Interrupt TEMP_RDY Status = %d\n", (instruction_buffer[0] >> 6) & 0x1);
    printk("Interrupt A_FULL Status = %d\n", (instruction_buffer[0] >> 7) & 0x1);
  */
  instruction_buffer[0] = LED_CONFIG_REG_OFFSET;
  instruction_buffer[1] = 0x66; // LED Current = 20 mA
  ret = i2c_master_send(i2c_client, instruction_buffer, 2);
  if (ret < 0)
  {
    pr_err("failed to send led config reg\n");
    return FAILURE;
  }
  printk("Done Initial Setup\n");
  return SUCCESS;
}

/*function to reset configuration*/
static void config_release(struct i2c_client *i2c_client)
{
  int ret;
  instruction_buffer[0] = LED_CONFIG_REG_OFFSET;
  instruction_buffer[1] = 0x00;
  ret = i2c_master_send(i2c_client, instruction_buffer, 2);
  if (ret < 0)
  {
    pr_err("failed to change LED config\n");
    return;
  }
  instruction_buffer[0] = SPO2_CONFIG_REG_OFFSET;
  instruction_buffer[1] = 0x00;
  ret = i2c_master_send(i2c_client, instruction_buffer, 2);
  if (ret < 0)
  {
    pr_err("failed to change SPO2 config\n");
    return;
  }
  instruction_buffer[0] = MODE_CONFIG_REG_OFFSET;
  instruction_buffer[1] = 0x00;
  ret = i2c_master_send(i2c_client, instruction_buffer, 2);
  if (ret < 0)
  {
    pr_err("failed to change Mode config\n");
    return;
  }
  printk("Done resetting all registers\n");
}

/*max30100 open function*/
static int max30100_open(struct inode *inode, struct file *file)
{
  int ret;
  max30100_buffer = kzalloc(BUF_SIZE, GFP_KERNEL);
  if (max30100_buffer == NULL)
  {
    pr_err("failed to allocate memory for driver buffer\n");
    return FAILURE;
  }
  ret = config_setup(i2c_client);
  if (ret < 0)
  {
    pr_err("failed to setup initial configuration\n");
    return FAILURE;
  }

  if (gpio_is_valid(MAX30100_INTERRUPT_PIN) == false)
  {
    pr_err("GPIO - %d is not valid\n", MAX30100_INTERRUPT_PIN);
    config_release(i2c_client);
    return FAILURE;
  }

  if (gpio_request(MAX30100_INTERRUPT_PIN, "MAX30100_INTERRUPT_PIN") < 0)
  {
    pr_err("failed to request GPIO - %d\n", MAX30100_INTERRUPT_PIN);
    config_release(i2c_client);
    return FAILURE;
  }

  if (gpio_direction_input(MAX30100_INTERRUPT_PIN) < 0)
  {
    pr_err("failed to set direction GPIO - %d\n", MAX30100_INTERRUPT_PIN);
    gpio_free(MAX30100_INTERRUPT_PIN);
    config_release(i2c_client);
    return FAILURE;
  }

  max30100_irq_num = gpio_to_irq(MAX30100_INTERRUPT_PIN);
  printk("IRQ Number = %d\n", max30100_irq_num);

  if (request_irq(max30100_irq_num, (void *)max30100_irq_handler, IRQF_TRIGGER_FALLING, "MAX30100_INTERRUPT_PIN", NULL) < 0)
  {
    pr_err("failed to request for irq\n");
    gpio_free(MAX30100_INTERRUPT_PIN);
    config_release(i2c_client);
    kfree(max30100_buffer);
    return FAILURE;
  }

  INIT_WORK(&max30100_workqueue, max30100_workqueue_fn);
  printk("I2C max30100 Slave Device Opened...\n");
  return SUCCESS;
}

/*max30100 close function*/
static int max30100_release(struct inode *inode, struct file *file)
{
  free_irq(max30100_irq_num, NULL);
  gpio_free(MAX30100_INTERRUPT_PIN);
  config_release(i2c_client);
  kfree(max30100_buffer);
  printk("I2C max30100 Slave Device Closed\n");
  return SUCCESS;
}

/*max30100 read function*/
static ssize_t max30100_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
  // int i = 0;
  // int temperature; // red, ir;
  // int rd_ptr, wr_ptr;
  // int ret, ov_ptr;
  length = len;

  // ir = (max30100_buffer[0] << 8) | max30100_buffer[1];
  // red = (max30100_buffer[2] << 8) | max30100_buffer[3];
  //  printk("IR =  %d ", ir);
  //  printk("RED = %d\n", red);
  // printk("flag = %d\n", flag);
  if (copy_to_user(buf, max30100_buffer, length) > 0)
  {
    pr_err("failed to read all the bytes\n");
    return FAILURE;
  }
  return len;
}

static long max30100_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
  int ret;
  printk("Performing IOCTL Operations...\n");
  switch (cmd)
  {
  case HEART_RATE_MODE:
  {
    instruction_buffer[0] = MODE_CONFIG_REG_OFFSET;
    instruction_buffer[1] = 0x02; // for Heart rate
    ret = i2c_master_send(i2c_client, instruction_buffer, 2);
    if (ret < 0)
    {
      pr_err("failed to set heart rate only mode\n");
      return FAILURE;
    }

    instruction_buffer[0] = INT_ENABLE_REG_OFFSET;
    instruction_buffer[1] = 0xa0;
    ret = i2c_master_send(i2c_client, instruction_buffer, 2);
    if (ret < 0)
    {
      pr_err("failed to enable interrupt\n");
      return -1;
    }

    instruction_buffer[0] = FIFO_WR_PTR_REG_OFFSET;
    instruction_buffer[1] = 0x00;
    ret = i2c_master_send(i2c_client, instruction_buffer, 2);
    if (ret < 0)
    {
      pr_err("failed to set write pointer\n");
      return FAILURE;
    }
    instruction_buffer[0] = FIFO_RD_PTR_REG_OFFSET;
    instruction_buffer[1] = 0x00;
    ret = i2c_master_send(i2c_client, instruction_buffer, 2);
    if (ret < 0)
    {
      pr_err("failed to read pointer\n");
      return FAILURE;
    }
    instruction_buffer[0] = FIFO_OVER_FLOW_REG_OFFSET;
    instruction_buffer[1] = 0x00;
    ret = i2c_master_send(i2c_client, instruction_buffer, 2);
    if (ret < 0)
    {
      pr_err("failed to over flow counter\n");
      return FAILURE;
    }
    flag = 0;
    printk("Succefully Set Heart Rate Mode\n");
    break;
  }
  case SPO2_MODE:
  {
    instruction_buffer[0] = MODE_CONFIG_REG_OFFSET;
    instruction_buffer[1] = 0x03; // for SPO2
    ret = i2c_master_send(i2c_client, instruction_buffer, 2);
    if (ret < 0)
    {
      pr_err("failed to set heart rate only mode\n");
      return FAILURE;
    }

    instruction_buffer[0] = SPO2_CONFIG_REG_OFFSET;
    instruction_buffer[1] = 0x43; // LED pulse width = 1600us & SPO2 sampling rate = 50 samples per second
    ret = i2c_master_send(i2c_client, instruction_buffer, 2);
    if (ret < 0)
    {
      pr_err("failed to set SPO2 Mode config reg\n");
      return ret;
    }

    instruction_buffer[0] = INT_ENABLE_REG_OFFSET;
    instruction_buffer[1] = 0xd0;
    ret = i2c_master_send(i2c_client, instruction_buffer, 2);
    if (ret < 0)
    {
      pr_err("failed to enable interrupt\n");
      return -1;
    }

    instruction_buffer[0] = FIFO_WR_PTR_REG_OFFSET;
    instruction_buffer[1] = 0x00;
    ret = i2c_master_send(i2c_client, instruction_buffer, 2);
    if (ret < 0)
    {
      pr_err("failed to set write pointer\n");
      return FAILURE;
    }
    instruction_buffer[0] = FIFO_RD_PTR_REG_OFFSET;
    instruction_buffer[1] = 0x00;
    ret = i2c_master_send(i2c_client, instruction_buffer, 2);
    if (ret < 0)
    {
      pr_err("failed to read pointer\n");
      return FAILURE;
    }

    instruction_buffer[0] = FIFO_OVER_FLOW_REG_OFFSET;
    instruction_buffer[1] = 0x00;
    ret = i2c_master_send(i2c_client, instruction_buffer, 2);
    if (ret < 0)
    {
      pr_err("failed to over flow counter\n");
      return FAILURE;
    }
    flag = 0;
    printk("Succefully Set SPO2 Mode\n");
    break;
  }
  case TEMPERATURE_MODE:
  {
    instruction_buffer[0] = MODE_CONFIG_REG_OFFSET;
    instruction_buffer[1] = 0x08; // for temperature
    ret = i2c_master_send(i2c_client, instruction_buffer, 2);
    if (ret < 0)
    {
      pr_err("failed to set heart rate only mode\n");
      return FAILURE;
    }

    instruction_buffer[0] = INT_ENABLE_REG_OFFSET;
    instruction_buffer[1] = 0x10;
    ret = i2c_master_send(i2c_client, instruction_buffer, 2);
    if (ret < 0)
    {
      pr_err("failed to enable interrupt\n");
      return -1;
    }
    flag = 1;
    printk("Temperature Mode Set\n");
    break;
  }
  default:
  {
    printk("Invalid Mode selected\n");
    break;
  }
  }
  return SUCCESS;
}

/*max30100 file operations structure*/
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = max30100_open,
    .read = max30100_read,
    .unlocked_ioctl = max30100_ioctl,
    .release = max30100_release,
};

/*max30100 driver probe function*/
static int max30100_probe(struct i2c_client *i2c_client, const struct i2c_device_id *max30100_ids)
{
  int ret;
  ret = alloc_chrdev_region(&max30100_dev, 0, 1, SLAVE_DEVICE_NAME);
  if (ret < 0)
  {
    pr_err("Failed to register i2c max30100 slave device\n");
    return FAILURE;
  }

  cdev_init(&max30100_cdev, &fops);

  if (cdev_add(&max30100_cdev, max30100_dev, 1) < 0)
  {
    pr_err("Failed i2c max30100 slave cdev_add\n");
    unregister_chrdev_region(max30100_dev, 1);
    return FAILURE;
  }

  max30100_class = class_create(THIS_MODULE, "max30100_slave_class");
  if (max30100_class == NULL)
  {
    pr_err("Failed to create i2c max30100 slave class\n");
    cdev_del(&max30100_cdev);
    unregister_chrdev_region(max30100_dev, 1);
    return FAILURE;
  }

  if (device_create(max30100_class, NULL, max30100_dev, NULL, SLAVE_DEVICE_NAME) == NULL)
  {
    pr_err("Failed to create i2c max30100 slave device file\n");
    class_destroy(max30100_class);
    cdev_del(&max30100_cdev);
    unregister_chrdev_region(max30100_dev, 1);
    return FAILURE;
  }

  instruction_buffer[0] = PART_ID_REG_OFFSET;
  ret = i2c_master_send(i2c_client, instruction_buffer, 1);
  if (ret < 0)
  {
    pr_err("Failed to send PART ID Register\n");
    return FAILURE;
  }
  ret = i2c_master_recv(i2c_client, instruction_buffer, 1);
  if (ret < 0)
  {
    pr_err("Failed to read PART ID Register\n");
    return FAILURE;
  }
  printk("PART ID = %x\n", instruction_buffer[0]);

  instruction_buffer[0] = REVISION_ID_REG_OFFSET;
  ret = i2c_master_send(i2c_client, instruction_buffer, 1);
  if (ret < 0)
  {
    pr_err("Failed to send Revision ID Register\n");
    return FAILURE;
  }
  ret = i2c_master_recv(i2c_client, instruction_buffer, 1);
  if (ret < 0)
  {
    pr_err("Failed to read Revision ID Register\n");
    return FAILURE;
  }
  printk("Revision ID = %x\n", instruction_buffer[0]);
  printk("I2C max30100 Salve Driver Probed\n");
  return SUCCESS;
}

/*max30100 driver remove function*/
static void max30100_remove(struct i2c_client *i2c_client)
{
  device_destroy(max30100_class, max30100_dev);
  class_destroy(max30100_class);
  cdev_del(&max30100_cdev);
  unregister_chrdev_region(max30100_dev, 1);
  printk("I2C max30100 Salve Driver Removed\n");
}

/*i2c slave(max30100) device id structure*/
static const struct i2c_device_id max30100_ids[] = {
    {SLAVE_DEVICE_NAME, 0},
    {}};

MODULE_DEVICE_TABLE(i2c, max30100_ids);

/*i2c driver structure*/
static struct i2c_driver max30100_driver = {
    .driver = {
        .name = SLAVE_DEVICE_NAME,
        .owner = THIS_MODULE,
    },
    .probe = max30100_probe,
    .remove = max30100_remove,
    .id_table = max30100_ids,
};

/*max30100 IC information structure*/
static const struct i2c_board_info max30100_ic_info = {
    I2C_BOARD_INFO(SLAVE_DEVICE_NAME, MAX30100_OXIMETER_ADDRESS),
};

/*module init function*/
static int __init max30100_init(void)
{
  i2c_adapter = i2c_get_adapter(I2C_BUS);

  if (i2c_adapter == NULL)
  {
    pr_err("I2C adapter failed to allocate\n");
    return FAILURE;
  }

  i2c_client = i2c_new_client_device(i2c_adapter, &max30100_ic_info);

  if (i2c_client == NULL)
  {
    pr_err("Failed to create new client device for i2c max30100 slave\n");
    return FAILURE;
  }
  i2c_add_driver(&max30100_driver);
  printk("I2C max30100 Salve Driver loaded successsfully\n");
  return SUCCESS;
}

/*module exit function*/
static void __exit max30100_cleanup(void)
{
  i2c_unregister_device(i2c_client);
  i2c_del_driver(&max30100_driver);
  printk("I2C max30100 Salve Driver unloaded successfully\n");
}

module_init(max30100_init);
module_exit(max30100_cleanup);

/*Meta information*/
MODULE_AUTHOR("Ram");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MAX30100 Pulse Oximeter Device Driver");
MODULE_VERSION("1.0");
