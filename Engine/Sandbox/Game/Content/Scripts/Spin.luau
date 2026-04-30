local Script = EntityScript.new()

Script.Exports = {
    TimeScale = 10.0,
}

function Script:OnReady()
    if self.Exports.TimeScale == 0.0 then
        self.Exports.TimeScale = math.random(5.5, 15.0)
    end
end

function Script:Update(DeltaTime: number)
    local Spin = vector.create(0.0, self.Exports.TimeScale * DeltaTime, 0.0)
    self.Transform:AddRotationFromEuler(Spin)
    
    local x = 12
    local y = x
    local z = y + x
    
end

return Script
