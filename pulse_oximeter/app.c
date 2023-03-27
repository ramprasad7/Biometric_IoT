#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>

/*IOCLT Commands to set mode configurations*/
#define HEART_RATE_MODE _IOW('a', 'b', int)
#define SPO2_MODE _IOW('a', 'a', int)
#define TEMPERATURE_MODE _IOW('a', 'c', int)

int main()
{
    char buf[100] = {0};
    int len, fd, op;
    fd = open("/dev/pulse_oximeter", O_RDWR);
    if (fd < 0)
    {
        perror("failed to open device file\n");
        return -1;
    }
    printf("Enter Mode you want to choose(1.Heart Rate,2.SPO2 Mode):\n");
    scanf("%d", &op);

    switch (op)
    {
    case 1:
    {
        if (ioctl(fd, HEART_RATE_MODE, 0) < 0)
        {
            perror("failed to set Heart rate mode\n");
            return -1;
        }
        len = 2;
        printf("Succefully set Heart Rate Only Mode\n");
        while (1)
        {
            if (read(fd, buf, len) < 0)
            {
                perror("failed to read from device\n");
                return -1;
            }
            printf("Readings: ");
            for (int i = 0; i < len; i++)
            {
                printf("%d ", buf[i]);
            }
            /*printf("Hear Rate = %d bpm\t", buf[0]);
            printf("SPO2 = %d \t", buf[1]);
            printf("Temperature = %d deg C\n", buf[2]);
            */
            printf("\n");
            usleep(1000000);
        }
        break;
    }
    case 2:
    {
        if (ioctl(fd, SPO2_MODE, 0) < 0)
        {
            perror("failed to set SPO2 mode\n");
            return -1;
        }
        len = 5;
        printf("Succefully set SPO2 Mode\n");
        while (1)
        {
            if (read(fd, buf, len) < 0)
            {
                perror("failed to read from device\n");
                return -1;
            }
            printf("Readings: ");
            for (int i = 0; i < len - 1; i++)
            {
                printf("%d ", buf[i]);
            }
            printf("Temperature = %d deg C\n", buf[len - 1]);
            /*printf("Hear Rate = %d bpm\t", buf[0]);
            printf("SPO2 = %d \t", buf[1]);
            printf("Temperature = %d deg C\n", buf[2]);
            */
            printf("\n");
            usleep(1000000);
        }
        break;
    }
        /*case 3:
        {
            if (ioctl(fd, TEMPERATURE_MODE, 0) < 0)
            {
                perror("failed to set Temperature mode\n");
                return -1;
            }
            len = 1;
            printf("Succefully set Temperature Mode\n");
            while (1)
            {
                if (read(fd, buf, len) < 0)
                {
                    perror("failed to read from device\n");
                    return -1;
                }
                printf("Temperature = %d °C\n", buf[0]);
                usleep(1000000);
            }
            break;
    }*/
    default:
    {
        printf("Invalid Chocie.\n");
        break;
    }
    }
    close(fd);
    return 0;
}

/*ret = i2c_master_recv(i2c_client, max30100_buffer, len);
if (ret < 0)
{
  pr_err("failed to read 4 bytes\n");
  return -1;
}
IR = (buffer[0] << 8) | buffer[1];
RED = (buffer[2] << 8) | buffer[3];
printk("IR =  %d\n", IR);
printk("RED = %d\n", RED);
max30100_buffer[0] = IR;
max30100_buffer[1] = RED;*/