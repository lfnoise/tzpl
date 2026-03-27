-- test_nats.x
-- Integration test script for NATS FFI functions.

import nats.*;

-- Connection status (should be false if not connected via CLI)
let connected = natsIsConnected();
println(connected);  -- should print false

-- URL should be empty
let url = natsUrl();
println(url);  -- should print ""

-- Publish will silently fail if not connected, which is fine
natsPub("test.hello", "from tzopilotl");
natsPubI("test.int", 42);
natsPubF("test.float", 3.14);

println("NATS FFI test passed");
