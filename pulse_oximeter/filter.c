#include <stdio.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <time.h>
#include <sys/ioctl.h>

/*IOCLT Commands to set mode configurations*/
#define HEART_RATE_MODE _IOW('a', 'b', int)
#define SPO2_MODE _IOW('a', 'a', int)
#define TEMPERATURE_MODE _IOW('a', 'c', int)

typedef uint8_t byte;

#define MAX 64

/* SaO2 parameters */
#define RESET_SPO2_EVERY_N_PULSES 4

/* Filter parameters */
#define ALPHA 0.95 // dc filter alpha value
#define MEAN_FILTER_SIZE 15

/* Pulse detection parameters */
#define PULSE_MIN_THRESHOLD 100 // 300 is good for finger, but for wrist you need like 20, and there is shitloads of noise
#define PULSE_MAX_THRESHOLD 2000
#define PULSE_GO_DOWN_THRESHOLD 1

#define PULSE_BPM_SAMPLE_SIZE 10 // Moving average size
/* Adjust RED LED current balancing*/
#define MAGIC_ACCEPTABLE_INTENSITY_DIFF 65000
#define RED_LED_CURRENT_ADJUSTMENT_MS 500

/*read buffer*/
static char buf[MAX];

/*millis*/
unsigned int millis()
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec * 1000 + (t.tv_usec + 500) / 1000;
}

unsigned int millis1()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC_RAW, &t); // change CLOCK_MONOTONIC_RAW to CLOCK_MONOTONIC on non linux computers
    return (t.tv_sec * 1000 + (t.tv_nsec + 500000) / 1000000);
}

/* Enums, data structures and typdefs. DO NOT EDIT */
typedef struct pulseoxymeter_t
{
    bool pulseDetected;
    float heartBPM;

    float irCardiogram;

    float irDcValue;
    float redDcValue;

    float SaO2;

    uint32_t lastBeatThreshold;

    float dcFilteredIR;
    float dcFilteredRed;
} pulseoxymeter_t;

typedef enum PulseStateMachine
{
    PULSE_IDLE,
    PULSE_TRACE_UP,
    PULSE_TRACE_DOWN
} PulseStateMachine;

typedef struct fifo_t
{
    uint16_t rawIR;
    uint16_t rawRed;
} fifo_t;

typedef struct dcFilter_t
{
    float w;
    float result;
} dcFilter_t;

typedef struct butterworthFilter_t
{
    float v[2];
    float result;
} butterworthFilter_t;

typedef struct meanDiffFilter_t
{
    float values[MEAN_FILTER_SIZE];
    byte index;
    float sum;
    byte count;
} meanDiffFilter_t;

/*global variables*/

dcFilter_t dcFilterIR;
dcFilter_t dcFilterRed;
butterworthFilter_t lpbFilterIR;
meanDiffFilter_t meanDiffIR;
fifo_t prevFifo;

float irACValueSqSum;
float redACValueSqSum;
uint16_t samplesRecorded;
uint16_t pulsesDetected;
float currentSaO2Value;

bool debug;

uint8_t redLEDCurrent;
float lastREDLedCurrentCheck;

uint8_t currentPulseDetectorState;
float currentBPM;
float valuesBPM[PULSE_BPM_SAMPLE_SIZE];
float valuesBPMSum;
uint8_t valuesBPMCount;
uint8_t bpmIndex;
uint32_t lastBeatThreshold;

static fifo_t readFIFO()
{
    fifo_t result;
    result.rawIR = (buf[0] << 8) | buf[1];
    result.rawRed = (buf[2] << 8) | buf[3];
    return result;
}

static dcFilter_t dcRemoval(float x, float prev_w, float alpha)
{
    dcFilter_t filtered;
    filtered.w = x + alpha * prev_w;
    filtered.result = filtered.w - prev_w;
    return filtered;
}
static void lowPassButterworthFilter(float x, butterworthFilter_t *filterResult)
{
    filterResult->v[0] = filterResult->v[1];
    // Fs = 100Hz and Fc = 10Hz
    filterResult->v[1] = (2.452372752527856026e-1 * x) + (0.50952544949442879485 * filterResult->v[0]);

    filterResult->result = filterResult->v[0] + filterResult->v[1];
}
static float meanDiff(float M, meanDiffFilter_t *filterValues)
{
    float avg = 0.0;
    filterValues->sum -= filterValues->values[filterValues->index];
    filterValues->values[filterValues->index] = M;
    filterValues->sum += filterValues->values[filterValues->index];

    filterValues->index++;
    filterValues->index = filterValues->index % MEAN_FILTER_SIZE;

    if (filterValues->count < MEAN_FILTER_SIZE)
        filterValues->count++;

    avg = filterValues->sum / filterValues->count;
    return avg - M;
}
bool detectPulse(float sensor_value)
{
    static float prev_sensor_value = 0;
    static uint8_t values_went_down = 0;
    static uint32_t currentBeat = 0;
    static uint32_t lastBeat = 0;

    if (sensor_value > PULSE_MAX_THRESHOLD)
    {
        currentPulseDetectorState = PULSE_IDLE;
        prev_sensor_value = 0;
        lastBeat = 0;
        currentBeat = 0;
        values_went_down = 0;
        lastBeatThreshold = 0;
        return false;
    }

    switch (currentPulseDetectorState)
    {
    case PULSE_IDLE:
        if (sensor_value >= PULSE_MIN_THRESHOLD)
        {
            currentPulseDetectorState = PULSE_TRACE_UP;
            values_went_down = 0;
        }
        break;

    case PULSE_TRACE_UP:
        if (sensor_value > prev_sensor_value)
        {
            currentBeat = millis();
            lastBeatThreshold = sensor_value;
        }
        else
        {

            if (debug == true)
            {
                printf("Peak reached: ");
                printf("%f", sensor_value);
                printf(" ");
                printf("%f\n", prev_sensor_value);
            }

            uint32_t beatDuration = currentBeat - lastBeat;
            lastBeat = currentBeat;

            float rawBPM = 0;
            if (beatDuration > 0)
                rawBPM = 60000.0 / (float)beatDuration;
            if (debug == true)
                printf("raw BPM = %f\n", rawBPM);

            // This method sometimes glitches, it's better to go through whole moving average everytime
            // IT's a neat idea to optimize the amount of work for moving avg. but while placing, removing finger it can screw up
            // valuesBPMSum -= valuesBPM[bpmIndex];
            // valuesBPM[bpmIndex] = rawBPM;
            // valuesBPMSum += valuesBPM[bpmIndex];

            valuesBPM[bpmIndex] = rawBPM;
            valuesBPMSum = 0;
            for (int i = 0; i < PULSE_BPM_SAMPLE_SIZE; i++)
            {
                valuesBPMSum += valuesBPM[i];
            }

            if (debug == true)
            {
                printf("CurrentMoving Avg: ");
                for (int i = 0; i < PULSE_BPM_SAMPLE_SIZE; i++)
                {
                    printf("%f", valuesBPM[i]);
                    printf(" ");
                }
                printf(" \n");
            }

            bpmIndex++;
            bpmIndex = bpmIndex % PULSE_BPM_SAMPLE_SIZE;

            if (valuesBPMCount < PULSE_BPM_SAMPLE_SIZE)
                valuesBPMCount++;

            currentBPM = valuesBPMSum / valuesBPMCount;
            if (debug == true)
            {
                printf("Avg. BPM: ");
                printf("%f\n", currentBPM);
            }

            currentPulseDetectorState = PULSE_TRACE_DOWN;

            return true;
        }
        break;

    case PULSE_TRACE_DOWN:
        if (sensor_value < prev_sensor_value)
        {
            values_went_down++;
        }

        if (sensor_value < PULSE_MIN_THRESHOLD)
        {
            currentPulseDetectorState = PULSE_IDLE;
        }
        break;
    }

    prev_sensor_value = sensor_value;
    return false;
}

void init()
{
    currentPulseDetectorState = PULSE_IDLE;
    prevFifo.rawIR = 0;
    prevFifo.rawRed = 0;
    // redLEDCurrent = (uint8_t)STARTING_RED_LED_CURRENT;
    lastREDLedCurrentCheck = 0;
    dcFilterIR.w = 0;
    dcFilterIR.result = 0;

    dcFilterRed.w = 0;
    dcFilterRed.result = 0;

    lpbFilterIR.v[0] = 0;
    lpbFilterIR.v[1] = 0;
    lpbFilterIR.result = 0;

    meanDiffIR.index = 0;
    meanDiffIR.sum = 0;
    meanDiffIR.count = 0;

    valuesBPM[0] = 0;
    valuesBPMSum = 0;
    valuesBPMCount = 0;
    bpmIndex = 0;

    irACValueSqSum = 0;
    redACValueSqSum = 0;
    samplesRecorded = 0;
    pulsesDetected = 0;
    currentSaO2Value = 0;

    lastBeatThreshold = 0;
}
static pulseoxymeter_t update()
{
    pulseoxymeter_t result = {
        /*bool pulseDetected*/ false,
        /*float heartBPM*/ 0.0,
        /*float irCardiogram*/ 0.0,
        /*float irDcValue*/ 0.0,
        /*float redDcValue*/ 0.0,
        /*float SaO2*/ currentSaO2Value,
        /*uint32_t lastBeatThreshold*/ 0,
        /*float dcFilteredIR*/ 0.0,
        /*float dcFilteredRed*/ 0.0};

    fifo_t rawData = readFIFO();

    dcFilterIR = dcRemoval((float)rawData.rawIR, prevFifo.rawIR, ALPHA);
    dcFilterRed = dcRemoval((float)rawData.rawRed, prevFifo.rawRed, ALPHA);
    prevFifo = rawData;
    float meanDiffResIR = meanDiff(dcFilterIR.result, &meanDiffIR);
    lowPassButterworthFilter(meanDiffResIR /* -dcFilterIR.result*/, &lpbFilterIR);
    irACValueSqSum += dcFilterIR.result * dcFilterIR.result;
    redACValueSqSum += dcFilterRed.result * dcFilterRed.result;
    samplesRecorded++;

    if (detectPulse(lpbFilterIR.result) && samplesRecorded > 0)
    {
        result.pulseDetected = true;
        pulsesDetected++;

        float ratioRMS = log(sqrt(redACValueSqSum / samplesRecorded)) / log(sqrt(irACValueSqSum / samplesRecorded));

        if (debug == true)
        {
            printf("RMS Ratio: ");
            printf("%f\n", ratioRMS);
        }

        // This is my adjusted standard model, so it shows 0.89 as 94% saturation. It is probably far from correct, requires proper empircal calibration
        currentSaO2Value = 110.0 - (18.0 * ratioRMS);
        result.SaO2 = currentSaO2Value;

        if (pulsesDetected % RESET_SPO2_EVERY_N_PULSES == 0)
        {
            irACValueSqSum = 0;
            redACValueSqSum = 0;
            samplesRecorded = 0;
        }
    }

    result.heartBPM = currentBPM;
    result.irCardiogram = lpbFilterIR.result;
    result.irDcValue = dcFilterIR.w;
    result.redDcValue = dcFilterRed.w;
    result.lastBeatThreshold = lastBeatThreshold;
    result.dcFilteredIR = dcFilterIR.result;
    result.dcFilteredRed = dcFilterRed.result;

    return result;
}

pulseoxymeter_t pulseoxymter;
int main()
{
    int fd, op;
    fd = open("/dev/pulse_oximeter", O_RDWR);
    if (fd < 0)
    {
        perror("failed to open");
        return -1;
    }
    printf("Enter 1-HR,2-SPO2,3-Temp: ");
    scanf("%d", &op);
    init();
    debug = false;

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
            if (read(fd, buf, 4) < 0)
            {
                perror("failed to read");
                return -1;
            }
            pulseoxymter = update();

            if (pulseoxymter.pulseDetected == true)
            {
                printf("BPM = %.3f\n", pulseoxymter.heartBPM);
            }
            usleep(10000);
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
            if (read(fd, buf, 4) < 0)
            {
                perror("failed to read");
                return -1;
            }
            pulseoxymter = update();

            if (pulseoxymter.pulseDetected == true)
            {
                printf("SPO2= %.3f\n", pulseoxymter.SaO2);
            }
            usleep(10000);
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
        if (read(fd, buf, 2) < 0)
        {
            perror("failed to read\n");
            return -1;
        }
        temp = buf[0];
        frac_temp = (float)buf[1] * (0.0625);

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