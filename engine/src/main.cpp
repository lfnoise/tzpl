// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

//
//  main.cpp
//  audio engine
//
//  Created by James McCartney on 2/2/21.
//

#include "tzpl_sexpr.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_test_plugins.hpp"
#include <thread>
#include <chrono>
#include <cassert>
#include <dlfcn.h>
#include <random>

using namespace engine;

void sleepf(double t) {
    std::this_thread::sleep_for(std::chrono::duration<double>(t));
}

void sleepSec(int t) {
    std::this_thread::sleep_for(std::chrono::seconds(t));
}

void loadDef_test()
{
    EngineConfig config;
    config.numSilos = 1;

    AudioStreamParameters asp{.deviceName = "default", .channels = 2, .firstChannel = 0, .bufferFrames = 256, .sampleRate = 48000.};
    auto e = newEngine(config, asp);

    loadDef(e, "/Users/jamesmcc/tzpl-build/dylib", "bubbles");
}

void test0()
{
    printf("--- test0 ---------------------------------\n");
    using namespace engine;

    EngineConfig config;
    config.numSilos = 1;

    AudioStreamParameters asp{.deviceName = "default", .channels = 2, .firstChannel = 0, .bufferFrames = 256, .sampleRate = 48000.};
    auto e = newEngine(config, asp);

    createSineNode(e);

    printf("start\n");
    startAudio(e);
    printf("running %d\n", isAudioRunning(e));

    sleepSec(1);

    printf("create graph with only a sine oscillator\n");
    f32 freq;
    f32 amp;

    begin(e, 0);
    newNode("sinosc", 101);
    freq = 240;
    setInput({101, 0}, 1, &freq);
    amp = 0.15;
    setInput({101, 1}, 1, &amp, .2);
    connect({101, 0}, {0, 0});
    go();

    sleepSec(8);


    printf("stop\n");
    stopAudio(e);

    printf("free engine\n");
    freeEngine(e);
}

void test1()
{
    printf("--- test1 ---------------------------------\n");
    using namespace engine;

    EngineConfig config;
    config.numSilos = 2;
    int siloA = 0;
    int siloB = 1;

    AudioStreamParameters asp{.deviceName = "default", .channels = 2, .firstChannel = 0, .bufferFrames = 256, .sampleRate = 96000.};

    auto e = newEngine(config, asp);
    printDevices(e);

    createAddOpNode(e);
    createMulOpNode(e);
    createSineNode(e);

    startAudio(e);
    printf("running %d\n", isAudioRunning(e));

    sleepSec(1);

    printf("create graph A\n");
    f32 freq;
    f32 amp;

    begin(e, siloA);
    newNode("sinosc", 101);
    newNode("sinosc", 102);
    newNode("+", 103);
    newNode("*", 203);
    freq = 240;
    setInput({101, 0}, 1, &freq);
    freq = 360.5;
    setInput({102, 0}, 1, &freq);
    connect({101, 0}, {103, 0});
    connect({102, 0}, {103, 1});
    connect({103, 0}, {0, 0});
    go();


    sleepSec(2);

    for (int h = 0; h < 3; ++h) {
        printf("replace 103 -> 203 w fade\n");
        begin(e, siloA);
        replaceNode(103, 203, 1.25);
        go();

        sleepSec(2);

        printf("replace 203 -> 103 w fade\n");
        begin(e, siloA);
        replaceNode(203, 103, 1.25);
        go();

        sleepSec(2);
    }

    begin(e, siloB);
    printf("create graph B\n");
    newNode("sinosc", 101);
    newNode("sinosc", 102);
    newNode("+", 103);
    freq = 480;
    setInput({101, 0}, 1, &freq);
    freq = 840;
    setInput({102, 0}, 1, &freq);
    amp = .125;
    setInput({101, 1}, 1, &amp);
    setInput({102, 1}, 1, &amp);
    connect({101, 0}, {103, 0});
    connect({102, 0}, {103, 1});
    connect({103, 0}, {0, 0});
    go();

    sleepSec(1);

    printf("attempt to change a connection.\n");
    begin(e, siloB);
    newNode("sinosc", 104);
    freq = 400;
    setInput({104, 0}, 1, &freq);
    connect({104, 0}, {103, 0}, 0.1);
    go();

    sleepSec(1);
    printf("ok, set it back.\n");
    begin(e, siloB);
    connect({101, 0}, {103, 0}, 0.1);
    go();
    sleepf(.4);
    begin(e, siloB);
    disconnectNode(104);
    go();
    sleepSec(2);

    printf("slide freq to 600 in 8 seconds\n");
    freq = 600;
    begin(e, siloA);
    setInput({101, 0}, 1, &freq, 8);
    go();

    sleepSec(4);
    printf("attempt to start a new slide while the first is running\n");
    freq = 400;
    begin(e, siloA);
    setInput({101, 0}, 1, &freq, 8);
    go();
    sleepSec(4);
    printf("first slide should be done about now\n");
    sleepSec(4);
    printf("second slide should be done about now\n");

    sleepSec(3);

    printf("disconnect a sinosc\n");
    begin(e, siloA);
    disconnectInput({103, 1}, .5);
    go();

    sleepSec(3);

    printf("reconnect a sinosc\n");
    begin(e, siloA);
    connect({102, 0}, {103, 1}, .5);
    go();

    sleepSec(4);


    printf("tremolo\n");
    for (int i = 0; i < 16; ++i) {
        amp = (i&1) ? .2 : .02;
        begin(e, siloA);
        setInput({101, 1}, 1, &amp, .1, fadeSmoothstep);
        setInput({102, 1}, 1, &amp, .1, fadeSmoothstep);
        go();

        sleepf(.2);
    }
    sleepSec(2);

    printf("very loud (engage safety limiter).\n");
    amp = 20.;
    begin(e, siloA);
    setInput({101, 1}, 1, &amp, .04);
    go();

    sleepSec(1);

    printf("set amp .2\n");
    amp = .2;
    begin(e, siloA);
    setInput({101, 1}, 1, &amp, .04);
    go();

    sleepSec(1);

    printf("random amplitudes 0.1 .. 20.0\n");
    for (int i = 0; i < 8; ++i) {
        { static std::mt19937 rng{std::random_device{}()};
          amp = std::uniform_int_distribution<int>(0, 199)(rng) * .1f; }
        begin(e, siloA);
        setInput({101, 1}, 1, &amp, .04);
        go();

        sleepf(.1);
    }

    printf("set amp .2\n");
    amp = .2;
    begin(e, siloA);
    setInput({101, 1}, 1, &amp, .04);
    go();

    sleepSec(2);

    printf("set graph B amps silent\n");
    amp = 0.;
    begin(e, siloB);
    setInput({101, 1}, 1, &amp, 3., fadeEaseOutCubic);
    setInput({102, 1}, 1, &amp, 3., fadeEaseOutCubic);
    go();

    sleepSec(4);

    printf("set graph A amps silent\n");
    amp = 0.;
    begin(e, siloA);
    setInput({101, 1}, 1, &amp, 3., fadeEaseOutCubic);
    setInput({102, 1}, 1, &amp, 3., fadeEaseOutCubic);
    go();

    sleepSec(4);

    printf("free graph B\n");
    begin(e, siloB);
    freeAllNodes();
    go();

    sleepSec(1);

    printf("free graph A\n");
    begin(e, siloA);
    freeAllNodes();
      go();

    sleepSec(1);

    printf("stop\n");
    stopAudio(e);

    printf("free engine\n");
    freeEngine(e);
}

void test5()
{
    printf("--- test5 ---------------------------------\n");
    using namespace engine;

    EngineConfig config;
    config.numSilos = 1;

    AudioStreamParameters asp{.deviceName = "default", .channels = 2, .firstChannel = 0, .bufferFrames = 256, .sampleRate = 48000.};
    auto e = newEngine(config, asp);

    createAddOpNode(e);
    createSineNode(e);

    printf("start\n");
    startAudio(e);
    printf("running %d\n", isAudioRunning(e));

    sleepSec(1);

    printf("create graph A\n");
    f32 freq;
    f32 amp;

    begin(e, 0);
    newNode("sinosc", 101);
    newNode("+", 102);
    freq = 240;
    setInput({101, 0}, 1, &freq);
    amp = 0.15;
    setInput({101, 1}, 1, &amp, .2);
    connect({101, 0}, {102, 0});
    connect({102, 0}, {0, 0});
    go();

    sleepSec(1);

    printf("test fan out\n");
    for (int i = 0; i < 4; ++i) {
        begin(e, 0);
        connect({101, 0}, {102, 1}, 0.3); // test fan out.
        go();

        sleepf(.4);

        begin(e, 0);
        disconnectInput({102, 1}, 0.3); // test fan out.
        go();

        sleepf(.4);
    }

     sleepSec(2);

    printf("stop\n");
    stopAudio(e);

    printf("free engine\n");
    freeEngine(e);
}

void test2()
{
    printf("--- test2 ---------------------------------\n");
    using namespace engine;

    EngineConfig config;
    config.numSilos = 8;

    AudioStreamParameters asp{.deviceName = "default", .channels = 2, .firstChannel = 0, .bufferFrames = 256, .sampleRate = 96000.};
    auto e = newEngine(config, asp);

    createAddOpNode(e);
    createSineNode(e);

    printf("start\n");
    startAudio(e);
    printf("running %d\n", isAudioRunning(e));

    sleepSec(1);

    f32 freq;
    f32 amp;

    f32 latency = .02;
    f32 dt = .2;
    printf("start graphs on %d threads\n", config.numSilos);
    f64 t0 = getStreamTime(e);
    for (int i = 0; i < config.numSilos; ++i) {
        begin(e, i);
        newNode("sinosc", 101);
        freq = 240 + 60 * i;
        setInput({101, 0}, 1, &freq);
        amp = 0.0;
        setInput({101, 1}, 1, &amp);
        amp = 0.05;
        setInput({101, 1}, 1, &amp, .2, fadeEaseInCubic);
        connect({101, 0}, {0, 0});
        f64 t = t0 + latency + i * dt;
        sched(t);
    }

    sleepSec(5);

    printf("change freqs\n");
    t0 = getStreamTime(e);
    dt = .5;
    for (int i = 0; i < config.numSilos; ++i) {
        freq = 360 + 180 * i;
        begin(e, i);
        setInput({101, 0}, 1, &freq, .5, fadeExponential);
        f64 t = t0 + latency + i * dt;
        sched(t);
    }

    sleepSec(8);

    printf("reset freqs\n");
    t0 = getStreamTime(e);
    for (int i = 0; i < config.numSilos; ++i) {
        freq = 240 + 60 * i;
        begin(e, i);
        setInput({101, 0}, 1, &freq, .5, fadeExponential);
        f64 t = t0 + latency + i * dt;
        sched(t);
    }

    sleepSec(8);

    printf("stop\n");
    stopAudio(e);
    sleepSec(1);
    printf("start\n");
    startAudio(e);


    sleepSec(4);

    printf("set silent\n");
    amp = 0.;
    t0 = getStreamTime(e);
    dt = .4;
    for (int i = 0; i < config.numSilos; ++i) {
        begin(e, i);
        setInput({101, 1}, 1, &amp, .5, fadeEaseOutCubic);
        f64 t = t0 + latency + i * dt;
        sched(t);
    }

    sleepSec(6);

    printf("stop\n");
    stopAudio(e);

    printf("free engine\n");
    freeEngine(e);
}

void test3()
{
    printf("--- test3 ---------------------------------\n");
    using namespace engine;

    EngineConfig config;
    config.numSilos = 1;

    AudioStreamParameters asp{.deviceName = "default", .channels = 2, .firstChannel = 0, .bufferFrames = 256, .sampleRate = 96000.};
    auto e = newEngine(config, asp);

    createVoicerTestNode(e);

    printf("start\n");
    startAudio(e);
    printf("running %d\n", isAudioRunning(e));

    sleepSec(1);

    printf("create graph A\n");

    begin(e, 0);
        newNode("voicer", 101);
        connect({101, 0}, {0,0});
    go();

    sleepSec(1);

    const int numPitches = 6;
    f32 pitches[numPitches] = {60, 65, 67, 70, 74, 77};

    int noteID = 0;


    sleepSec(1);

    for (int k = 0; k < 20; ++k) {
        f64 dt = .1;
        f64 latency = .02;
        f64 t0 = getStreamTime(e);
        {
            f32 root = pitches[0] - 1 * k + -2;
            f32 veloc = .7;
            f32 drive = 4.7;
            f32 params[6] = { root, veloc, drive, -.3, .01, .2 };
            begin(e, 0);
            f64 t = t0 + latency;
            noteOn(101, noteID, 6, params);
            params[0] += 7;
            params[3] = .3;
            noteOn(101, noteID+1, 6, params);
            sched(t);

            begin(e, 0);
            noteOff(101, noteID);
            noteOff(101, noteID+1);
            t += numPitches * dt;
            sched(t);

            noteID+=2;
        }
        for (int i = 0; i < numPitches; ++i) {
            begin(e, 0);
            f32 pitch = pitches[i] - 1 * k + 10;
            f32 veloc = .5 + .04 * (numPitches - i - 1);
            f32 drive = 1. + .3 * k;
            f32 pan = -0.8 + (1.6 / (numPitches-1)) * i;
            f32 params[6] = { pitch, veloc, drive, pan, .01, .2 };
            noteOn(101, noteID, 6, params);

            f64 t = t0 + latency + i * dt;
            sched(t);

            t += .1 + .04 * k;
            begin(e, 0);
            noteOff(101, noteID);
            sched(t);
            ++noteID;
        }
        sleepf(.6);

        t0 = getStreamTime(e);
        {
            f32 root = pitches[0] - 1 * k + -4;
            f32 veloc = .7;
            f32 drive = 4.7;
            f32 params[6] = { root, veloc, drive, -.3, .01, .2 };
            begin(e, 0);
            f64 t = t0 + latency;
            noteOn(101, noteID, 6, params);
            params[0] += 7;
            params[3] = .3;
            noteOn(101, noteID+1, 6, params);
            sched(t);

            begin(e, 0);
            noteOff(101, noteID);
            noteOff(101, noteID+1);
            t += numPitches * dt;
            sched(t);

            noteID+=2;
        }
        for (int i = 0; i < numPitches; ++i) {
            begin(e, 0);
            f32 pitch = pitches[numPitches-i-1] - 1 * k + 8;
            f32 veloc = .5 + .04 * (numPitches - i - 1);
            f32 drive = 1.15 + .3 * k;
            f32 pan = -0.8 + (1.6 / (numPitches-1)) * i;
            f32 params[6] = { pitch, veloc, drive, pan, .01, .2 };
            noteOn(101, noteID, 6, params);

            f64 t = t0 + latency + i * dt;
            sched(t);

            t += .1 + .04 * k;
            begin(e, 0);
            noteOff(101, noteID);
            sched(t);
            ++noteID;
        }
        sleepf(.6);
    }
    sleepSec(4);



    for (int k = 0; k < 8; ++k) {
        f64 dt = .25;
        f64 latency = .02;
        f64 t0 = getStreamTime(e);
        for (int i = 0; i < numPitches; ++i) {
            begin(e, 0);
            f32 pitch = pitches[i] - 5 * k + 10;
            noteOn(101, noteID, 1, &pitch);
            f32 drive = 1. + 2.3 * k;
            noteSetParamRange(101, noteID, 2, 1, &drive);

            f64 t = t0 + latency + i * dt;
            sched(t);

            t += 2.;
            begin(e, 0);
            noteOff(101, noteID);
            sched(t);
            ++noteID;
        }
        sleepSec(3);

        t0 = getStreamTime(e);
        for (int i = 0; i < numPitches; ++i) {
            begin(e, 0);
            f32 pitch = 2 + pitches[i] - 5 * k + 10;
            noteOn(101, noteID, 1, &pitch);
            f32 drive = 2. + 2.3 * k;
            noteSetParamRange(101, noteID, 2, 1, &drive);

            f64 t = t0 + latency + i * dt;
            sched(t);

            t += 2.;
            begin(e, 0);
            noteOff(101, noteID);
            sched(t);
            ++noteID;
        }
        sleepSec(3);
    }
    sleepSec(4);
    printf("allNotesOff\n");
    begin(e, 0);
        allNotesOff(101);
    go();
    sleepSec(5);

    printf("stop\n");
    stopAudio(e);

    printf("free engine\n");
    freeEngine(e);
}

void test4()
{
    printf("--- test4 ---------------------------------\n");
    using namespace engine;

    EngineConfig config;
    config.numSilos = 10;

    AudioStreamParameters asp{.deviceName = "default", .channels = 2, .firstChannel = 0, .bufferFrames = 256, .sampleRate = 48000.};
    auto e = newEngine(config, asp);

    createSineNode(e);

    printf("start audio\n");
    startAudio(e);

    sleepSec(1);

    f32 freq;
    f32 amp;

    printf("create graphs on %d threads\n", config.numSilos);
    for (int i = 0; i < config.numSilos; ++i) {
        sleepf(.5);
        begin(e, i);
        newNode("sinosc", 101);
        freq = 240 + 73.371 * i;
        setInput({101, 0}, 1, &freq);
        amp = 0.0;
        setInput({101, 1}, 1, &amp);
        amp = 0.05;
        setInput({101, 1}, 1, &amp, .5, fadeEaseInCubic);
        connect({101, 0}, {0, 0});
        go();
    }

    sleepSec(4);

    for (int i = 0; i < config.numSilos; ++i) {
        sleepf(.5);
        begin(e, config.numSilos-i-1);
        amp = 0.00;
        setInput({101, 1}, 1, &amp, .5, fadeEaseOutCubic);
        go();
    }
    sleepSec(1);

    printf("stop\n");
    stopAudio(e);

    printf("free engine\n");
    freeEngine(e);
}

int main(int argc, const char * argv[])
{
    test_sexpr();
    try {
            test5();
            test1();
            test2();
            test4();
            test3();
    } catch (std::exception& exc) {
        printf("an exception occurred: %s\n", exc.what());
    } catch (int& errc) {
        printf("an exception occurred: %d\n", errc);
    } catch (...) {
        printf("an unknown exception occurred.\n");
    }
    return 0;
}
