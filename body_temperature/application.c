#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <time.h>

int main()
{
    int fd;
    char buf[100];
    char *temp;
    fd = open("/sys/bus/w1/devices/28-3c01f096df26/w1_slave", O_RDONLY);

    if (fd < 0)
    {
        printf("failed to open file\n");
        return -1;
    }
    if (read(fd, buf, sizeof(buf)) < 0)
    {
        printf("failed to read\n");
        return -1;
    }

    printf("Read: %s\n", buf);
    temp = strchr(buf, 't');
    sscanf(temp, "t=%s", temp);
    printf("temp = %s\n", temp);
    float value = atof(temp) / 1000;
    printf("Temperature = %.3f\n", value);
    return 0;
}