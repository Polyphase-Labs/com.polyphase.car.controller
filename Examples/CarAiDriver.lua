-- CarAiDriver
--
-- Drives a CarController3D around a loop of waypoints using car:SetInput().
--
-- Setup:
--   1. Build a car with Tools -> Car -> Setup Wizard (or place a CarController3D).
--   2. Make an empty Node3D called "Waypoints" and give it Node3D children
--      laid out around the track, in driving order.
--   3. Attach this script to the car node itself, and assign `waypoints`.
--
-- The player's input is ignored while this script drives. Set `enabled` false
-- (or destroy the node) and control is handed back via car:ClearInput().

CarAiDriver = {}

function CarAiDriver:Create()
    self.waypoints = nil       -- Node3D whose children are the waypoints
    self.enabled = true
    self.arriveRadius = 8.0    -- metres; advance to the next waypoint inside this
    self.cruiseKph = 140.0     -- target speed on straights
    self.cornerKph = 70.0      -- target speed into a sharp corner
    self.steerGain = 2.5       -- higher = snappier steering

    self.index = 1             -- GetChild is 1-based
    self.driving = false
end

function CarAiDriver:GatherProperties()
    return
    {
        { name = "waypoints",    type = DatumType.Node },
        { name = "enabled",      type = DatumType.Bool },
        { name = "arriveRadius", type = DatumType.Float },
        { name = "cruiseKph",    type = DatumType.Float },
        { name = "cornerKph",    type = DatumType.Float },
        { name = "steerGain",    type = DatumType.Float },
    }
end

-- Resolve the waypoints in Start, not Create: editor-assigned properties are
-- not uploaded to the script table until after Create has run.
function CarAiDriver:Start()
    if self.waypoints == nil then
        self.waypoints = self:GetWorld():FindNode("Waypoints")
    end

    if self.waypoints == nil or self.waypoints:GetNumChildren() == 0 then
        Log.Warning("CarAiDriver: no waypoints. Assign a node whose children are the track points.")
        self.enabled = false
    end
end

local function Clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

function CarAiDriver:Tick(deltaTime)
    if not self.enabled then
        -- SetInput is sticky: the car keeps its last input forever unless we
        -- explicitly hand control back.
        if self.driving then
            self:ClearInput()
            self.driving = false
        end
        return
    end

    local count = self.waypoints:GetNumChildren()
    local target = self.waypoints:GetChild(self.index)

    -- Work on the ground plane (x/z) with plain numbers. Engine Vectors are
    -- 4-component, so Length()/Dot() would also fold in `w`.
    local carPos = self:GetWorldPosition()
    local tgtPos = target:GetWorldPosition()
    local dx = tgtPos.x - carPos.x
    local dz = tgtPos.z - carPos.z
    local dist = math.sqrt(dx * dx + dz * dz)

    if dist < self.arriveRadius then
        self.index = (self.index % count) + 1
        return
    end

    dx = dx / dist
    dz = dz / dist

    -- The controller rewrites the root node's rotation from its heading every
    -- frame, so the node's own forward/right vectors are the car's heading.
    local forward = self:GetForwardVector()
    local right = self:GetRightVector()

    local sideways = dx * right.x + dz * right.z       -- -1 = hard left, +1 = hard right
    local ahead = dx * forward.x + dz * forward.z      --  1 = dead ahead

    -- Slow down for corners in proportion to how far off-nose the target is.
    local cornerness = Clamp(1.0 - ahead, 0.0, 1.0)
    local targetKph = self.cruiseKph + (self.cornerKph - self.cruiseKph) * cornerness
    local kph = self:GetSpeedKph()

    local throttle = 0.0
    local brake = 0.0
    if kph < targetKph then
        throttle = 1.0
    elseif kph > targetKph + 15.0 then
        brake = 0.6
    end

    -- Target behind us: full lock and a dab of handbrake swings the tail round.
    local handbrake = 0.0
    if ahead < 0.0 and kph > 30.0 then
        handbrake = 1.0
    end

    self:SetInput({
        throttle  = throttle,
        brake     = brake,
        steer     = Clamp(sideways * self.steerGain, -1.0, 1.0),
        handbrake = handbrake,
    })
    self.driving = true
end

function CarAiDriver:Destroy()
    if self.driving then
        self:ClearInput()
    end
end

return CarAiDriver
