-- chord_task.x -- a held triad, retriggered every 4 beats, on node 301
import audio_engine.*;

coro fn play() Float {
    var b = 0;
    while (true) {
        playNote(301, b,     [60.0, 0.3, 5.0, 0.0, 0.05, 0.5]);
        playNote(301, b + 1, [64.0, 0.3, 5.0, 0.0, 0.05, 0.5]);
        playNote(301, b + 2, [67.0, 0.3, 5.0, 0.0, 0.05, 0.5]);
        yield 3.5;
        releaseNote(301, b);
        releaseNote(301, b + 1);
        releaseNote(301, b + 2);
        yield 0.5;
        b = b + 3;
    }
}

fn start() Void { spawn(0, play()); }
