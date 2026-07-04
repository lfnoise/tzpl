-- test3: Voicer with note events
import clock.*;
import audio_engine.*;

println("--- test3 ---------------------------------");

fn test3() {
go(coro fn() Float {
    engineStart();
    println(isAudioRunning());
    yield 1.0;

    println("create graph A");
    begin();
    newNode("voicer", 101);
    connect(101, 0, 0, 0);
    sched(0);
    yield 1.0;

    let numPitches = 6;
    let pitches = [60.0, 65.0, 67.0, 70.0, 74.0, 77.0];
    var noteID = 0;
    yield 1.0;

    -- Arpeggiated chords section
    for (k : (0..19)) {
        let dt = 0.1;
        let latency = 0.02;
        let t0 = getStreamTime();

        -- Chord: two bass notes
        let root = pitches[0] - 1.0 * k - 2.0;
		"root %^" fmt(root) println;
        begin();
        noteOn(101, noteID, [root, 0.7, 4.7, -0.3, 0.01, 0.2]);
        noteOn(101, noteID + 1, [root + 7.0, 0.7, 4.7, 0.3, 0.01, 0.2]);
        sched(0, 0, t0 + latency);

        begin();
        noteOff(101, noteID);
        noteOff(101, noteID + 1);
        sched(0, 0, t0 + latency + numPitches * dt);

        noteID = noteID + 2;

        -- Ascending arpeggio
        for (i : (0..5)) {
            let pitch = pitches[i] - 1.0 * k + 10.0;
            let veloc = 0.5 + 0.04 * (numPitches - i - 1);
            let drive = 1.0 + 0.3 * k;
            let pan = -0.8 + (1.6 / (numPitches - 1)) * i;
			"pitch %^" fmt(pitch) println;
            begin();
            noteOn(101, noteID, [pitch, veloc, drive, pan, 0.01, 0.2]);
            sched(0, 0, t0 + latency + i * dt);

            begin();
            noteOff(101, noteID);
            sched(0, 0, t0 + latency + i * dt + 0.1 + 0.04 * k);

            noteID = noteID + 1;
        }
        yield 0.6;

        -- Descending section
        let t1 = getStreamTime();
        let root2 = pitches[0] - 1.0 * k - 4.0;
        begin();
		"root2 %^" fmt(root2) println;
        noteOn(101, noteID, [root2, 0.7, 4.7, -0.3, 0.01, 0.2]);
        noteOn(101, noteID + 1, [root2 + 7.0, 0.7, 4.7, 0.3, 0.01, 0.2]);
        sched(0, 0, t1 + latency);

        begin();
        noteOff(101, noteID);
        noteOff(101, noteID + 1);
        sched(0, 0, t1 + latency + numPitches * dt);

        noteID = noteID + 2;

        for (i : (0..5)) {
            let pitch = pitches[numPitches - i - 1] - 1.0 * k + 8.0;
            let veloc = 0.5 + 0.04 * (numPitches - i - 1);
            let drive = 1.15 + 0.3 * k;
            let pan = -0.8 + (1.6 / (numPitches - 1)) * i;
			"pitch %^" fmt(pitch) println;
            begin();
            noteOn(101, noteID, [pitch, veloc, drive, pan, 0.01, 0.2]);
            sched(0, 0, t1 + latency + i * dt);

            begin();
            noteOff(101, noteID);
            sched(0, 0, t1 + latency + i * dt + 0.1 + 0.04 * k);

            noteID = noteID + 1;
        }
        yield 0.6;
    }
    yield 4.0;

    -- Sustained notes with noteSetParams
    for (k : (0..7)) {
        let dt = 0.25;
        let latency = 0.02;
        let t0 = getStreamTime();

        for (i : (0..5)) {
            let pitch = pitches[i] - 5.0 * k + 10.0;
            let drive = 1.0 + 2.3 * k;
			"pitch %^" fmt(pitch) println;
            begin();
            noteOn(101, noteID, [pitch]);
            noteSetParams(101, noteID, 2, [drive]);
            sched(0, 0, t0 + latency + i * dt);

            begin();
            noteOff(101, noteID);
            sched(0, 0, t0 + latency + i * dt + 2.0);

            noteID = noteID + 1;
        }
        yield 3.0;

        let t1 = getStreamTime();
        for (i : (0..5)) {
            let pitch = 2.0 + pitches[i] - 5.0 * k + 10.0;
            let drive = 2.0 + 2.3 * k;
			"pitch %^" fmt(pitch) println;
            begin();
            noteOn(101, noteID, [pitch]);
            noteSetParams(101, noteID, 2, [drive]);
            sched(0, 0, t1 + latency + i * dt);

            begin();
            noteOff(101, noteID);
            sched(0, 0, t1 + latency + i * dt + 2.0);

            noteID = noteID + 1;
        }
        yield 3.0;
    }
    yield 4.0;

    println("allNotesOff");
    begin();
    allNotesOff(101);
    sched(0);
    yield 5.0;

    println("stop");
    begin();
	freeNode(101);
    sched(0);
    engineStop();
}());

}

fn render_test3() {
	let h = "/tmp/test3.wav" renderNRT(200, test3);
	await renderDone(h);    -- block until the NRT render finishes
	"render done" println;
}

test3();
--render_test3();

