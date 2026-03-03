-- test4: Multiple silos with sequential graph creation
import audio_engine.*;

println("--- test4 ---------------------------------");

let numSilos = 10;

println("start audio");
engineStart();

sleep(1.0);

println("create graphs on 10 threads");
for (i : (0..9)) {
    sleep(0.5);
    begin(i);
    newNode("sinosc", 101);
    setInput(101, 0, 240.0 + 73.371 * i);
    setInput(101, 1, 0.0);
    setInputX(101, 1, 0.05, 0.5, FadeCurve.fadeEaseInCubic);
    connect(101, 0, 0, 0);
    go();
}

sleep(4.0);

for (i : (0..9)) {
    sleep(0.5);
    begin(numSilos - i - 1);
    setInputX(101, 1, 0.0, 0.5, FadeCurve.fadeEaseOutCubic);
    go();
}

sleep(1.0);

println("stop");
engineStop();
