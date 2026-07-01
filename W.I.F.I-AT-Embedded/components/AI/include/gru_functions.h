#ifndef GRU_FUNCTIONS_H
#define GRU_FUNCTIONS_H

// GRU 낙상 판정 모델 입력 형상 (AI 담당자 export 기준: x_train shape (N, 71, 64))
#define GRU_SEQ_LEN   71   // 시퀀스 길이(프레임 수)
#define GRU_INPUT_DIM 64   // 프레임당 서브캐리어 수

/**
 * @brief 71x64 raw amplitude 윈도 -> 확률 1개.
 * @details gru_functions.c 구현. 반환값 의미(정상/낙상 극성)는 학습 라벨 인코딩에 따름.
 *          gru_functions.c 상단 주석 기준: "probability of normal (class 1)".
 * @return sigmoid 출력 [0,1]
 */
float gru_predict(const float window[GRU_SEQ_LEN][GRU_INPUT_DIM]);

#endif
