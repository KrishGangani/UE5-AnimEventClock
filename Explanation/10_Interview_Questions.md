# 10 · Interview Questions: AnimEventClock

Answers grounded in this project's code and measurements (batches in [09](09_Testing_And_Benchmarks.md)).

**1. Why aren't anim notifies enough for gameplay events?** A notify fires only in a frame where its mesh's animation updates. URO, the budget allocator and visibility-based ticking change when that happens: measured here, URO at rate 4 moves Queued notifies from 2.9 frames early to 6.9 late (`final` batch), and `OnlyTickPoseWhenRendered` on a dedicated server loses every server notify (`m2_cond`).

**2. Why can a notify fire early?** URO's skipped time belongs to the mesh, not the montage: a montage started inside a skip interval inherits the time skipped before it started (`SkinnedMeshComponent.cpp:6103–6116`, 01b #2). Look-ahead makes it worse (12.5% fast at 60 fps, S3).

**3. What does the event clock do instead?** It builds a timeline from the montage's own notifies (same trigger times), advances a playhead by gameplay time × play rate every frame, and broadcasts every key it crosses with the exact due time. The montage stays the authoring tool; the clock does not need the mesh to tick.

**4. How do you keep "exactly once" across loops and jumps?** Keys are crossed in (A, B], or [A, B] right after an entry; every return to earlier montage time is a new pass; a window the path leaves is closed with a forced end in key order; a window entered partway begins at the entry (the engine's rule). 10 pure-timeline tests cover these (S5).

**5. What happens if a handler stops the action, starts another, or destroys the actor during dispatch?** Calls from a handler take effect at the handled event's due time; dispatch re-checks the path serial, the action and the owner after every broadcast; a handler's jump starts from where the playhead was at the handled crossing, not the frame's end (a code-review finding). Only the destroy case is in the shipped test.

**6. Why not just use timers?** On the authority a timer is already within one frame (measured: 0.88 frame p99). The point of the clock is timer-grade timing with the events still authored in the montage, play rate, sections and interruption handled in one place. It does not beat a timer; it matches one without hand-copied offsets.

**7. How did you make the benchmark trustworthy?** Fixed step, a seeded workload, every path recorded in the same run, 2 interleaved runs per combination after a discarded priming run, pinned to the performance cores, and a fire hash per run; a no-op hash warning caught a budget condition that measured nothing (S3-b).

**8. What did the network baselines show?** On a 30 Hz dedicated server, Queued window ends are 2 frames late and a remote player's events up to 1.9 frames early (the server advances that pose by client moves); on clients every path is late by the one-way delay; GAS's montage replication repeats events on simulated proxies at every latency (7–140 per 1,000). UE-308913 was not reproduced on 5.8.4 (M2).

**9. What would you do next?** The replicated client side (start time + path cuts, clock sync), designed and drafted but not built (02 §6.2); then the overhead batch for SC5 and a validator for SC7.

**10. What is the biggest limitation you would tell a lead about?** A server-initiated stop reaches a client a one-way delay late, so without a hold-back delay a client cannot avoid firing events the server cut; and here, only the authority side exists.
