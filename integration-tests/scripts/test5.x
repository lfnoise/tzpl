-- test5: Fan-out test
import clock.*;
import audio_engine.*;

println("--- test5 ---------------------------------");

go(coro fn() Float {
    engineStart();
    println(isAudioRunning());
    yield 1.0;

    println("create graph A");
    begin();
    newNode("sinosc", 101);
    newNode("+", 102);
    setInput(101, 0, 240.0);
    setInputX(101, 1, 0.15, 0.2, FadeCurve.fadeLinear);
    connect(101, 0, 102, 0);
    connect(102, 0, 0, 0);
    sched(0);
    yield 1.0;

    println("test fan out");
    for (i : (0..3)) {
        begin();
        connectX(101, 0, 102, 1, 0.3, FadeCurve.fadeLinear);
        sched(0);
        yield 0.4;

        begin();
        disconnectInputX(102, 1, 0.3, FadeCurve.fadeLinear);
        sched(0);
        yield 0.4;
    }
    yield 2.0;

    println("stop");
    
    begin();
    freeNode(101);    
    freeNode(102);
    sched(0);
        
    engineStop();
}());


