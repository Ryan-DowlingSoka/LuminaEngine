local DuplicateScript = EntityScript.new()

function DuplicateScript:OnUpdate(DeltaTime: number)

	for i = 1, 10 do
		local New = self._Registry:Duplicate(5)
		ECS.SetEntityLocation(self._Registry, New, Random.InSphere(500))
	end
end

return DuplicateScript