#ifndef ENCODERS_H_
#define ENCODERS_H_

#include <Arduino.h>
#include <config.h>
#include "commands.h"

// ************** encoder 1 *********************

void doEncoderA();

void doEncoderB();

// ************** encoder 2 *********************

void doEncoderC();

void doEncoderD();


void init_encoders();
long readEncoder(int i);
void resetEncoder(int i);
void resetEncoders();


#endif   


