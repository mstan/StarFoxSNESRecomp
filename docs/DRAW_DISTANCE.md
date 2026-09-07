# Enhanced renderer draw-distance investigation

The current renderer traverses the entire active object list at `$121D`, rather
than just copying the Super FX's visible draw list. It has no adjustable far
plane. It skips hidden/empty objects and objects wholly behind the camera, and
honors each shape's source LOD pointers at depths 1000, 2000 and 3000.

A read-only Corneria sample at frame 5501 contained 12 active objects. The
farthest modeled object was 1856 units from the camera. All modeled objects in
that sample had self-referencing LOD pointers; none was lost to a distant LOD.
This is a sample, not proof that every level has the same limit.

The retail level-script scheduler controls when future objects exist:
`UpdateZTimer` subtracts player forward movement and only continues the script
when the timer expires. Object-load commands `$00`, `$70`, and `$86` allocate
from the guest free list, set a model/behavior and place it relative to the
player. Increasing a renderer distance cannot reveal objects that the script
has not created yet.

A presentation-only extension would need a separate preview of future scenery:
read ahead through eligible script commands, resolve positions without running
guest behavior, and replace each preview when its real object appears. Script
branches, camera changes, animated structures and destruction need explicit
handling to avoid duplicate or incorrect scenery. Enemies and gameplay objects
must continue to spawn at their original times.

No distance setting or guest spawn/timing change was added in this pass. The
next useful experiment is a limited, opt-in preview of static Corneria scenery,
with screenshots across its handoff to real objects and unchanged guest-state
hashes. This is separate from restoring the missing ground dots.
