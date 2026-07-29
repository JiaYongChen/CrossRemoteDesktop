---
name: feedback_flaky_latency_test
description: FrameTransmissionLatencyTest 在完整套件并行跑时偶发失败（负载致延迟超阈值），单独重跑即过——非回归
metadata: 
  node_type: memory
  type: feedback
  originSessionId: a63f58be-19a6-4f15-b49f-2ba7a550835e
---

`FrameTransmissionLatencyTest`（标签 latency/network/performance）在**完整测试套件并行运行时偶发失败**，单独重跑（`ctest -C Debug --rerun-failed`）即通过。

**Why:** 它是性能测试，断言帧传输延迟低于某阈值。完整套件里多测试并行 → CPU 负载升高 → 实测延迟超阈值而失败；单独跑负载低则通过。与功能改动无关（曾于 MessageType 重排后复现，重排仅改枚举值不影响延迟）。

**How to apply:**
- 全套件跑出**仅此一项**失败时，先 `ctest -C Debug --rerun-failed --output-on-failure` 单独重跑确认是否抖动，再下"回归"结论。
- 不要为消除它而放宽阈值或改功能代码——那是掩盖真实的性能敏感性问题。
- 开发机性能有限（如旧 GPU/多核负载敏感）时此抖动更易出现。

相关：[[feedback_build_verification]]
