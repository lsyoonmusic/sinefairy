# Sine Fairy — by ASIMORPH's Fellas

실시간 기음($f_0$) 추적 다이내믹 로우컷 + **제로 레이턴시 직교 락인(Quadrature Lock-In) 저역 배음 사인파 재구성** 오디오 이펙터 VST3입니다.

---

## 핵심 DSP 아키텍처

1. **실시간 YIN 기음 추적기 (`Source/DSP/PitchTrackerYIN.h`)**
   - 12 kHz 데시메이션 모노 사이드체인 위에서 YIN(CMNDF) + 포물선 보간 + 서브옥타브 오인식 방지(Goertzel 톤 에너지 비교) + 3프레임 미디언 필터로 노이즈가 낀 보컬/악기에서도 안정적으로 $f_0$를 추적합니다.
   - 무성음(치찰음/파열음/숨소리)이나 묵음 구간에서는 Voicing Confidence가 부드럽게 `0`으로 내려가 자음 손상을 막고 로우컷을 안전하게 `FLOOR` 주파수에 안착시킵니다.
2. **피치 트래킹 로우컷 (`Source/DSP/TrackingLowCut.h`)**
   - 급격한 실시간 컷오프 변조에도 클릭·발진이 없는 **Cytomic TPT SVF** 직렬 Butterworth 하이패스(`OFF / 12 / 24 / 36 / 48 dB/oct`).
   - `OFFSET (-12..0 st)`으로 기음 바로 아래 안전 간격을 두고 따라다니며 서브 럼블과 험을 제거합니다.
3. **위상 동기 저역 배음 사인파 재구성 (`Source/DSP/HarmonicResynth.h`)**
   - 저역 해상도가 떨어지고 긴 레이턴시를 유발하는 FFT 페이즈 보코더 대신, **동적 LR4 크로스오버**($(N + 0.5)f_0$) + **복소 직교 복조(Quadrature Lock-In) 뱅크**를 사용합니다.
   - 1~6배음($H_1 \dots H_N$)의 실시간 진폭 엔벨로프와 **위상까지 100% 원음에 락인**된 순수 사인파를 생성하며, 크로스오버 경계의 $(N+1)$배음 가드까지 보정해 원음과 섞어도 콤 필터링이나 크로스오버 딥이 생기지 않습니다.
   - **`CLEAN (Replace)` 모드:** $(N+0.5)f_0$ 이하 대역의 노이즈 낀 저역을 순수 재구성 배음 사인파로 크로스페이드 교체해 **배음 사이사이의 노이즈·머드만 `-25 ~ -34 dB` 제거**하고, 그 위 고역(자음·에어감)은 손대지 않고 그대로 통과시킵니다.
   - **`ENHANCE (Parallel)` 모드:** 원음에 위상 일치 순수 저역 배음을 병렬로 보강합니다.
   - **`SOLO SINES` / `DELTA (NOISE)`:** 추출된 순수 사인파만 듣거나, 제거된 기음 아래 럼블 + 배음 사이 노이즈만 정확히 모니터링할 수 있습니다.

---

## 설치 방법

GitHub Actions 빌드 완료 후 `SineFairy-Windows-VST3` 아티팩트 안의 `Sine Fairy.vst3` 폴더를 아래 경로에 복사하고 DAW에서 리스캔하세요.

```text
C:\Program Files\Common Files\VST3\
```
