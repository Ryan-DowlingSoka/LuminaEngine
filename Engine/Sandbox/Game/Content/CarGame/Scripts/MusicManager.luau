
local Random = require("Stdlib/Random")

local MusicManager = EntityScript.new()


MusicManager.Exports = {
	Music = { "", "" }
}

function MusicManager:OnReady()
    local Pick = Random.Pick(self.Exports.Music)
   
    self.MusicHandle = Audio.PlaySound2DEx(Pick, 0.15, 1.0, true)
end

function MusicManager:OnDetach()
    if self.MusicHandle and self.MusicHandle:IsValid() then
        self.MusicHandle:FadeOut()
    end
end

return MusicManager