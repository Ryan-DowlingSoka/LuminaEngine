local TestAI: EntityScript = EntityScript.new()

local PathFollow: SPathFollowComponent = nil

function TestAI:OnReady()
    PathFollow = self:GetComponent(SPathFollowComponent)
    PathFollow:SetTargetLocation(vector.create(math.random(-100, 100), 0, math.random(-100, 100)))
end

function TestAI:OnUpdate()
    if not PathFollow:IsAtDestination() then return end

    PathFollow:SetTargetLocation(vector.create(math.random(-100, 100), 0, math.random(-100, 100)))

    if self.World:GetNumEntities() < 150 then
        self:Clone()
    end
end

return TestAI
