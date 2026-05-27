local PlayerControllerScript: EntityScript = EntityScript.new()


local PC: SCharacterControllerComponent = nil

function PlayerControllerScript:OnReady()

	PC = self:GetComponent(SCharacterControllerComponent)

end

function PlayerControllerScript:OnUpdate(DeltaTime: number)
	
	local X: number = 0.0
	local Z: number = 0.0
	
	Z = Input.GetActionAxis("MoveY")
	X = Input.GetActionAxis("MoveX")
	
	
	PC:AddMovementInput(vector.create(X, 0.0, Z))
	
	
end


return PlayerControllerScript