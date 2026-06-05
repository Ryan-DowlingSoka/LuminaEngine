local Script: EntityScript = EntityScript.new()

--@export(Category = "Replicated", ClampMin = 0, ClampMax = 100)
--@replicated
Script.Health = 100

--@replicated(OwnerOnly)
Script.Ammo = 30

--@replicated
Script.Stats = { score = 0, combo = { count = 0, best = 0 } }

--@replicated(InitialOnly)
Script.Team = "Red"

function Script:OnReady()
    self.Timer = 0.0
        
end

function Script:OnUpdate(DeltaTime)
    if not World.Net:IsServer() then
        return
    end

    self.Timer = self.Timer + DeltaTime
    if self.Timer < 1.0 then
        return
    end
    self.Timer = self.Timer - 1.0

    -- Mutate replicated state on the server; nested-table edits replicate recursively.
    self.Health = math.max(0, self.Health - 5)
    self.Ammo   = self.Ammo - 1
    self.Stats.score        = self.Stats.score + 10
    self.Stats.combo.count  = self.Stats.combo.count + 1
    if self.Stats.combo.count > self.Stats.combo.best then
        self.Stats.combo.best = self.Stats.combo.count
    end

    -- One dirty mark collects + diffs every eligible replicated field this tick.
    World.Net:MarkDirty(self.Entity)
end

-- OnRep hooks run on the CLIENT after a replicated field is applied; `old` is the prior value.
function Script:OnRep_Health(old)
    
end

function Script:OnRep_Stats(old)
    -- React to the nested-table change here.
end

return Script
