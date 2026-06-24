-- bass_task.x -- a walking bass line on node 101
import audio_engine.*;

let pitches = [36.0, 36.0, 43.0, 41.0];

coro fn play() Float {
    var i = 0;
    while (true) {
        playNote(101, i % 16, [pitches[i % 4], 0.7, 4.7, 0.0, 0.005, 0.15]);
        yield 0.9;
        releaseNote(101, i % 16);
        yield 0.1;
        i = i + 1;
    }
}

fn start() Void { spawn(0, play()); }
