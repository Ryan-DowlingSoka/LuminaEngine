local NewScript: EntityScript = EntityScript.new()

function NewScript:OnUpdate()

	local Foobar = self:GetParentScript()
	if Foobar then
		Foobar:Hello()
	end

end


function NewScript:Hello()

	print("Hello from: " .. self.Entity)

end


return NewScript