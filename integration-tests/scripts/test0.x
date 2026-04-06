-- test0: Simple sine oscillator
import clock.*;
import audio_engine.*;

println("--- test0 ---------------------------------");

go(coro fn() Float {
    engineStart();
    println(isAudioRunning());
    yield 1.0;

    println("create graph with only a sine oscillator");
    begin(0);
    newNode("sinosc", 101);
    setInput(101, 0, 240.0);
    setInputX(101, 1, 0.15, 0.2, FadeCurve.fadeLinear);
    connect(101, 0, 0, 0);
    sched();
    yield 8.0;

    println("stop");
    engineStop();
}());
