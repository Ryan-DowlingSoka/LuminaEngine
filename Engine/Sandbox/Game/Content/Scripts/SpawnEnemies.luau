local Script = EntityScript.new()

function Script:Update(DeltaTime: number)
    for i = 1, 10 do
        local NewEntity = _Registry:Duplicate(2)
        local NewTransform = self.World:GetEntityTransform(NewEntity)

        NewTransform:SetLocalLocation(vector.create(
            math.random(-5000, 5000),
            math.random(50, 1500),
            math.random(-5000, 5000)))

        local Random = math.random(1, 5)
        NewTransform:SetLocalScale(vector.create(Random, Random, Random))
    end
end

return Script
