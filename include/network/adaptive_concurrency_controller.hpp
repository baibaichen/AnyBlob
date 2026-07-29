#pragma once
#include "network/config.hpp"
#include <chrono>
#include <cstdint>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2026
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network {
//---------------------------------------------------------------------------
class TaskedSendReceiverGroup;
//---------------------------------------------------------------------------
/// Advisory controller that scales the requests per thread first and then the threads to saturate the bandwidth target with minimal cores
/// The caller owns the threads and follows the recommendation while the controller applies the requests knob directly on the group
class AdaptiveConcurrencyController {
    public:
    /// Control epoch length
    static constexpr std::chrono::milliseconds epochLength{1000};
    /// Relative throughput band that separates a real change from noise
    static constexpr double tolerance = 0.05;
    /// Fraction of a reference throughput that counts as attained
    static constexpr double attainment = 1 - 2 * tolerance;
    /// Step size for the requests knob
    static constexpr unsigned requestStep = 2;
    /// Epochs between downward probes while holding
    static constexpr unsigned probeInterval = 10;

    /// The advisory output
    struct Recommendation {
        /// Threads the caller should run
        unsigned threads;
        /// Concurrent requests per thread that are already applied to the group
        unsigned requestsPerThread;

        /// Comparison
        bool operator==(const Recommendation& other) const = default;
    };

    /// One epoch measurement that is public such that tests can feed synthetic samples
    struct Sample {
        /// Bytes finished during the epoch
        uint64_t transferredBytes;
        /// Wall time of the epoch
        std::chrono::nanoseconds elapsed;
        /// Group submission queue depth at epoch end
        uint64_t queuedMessages;
    };

    /// Constructor that seeds from the config and hill climbs on the throughput gradient if the bandwidth is unknown
    AdaptiveConcurrencyController(TaskedSendReceiverGroup& group, const Config& config);

    /// Caller driven tick that limits itself to one control decision per epoch
    [[nodiscard]] Recommendation recommend();
    /// Get the current recommendation without measuring
    [[nodiscard]] Recommendation current() const { return _current; }
    /// Get the maximum number of threads the controller recommends
    [[nodiscard]] unsigned maxThreads() const { return _maxThreads; }

    /// Deterministic control law that processes one epoch sample and is used directly by tests
    Recommendation update(const Sample& sample);

    private:
    /// The control phase
    enum class Phase : uint8_t {
        ScaleRequests,
        ScaleThreads,
        Hold,
        Idle
    };

    /// The group whose requests knob is controlled
    TaskedSendReceiverGroup& _group;
    /// The target in bytes per second where zero means hill climbing
    double _targetBytesPerSec;
    /// The upper bound for the requests knob
    unsigned _maxRequests;
    /// The upper bound for the threads knob
    unsigned _maxThreads;
    /// The current recommendation
    Recommendation _current;
    /// The configuration before the last unjudged step
    Recommendation _previous;
    /// The configuration to restore when demand returns after idling
    Recommendation _bestKnown;
    /// The throughput at the best known configuration
    double _bestBps = 0;
    /// The throughput of the previous epoch
    double _lastBps = 0;
    /// The control phase
    Phase _phase = Phase::ScaleRequests;
    /// Epochs since the last downward probe
    unsigned _sinceProbe = 0;
    /// Start of the current measurement epoch
    std::chrono::steady_clock::time_point _lastEpochStart;
    /// Transferred bytes at epoch start
    uint64_t _lastBytes;

    /// Set the requests knob on the group and the recommendation
    void applyRequests(unsigned requests);
};
//---------------------------------------------------------------------------
} // namespace anyblob::network
