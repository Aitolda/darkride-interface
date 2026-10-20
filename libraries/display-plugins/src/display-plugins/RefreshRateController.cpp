//
//  RefreshRateController.cpp
//  libraries/display-pluging/src/display-plugin
//
//  Created by Dante Ruiz on 2019-04-15.
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "RefreshRateController.h"

#include <QtCore/QThread>
#include <thread>
#include <NumericalConstants.h>

#include "DisplayLink.h"

long int hzToDurationNanoseconds(int refreshRate) {
    return (int64_t) (NSECS_PER_SECOND / (quint64) refreshRate);
}

int durationNanosecondsToHz(int64_t refreshRateLimitPeriod) {
    return (int) (NSECS_PER_SECOND / (quint64) refreshRateLimitPeriod);
}

void RefreshRateController::setRefreshRateLimitPeriod(int refreshRateLimit) {
    _refreshRateLimitPeriod = hzToDurationNanoseconds(refreshRateLimit);
}

int RefreshRateController::getRefreshRateLimitPeriod() const {
    return durationNanosecondsToHz(_refreshRateLimitPeriod);
}

float RefreshRateController::getPacedPeriodSeconds() const {
    // a period under a millisecond is none set yet (the default), one over 50 ms a window in the background
    const int64_t MIN_PERIOD_NS = 1000000;
    const int64_t MAX_PERIOD_NS = 50000000;
    int64_t period = _pacedPeriodNs > 0 ? _pacedPeriodNs.load() : _refreshRateLimitPeriod.load();
    return (period >= MIN_PERIOD_NS && period <= MAX_PERIOD_NS) ? (float)period * 1.0e-9f : 0.0f;
}

std::chrono::nanoseconds RefreshRateController::sleepThreadIfNeeded(QThread* thread, bool isHmd, bool vsync) {
    using namespace std::chrono;
    if (isHmd) {
        _scheduled = false;
        _slotIndex++;
        return nanoseconds(0);
    }
    // With vsync the swap waits for the display's refresh: a sleep on top of it only beats against the refreshes.
    // The display-based profile, whose target is the display's own rate, sleeps not at all then; the throttled
    // profiles keep their pacing.
    if (vsync && _displayRefreshRate > 0 && getRefreshRateLimitPeriod() >= _displayRefreshRate) {
        _scheduled = false;
        _slotIndex++;
        return nanoseconds(0);
    }
    auto period = nanoseconds(_refreshRateLimitPeriod);
    auto now = steady_clock::now();
    int64_t nowNs = duration_cast<nanoseconds>(now.time_since_epoch()).count();
    int64_t refreshNs = 0;
    int64_t refreshPeriodNs = 0;
    bool atDisplayRate = _displayRefreshRate > 0 && getRefreshRateLimitPeriod() >= _displayRefreshRate;
    if (atDisplayRate && DisplayLink::instance().nextRefresh(nowNs, refreshNs, refreshPeriodNs)) {
        // In phase with the display: one slot per refresh interval, a fixed phase after the interval's refresh, so
        // the frame is flushed at the same point of every interval and the compositor shows it at the next refresh.
        // The phase, 2 ms: measured on 2026-10-20 (a 144 Hz panel, 6.94 ms intervals, a 120 Hz second display), the
        // flush waits 1.4 to 2.4 ms for the compositor from a millisecond before each refresh to a millisecond after
        // it, 0.8 ms at 1.5 to 2 ms in and 0.3 to 0.5 ms from 2.5 to 5.5 ms in, with a tail to 4 ms wherever it
        // starts; a present of a millisecond's work from 2 ms in returns by 4 ms as a rule and has 3.9 ms for the
        // flush before the next refresh, which covers the tail (from 3 ms in, 2.9 ms did not: 1.7 refreshes a second
        // were missed). TIVOLI_PRESENT_PHASE_US in the environment sets another; TIVOLI_PRESENT_PHASE_SWEEP=1 walks it through the whole interval,
        // 50 us a present, for a scripted test's map of the flush's wait and the frames' arrival against the phase
        // (seat-jitter.js, PHYSICS.md "The driver's stutter"). This interval's slot is taken when it is still ahead or
        // at most a little behind (a present that overran presents at once, and still flushes before the next
        // refresh); further behind, the next interval's, a frame lost as it must be.
        static const int64_t DEFAULT_PHASE_NS = 2000000;
        static const int64_t SWEEP_STEP_NS = 50000;
        static const int64_t LATE_TOLERANCE_NS = 1500000;
        static const int64_t phaseSetting = qgetenv("TIVOLI_PRESENT_PHASE_US").isEmpty() ? DEFAULT_PHASE_NS :
            (int64_t)qgetenv("TIVOLI_PRESENT_PHASE_US").toLongLong() * 1000;
        static const bool sweep = !qgetenv("TIVOLI_PRESENT_PHASE_SWEEP").isEmpty() && qgetenv("TIVOLI_PRESENT_PHASE_SWEEP") != "0";
        int64_t phase = sweep ? (int64_t)(_presentCount * SWEEP_STEP_NS) % refreshPeriodNs : phaseSetting % refreshPeriodNs;
        _presentCount++;
        int64_t slot = refreshNs - refreshPeriodNs + phase;   // this interval's (the refresh is the next one)
        if (slot <= _lastSlotNs + refreshPeriodNs / 2) {
            // this interval's slot was the last present's: one present an interval
            slot += refreshPeriodNs;
        }
        while (slot < nowNs - LATE_TOLERANCE_NS) {
            slot += refreshPeriodNs;
        }
        // the intervals this slot is on from the last (one as a rule; a lost refresh counts)
        int64_t advanced = _lastSlotNs > 0 ? (slot - _lastSlotNs + refreshPeriodNs / 2) / refreshPeriodNs : 1;
        _slotIndex += (uint64_t)std::max<int64_t>(1, advanced);
        _lastSlotNs = slot;
        _nextPresent = steady_clock::time_point(nanoseconds(slot));
        _scheduled = false;   // the free schedule restarts from now if the link goes quiet
        _pacedPeriodNs = refreshPeriodNs;
    } else {
        auto previous = _nextPresent;
        bool restarted = !_scheduled || period != _scheduledPeriod || now > _nextPresent + period;
        if (restarted) {
            // the schedule starts, or restarts after a change of rate or a stall of over a period, from now
            _scheduled = true;
            _scheduledPeriod = period;
            _nextPresent = now;
        }
        _nextPresent += period;
        int64_t advanced = restarted && previous.time_since_epoch().count() > 0 && _nextPresent > previous ?
            (duration_cast<nanoseconds>(_nextPresent - previous).count() + period.count() / 2) / period.count() : 1;
        _slotIndex += (uint64_t)std::max<int64_t>(1, std::min<int64_t>(advanced, 16));
        _pacedPeriodNs = period.count();
    }
    auto remaining = duration_cast<nanoseconds>(_nextPresent - now);
    if (remaining.count() > 0) {
        // the kernel's sleep overshoots by tens of microseconds: sleep to within a margin of the slot, spin the rest
        static const nanoseconds SPIN_MARGIN = microseconds(600);
        if (remaining > SPIN_MARGIN) {
            std::this_thread::sleep_for(remaining - SPIN_MARGIN);
        }
        while (steady_clock::now() < _nextPresent) {
            std::this_thread::yield();
        }
    }
    return remaining;
}
