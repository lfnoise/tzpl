-- test4: Multiple silos with sequential graph creation
import clock.*;
import audio_engine.*;

println("--- test4 ---------------------------------");

let numSilos = 10;

go(coro fn() Float {
    println("start audio");
    engineStart();
    yield 1.0;

    println("create graphs on 10 threads");
    for (i : (0..9)) {
        yield 0.5;
        begin(i);
        newNode("sinosc", 101);
        setInput(101, 0, 240.0 + 73.371 * i);
        setInput(101, 1, 0.0);
        setInputX(101, 1, 0.05, 0.5, FadeCurve.fadeEaseInCubic);
        connect(101, 0, 0, 0);
        sched();
    }
    yield 4.0;

    for (i : (0..9)) {
        yield 0.5;
        begin(numSilos - i - 1);
        setInputX(101, 1, 0.0, 0.5, FadeCurve.fadeEaseOutCubic);
        sched();
    }
    yield 1.0;

    println("stop");
    for (i : (0..9)) {
        begin(i);
		freeNode(101);
        sched();
	}
    engineStop();
}());
