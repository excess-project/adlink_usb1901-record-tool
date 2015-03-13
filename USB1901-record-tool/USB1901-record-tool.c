/*----------------------------------------------------------------------------*/
/* Anders Gidenstam for the EXCESS project, 2014                              */
/*                                                                            */
/* This program records analog input samples in differential mode from a      */
/* USB-1901 in double buffered mode.                                          */
/*                                                                            */
/* The program is based on the USB1901 manual and inspired by the             */
/* UDASK C1902_AI_DBFtoFile sample.                                           */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "UsbDask.h"

/* Channels descriptor. */
/* Note: Single-ended or differential mode applies to all channels.
 *       Voltage range can be selected per channel.
 */
typedef struct {
    U16 id;
    U16 AdRange;
} channel_t;

/* USB-1901 related constants. */
#define MAX_CHANNELS        8
#define AI_COUNT            20480       // Buffer size.
#define U1902_TIMEBASE      80000000    // 80MHz clock.
#define INVALID_CARD_ID     0xFFFF

/* Internal functions. */
static void print_usage(int argc, char** argv);
static void process_arguments(int argc, char** argv);
static int process_samples(signed short* buffer, int length, int offset);
static double ad_range_to_volt(U16 range);
static I16 open_USB1901();
static void clean_exit(U16 card, int code);

/* Global state. Use with care. */
char default_file_name[] = "data.csv";
char* file_name = default_file_name;
FILE* file = NULL;
I16 card = INVALID_CARD_ID;
int sample_rate = 200;
int duration = -1;
int no_channels = 0;
channel_t channel[MAX_CHANNELS];

int main(int argc, char **argv)
{
    I16 err;
    BOOLEAN Stopped;
    BOOLEAN HalfReady;
    U32 AccessCnt = 0;
    int offset = 0;
    int samples = 0;
    signed short buffer[AI_COUNT];
    DWORD t1, t2; /* milliseconds */
    /*--------------------------------*/

    process_arguments(argc, argv);
    card = open_USB1901();

    t1 = GetTickCount();

    /* Open the data file */
    file = fopen(file_name, "w");
    if (file == NULL) {
        perror("fopen error: ");
    }
    
    /* Acquire and store the samples. */
    if (duration < 1) {
        printf("                            Press any key to stop...\n");
    }
    for (;;) {
        /* Wait some milliseconds. */
        Sleep(10);

        /* Check Buffer Ready */
        err = UD_AI_AsyncDblBufferHalfReady(card, &HalfReady, &Stopped);
        if (err < 0) {
            fprintf(stderr, "AI_AsyncDblBufferHalfReady Error: %d\n", err);
            UD_AI_AsyncClear(card, &AccessCnt);
            clean_exit(card, 1);
        }
        
        if(HalfReady) {
            printf("\nBuffer Half Ready...\n");
            printf("Writing %d samples to the file '%s'...\n", AI_COUNT/2, file_name);
            if (duration < 1) {
                printf("                            Press any key to stop...\n");
            }
            err = UD_AI_AsyncDblBufferTransfer(card, (U16*)buffer);
            if (err < 0) {
                fprintf(stderr, "AI_AsyncDblBufferTransfer Error: %d\n", err);
                UD_AI_AsyncClear(card, &AccessCnt);
                clean_exit(card, 1);
            }
            samples += AI_COUNT/2;
            offset = process_samples(buffer, AI_COUNT/2, offset);
        }

        /* Exit check. */
        if (duration < 1) {
            if (kbhit()) {
                getch();
                break;
            }
        } else {
            t2 = GetTickCount();
            if ((t2 - t1) > 1000*duration) {
                break;
            }
        }
    }

    /* Clear AI Setting and Get Remaining data */
    err = UD_AI_AsyncClear(card, &AccessCnt);
    if (err < 0) {
        fprintf(stderr, "AI_AsyncClear Error: %d\n", err);
        clean_exit(card, 1);
    }
    t2 = GetTickCount();

    /* Read the last data. */
    err = UD_AI_AsyncDblBufferTransfer(card, (U16*)buffer);
    if (err < 0) {
        fprintf(stderr, "AI_AsyncDblBufferTransfer Error: %d\n", err);
        clean_exit(card, 1);
    }

    printf("\nWriting the last %d samples out of %d to '%s'. Total duration %f sec.\n",
           AccessCnt,
           samples + AccessCnt,
           file_name,
           (double)(t2 - t1) / 1000.0);
    offset = process_samples(buffer, AccessCnt, offset);

    UD_Release_Card(card);

    if (duration < 1) {
        printf("                            Press any key to exit...\n");
        getch();
    }

    clean_exit(card, 0);
}

static void print_usage(int argc, char** argv)
{
    printf("Usage: %s [OPTIONS]\n\n", argv[0]);
    printf("  -h                       Print this message and exit.\n");
    printf("  -o <file name>           Save the result in the named file.\n");
    printf("                           The default is 'data.csv'.\n");
    printf("  -s <sample rate in Hz>   Set the sample rate in Hz. The default is 200 Hz.\n");
    printf("  -c <channel id>:<range>  Add <channel id> to the set of sampled channels with\n");
    printf("                           the selected range. Ranges: 0 - +/-200mV;\n");
    printf("                           1 - +/-1.00V; 2 - +/-2.00V; 3 - +/-10.0V.\n");
    printf("  -d <duration>            Sample for <duration> seconds.\n");
    printf("                           The default is until a key is pressed. \n");
}

static void process_arguments(int argc, char** argv)
{
    int i = 1;
    no_channels = 0;

    while (i < argc) {
        if (strcmp(argv[i], "-h") == 0) {
            print_usage(argc, argv);
            exit(0);
        } else if (strcmp(argv[i], "-o") == 0) {
            i++;
            if (i >= argc) {
                fprintf(stderr, "%s: No file name given to '-o'.\n", argv[0]);
                exit(-1);
            }
            file_name = argv[i];
        } else if (strcmp(argv[i], "-s") == 0) {
            int s;
            i++;
            if (i >= argc || 1 != sscanf(argv[i], "%d", &s) || s < 1) {
                fprintf(stderr, "%s: Bad sample frequency given to '-s'.\n", argv[0]);
                exit(-1);
            }
            sample_rate = s;
        } else if (strcmp(argv[i], "-c") == 0) {
            if (no_channels < MAX_CHANNELS) {
                int id, range;
                i++;
                if (i >= argc || 2 != sscanf(argv[i], "%d:%d", &id, &range) ||
                    id < 0 || id > 15 || range < 0 || range > 3) {
                    fprintf(stderr, "%s: Bad parameter '%s' given to '-c'.\n", argv[0], argv[i]);
                    exit(-1);
                }
                channel[no_channels].id = id;
                switch (range) {
                case 0:
                    channel[no_channels].AdRange = AD_B_0_2_V;
                    break;
                case 1:
                    channel[no_channels].AdRange = AD_B_1_V;
                    break;
                case 2:
                    channel[no_channels].AdRange = AD_B_2_V;
                    break;
                case 3:
                    channel[no_channels].AdRange = AD_B_10_V;
                    break;
                }
                no_channels++;
            } else {
                fprintf(stderr, "%s: Too many channels.\n", argv[0]);
                exit(-1);
            }
        } else if (strcmp(argv[i], "-d") == 0) {
            int d;
            i++;
            if (i >= argc || 1 != sscanf(argv[i], "%d", &d) || d < 1) {
                fprintf(stderr, "%s: Bad duration given to '-d'.\n", argv[0]);
                exit(-1);
            }
            duration = d;
        } else {
            fprintf(stderr, "%s: Unknown commandline argument '%s'.\n", argv[0], argv[i]);
            exit(-1);
        }
        i++;
    }
}

int process_samples(signed short* buffer, int length, int offset)
{
    int i;
    double toVolts[MAX_CHANNELS];
    double samplesPerChannel = (double)(length/no_channels);
    double sum[MAX_CHANNELS];

    for (i = 0; i < no_channels; i++) {
        sum[i] = 0.0;
        toVolts[i] = ad_range_to_volt(channel[i].AdRange)/(double)(1<<15);
    }
    for (i = 0; i < length; i++) {
        int c = (i + offset) % no_channels;
        double volts = (double)buffer[i] * toVolts[c];

        sum[c] += volts;

        /* Write data into the file. */
        if ((c + 1) % no_channels) {
            fprintf(file, "%e,\t", volts);
        } else {
            fprintf(file, "%e\n", volts);
        }
    }
    for (i = 0; i < no_channels; i++) {
        //printf("  Channel %d first value %e V.\n", i, (double)buffer[i] * toVolts[i]);
        printf("  Channel %d average %e V.\n", channel[i].id, sum[i]/samplesPerChannel);
    }
    return (length + offset) % no_channels;
}

static double ad_range_to_volt(U16 range)
{
    switch (range) {
    case AD_B_0_2_V:
        return 0.200;
    case AD_B_1_V:
        return 1.00;
    case AD_B_2_V:
        return 2.00;
    case AD_B_10_V:
        return 10.00;
    default:
        fprintf(stderr, "ad_range_to_volt: Unknown AD range.");
        return 0.00;
    }
}

static I16 open_USB1901()
{
    I16 card, err;
    U16 card_num;
    U16 wModuleNum;
    USBDAQ_DEVICE AvailModules[MAX_USB_DEVICE];

    /* Card configuration. */
    U16 ConfigCtrl =
        P1902_AI_Differential|P1902_AI_CONVSRC_INT;         /* Gives reasonable numbers for diff channels 0 and 1 but not 2 and 3? */
        //P1902_AI_NonRef_SingEnded|P1902_AI_CONVSRC_INT;   /* Gives very strange numbers. */
        //P1902_AI_SingEnded|P1902_AI_CONVSRC_INT;          /* Works. Note: each end of the shunt is a channel. Leads to poor accuracy. */
    U16 TrigCtrl = P1902_AI_TRGMOD_POST|P1902_AI_TRGSRC_SOFT;
    U32 TriggerLvel = 0;  /* Ignore for P1902_AI_TRGSRC_SOFT */
    U32 ReTriggerCount = 0; /*Ignore in Double Buffer Mode*/
    U32 DelayCount = 0; /* Ignore for P1902_AI_TRGSRC_SOFT */
    U32 ScanIntrv = U1902_TIMEBASE/sample_rate; /* Interval in clock cycles between scans of the channels. 80Mhz/scan freq. */
    U32 SampIntrv = 128*320;  /* Interval in clock cycles between each A/D conversion. The UD-DASK manual claims that 320 is the only valid value for USB-1901. The USB-1901 manual says it is the _minimum_ value. */
    U32 AI_ReadCount = AI_COUNT; /*AI read count per one buffer*/

    U16 NumChans = no_channels; /*AI Channel Counts to be read*/
    U16 Chans[MAX_CHANNELS]; /*AI Channels array*/
    U16 AdRanges[MAX_CHANNELS]; /*AI Ranges array*/
    U32 i;

    /* Configure the channel order and per-channel range. */
    for (i=0; i < NumChans; i++) {
        Chans[i] = channel[i].id;
        AdRanges[i] = channel[i].AdRange;
    }

    printf("Configuring USB-1901 to perform analog data acquisition from %d channels\n", no_channels);
    printf("at %6.3lf Hz scan rate in double buffer mode.\n\n", (double)U1902_TIMEBASE/(double)ScanIntrv);

    /* Find all devices. */
    err = UD_Device_Scan(&wModuleNum, AvailModules);
    if (err < 0) {
        fprintf(stderr, "UD_Device_Scan Error: %d\n", err);
        exit(1);
    }

    card_num = INVALID_CARD_ID;

    /* Pick the first available device of the right type. */
    for (i = 0; i < wModuleNum; i++) {
        if (AvailModules[i].wModuleType == USB_1901) {
            card_num = AvailModules[i].wCardID;
            break;
        }
    }

    if (card_num == INVALID_CARD_ID) {
        fprintf(stderr, "No active USB_1901 USB device\n");
        exit(2);
    }

    /* Register/open the device. */
    card = UD_Register_Card(USB_1901, card_num);
    if (card < 0) {
        fprintf(stderr, "UD_Register_Card Error: %d\n", card);
        exit(3);
    }

    /* Configure Analog Input */
    err = UD_AI_1902_Config(card, ConfigCtrl, TrigCtrl, TriggerLvel, ReTriggerCount, DelayCount);
    if(err < 0) {
        fprintf(stderr, "UD_AI_1902_Config Error: %d\n", err);
        exit(1);
    }

    /* Enable Double Buffer Mode */
    err = UD_AI_AsyncDblBufferMode(card, 1); // double-buffer mode
    if (err < 0) {
        fprintf(stderr, "UD_AI_AsyncDblBufferMode Error: %d\n", err);
        exit(1);
    }

    /* Set Scan and Sampling Rate */
    if (NumChans == 1) {
        SampIntrv = 320; /* With just one channel the minimum value should be safe. */
    }
    err = UD_AI_1902_CounterInterval(card, ScanIntrv, SampIntrv);
    if (err < 0) {
        fprintf(stderr, "UD_AI_1902_CounterInterval Error: %d\n", err);
        exit(1);
    }

    /* AI Acquisition Start */
    if (NumChans == 1) {
        err = UD_AI_ContReadChannel(card, Chans[0], AdRanges[0], NULL /* Not used for DB */, AI_ReadCount, 0/*Ignore*/, ASYNCH_OP);
        if (err < 0) {
            DWORD dwError = GetLastError();

            fprintf(stderr, "UD_AI_ContReadChannel Error: %d, GetLastError = %d\n", err, dwError );
            clean_exit(card, 1);
        }
    } else {
        err = UD_AI_ContReadMultiChannels(card, NumChans, Chans, AdRanges, NULL /* Not used for DB */, AI_ReadCount, 0/*Ignore*/, ASYNCH_OP);
        if (err < 0) {
            DWORD dwError = GetLastError();

            fprintf(stderr, "UD_AI_ContReadMultiChannels Error: %d, GetLastError = %d\n", err, dwError );
            clean_exit(card, 1);
        }
    }

    return card;
}

void clean_exit(U16 card, int code)
{
    UD_Release_Card(card);
    if (file) fclose(file);
    exit(code);
}
