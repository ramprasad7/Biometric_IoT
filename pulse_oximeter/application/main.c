#include "filter.h"

/*read buffer*/
char buffer[BUF_SIZE];

char filter_buf[4];
/*file desciptor*/
int fd;

pulseoxymeter_t pulseoxymter;

int main()
{
    int op;
    fd = open("/dev/pulse_oximeter", O_RDWR);
    if (fd < 0)
    {
        perror("failed to open");
        return -1;
    }
    printf("Enter 1-HR,2-SPO2,3-Temp: ");
    scanf("%d", &op);
    init();
    // debug = true;
    switch (op)
    {
    case 1:
    {
        if (ioctl(fd, HEART_RATE_MODE, 0) < 0)
        {
            perror("failed to set Heart rate mode\n");
            return -1;
        }
        while (1)
        {
            int ret = read(fd, buffer, sizeof(buffer));

            if (ret < 0)
            {
                perror("failed to read");
                return -1;
            }
            printf("number of bytes read = %d\n", ret);
            int num_of_samples = ret / 4;
            printf("num of sample = %d\n", num_of_samples);
            for (int i = 0; i < num_of_samples; i++)
            {

                filter_buf[0] = buffer[i];
                filter_buf[1] = buffer[i + 1];
                filter_buf[2] = buffer[i + 2];
                filter_buf[3] = buffer[i + 3];
                pulseoxymter = update();

                if (pulseoxymter.pulseDetected == true)
                {
                    printf("BPM = %.3f\n", pulseoxymter.heartBPM);
                }
                else
                {
                    printf("Pulse Not detected\n");
                }
            }
            usleep(1000);
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
        while (1)
        {
            if (read(fd, buffer, sizeof(buffer)) < 0)
            {
                perror("failed to read");
                return -1;
            }

            pulseoxymter = update();

            if (pulseoxymter.pulseDetected == true)
            {
                printf("SPO2= %.3f\n", pulseoxymter.SaO2);
            }
            usleep(100000);
        }
        break;
    }
    case 3:
    {
        uint8_t temp;
        float final_temp, frac_temp;
        if (ioctl(fd, TEMPERATURE_MODE, 0) < 0)
        {
            perror("failed to set Temp mode\n");
            return -1;
        }
        if (read(fd, buffer, 2) < 0)
        {
            perror("failed to read\n");
            return -1;
        }
        temp = buffer[0];
        frac_temp = (float)buffer[1] * (0.0625);

        final_temp = frac_temp + temp;
        printf("Temp(int) = %d\nTemp(frac) = %.3f\nFinal Temperature = %.3f\n", temp, frac_temp, final_temp);
        exit(1);
    }
    default:
    {
        printf("Invalid choice.");
        exit(1);
    }
    }
    close(fd);
    return 0;
}