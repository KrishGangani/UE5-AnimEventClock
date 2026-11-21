# 12 · Future Scalability: AnimEventClock

How the system would grow into a production game, in order of value:

1. **The client side (designed, not built).** Replicate each action's server start time and every cut of its path (a stop, a jump, the end of a replaced action) as the server's last crossing (pass, position, key); a client walks to each cut exactly and catches up a late start, firing what was due on arrival or expiring it beyond 0.5 s. A Cristian clock sync on the player controller (minimum round trip of 8 samples) gives the server time (02 §6.2).
2. **A hold-back delay on clients.** A server-initiated stop or jump arrives a one-way delay late; a client clock running a configurable delay behind the server would never fire past a cut it has not heard of, at the cost of that lateness (SC3 against SC2).
3. **Client-predicted actions** with validated start times, the other way out of (2).
4. **Lag compensation from the exact due time:** every event carries its due time, so a server hit check can rewind to it rather than to the frame it ran in.
5. **Cost at scale:** a shared read-only timeline per montage, one manager tick for all clocks instead of one tick function each, and a native delegate beside the Blueprint one; each waits for an overhead measurement (11).
6. **Authoring:** an editor validator for the marker mistakes that make events wrong at run time (no name, outside the montage, empty windows, duplicates, BranchingPoint, markers on slot sequences): drafted, not built.
