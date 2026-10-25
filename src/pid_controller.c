/*
 * pid_controller.c
 *
 * Created on: Mar 10, 2026
 * Author: Gemini Code Assist
 */

#include "pid_controller.h"
#include <math.h>
/* Ported from laphtec/Laser-Controller; fail closed on invalid arithmetic. */

void pid_init(PID_Controller* pid, float Kp, float Ki, float Kd, float output_min, float output_max, float integral_min, float integral_max) {
    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;
    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->prev_process_variable = 0.0f;
    pid->derivative_filter = 0.0f;
    pid->output_limit_min = output_min;
    pid->output_limit_max = output_max;
    pid->integral_limit_min = integral_min;
    pid->integral_limit_max = integral_max;
    pid->derivative_initialized = false;
}

float pid_calculate(PID_Controller* pid, float setpoint, float process_variable, float dt, float feedforward) {
    if (!isfinite(dt) || dt <= 0.0f || !isfinite(setpoint) ||
        !isfinite(process_variable) || !isfinite(feedforward) ||
        !isfinite(pid->Kp) || !isfinite(pid->Ki) || !isfinite(pid->Kd)) {
        pid_reset(pid);
        return NAN;
    }

    float error = setpoint - process_variable;

    // Proportional term
    float proportional = pid->Kp * error;

    // Integral term with anti-windup
    pid->integral += pid->Ki * error * dt;

    if (pid->integral > pid->integral_limit_max) {
        pid->integral = pid->integral_limit_max;
    } else if (pid->integral < pid->integral_limit_min) {
        pid->integral = pid->integral_limit_min;
    }

    float integral = pid->integral;

    // Derivative-on-measurement avoids setpoint kick; low-pass filtering reduces sample noise.
    float raw_derivative = 0.0f;
    if (pid->derivative_initialized) {
        raw_derivative = -1.0f * (process_variable - pid->prev_process_variable) / dt;
        float alpha = dt / (PID_DERIVATIVE_FILTER_TAU_SECONDS + dt);
        pid->derivative_filter += alpha * (raw_derivative - pid->derivative_filter);
    } else {
        pid->derivative_filter = 0.0f;
        pid->derivative_initialized = true;
    }

    float derivative = pid->Kd * pid->derivative_filter;
    pid->prev_error = error;
    pid->prev_process_variable = process_variable;

    float output = proportional + integral + derivative + feedforward;

    if (!isfinite(output)) { pid_reset(pid); return NAN; }
    // Clamp output
    return (output > pid->output_limit_max) ? pid->output_limit_max : ((output < pid->output_limit_min) ? pid->output_limit_min : output);
}

void pid_reset(PID_Controller* pid) {
    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->prev_process_variable = 0.0f;
    pid->derivative_filter = 0.0f;
    pid->derivative_initialized = false;
}
