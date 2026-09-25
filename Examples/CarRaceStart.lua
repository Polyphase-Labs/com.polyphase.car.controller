-- CarRaceStart
--
-- A 3-2-1-GO start: holds the car on the line with the brake during the
-- countdown, then hands control back to the player. Also shows how to point a CarCamera3D at
-- a different car (e.g. a "rival cam" before the start).
--
-- Demonstrates the one rule of scripted input: car:SetInput() is STICKY. It
-- keeps overriding the player until car:ClearInput() is called -- it does not
-- expire by itself, and ResetTo/ResetInPlace do not clear it either.
--
-- Setup:
--   Attach to any node. Assign `car`, and optionally `camera`, `rival`, and a
--   Text widget for the countdown.

CarRaceStart = {}

function CarRaceStart:Create()
    self.car = nil
    self.camera = nil          -- CarCamera3D
    self.rival = nil           -- another CarController3D to show first
    self.countdownText = nil   -- Text widget
    self.countdown = 3.0
    self.rivalCamSeconds = 2.0

    self.time = 0.0
    self.released = false
end

function CarRaceStart:GatherProperties()
    return
    {
        { name = "car",             type = DatumType.Node },
        { name = "camera",          type = DatumType.Node },
        { name = "rival",           type = DatumType.Node },
        { name = "countdownText",   type = DatumType.Node },
        { name = "countdown",       type = DatumType.Float },
        { name = "rivalCamSeconds", type = DatumType.Float },
    }
end

function CarRaceStart:Start()
    if self.car == nil then
        Log.Warning("CarRaceStart: assign `car`.")
        return
    end

    -- Freeze the car: full brake, no throttle. The player is locked out from
    -- this point until ClearInput().
    self.car:SetInput({ brake = 1.0 })

    if self.camera ~= nil and self.rival ~= nil then
        self.camera:SetTargetCar(self.rival)
    end
end

function CarRaceStart:Tick(deltaTime)
    if self.car == nil or self.released then
        return
    end

    self.time = self.time + deltaTime

    -- Swing the chase camera back to the player's car after the rival shot.
    if self.camera ~= nil and self.time >= self.rivalCamSeconds then
        self.camera:SetTargetCar(self.car)
    end

    local total = self.rivalCamSeconds + self.countdown
    local remaining = total - self.time

    if remaining > 0.0 then
        if self.time >= self.rivalCamSeconds and self.countdownText ~= nil then
            self.countdownText:SetText(tostring(math.ceil(remaining)))
        end
        return
    end

    -- GO: give control back to the player's bindings.
    self.car:ClearInput()
    self.released = true

    if self.countdownText ~= nil then
        self.countdownText:SetText("GO!")
    end
end

function CarRaceStart:Destroy()
    -- Never leave the car locked if this node goes away mid-countdown.
    if self.car ~= nil and not self.released then
        self.car:ClearInput()
    end
end

return CarRaceStart
