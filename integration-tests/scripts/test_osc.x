-- test_osc.x
-- Integration test script for OSC FFI functions.

import osc.*;
import audio_engine.*;

-- Server should not be running initially
let port = oscServerPort();
println(port);  -- should print 0

-- Send to local engine (bypass network)
oscSendLocal("/engine/isAudioRunning");

-- Send remote (will fail silently if no listener, which is fine)
oscSend("127.0.0.1", 57999, "/test/hello");
oscSendF("127.0.0.1", 57999, "/test/float", 3.14);
oscSendI("127.0.0.1", 57999, "/test/int", 42);
oscSendS("127.0.0.1", 57999, "/test/string", "hello");

println("OSC FFI test passed");
