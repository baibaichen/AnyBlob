#include "network/adaptive_concurrency_controller.hpp"
#include "network/tasked_send_receiver.hpp"
#include <algorithm>
#include <thread>
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
using namespace std;
//---------------------------------------------------------------------------
AdaptiveConcurrencyController::AdaptiveConcurrencyController(TaskedSendReceiverGroup& group, const Config& config) : _group(group)
// Constructor that seeds the recommendation from the static model
{
    auto hardwareThreads = max(1u, thread::hardware_concurrency());
    _maxRequests = group.maxConcurrentRequests();
    if (config.bandwidth() > 0) {
        // Known bandwidth seeds the static model and allows two extra threads since the measured optimum on c5n.18xlarge was 14 instead of 13
        _targetBytesPerSec = static_cast<double>(config.bandwidth()) * 1000 * 1000 / 8;
        _current.threads = min(static_cast<unsigned>(config.retrievers()), hardwareThreads);
        _current.requestsPerThread = min(config.coreRequests(), _maxRequests);
        _maxThreads = min(static_cast<unsigned>(config.retrievers()) + 2, hardwareThreads);
    } else {
        // Unknown bandwidth starts small and hill climbs on the throughput gradient
        _targetBytesPerSec = 0;
        _current.threads = max(1u, hardwareThreads / 8);
        _current.requestsPerThread = min(static_cast<unsigned>(Config::defaultCoreConcurrency), _maxRequests);
        _maxThreads = hardwareThreads;
    }
    _previous = _bestKnown = _current;
    applyRequests(_current.requestsPerThread);
    _lastEpochStart = chrono::steady_clock::now();
    _lastBytes = group.getTransferredBytes();
}
//---------------------------------------------------------------------------
AdaptiveConcurrencyController::Recommendation AdaptiveConcurrencyController::recommend()
// Caller driven tick that limits itself to one control decision per epoch
{
    auto now = chrono::steady_clock::now();
    auto elapsed = now - _lastEpochStart;
    if (elapsed < epochLength)
        return _current;

    auto bytes = _group.getTransferredBytes();
    Sample sample = {bytes - _lastBytes, chrono::duration_cast<chrono::nanoseconds>(elapsed), _group.getQueuedMessages()};
    _lastEpochStart = now;
    _lastBytes = bytes;
    return update(sample);
}
//---------------------------------------------------------------------------
AdaptiveConcurrencyController::Recommendation AdaptiveConcurrencyController::update(const Sample& sample)
// The deterministic control law that processes one epoch sample
{
    auto seconds = chrono::duration<double>(sample.elapsed).count();
    auto bps = seconds > 0 ? static_cast<double>(sample.transferredBytes) / seconds : 0.0;
    auto targetMet = (_targetBytesPerSec > 0) && (bps >= attainment * _targetBytesPerSec);

    // Without demand release the threads since the requests knob is free on an empty queue
    if (!sample.transferredBytes && !sample.queuedMessages) {
        if (_phase != Phase::Idle) {
            _bestKnown = _current;
            _current.threads = 1;
            _previous = _current;
            _phase = Phase::Idle;
        }
        return _current;
    }

    // Returning demand reattaches to the last working configuration
    if (_phase == Phase::Idle) {
        _current = _bestKnown;
        _previous = _current;
        applyRequests(_current.requestsPerThread);
        _phase = Phase::Hold;
        _lastBps = bps;
        return _current;
    }

    // Judge the last step where an upscale must improve the throughput and a downward probe must not lose the target
    if (_current != _previous) {
        auto up = _current.threads + _current.requestsPerThread > _previous.threads + _previous.requestsPerThread;
        auto keep = up ? bps >= (1 + tolerance) * _lastBps
                       : targetMet || bps >= (1 - tolerance) * _lastBps;
        if (!keep) {
            // Undo the step where a failed upscale advances the phase and a failed probe stays on hold
            _current = _previous;
            applyRequests(_current.requestsPerThread);
            if (up)
                _phase = _phase == Phase::ScaleRequests ? Phase::ScaleThreads : Phase::Hold;
            _lastBps = bps;
            return _current;
        }
        _previous = _current;
        _bestKnown = _current;
        _bestBps = bps;
    }

    // Hold on attainment and resume climbing when the throughput drops off the best
    if (targetMet)
        _phase = Phase::Hold;
    else if (_phase == Phase::Hold && bps < attainment * _bestBps)
        _phase = Phase::ScaleRequests;

    switch (_phase) {
        case Phase::ScaleRequests:
            if (_current.requestsPerThread + requestStep <= _maxRequests) {
                applyRequests(_current.requestsPerThread + requestStep);
                break;
            }
            _phase = Phase::ScaleThreads;
            [[fallthrough]];
        case Phase::ScaleThreads:
            if (_current.threads < _maxThreads)
                _current.threads++;
            else
                _phase = Phase::Hold;
            break;
        case Phase::Hold:
            // Periodically probe downwards to free resources with threads first
            if (++_sinceProbe >= probeInterval) {
                _sinceProbe = 0;
                if (_current.threads > 1)
                    _current.threads--;
                else if (_current.requestsPerThread > requestStep)
                    applyRequests(_current.requestsPerThread - requestStep);
            }
            break;
        case Phase::Idle:
            break;
    }
    _lastBps = bps;
    return _current;
}
//---------------------------------------------------------------------------
void AdaptiveConcurrencyController::applyRequests(unsigned requests)
// Set the requests knob on the group and the recommendation
{
    _group.setConcurrentRequests(requests);
    _current.requestsPerThread = requests;
}
//---------------------------------------------------------------------------
} // namespace anyblob::network
