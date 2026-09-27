#include <motor_driver.h>

void robot_setup() {
  // Direction pins
  ledcAttach(RIGHT_MOTOR_IN1, freq, lresolution);
  ledcAttach(RIGHT_MOTOR_IN2, freq, lresolution);
  ledcAttach(LEFT_MOTOR_IN3,  freq, lresolution);
  ledcAttach(LEFT_MOTOR_IN4,  freq, lresolution);

  ledcWrite(RIGHT_MOTOR_IN1, 0);
  ledcWrite(RIGHT_MOTOR_IN2, 0);
  ledcWrite(LEFT_MOTOR_IN3,  0);
  ledcWrite(LEFT_MOTOR_IN4,  0);
}

void setMotorSpeed(int i, int spd) {
  unsigned char reverse = 0;

  if (spd < 0) {
    spd = -spd;
    reverse = 1;
  }
  if (spd > 255) spd = 255;

  if (i == LEFT) {
  if (reverse == 0) { ledcWrite(LEFT_MOTOR_IN3, spd); ledcWrite(LEFT_MOTOR_IN4, 0); }
  else              { ledcWrite(LEFT_MOTOR_IN3, 0);   ledcWrite(LEFT_MOTOR_IN4, spd); }
} else {
  if (reverse == 0) { ledcWrite(RIGHT_MOTOR_IN1, spd); ledcWrite(RIGHT_MOTOR_IN2, 0); }
  else              { ledcWrite(RIGHT_MOTOR_IN1, 0);   ledcWrite(RIGHT_MOTOR_IN2, spd); }
}
}

void setMotorSpeeds(int leftSpeed, int rightSpeed) {
  setMotorSpeed(LEFT,  leftSpeed);
  setMotorSpeed(RIGHT, rightSpeed);
}