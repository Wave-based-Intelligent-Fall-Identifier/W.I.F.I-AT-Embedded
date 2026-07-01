/*
 * 보드 없는 검증 #3 — 이식형 호스트 C 테스트.
 *
 * 친구의 gru_functions.c 를 '그대로' 컴파일/실행해서 검증한다(재구현 아님).
 * 네이티브 gcc/clang 이 있으면 build_check.sh 가 빌드+실행한다.
 *
 * 검사: 출력범위(0,1)+유한, 결정성, 민감도(정적≠동적 윈도).
 * gru_functions.c 를 직접 include → 단일 TU (gru_weights.h const 중복정의 회피).
 */
#include <stdio.h>
#include <math.h>
#include "gru_functions.c"

static unsigned s = 12345u;
static float rnd(void) { s = 1103515245u * s + 12345u; return (float)((s >> 8) & 0xffff) / 65535.0f; }

static void fill(float w[GRU_SEQ_LEN][GRU_INPUT_DIM], int kind) {
    float base[GRU_INPUT_DIM];
    for (int i = 0; i < GRU_INPUT_DIM; i++) base[i] = 10.0f + 2.0f * rnd();
    for (int t = 0; t < GRU_SEQ_LEN; t++)
        for (int i = 0; i < GRU_INPUT_DIM; i++) {
            if (kind == 0)      w[t][i] = 0.0f;                 /* zero   */
            else if (kind == 1) w[t][i] = base[i] + 0.01f*rnd();/* static */
            else                w[t][i] = 5.0f + 20.0f*rnd();   /* dynamic*/
        }
}

int main(void) {
    static float w[GRU_SEQ_LEN][GRU_INPUT_DIM];
    int fails = 0;

    fill(w, 0); float o_zero = gru_predict(w);
    fill(w, 1); float o_stat = gru_predict(w);
    fill(w, 2); float o_dyn  = gru_predict(w);

    /* 1) 범위 + 유한 */
    float outs[3] = { o_zero, o_stat, o_dyn };
    for (int k = 0; k < 3; k++)
        if (!(outs[k] > 0.0f && outs[k] < 1.0f) || isnan(outs[k]) || isinf(outs[k])) fails++;
    printf("[1] range(0,1): %s  zero=%.4f static=%.4f dynamic=%.4f\n",
           fails ? "FAIL" : "PASS", o_zero, o_stat, o_dyn);

    /* 2) 결정성 */
    s = 42u; fill(w, 2); float a = gru_predict(w);
    s = 42u; fill(w, 2); float b = gru_predict(w);
    int det = (a == b); if (!det) fails++;
    printf("[2] determinism: %s (%.6f==%.6f)\n", det ? "PASS" : "FAIL", a, b);

    /* 3) 민감도 */
    int sens = fabsf(o_stat - o_dyn) > 1e-4f; if (!sens) fails++;
    printf("[3] sensitivity: %s |d|=%.4f\n", sens ? "PASS" : "FAIL", fabsf(o_stat - o_dyn));

    printf("----\n%s\n", fails ? "결과: FAIL" : "결과: PASS (gru_functions.c 실행 검증)");
    return fails ? 1 : 0;
}
