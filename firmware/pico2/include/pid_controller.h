/*
 * pid_controller.h
 *
 * Created on: Mar 10, 2026
 * Author: Gemini Code Assist
 */

#ifndef INC_PID_CONTROLLER_H_
#define INC_PID_CONTROLLER_H_

#include <stdbool.h>
#include <stdint.h>

#define PID_DERIVATIVE_FILTER_TAU_SECONDS 0.1f

/**
 * @brief PID_Controller structure for a generic PID control loop.
 */
typedef struct {
    float Kp;               // Proportional gain
    float Ki;               // Integral gain
    float Kd;               // Derivative gain
    float integral;         // Integral accumulator
    float prev_error;       // Previous error for derivative calculation
    float prev_process_variable; // Previous measurement for derivative-on-measurement
    float derivative_filter; // Low-pass filtered measurement derivative
    float output_limit_min; // Minimum output value
    float output_limit_max; // Maximum output value
    float integral_limit_min; // Minimum integral value (anti-windup)
    float integral_limit_max; // Maximum integral value (anti-windup)
    bool derivative_initialized; // First-sample guard for derivative state
} PID_Controller;

/**
 * @brief Initializes the PID controller parameters.
 * @param pid Pointer to the PID_Controller structure.
 * @param Kp Proportional gain.
 * @param Ki Integral gain.
 * @param Kd Derivative gain.
 * @param output_min Minimum allowed output value.
 * @param output_max Maximum allowed output value.
 * @param integral_min Minimum allowed integral accumulator value (anti-windup).
 * @param integral_max Maximum allowed integral accumulator value (anti-windup).
 */
void pid_init(PID_Controller* pid, float Kp, float Ki, float Kd, float output_min, float output_max, float integral_min, float integral_max);

/**
 * @brief Calculates the PID output based on the current error.
 * @param pid Pointer to the PID_Controller structure.
 * @param setpoint The desired target value.
 * @param process_variable The current measured value.
 * @param dt Time elapsed since the last calculation in seconds.
 * @return The calculated PID output.
 */
float pid_calculate(PID_Controller* pid, float setpoint, float process_variable, float dt, float feedforward);

void pid_reset(PID_Controller* pid);

#endif /* INC_PID_CONTROLLER_H_ */
