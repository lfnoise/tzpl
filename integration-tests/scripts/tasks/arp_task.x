-- arp_task.x -- a fast arpeggio on node 201
import audio_engine.*;

let pitches = [72.0, 76.0, 79.0, 84.0];

coro fn play() Float {
    var i = 0;
    while (true) {
        playNote(201, i % 16, [pitches[i % 4], 0.25, 4.5, 0.0, 0.005, 0.1]);
        yield 0.2;
        releaseNote(201, i % 16);
        yield 0.05;
        i = i + 1;
    }
}

fn start() Void { spawn(0, play()); }
