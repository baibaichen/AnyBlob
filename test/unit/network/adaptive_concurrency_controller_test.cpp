#include "catch2/single_include/catch2/catch.hpp"
#include "network/adaptive_concurrency_controller.hpp"
#include "network/tasked_send_receiver.hpp"
#include <chrono>
#include <thread>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2026
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network::test {
//---------------------------------------------------------------------------
using namespace std::chrono_literals;
using Controller = AdaptiveConcurrencyController;
//---------------------------------------------------------------------------
namespace {
//---------------------------------------------------------------------------
/// Builds a sample worth one second with the given throughput in bytes/s
Controller::Sample sampleBps(uint64_t bytesPerSec, uint64_t queued = 1000) {
    return {bytesPerSec, std::chrono::nanoseconds(1s), queued};
}
//---------------------------------------------------------------------------
/// Feed identical samples for a number of epochs
Controller::Recommendation feedEpochs(Controller& controller, uint64_t bytesPerSec, unsigned epochs, uint64_t queued = 1000) {
    Controller::Recommendation rec = controller.current();
    for (auto i = 0u; i < epochs; i++)
        rec = controller.update(sampleBps(bytesPerSec, queued));
    return rec;
}
//---------------------------------------------------------------------------
} // namespace
//---------------------------------------------------------------------------
TEST_CASE("adaptive_concurrency_controller_seeding") {
    TaskedSendReceiverGroup group;
    // Emulates a c5n.18xlarge with 100 Gbit network and the static model of 8000 Mbit per core
    Config config = {Config::defaultCoreThroughput, Config::defaultCoreConcurrency, 100000};
    Controller controller(group, config);
    auto rec = controller.current();
    auto expectedThreads = std::min(13u, std::max(1u, std::thread::hardware_concurrency()));
    REQUIRE(rec.threads == expectedThreads);
    REQUIRE(rec.requestsPerThread == 20);
    // The controller applies the requests knob directly on the group
    REQUIRE(group.getConcurrentRequests() == 20);

    // Unknown bandwidth seeds small
    Config unknownConfig = {Config::defaultCoreThroughput, Config::defaultCoreConcurrency, 0};
    Controller gradientController(group, unknownConfig);
    REQUIRE(gradientController.current().threads <= std::max(1u, std::thread::hardware_concurrency()));
    REQUIRE(gradientController.current().threads >= 1);
    REQUIRE(gradientController.current().requestsPerThread == 20);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_concurrency_controller_requests_first") {
    TaskedSendReceiverGroup group;
    // Uses a small 16 Gbit target with 2 seed threads and 4 max threads to be independent of the test machine
    Config config = {Config::defaultCoreThroughput, Config::defaultCoreConcurrency, 16000};
    Controller controller(group, config);
    REQUIRE(controller.current().threads == 2);
    REQUIRE(controller.current().requestsPerThread == 20);

    // Throughput grows with every step but stays below the target such that the requests knob must be exhausted before threads move
    uint64_t bps = 100'000'000;
    auto rec = controller.current();
    while (rec.requestsPerThread < group.maxConcurrentRequests()) {
        auto prev = rec;
        bps += bps / 2; // Every step gains 50 percent and is kept
        rec = controller.update(sampleBps(bps));
        REQUIRE(rec.threads == prev.threads);
        REQUIRE(rec.requestsPerThread >= prev.requestsPerThread);
    }
    REQUIRE(rec.requestsPerThread == 32);
    REQUIRE(group.getConcurrentRequests() == 32);

    // Now the requests knob is at its bound: the next steps add threads
    bps += bps / 2;
    rec = controller.update(sampleBps(bps));
    REQUIRE(rec.threads == 3);
    REQUIRE(rec.requestsPerThread == 32);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_concurrency_controller_marginal_gain_revert") {
    TaskedSendReceiverGroup group;
    Config config = {Config::defaultCoreThroughput, Config::defaultCoreConcurrency, 16000};
    Controller controller(group, config);

    // First epoch below target: controller steps the requests knob up
    auto rec = controller.update(sampleBps(100'000'000));
    REQUIRE(rec.requestsPerThread == 22);
    // Flat throughput afterwards: the step is reverted
    rec = controller.update(sampleBps(100'000'000));
    REQUIRE(rec.requestsPerThread == 20);
    REQUIRE(group.getConcurrentRequests() == 20);
    // The next upscale step must use the other knob (threads)
    rec = controller.update(sampleBps(100'000'000));
    REQUIRE(rec.threads == 3);
    REQUIRE(rec.requestsPerThread == 20);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_concurrency_controller_probe_down") {
    TaskedSendReceiverGroup group;
    Config config = {Config::defaultCoreThroughput, Config::defaultCoreConcurrency, 16000};
    Controller controller(group, config);
    auto seed = controller.current();

    // Meets the target and holds until the first probe epoch
    uint64_t targetBps = 2'000'000'000;
    auto rec = feedEpochs(controller, targetBps, Controller::probeInterval - 1);
    REQUIRE(rec == seed);

    // The probe removes a thread first to minimize cores
    rec = feedEpochs(controller, targetBps, 1);
    REQUIRE(rec.threads == seed.threads - 1);

    // The probe holds the target and the smaller configuration is kept
    rec = feedEpochs(controller, targetBps, 1);
    REQUIRE(rec.threads == seed.threads - 1);
    REQUIRE(rec.requestsPerThread == seed.requestsPerThread);

    // The next probe reduces the requests knob since threads are already at one
    rec = feedEpochs(controller, targetBps, Controller::probeInterval - 1);
    REQUIRE(rec.threads == 1);
    REQUIRE(rec.requestsPerThread == seed.requestsPerThread - Controller::requestStep);

    // The probe lost the target and is reverted
    rec = feedEpochs(controller, targetBps / 2, 1);
    REQUIRE(rec.requestsPerThread == seed.requestsPerThread);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_concurrency_controller_idle_collapse_and_reattach") {
    TaskedSendReceiverGroup group;
    Config config = {Config::defaultCoreThroughput, Config::defaultCoreConcurrency, 16000};
    Controller controller(group, config);
    auto seed = controller.current();

    // Grow one kept step so the working configuration differs from the seed
    auto rec = controller.update(sampleBps(100'000'000));
    rec = controller.update(sampleBps(200'000'000));
    auto grown = rec;
    REQUIRE(grown.requestsPerThread > seed.requestsPerThread);

    // An idle epoch releases the threads while the requests knob stays untouched
    rec = feedEpochs(controller, 0, 1, 0);
    REQUIRE(rec.threads == 1);
    REQUIRE(rec.requestsPerThread == grown.requestsPerThread);

    // Returning demand reattaches to the last working configuration
    rec = controller.update(sampleBps(200'000'000));
    REQUIRE(rec == grown);
    REQUIRE(group.getConcurrentRequests() == grown.requestsPerThread);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_concurrency_controller_gradient_mode") {
    TaskedSendReceiverGroup group;
    // Unknown bandwidth uses pure hill climbing
    Config config = {Config::defaultCoreThroughput, Config::defaultCoreConcurrency, 0};
    Controller controller(group, config);

    // Simulates a plateau where throughput scales with the requests knob up to 26 and extra threads never help
    auto plateauBps = [](const Controller::Recommendation& rec) -> uint64_t {
        return 10'000'000ull * std::min(rec.requestsPerThread, 26u);
    };
    auto rec = controller.current();
    for (auto i = 0u; i < 2000; i++) {
        rec = controller.update(sampleBps(plateauBps(rec)));
        // Climbing must stop at the plateau since the overshoot step gets reverted
        REQUIRE(rec.requestsPerThread <= 28);
    }
    // The downward probes release all unhelpful threads and the requests knob converges to the plateau
    REQUIRE(rec.threads == 1);
    REQUIRE(rec.requestsPerThread >= 24);
}
//---------------------------------------------------------------------------
TEST_CASE("adaptive_concurrency_controller_clamping") {
    TaskedSendReceiverGroup group;
    REQUIRE(group.maxConcurrentRequests() == 32);
    group.setConcurrentRequests(1000);
    REQUIRE(group.getConcurrentRequests() == 32);
    group.setConcurrentRequests(10);
    REQUIRE(group.getConcurrentRequests() == 10);

    // The transferred bytes counter starts at zero
    REQUIRE(group.getTransferredBytes() == 0);
    REQUIRE(group.getQueuedMessages() == 0);
}
//---------------------------------------------------------------------------
} // namespace anyblob::network::test
