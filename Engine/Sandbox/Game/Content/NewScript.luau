local DuplicateScript = EntityScript.new()

function DuplicateScript:OnUpdate(DeltaTime: number)
	local New = self._Registry:Duplicate(5)
	ECS.SetEntityLocation(self._Registry, New, Random.InSphere(500))
end

return DuplicateScript