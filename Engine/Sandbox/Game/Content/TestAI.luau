TestAI = EntityScript.new()

local PathFollow : SPathFollowComponent = nil

function TestAI:OnReady()
	PathFollow = self:GetComponent(SPathFollowComponent)
	PathFollow:SetTargetLocation(vector.create(math.random(-100, 100), 0, math.random(-100, 100)))
	
end

function TestAI:OnUpdate()
	if PathFollow:IsAtDestination() then
		PathFollow:SetTargetLocation(vector.create(math.random(-100, 100), 0, math.random(-100, 100)))
		
		if self.World:GetNumEntities() > 150 then
			return
		end
		
		self._Registry:Duplicate(self.Entity)
	end
end


return TestAI