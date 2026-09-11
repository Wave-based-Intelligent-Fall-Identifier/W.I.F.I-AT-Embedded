#include <math.h>
#include "gru_weights.h"

#define INPUT_DIM 12
#define UNITS     16
#define SEQ_LEN   71

static inline float sigmoidf(float x) { return 1.0f / (1.0f + expf(-x)); }

// one timestep update: h_prev (UNITS) + x_t (INPUT_DIM) -> h_new (UNITS)
static void gru_step(const float* x_t, float* h) {
    float x_z[UNITS], x_r[UNITS], x_h[UNITS];
    float r_z[UNITS], r_r[UNITS], r_h[UNITS];

    // x_t @ kernel  (+ input_bias)
    for (int j = 0; j < UNITS; j++) {
        float sz = b_input_z[j], sr = b_input_r[j], sh = b_input_h[j];
        for (int i = 0; i < INPUT_DIM; i++) {
            sz += x_t[i] * W_z[i * UNITS + j];
            sr += x_t[i] * W_r[i * UNITS + j];
            sh += x_t[i] * W_h[i * UNITS + j];
        }
        x_z[j] = sz; x_r[j] = sr; x_h[j] = sh;
    }

    // h @ recurrent_kernel (+ recurrent_bias)
    for (int j = 0; j < UNITS; j++) {
        float sz = b_rec_z[j], sr = b_rec_r[j], sh = b_rec_h[j];
        for (int i = 0; i < UNITS; i++) {
            sz += h[i] * U_z[i * UNITS + j];
            sr += h[i] * U_r[i * UNITS + j];
            sh += h[i] * U_h[i * UNITS + j];
        }
        r_z[j] = sz; r_r[j] = sr; r_h[j] = sh;
    }

    for (int j = 0; j < UNITS; j++) {
        float z = sigmoidf(x_z[j] + r_z[j]);
        float r = sigmoidf(x_r[j] + r_r[j]);
        float hh = tanhf(x_h[j] + r * r_h[j]);   // reset_after=True: r applied AFTER bias add
        h[j] = z * h[j] + (1.0f - z) * hh;
    }
}

// full forward pass: window[SEQ_LEN][INPUT_DIM] -> probability of "normal" (class 1)
float gru_predict(const float window[SEQ_LEN][INPUT_DIM]) {
    float h[UNITS] = { 0 };

    for (int t = 0; t < SEQ_LEN; t++) {
        gru_step(window[t], h);
    }

    // Dense(16, relu)
    float d1_out[16];
    for (int j = 0; j < 16; j++) {
        float s = dense1_b[j];
        for (int i = 0; i < UNITS; i++) s += h[i] * dense1_w[i * 16 + j];
        d1_out[j] = s > 0 ? s : 0.0f;   // relu
    }

    // Dense(1, sigmoid)
    float s = dense2_b[0];
    for (int i = 0; i < 16; i++) s += d1_out[i] * dense2_w[i];
    return sigmoidf(s);
}