#ifndef MOTOR_DRIVER_H_
#define MOTOR_DRIVER_H_

#include <Arduino.h>
#include <config.h>
#include "commands.h"

const int lresolution = 8;
const int freq        = 4000;

void robot_setup();
void setMotorSpeed(int i, int spd);
void setMotorSpeeds(int leftSpeed, int rightSpeed);

#endif