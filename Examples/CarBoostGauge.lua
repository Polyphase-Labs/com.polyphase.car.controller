-- CarBoostGauge
--
-- Speed and boost bars that fill left to right, plus a flash while drifting.
--
-- GetSpeed() and GetBoost() are in raw units (m/s and seconds of boost), so
-- this normalises them with GetTopSpeed() and GetBoostCapacity(). That keeps
-- the bars correct whatever preset or tuning the car uses.
--
-- Setup:
--   1. Attach this script to any Widget in your HUD.
--   2. Assign `speedBar` and `boostBar` (plain Quad widgets work well) and set
--      their width in the editor to the "full" length. That width is captured
--      at Start and treated as 100%.
--   3. Optionally assign `car`; otherwise the node named "Car" is used.

CarBoostGauge = {}

function CarBoostGauge:Create()
    self.car = nil
    self.speedBar = nil
    self.boostBar = nil
    self.driftColor = Vec(1.0, 0.55, 0.1, 1.0)
    self.boostColor = Vec(0.2, 0.7, 1.0, 1.0)
end

function CarBoostGauge:GatherProperties()
    return
    {
        { name = "car",        type = DatumType.Node },
        { name = "speedBar",   type = DatumType.Node },
        { name = "boostBar",   type = DatumType.Node },
        { name = "driftColor", type = DatumType.Color },
        { name = "boostColor", type = DatumType.Color },
    }
end

function CarBoostGauge:Start()
    if self.car == nil then
        self.car = self:GetWorld():FindNode("Car")
    end

    if self.speedBar ~= nil then self.speedFull = self.speedBar:GetWidth() end
    if self.boostBar ~= nil then self.boostFull = self.boostBar:GetWidth() end
end

function CarBoostGauge:Tick(deltaTime)
    local car = self.car
    if car == nil then
        return
    end

    if self.speedBar ~= nil then
        -- Boost can push past top speed, so clamp the bar at full.
        local t = math.min(math.abs(car:GetSpeed()) / car:GetTopSpeed(), 1.0)
        self.speedBar:SetWidth(self.speedFull * t)
    end

    if self.boostBar ~= nil then
        local t = car:GetBoost() / car:GetBoostCapacity()
        self.boostBar:SetWidth(self.boostFull * t)

        if car:IsDrifting() then
            self.boostBar:SetColor(self.driftColor)
        else
            self.boostBar:SetColor(self.boostColor)
        end
    end
end

return CarBoostGauge
