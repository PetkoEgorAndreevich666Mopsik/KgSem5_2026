#include "GameTimer.h"

#include <Windows.h>

GameTimer::GameTimer() {
    std::int64_t countsPerSec;
    QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(&countsPerSec));
    mSecondsPerCount = 1.0 / static_cast<double>(countsPerSec);
}

float GameTimer::TotalTime() const {
    if (mStopped) {
        return static_cast<float>(((mStopTime - mPausedTime) - mBaseTime) * mSecondsPerCount);
    }
    return static_cast<float>(((mCurrTime - mPausedTime) - mBaseTime) * mSecondsPerCount);
}

float GameTimer::DeltaTime() const {
    return static_cast<float>(mDeltaTime);
}

void GameTimer::Reset() {
    std::int64_t curr;
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&curr));

    mBaseTime = curr;
    mPrevTime = curr;
    mStopTime = 0;
    mStopped = false;
    mPausedTime = 0;
}

void GameTimer::Start() {
    std::int64_t startTime;
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&startTime));

    if (mStopped) {
        mPausedTime += (startTime - mStopTime);
        mPrevTime = startTime;
        mStopTime = 0;
        mStopped = false;
    }
}

void GameTimer::Stop() {
    if (!mStopped) {
        std::int64_t curr;
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&curr));

        mStopTime = curr;
        mStopped = true;
    }
}

void GameTimer::Tick() {
    if (mStopped) {
        mDeltaTime = 0.0;
        return;
    }

    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&mCurrTime));
    mDeltaTime = (mCurrTime - mPrevTime) * mSecondsPerCount;
    mPrevTime = mCurrTime;

    if (mDeltaTime < 0.0) {
        mDeltaTime = 0.0;
    }
}
