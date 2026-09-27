#ifndef CONFIG_H_
#define CONFIG_H_

#define USE_BASE 

#define USE_GENERIC_MOTOR_DRIVER   // Motor drivers with 1 Direction Pin(INA) and 1 PWM(ENABLE) pin.

#define INDIVIDUAL_PID_GAINS     // Use specific PID coefficients for each motor
//#define IDENTICAL_PID_GAINS     // Use identical PID coefficients for each motor

/* Serial port baud rate */
#define BAUDRATE 115200
#define PID_RATE 30 // Hz
#define MAX_PWM 255


/* PID Parameters */
#ifdef INDIVIDUAL_PID_GAINS    
    #define KP_RIGHT 20
    #define KD_RIGHT 12
    #define KI_RIGHT 0
    #define KO_RIGHT 30

    #define KP_LEFT 18
    #define KD_LEFT 12
    #define KI_LEFT 0
    #define KO_LEFT 30
#endif

#ifdef IDENTICAL_PID_GAINS  
    #define KP 20
    #define KD 12
    #define KI 0
    #define KO 50
#endif


/* Encoder Interrupts */
#define encoder0PinA 32      // encoder b Left
#define encoder0PinB 35      // encoder a left

#define encoder1PinA 34     // encoder b right
#define encoder1PinB 33     // encoder a right


/* Motor Driver Interrupts */
#ifdef USE_GENERIC_MOTOR_DRIVER
    //#define RIGHT_MOTOR_ENA  14    // PWM
    #define RIGHT_MOTOR_IN1  14    // direction
    #define RIGHT_MOTOR_IN2  25    // direction

    //#define LEFT_MOTOR_ENB   27    // PWM
    #define LEFT_MOTOR_IN3   26    // direction  ← pick free GPIO
    #define LEFT_MOTOR_IN4   27    // direction  ← pick free GPIO
#endif

#endif
