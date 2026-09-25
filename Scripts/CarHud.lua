-- CarHud
--
-- Minimal speed / gear / drift readout for a CarController3D.
--
-- Attach to any Widget node in your HUD, then either assign `car` in the
-- inspector or leave it empty and let the script find the first CarController3D
-- in the scene.
--
-- Referenced from a project as:
--   Packages/com.polyphase.car.controller/CarHud.lua

CarHud = {}

-- Script-exposed fields. `car` is optional; auto-discovery covers the common case.
CarHud.car = nil
CarHud.speedLabel = nil
CarHud.gearLabel = nil
CarHud.driftLabel = nil

local function FindCar(self)
    if self.car ~= nil then
        return self.car
    end

    -- Walk out to the scene root, then search down for the controller.
    local node = self:GetParent()
    while node ~= nil and node:GetParent() ~= nil do
        node = node:GetParent()
    end

    if node == nil then
        return nil
    end

    return node:FindChild("Car", true)
end

function CarHud:Start()
    self.resolvedCar = FindCar(self)

    if self.resolvedCar == nil then
        Log.Warning("CarHud: no CarController3D found. Assign 'car' in the inspector.")
    end
end

function CarHud:Tick(deltaTime)
    local car = self.resolvedCar
    if car == nil then
        return
    end

    -- GetSpeedKph is always positive; sign comes from GetSpeed.
    local kph = car:GetSpeedKph()
    local reversing = car:GetSpeed() < -0.5

    if self.speedLabel ~= nil then
        if reversing then
            self.speedLabel:SetText(string.format("%d km/h  (R)", math.floor(kph)))
        else
            self.speedLabel:SetText(string.format("%d km/h", math.floor(kph)))
        end
    end

    if self.gearLabel ~= nil then
        if reversing then
            self.gearLabel:SetText("R")
        else
            self.gearLabel:SetText(tostring(car:GetGear()))
        end
    end

    if self.driftLabel ~= nil then
        if car:IsDrifting() then
            -- GetDriftAmount ramps 0..1, so this reads as the slide building up
            -- and easing back out rather than flicking on and off.
            self.driftLabel:SetText(string.format("DRIFT %d%%", math.floor(car:GetDriftAmount() * 100)))
        else
            self.driftLabel:SetText("")
        end
    end
end

return CarHud
