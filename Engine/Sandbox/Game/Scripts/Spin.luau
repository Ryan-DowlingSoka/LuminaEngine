NewScript = {}

local Exports = {
    TimeScale = 10.0,
}

NewScript.Exports = Exports

function NewScript:OnAttach()
end

function NewScript:OnReady()
    if Exports.TimeScale == 0.0 then
        Exports.TimeScale = math.random(5.5, 15.0)
    end
end

function NewScript:Update(DeltaTime: number)
    local Spin = vector.create(0.0, Exports.TimeScale * DeltaTime, 0.0)
    self.Transform:AddRotationFromEuler(Spin)
end

function NewScript:OnDetach()
end

return NewScript