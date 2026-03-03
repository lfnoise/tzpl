-- test0: Simple sine oscillator
import audio_engine.*;

println("--- test0 ---------------------------------");

engineStart();
println(isAudioRunning());

sleep(1.0);

println("create graph with only a sine oscillator");
begin(0);
newNode("sinosc", 101);
setInput(101, 0, 240.0);
setInputX(101, 1, 0.15, 0.2, FadeCurve.fadeLinear);
connect(101, 0, 0, 0);
go();

sleep(8.0);

println("stop");
engineStop();
