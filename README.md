# Arcade Car Controller

Burnout / OutRun–style car controller for Polyphase, plus a **Car Setup Wizard**
that turns a static mesh into a drivable car.

Deliberately arcade, not a simulation: no suspension springs, no tire force
curves, no drivetrain. Grip never falls off past a peak, so the car cannot spin
you out for over-slipping.

| | |
| --- | --- |
| **Package id** | `com.polyphase.car.controller` |
| **Version** | 1.0.0 |
| **Target** | `engine` (runtime + editor tooling) |
| **Plugin API** | 8 |
| **Nodes** | `CarController3D`, `CarCamera3D` |

**Contents:** [Quick start](#quick-start) · [Handling](#how-the-handling-works) ·
[Nodes](#nodes) · [Presets](#handling-presets) · [Property reference](#property-reference) ·
[Mesh authoring](#how-to-author-the-car-mesh) · [Input](#input) · [Lua API](#lua-api) ·
[Examples](#examples) · [Troubleshooting](#troubleshooting) · [Implementation notes](#implementation-notes) ·
[Building](#building)

## Installation

Copy (or clone) this folder into your project as
`<Project>/Packages/com.polyphase.car.controller/`, or install it from the
**Addons** window. The editor discovers it from `package.json`, builds it, and
loads it. Requires the Vulkan SDK headers like every native addon.

For shipped builds, run **Tools → Addons → Regenerate Native Addon
Dependencies** once so the addon is statically linked into the game.

## Quick start

1. **Tools → Addons → Reload Native Addons**
2. **Tools → Car → Setup Wizard…**
3. Drag a StaticMesh onto the *Body Mesh* slot (or pick one from the browser).
4. Choose a handling preset, hit **Create Car**.
5. Press Play. `WASD` / arrows, `Space` handbrake, `Shift` boost, `R` reset.
   Gamepad works out of the box: RT/LT throttle & brake, left stick steer.

## How the handling works

The whole model is one relationship:

> Steering rotates the car's **heading**. The **velocity vector chases that
> heading** at a rate equal to grip.

High grip → the car goes exactly where it points. Drop the rate → the tail steps
out. That is the entire drift system; there is no second mechanism.

```
gripRate = mix(BaseGrip, DriftGrip, driftAmount)
velDir   = RotateTowards(velDir, forward, gripRate * dt)
velocity = velDir * |speed|
```

`Base Grip` / `Drift Grip` are therefore the two dials that matter most:

| Base Grip | Feel |
| --------- | ---- |
| 10+       | On rails, kart-like |
| 8–9       | Planted street car |
| 6–7       | Loose, slides when provoked |
| Drift Grip < 2 | Long lurid slides |

Drift **exit** is rate-limited (`Drift Exit Time`, default 0.3 s) so grip eases
back instead of snapping the car straight. Neither Godot reference handles this,
and it is the difference between a slide that feels controllable and one that
feels like a punishment.

Steering uses a separate, much faster **countersteer rate** when the input
crosses centre, so a slide is always catchable.

## Nodes

**`CarController3D`** (Add Node → 3D → Vehicles) — kinematic. Runs its own fixed
120 Hz accumulator inside `Tick`, because the engine steps Bullet with a variable
delta and only 2 substeps, and node ticks run *after* that step.

**`CarCamera3D`** — chase camera with late tick. Planar leash (no vertical swing
on jumps), independently smoothed position and aim, speed-driven FOV, velocity
look-ahead, and a drift factor that swings the aim toward the direction of travel
so a slide shows the car's flank. With no *Target Car* assigned it adopts its
parent if that is a car, otherwise the first `CarController3D` in the scene, so
a hand-placed camera works without wiring.

## Handling presets

The wizard offers four starting points. Every value stays editable on the node
afterwards — a preset is a baseline, not a lock-in.

| Preset | Feel | Top speed | Base / Drift grip | Drift mode | Drive |
| ------ | ---- | --------- | ----------------- | ---------- | ----- |
| **Street** | Balanced, forgiving; slides only when provoked | 55 m/s | 8.5 / 1.4 | Assisted | RWD |
| **Drift** | Low grip, huge lock, strong yaw assist | 58 m/s | 6.0 / 0.9 | Always | RWD |
| **Muscle** | Fast in a straight line, lazy to turn | 64 m/s | 7.2 / 1.1 | Assisted | RWD |
| **Kart** | Light, twitchy, almost no slide | 42 m/s | 12.0 / 3.0 | Handbrake Only | AWD |

## Property reference

All `CarController3D` properties, grouped as they appear in the inspector.
Units are metres, seconds and degrees unless stated.

**Car|Drive**

| Property | Default | Meaning |
| -------- | ------- | ------- |
| Top Speed | 55 | m/s (~198 km/h) |
| Reverse Top Speed | 12 | m/s |
| Accel Rate | 17 | m/s² from standstill |
| Accel Falloff | 0.35 | Acceleration multiplier once at top speed |
| Brake Rate | 32 | m/s² |
| Coast Rate | 4.5 | Engine braking off the pedals |
| Drag Coeff | 0.0016 | Quadratic drag; dominates at high speed |
| Drive Layout | Rear Wheel | Only biases how readily the tail steps out |

**Car|Steering**

| Property | Default | Meaning |
| -------- | ------- | ------- |
| Max Steer Angle | 38 | Degrees at full lock, low speed |
| Steer Rate | 5 | Lock fractions per second |
| Countersteer Rate | 12 | Faster rate when steering back through centre |
| High Speed Steer Scale | 0.40 | Lock multiplier at top speed |
| Steer Exponent | 1.4 | >1 softens small stick deflections |
| Yaw Rate Scale | 2.4 | Overall rotation speed |
| Yaw Full Speed | 11 | m/s at which the car has full rotational authority |
| Air Steer Scale | 0.45 | Steering authority while airborne |
| Invert X Input | off | Flips player steering (not applied to `SetInput`) |

**Car|Drift**

| Property | Default | Meaning |
| -------- | ------- | ------- |
| Drift Mode | Assisted | *Handbrake Only*, *Assisted* (handbrake or hard turn under power), *Always* |
| Base Grip | 8.5 | rad/s the velocity chases the heading |
| Drift Grip | 1.4 | Same, while drifting |
| Air Grip | 0.6 | Same, while airborne |
| Drift Min Speed | 7 | m/s below which no drift starts |
| Drift Enter Angle / Exit Angle | 14 / 6 | Slip angle thresholds (degrees) |
| Drift Enter Time / Exit Time | 0.10 / 0.30 | Ramp in / ease out, seconds |
| Drift Yaw Assist | 22 | Extra deg/s of rotation while sliding |

**Car|Boost**

| Property | Default | Meaning |
| -------- | ------- | ------- |
| Boost Speed Mult / Accel Mult | 1.32 / 1.9 | Multipliers while boosting |
| Boost Use Meter | on | Off = unlimited boost |
| Boost Capacity | 2.5 | Seconds of continuous boost |
| Boost Drain Rate / Refill Rate | 1.0 / 0.35 | Per second |

**Car|Ground** — see [Troubleshooting](#troubleshooting) for how these interact.

| Property | Default | Meaning |
| -------- | ------- | ------- |
| Gravity | -26 | m/s², heavier than real (standard arcade) |
| Ride Height | 0.35 | Origin height above the ground |
| Ground Probe Distance | 1.4 | Downward ray length |
| Ground Snap Rate / Align Rate | 14 / 7 | How fast the car settles onto / tilts with the ground |
| Max Ground Angle | 50 | Steeper surfaces count as walls |
| Wall Speed Loss | 0.45 | Speed scrubbed on a head-on hit |
| Wall Bounce | 0.35 | Restitution, 0–1 |
| Collision Half Extents | (0.9, 0.55, 2.0) | Box collider size |
| Collider Ground Clearance | 0.08 | Gap under the box; keep > 0 |
| Ground Collision Mask | all | Groups the probe and sweep can hit |

**Car|Visuals** — Body Yaw Offset (0), Body Roll Angle (6), Body Pitch Angle
(3.5), Body Visual Rate (9), Wheel Radius (0.34), Wheel Steer Visual Max (30).

**Car|Audio** — Engine Pitch Min/Max (0.75 / 2.10), Engine Volume Min/Max
(0.35 / 1.0; keep the minimum above 0 or the voice gets dropped), Gear Count (6).

**Car|Input** — Input Category (`Car`), Gamepad Index (0), Stick Deadzone (0.12).

**Car|Nodes** — Collider, Body, Wheel FL/FR/RL/RR, Engine Audio, Smoke L/R.
The wizard fills these in; for a hand-built car, assign them yourself.

**Car|Debug** — Debug Draw: draws the ground probe (blue = hit, grey = miss).

**`CarCamera3D`**

| Group | Property | Default |
| ----- | -------- | ------- |
| Target | Target Car / Make Main Camera | auto / on |
| Framing | Follow Distance / Follow Height / Look At Height | 6.5 / 2.4 / 0.9 |
| Smoothing | Position Rate / Aim Rate / Up Rate | 6 / 9 / 4 |
| Feel | Base FOV / Max FOV / FOV Rate | 65 / 88 / 3 |
| Feel | Look Ahead Time / Drift Aim Blend | 0.28 s / 0.55 |

## How to author the car mesh

**Body mesh separate, wheels separate.** The wheels have to be their own nodes so
they can spin about X and steer about Y independently of the body.

The wizard takes **one body mesh** and **one wheel mesh**, and reuses that single
wheel for all four corners — positioning them from the derived wheelbase and
track width, and scaling them to the derived radius. You do not author four
wheels.

Because a wheel mesh is almost always asymmetric (the rim face points outward),
the wizard yaws the **left-hand pair 180°** so both sides face outward. The
controller then negates spin for any wheel that is flipped, so the two sides
still rotate the same way. Turn *Mirror left wheels* off if your wheel is
symmetric or already authored for the left side.

Other layouts, and what happens:

| You have | Result |
| -------- | ------ |
| Body + one wheel mesh | The happy path. Wheels spin and steer. |
| One mesh, wheels baked in | Leave the wheel slot empty. Wheels become empty markers, nothing animates, the car drives fine. |
| A Scene the artist already assembled | The wizard does not ingest Scenes. Add a `CarController3D` yourself and assign the wheel/body nodes in *Car\|Nodes*. Whatever rotation you authored is preserved — the controller captures each wheel's base rotation at Start and applies spin and steer relative to it. |

Wheel mesh orientation: author it rolling about **X**, facing **+X** (the
right-hand side of the car). Engine convention is Y-up, forward = **-Z**.

## Wizard-built hierarchy

```
Car              CarController3D
├── Collider     Box3D, inherit-transform off, sized from the mesh AABB
├── Body         StaticMesh3D, re-centred if the mesh pivot is off-centre
├── Wheel_FL/FR/RL/RR
├── EngineAudio  Audio3D (assign a looping SoundWave yourself)
├── Smoke_L/R    Particle3D (assign a ParticleSystem yourself)
└── CarCamera    CarCamera3D
```

Dimensions are measured from the mesh's **actual vertex AABB**, not its bounds
sphere, then used to derive collision extents, wheelbase, track width, wheel
radius and ride height. Every derived value is overridable in the wizard before
you build, and every tuning value stays editable on the node afterwards.

Optionally saves the result as a Scene asset for reuse.

## Input

Bindings come from the engine's action system under the `Car` category
(`Throttle`, `Brake`, `SteerLeft`, `SteerRight`, `Handbrake`, `Boost`,
`ResetCar`). `Source/Defaults/Car.input.json` is installed to
`<Project>/InputActions.json` on first load if that file does not already exist —
existing bindings are never overwritten.

If no actions are configured, the controller falls back to raw input polling, so
the car is drivable immediately with no project setup.

## Lua API

Both nodes inherit every `Node3D` / `Camera3D` method. A script attached to the
car node can call these on `self`.

### `CarController3D` — queries

| Method | Returns |
| ------ | ------- |
| `GetSpeed()` | m/s along the heading, **signed** (negative = reversing) |
| `GetSpeedKph()` | km/h, always positive |
| `GetTopSpeed()` | *Top Speed* property, for normalising `GetSpeed()` |
| `GetRpm()` / `GetGear()` | Simulated rev counter and gear (1..Gear Count) |
| `IsDrifting()` | `true` while in a drift |
| `GetDriftAmount()` | 0..1, ramps in and out rather than flicking |
| `GetSlipAngle()` | Degrees between heading and travel direction |
| `IsGrounded()` | `true` when the ground probe hits |
| `GetBoost()` | Seconds of boost left in the meter |
| `GetBoostCapacity()` | *Boost Capacity*, for normalising `GetBoost()` |
| `GetVelocity()` | Three numbers: `local vx, vy, vz = car:GetVelocity()` |

### `CarController3D` — control

| Method | Effect |
| ------ | ------ |
| `SetInput{ throttle, brake, steer, handbrake, boost, reset }` | Drive the car from script. Omitted fields are 0. `steer` is -1 (left)..1 (right), the rest 0..1, `reset` a bool. |
| `ClearInput()` | Hand control back to the player's bindings. |
| `ResetTo(x, y, z [, yawDegrees])` | Teleport, zero all motion, refill boost. |
| `ResetInPlace()` | `ResetTo` the pose captured at `Start`. |

> **`SetInput` is sticky.** Once called, the car ignores the player until
> `ClearInput()` is called — it never expires on its own, and `ResetTo` /
> `ResetInPlace` do not clear it. Always pair them, including in `Destroy()`.

### `CarCamera3D`

| Method | Effect |
| ------ | ------ |
| `SetTargetCar(car)` | Follow a different `CarController3D`; `nil` detaches |
| `GetTargetCar()` | The car being followed |

```lua
local car = self:GetWorld():FindNode("Car")

Log.Debug(string.format("%.0f km/h, gear %d", car:GetSpeedKph(), car:GetGear()))

car:SetInput({ throttle = 1.0, steer = -0.5 })  -- AI / cutscene control
car:ClearInput()                                 -- player drives again
car:ResetInPlace()
```

## Examples

Ready-to-use scripts. `Scripts/` ships with the addon; `Examples/` is not
loaded automatically — copy a file into your project's `Scripts/` folder, then
attach it to a node. Each file's header comment lists its setup steps.

| Script | Attach to | Shows |
| ------ | --------- | ----- |
| [`Scripts/CarHud.lua`](Scripts/CarHud.lua) | A HUD widget | Speed / gear / drift text readout |
| [`Examples/CarBoostGauge.lua`](Examples/CarBoostGauge.lua) | A HUD widget | Speed and boost bars normalised with `GetTopSpeed` / `GetBoostCapacity` |
| [`Examples/CarAiDriver.lua`](Examples/CarAiDriver.lua) | The car | Waypoint-following AI via `SetInput`, with a clean `ClearInput` hand-back |
| [`Examples/CarRespawn.lua`](Examples/CarRespawn.lua) | The car | Checkpoint respawn with `ResetTo` when the car falls off or gets beached |
| [`Examples/CarRaceStart.lua`](Examples/CarRaceStart.lua) | Anything | 3-2-1-GO countdown lock, and switching the camera with `SetTargetCar` |

`CarHud.lua` is referenced from a project as
`Packages/com.polyphase.car.controller/CarHud.lua` (the addon's `Scripts/`
folder is the root).

### A minimal AI driver

The core of `CarAiDriver.lua` — steer toward a point on the ground plane:

```lua
function CarAiDriver:Tick(deltaTime)
    local car, tgt = self:GetWorldPosition(), self.target:GetWorldPosition()
    local dx, dz = tgt.x - car.x, tgt.z - car.z
    local len = math.sqrt(dx * dx + dz * dz)
    dx, dz = dx / len, dz / len

    -- The controller keeps the node's rotation equal to the car's heading,
    -- so its own right vector says which way to turn.
    local right = self:GetRightVector()
    local steer = math.max(-1, math.min(1, (dx * right.x + dz * right.z) * 2.5))

    self:SetInput({ throttle = 1.0, steer = steer })
end

function CarAiDriver:Destroy()
    self:ClearInput()   -- SetInput is sticky; always hand control back
end
```

Scripted input bypasses the player's *Invert X Input* setting, so an AI's
steering is never flipped by a player preference.

### Respawn at a checkpoint

```lua
local cp = self.lastCheckpoint
local p, r = cp:GetWorldPosition(), cp:GetWorldRotation()
self:ResetTo(p.x, p.y, p.z, r.y)    -- yaw in degrees; forward is -Z
```

### A boost bar

```lua
local t = car:GetBoost() / car:GetBoostCapacity()   -- 0..1
self.boostBar:SetWidth(self.fullWidth * t)
```

### Building a car by hand (no wizard)

1. Add a `CarController3D` (Add Node → 3D → Vehicles), named `Car`.
2. Add children: a `StaticMesh3D` body and four wheel nodes. Wheels roll about
   **X**; forward is **−Z**.
3. Assign them in **Car|Nodes**. The collider is created automatically from
   *Collision Half Extents*.
4. Add a `CarCamera3D` anywhere in the scene. It finds the car on its own.
5. Place the car so its origin sits *Ride Height* above the ground, and make
   sure the ground has collision enabled.

## Troubleshooting

### The car falls through everything / will not accelerate

Both symptoms have the same root: the car thinks it is airborne. Drive force is
zeroed in the air, so a car that never finds ground also never moves.

The controller now logs a warning after 2 seconds of never touching ground.
Switch on **`Car|Debug` → Debug Draw** to see the probe: the downward line turns
blue when it finds ground and stays grey when it does not.

Two things to check:

1. **`Car|Ground → Ground Collision Mask`** must include the group your level
   geometry is on. The editor puts every static mesh it spawns on **ColGroup1**,
   so a mask that excludes ColGroup1 makes the whole level invisible to the car.
   Leave all groups ticked unless you have a specific reason not to.
2. **The ground needs Collision enabled.** Physics can stay off; collision is
   what queries hit.

### The car mesh faces backwards

Set **`Car|Visuals` → Body Yaw Offset** to `180`, or tick **"Body mesh faces
backwards"** in the wizard. Engine forward is **−Z**; a mesh authored facing +Z
needs this.

Two things that look like they should work but don't:

- **Rotating the CarController3D node.** Its world rotation is rewritten from
  the heading every frame by `UpdateOrientation`, so any authored rotation is
  overwritten immediately. (It *is* read once at `Start` to seed the initial
  heading — so it changes which way the car sets off, not which way the mesh
  points.)
- **Rotating the mesh asset.** The wizard derives every dimension from the
  mesh's bounding box, and an AABB is unchanged by a 180° yaw — so nothing
  about the generated car differs.

**The wheels do not need swapping.** The steering pair sits at −Z, which is the
mechanical front and is already correct. Swapping them only moves the steering
to the visual rear. Fix the body yaw and leave the wheels alone.

Body Yaw Offset stacks on top of whatever rotation the Body node is authored
with, so it composes with a mesh that also needs a small trim.

### Collision geometry

The car's origin sits at wheel-contact level, one **Ride Height** above the
ground. The collision box is centred on its own node, so it is lifted by
`halfHeight - rideHeight + clearance` to put its underside just above the ride
plane. That split is deliberate:

- the **downward raycast** owns the ground,
- the **box sweep** owns walls only.

If the box were left centred on the origin it would rest the car `halfHeight`
above the ground rather than `rideHeight`, and when `halfHeight > rideHeight`
the grounded test could never pass. If it were flush with the ground it would
graze on every forward sweep and read as a wall.

**`Collider Ground Clearance`** (default 0.08) is that gap. Keep it above zero.
Raise it if the car catches on flat ground; lower it if the car climbs kerbs it
should hit.

### Wall response

Wall hits are sweep-and-slide with two tunables in `Car|Ground`:

- **`Wall Speed Loss`** (default 0.45): fraction of speed scrubbed on a fully
  head-on hit, scaled down for glancing contact. Applied per internal 120 Hz
  substep, so sustained head-on contact stops the car within a few frames.
- **`Wall Bounce`** (default 0.35, clamped 0–1): restitution. `0` is the legacy
  pure-slide behavior; `1` fully reflects a head-on hit. Also scaled by how
  square-on the impact is. Beyond reflecting the motion, a non-zero bounce
  deflects the car's *heading* toward the reflection and re-points the velocity
  along the post-hit direction — without those, a head-on car stayed pinned:
  the nose kept pointing into the wall, speed collapsed from the per-substep
  scrub, and yaw authority (which scales with speed) vanished, so it couldn't
  even steer free. AI drivers especially need this to glance off walls instead
  of deadlocking against them.

## Implementation notes

These are consequences of what Polyphase actually exports to addon DLLs. Each one
shaped the design, so they are worth knowing before editing this code.

- **Bullet is not exported.** `btBoxShape` cannot be constructed here, which is
  why collision lives on a `Box3D` child rather than on a shape owned by the
  controller.
- **`Box3D` and `Particle3D` are not exported.** Both are created via
  `CreateChild("TypeName")` and configured through property reflection, which
  routes through their `HandlePropChange` exactly as the inspector would.
- **The collider uses inherit-transform off.** `World::SweepTest` orients the
  swept shape by the primitive's *local* rotation, so local must equal world.
- **`Maths`, `Gizmos`, `Polyphase::AssetRefPicker` and `PlayerInputSystem` are
  not exported.** Hence the inline helpers in `CarMath.h`, debug drawing via
  `World::AddLine`, the hand-rolled drag-drop asset slot, and input going through
  the Lua `PlayerInput` bridge.
- **`FindRelativeNodePath` / `ResolveNodePath` are not exported**, so node
  references are re-bound after a PIE world clone by *name* lookup beneath the
  car. Clones preserve names, which is also why the wizard's naming convention
  matters.
- **There is no selection API for addons**, so the wizard cannot select the car
  it just created. It clears the selection and names the node instead.
- **ImGui is shared, not duplicated.** The engine exports it; the addon only
  binds to the editor's context and allocators (`CarAddonImgui.cpp`). Never
  compile `imgui*.cpp` into this addon.

## Building

`com.polyphase.car.controller.vcxproj` (Debug/Release x64) or CMake. Both pick up
new files automatically — CMake globs `Source/**`, and the editor regenerates the
vcxproj's file list by scanning `Source/` each time the project is opened.

### The `Directory.Build.targets` workaround

There is a bug in the generated addon project: its include list is built as
`$(PolyphasePath)Engine\Source` with no separator, while its linker list uses
`$(PolyphasePath)\Engine` with one. The generated `Directory.Build.props` supplies
the path *without* a trailing separator, so every include resolves to
`...polyphase-engineEngine\Source` and no engine header is found.

Both generated files are rewritten every time the project is opened, so patching
either does not stick. `Directory.Build.targets` at the project root normalises
the path instead; the editor does not generate that file, so the fix survives.

**The generator has now been fixed upstream** — `NativeAddonManager.cpp`
(`WriteVSProject`) emits `$(PolyphasePath)\Engine\Source` with a separator, matching
what its library-path list already did. That fix only takes effect once the
engine is rebuilt.

The `Directory.Build.targets` workaround is safe to keep either way (it just
yields a harmless doubled separator once the generator fix is live), and can be
deleted after an engine rebuild.
