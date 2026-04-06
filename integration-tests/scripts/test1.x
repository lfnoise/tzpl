-- test1: Two silos, replace nodes, tremolo, safety limiter
import clock.*;
import audio_engine.*;

println("--- test1 ---------------------------------");

let siloA = 0;
let siloB = 1;

go(coro fn() Float {
    engineStart();
    println(isAudioRunning());
    yield 1.0;

    println("create graph A");
    begin(siloA);
    newNode("sinosc", 101);
    newNode("sinosc", 102);
    newNode("+", 103);
    newNode("*", 203);
    setInput(101, 0, 240.0);
    setInput(102, 0, 360.5);
    connect(101, 0, 103, 0);
    connect(102, 0, 103, 1);
    connect(103, 0, 0, 0);
    sched();
    yield 2.0;

    for (h : (0..2)) {
        println("replace 103 -> 203 w fade");
        begin(siloA);
        replaceNode(103, 203, 1.25, FadeCurve.fadeLinear);
        sched();
        yield 2.0;

        println("replace 203 -> 103 w fade");
        begin(siloA);
        replaceNode(203, 103, 1.25, FadeCurve.fadeLinear);
        sched();
        yield 2.0;
    }

    begin(siloB);
    println("create graph B");
    newNode("sinosc", 101);
    newNode("sinosc", 102);
    newNode("+", 103);
    setInput(101, 0, 480.0);
    setInput(102, 0, 840.0);
    setInput(101, 1, 0.125);
    setInput(102, 1, 0.125);
    connect(101, 0, 103, 0);
    connect(102, 0, 103, 1);
    connect(103, 0, 0, 0);
    sched();
    yield 1.0;

    println("attempt to change a connection.");
    begin(siloB);
    newNode("sinosc", 104);
    setInput(104, 0, 400.0);
    connectX(104, 0, 103, 0, 0.1, FadeCurve.fadeLinear);
    sched();
    yield 1.0;

    println("ok, set it back.");
    begin(siloB);
    connectX(101, 0, 103, 0, 0.1, FadeCurve.fadeLinear);
    sched();
    yield 0.4;

    begin(siloB);
    disconnectNode(104);
    sched();
    yield 2.0;

    println("slide freq to 600 in 8 seconds");
    begin(siloA);
    setInputX(101, 0, 600.0, 8.0, FadeCurve.fadeLinear);
    sched();
    yield 4.0;

    println("attempt to start a new slide while the first is running");
    begin(siloA);
    setInputX(101, 0, 400.0, 8.0, FadeCurve.fadeLinear);
    sched();
    yield 4.0;

    println("first slide should be done about now");
    yield 4.0;

    println("second slide should be done about now");
    yield 3.0;

    println("disconnect a sinosc");
    begin(siloA);
    disconnectInputX(103, 1, 0.5, FadeCurve.fadeLinear);
    sched();
    yield 3.0;

    println("reconnect a sinosc");
    begin(siloA);
    connectX(102, 0, 103, 1, 0.5, FadeCurve.fadeLinear);
    sched();
    yield 4.0;

    println("tremolo");
    for (ti : (0..15)) {
        let amp = ti % 2 == 1 ? 0.2 : 0.02;
        begin(siloA);
        setInputX(101, 1, amp, 0.1, FadeCurve.fadeSmoothstep);
        setInputX(102, 1, amp, 0.1, FadeCurve.fadeSmoothstep);
        sched();
        yield 0.2;
    }
    yield 2.0;

    println("very loud (engage safety limiter).");
    begin(siloA);
    setInputX(101, 1, 20.0, 0.04, FadeCurve.fadeLinear);
    sched();
    yield 1.0;

    println("set amp .2");
    begin(siloA);
    setInputX(101, 1, 0.2, 0.04, FadeCurve.fadeLinear);
    sched();
    yield 1.0;

    println("random amplitudes 0.1 .. 20.0");
    for (ri : (0..7)) {
        let ramp = rand(0.0, 20.0);
        begin(siloA);
        setInputX(101, 1, ramp, 0.04, FadeCurve.fadeLinear);
        sched();
        yield 0.1;
    }

    println("set amp .2");
    begin(siloA);
    setInputX(101, 1, 0.2, 0.04, FadeCurve.fadeLinear);
    sched();
    yield 2.0;

    println("set graph B amps silent");
    begin(siloB);
    setInputX(101, 1, 0.0, 3.0, FadeCurve.fadeEaseOutCubic);
    setInputX(102, 1, 0.0, 3.0, FadeCurve.fadeEaseOutCubic);
    sched();
    yield 4.0;

    println("set graph A amps silent");
    begin(siloA);
    setInputX(101, 1, 0.0, 3.0, FadeCurve.fadeEaseOutCubic);
    setInputX(102, 1, 0.0, 3.0, FadeCurve.fadeEaseOutCubic);
    sched();
    yield 4.0;

    println("free graph B");
    begin(siloB);
    freeAllNodes();
    sched();
    yield 1.0;

    println("free graph A");
    begin(siloA);
    freeAllNodes();
    sched();
    yield 1.0;

    println("stop");
    engineStop();
}());
