#pragma once

#include <cstdint>

class GameTimer {
public:
    GameTimer();

    float TotalTime() const;
    float DeltaTime() const;

    void Reset();
    void Start();
    void Stop();
    void Tick();

private:
    double mSecondsPerCount = 0.0;
    double mDeltaTime = -1.0;

    std::int64_t mBaseTime = 0;
    std::int64_t mPausedTime = 0;
    std::int64_t mStopTime = 0;
    std::int64_t mPrevTime = 0;
    std::int64_t mCurrTime = 0;

    bool mStopped = false;
};
