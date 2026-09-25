-- CarRespawn
--
-- Puts the car back on the track when it falls off the world or is left
-- hanging in the air (beached on a kerb, balanced on a wall top),
-- at the last checkpoint it passed rather than the spawn point.
--
-- Setup:
--   1. Attach this script to the CarController3D node.
--   2. Optionally make a Node3D called "Checkpoints" whose children are placed
--      along the track. Each child's position and Y rotation (yaw) is the
--      respawn pose. Without checkpoints, it falls back to car:ResetInPlace()
--      (the pose the car had when play started).

CarRespawn = {}

function CarRespawn:Create()
    self.checkpoints = nil
    self.killY = -50.0          -- below this height the car is respawned
    self.checkpointRadius = 12.0
    self.airSeconds = 4.0       -- ungrounded for longer than this = beached

    self.lastCheckpoint = nil
    self.airTime = 0.0
end

function CarRespawn:GatherProperties()
    return
    {
        { name = "checkpoints",      type = DatumType.Node },
        { name = "killY",            type = DatumType.Float },
        { name = "checkpointRadius", type = DatumType.Float },
        { name = "airSeconds",       type = DatumType.Float },
    }
end

function CarRespawn:Start()
    if self.checkpoints == nil then
        self.checkpoints = self:GetWorld():FindNode("Checkpoints")
    end
end

function CarRespawn:Respawn()
    local cp = self.lastCheckpoint
    if cp ~= nil then
        local pos = cp:GetWorldPosition()
        local rot = cp:GetWorldRotation()       -- Euler degrees
        -- ResetTo zeroes velocity, drift state and refills the boost meter.
        self:ResetTo(pos.x, pos.y, pos.z, rot.y)
    else
        self:ResetInPlace()
    end
    self.airTime = 0.0
end

function CarRespawn:Tick(deltaTime)
    local pos = self:GetWorldPosition()

    -- Track the most recent checkpoint we drove through.
    if self.checkpoints ~= nil then
        for i = 1, self.checkpoints:GetNumChildren() do   -- GetChild is 1-based
            local cp = self.checkpoints:GetChild(i)
            local cpPos = cp:GetWorldPosition()
            local dx = cpPos.x - pos.x
            local dz = cpPos.z - pos.z
            if dx * dx + dz * dz < self.checkpointRadius * self.checkpointRadius then
                self.lastCheckpoint = cp
            end
        end
    end

    -- Fell off the world.
    if pos.y < self.killY then
        self:Respawn()
        return
    end

    -- Never landed again. Long jumps are fine; nothing legitimate stays
    -- airborne for several seconds on a race track.
    if self:IsGrounded() then
        self.airTime = 0.0
    else
        self.airTime = self.airTime + deltaTime
        if self.airTime > self.airSeconds then
            self:Respawn()
        end
    end
end

return CarRespawn
