-- test2: Multiple silos with scheduled events
import audio_engine.*;

println("--- test2 ---------------------------------");

let numSilos = 8;

engineStart();
println(isAudioRunning());

sleep(1.0);

let latency = 0.02;

println("start graphs on 8 threads");
var t0 = getStreamTime();
for (i : (0..7)) {
    begin(i);
    newNode("sinosc", 101);
    setInput(101, 0, 240.0 + 60.0 * i);
    setInput(101, 1, 0.0);
    setInputX(101, 1, 0.05, 0.2, FadeCurve.fadeEaseInCubic);
    connect(101, 0, 0, 0);
    sched(t0 + latency + i * 0.2);
}

sleep(5.0);

println("change freqs");
t0 = getStreamTime();
for (i : (0..7)) {
    begin(i);
    setInputX(101, 0, 360.0 + 180.0 * i, 0.5, FadeCurve.fadeExponential);
    sched(t0 + latency + i * 0.5);
}

sleep(8.0);

println("reset freqs");
t0 = getStreamTime();
for (i : (0..7)) {
    begin(i);
    setInputX(101, 0, 240.0 + 60.0 * i, 0.5, FadeCurve.fadeExponential);
    sched(t0 + latency + i * 0.5);
}

sleep(8.0);

println("stop");
engineStop();
sleep(1.0);
println("start");
engineStart();

sleep(4.0);

println("set silent");
t0 = getStreamTime();
for (i : (0..7)) {
    begin(i);
    setInputX(101, 1, 0.0, 0.5, FadeCurve.fadeEaseOutCubic);
    sched(t0 + latency + i * 0.4);
}

sleep(6.0);

println("stop");
engineStop();
