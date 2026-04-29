declare Input: {
	IsKeyDown: (number) -> boolean,
	IsKeyUp: (number) -> boolean,
	IsKeyPressed: (number) -> boolean,
	IsKeyRepeated: (number) -> boolean,
	GetMouseDeltaX: () -> number,
	GetMouseDeltaY: () -> number,
	GetMouseX: () -> number,
	GetMouseY: () -> number,
	GetMouseZ: () -> number,
}

declare World: {

}

-- Directed entity-to-entity messaging. The receiver declares a method
-- whose name starts with "On" -- e.g. `function self:OnDamage(payload)`
-- -- and `Messages:Send(target, "OnDamage", payload)` invokes it.
declare Messages: {
	Send: (self: any, Target: number, Name: string, Payload: any) -> (),
	SendDeferred: (self: any, Target: number, Name: string, Payload: any) -> (),
}

-- Broadcast/anonymous pub-sub. Use for game-wide signals where the
-- sender doesn't know (or care) who's listening.
declare Events: {
	Subscribe: (self: any, Name: string, Callback: (payload: any) -> ()) -> (),
	SubscribeEntity: (self: any, Owner: number, Name: string, Callback: (payload: any) -> ()) -> (),
	Unsubscribe: (self: any, Name: string, Callback: (payload: any) -> ()) -> (),
	Dispatch: (self: any, Name: string, Payload: any) -> (),
	DispatchDeferred: (self: any, Name: string, Payload: any) -> (),
	ClearEvent: (self: any, Name: string) -> (),
	GetSubscriberCount: (self: any, Name: string) -> number,
}