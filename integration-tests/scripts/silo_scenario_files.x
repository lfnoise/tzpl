-- Scenario 1, loading each part's task from a real .x file (not an inline
-- string) via siloLoadFile -> readFile. Run from the repo root so the relative
-- task paths resolve:
--   ./build/app/tzpl_app --nogui --wait -I lang/modules -I bridge/modules \
--       integration-tests/scripts/silo_scenario_files.x

import audio_engine.*;
import futures.*;
import silo.*;

fn preparePart(silo Int, node Int, path String) Future<String> {
    begin(); newNode("voicer", node); connect(node, 0, 0, 0); sched(silo);
    attachVM(silo);
    siloLoadFile(silo, path)
}

async fn startSession() Void {
    await awaitAll([preparePart(0, 101, "integration-tests/scripts/tasks/bass_task.x"),
                   preparePart(1, 201, "integration-tests/scripts/tasks/arp_task.x"),
                   preparePart(2, 301, "integration-tests/scripts/tasks/chord_task.x")]);
    "all parts loaded from .x files; starting on the next downbeat" println;
    siloStartAt(quantizeUp(clockBeats(0), 4.0), [0, 1, 2]);
    "started" println;
}

engineStart();
masterGain(0.22);
setTempo(0, 110.0);
await startSession();
"session running -- Ctrl-C to stop." println;
