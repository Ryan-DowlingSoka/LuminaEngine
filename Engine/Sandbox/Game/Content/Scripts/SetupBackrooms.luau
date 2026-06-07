local Script: EntityScript = EntityScript.new()


function Script:OnReady()

    local LightEntity = World.FindByTag("Light")
    local PillarEntity = World.FindByTag("Pillar")

	math.randomseed(os.time())


	local TestMaterial: CMaterial = Asset.Hard("/Game/Materials/M_BackroomsFloor")
	local TestEntity = 3145734	
	local StaticMeshComp: SStaticMeshComponent = World.GetComponent(TestEntity, SStaticMeshComponent)
	StaticMeshComp:SetMaterialAtSlot(TestMaterial, 0)

    for x = -100, 100, 10 do
        for z = -100, 100, 10 do
        	if math.random() < 0.3 then
            	local l = World.Duplicate(LightEntity)
            	World.SetLocation(l, Vec3(x, 11, z))
            end
        end
    end
    
    for x = -100, 100, 10 do
        for z = -100, 100, 10 do
        	if math.random() < 0.2 then
            	local l = World.Duplicate(PillarEntity)            
            	World.SetLocation(l, Vec3(x, 6, z))
            	World.SetScale(l, Vec3(math.random(1, 8), 5, 0.25))
            	World.SetRotationFromEuler(l, Vec3(0, math.random(-1, 1) * 90, 0))
            end
        end
    end
end

return Script