#ifndef PID_H_
#define PID_H_
#include "config.h"
#include "motor_driver.h"
#include "encoder.h"
#include "commands.h"

#ifdef IDENTICAL_PID_GAINS 
    int Kp = KP;
    int Kd = KD;
    int Ki = KI;
    int Ko = KO;
#endif

const int PID_INTERVAL = 1000 / PID_RATE;

/* Track the next time we make a PID calculation */
unsigned long nextPID = 0;

/* PID setpoint info For a Motor */
typedef struct
{
    double TargetTicksPerFrame; // target speed in ticks per frame
    long Encoder;               // encoder count
    long PrevEnc;               // last encoder count
    int PrevInput;              // last input
    int ITerm;                  // integrated term
    long output;                // last motor setting
} SetPointInfo;

SetPointInfo leftPID, rightPID;
unsigned char moving = 0; // is the base in motion?

void resetPID()
{
    leftPID.TargetTicksPerFrame = 0.0;
    leftPID.Encoder = readEncoder(LEFT);
    leftPID.PrevEnc = leftPID.Encoder;
    leftPID.output = 0;
    leftPID.PrevInput = 0;
    leftPID.ITerm = 0;

    rightPID.TargetTicksPerFrame = 0.0;
    rightPID.Encoder = readEncoder(RIGHT);
    rightPID.PrevEnc = rightPID.Encoder;
    rightPID.output = 0;
    rightPID.PrevInput = 0;
    rightPID.ITerm = 0;
    nextPID = millis() + PID_INTERVAL;
}

/* PID routine to compute the next motor commands */
void doPID(SetPointInfo *p, int Kp, int Kd, int Ki, int Ko)
{
    long Perror;
    long output;
    int input;

    input = p->Encoder - p->PrevEnc;
    Perror = p->TargetTicksPerFrame - input;

    output = (Kp * Perror - Kd * (input - p->PrevInput) + p->ITerm) / Ko;
    p->PrevEnc = p->Encoder;

    output += p->output;

    if (output >= MAX_PWM)
        output = MAX_PWM;
    else if (output <= -MAX_PWM)
        output = -MAX_PWM;
    else
        p->ITerm += Ki * Perror;

    p->output = output;
    p->PrevInput = input;
}

/* Read the encoder values and call the PID routine */
void updatePID()
{
    leftPID.Encoder = readEncoder(LEFT);
    rightPID.Encoder = readEncoder(RIGHT);

    if (!moving)
    {
        if (leftPID.PrevInput != 0 || rightPID.PrevInput != 0)
            resetPID();
        return;
    }

    #ifdef INDIVIDUAL_PID_GAINS    // Use specific PID coefficients for each motor
        doPID(&rightPID, KP_RIGHT, KD_RIGHT, KI_RIGHT, KO_RIGHT);
        doPID(&leftPID, KP_LEFT, KD_LEFT, KI_LEFT, KO_LEFT);
    #endif

    #ifdef IDENTICAL_PID_GAINS // Use identical PID coefficients for each motor
        doPID(&rightPID, Kp, Kd, Ki, Ko);
        doPID(&leftPID, Kp, Kd, Ki, Ko);
    #endif

    setMotorSpeeds(leftPID.output, rightPID.output);
}

#endif
