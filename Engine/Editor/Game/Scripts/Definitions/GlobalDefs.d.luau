
declare CAnimation: { }
declare class CAnimation
	Skeleton: CSkeleton
end

declare CMaterialNodeGraph: { }
declare class CMaterialNodeGraph
end

declare EWorldType: number

declare CMaterialGraphNode: { }
declare class CMaterialGraphNode
end

declare SAudioSystem: { }
declare class SAudioSystem
end

declare SDefaultWorldSettings: { }
declare class SDefaultWorldSettings
	WorldKillHeight: number
	GravityScale: number
	DeltaTimeScale: number
	GravityDirection: vec3
	PhysicsHz: number
	MaxPhysicsSteps: number
	bEnablePhysicsInterpolation: boolean
	bDeterministicSimulation: boolean
	NumVelocitySteps: number
	NumPositionSteps: number
	BaumgarteStabilizationFactor: number
	SpeculativeContactDistance: number
	PenetrationSlop: number
	MaxPenetrationDistance: number
	LinearCastThreshold: number
	LinearCastMaxPenetration: number
	ManifoldTolerance: number
	MinVelocityForRestitution: number
	TimeBeforeSleep: number
	SleepVelocityThreshold: number
	BodyPairCacheMaxDeltaPositionSq: number
	BodyPairCacheCosMaxDeltaRotationDiv2: number
	ContactNormalCosMaxDeltaRotation: number
	ContactPointPreserveLambdaMaxDistSq: number
	MaxInFlightBodyPairs: number
	StepListenersBatchSize: number
	StepListenerBatchesPerJob: number
	bConstraintWarmStart: boolean
	bUseBodyPairContactCache: boolean
	bUseManifoldReduction: boolean
	bUseLargeIslandSplitter: boolean
	bAllowSleeping: boolean
	bCheckActiveEdges: boolean
end

declare ECollisionProfiles: number
declare FCollisionProfile: { }
declare class FCollisionProfile
	Layer: number
	Mask: number
end
declare EMoveMode: number
declare EBodyType: number

declare CMesh: { }
declare class CMesh
	BoundingBox: FAABB
	function GetNumVertices(self): any
	function GetNumIndices(self): any
	function GetAABB(self): any
end

declare SScriptComponent: { }
declare class SScriptComponent
	ScriptPath: FScriptPath
end

declare SBillboardComponent: { }
declare class SBillboardComponent
	Texture: CTexture
	Scale: number
	Tint: vec4
end

declare CMaterial: { }
declare class CMaterial
	MaterialType: number
	BlendMode: number
	ShadingModel: number
	bCastShadows: boolean
	bTwoSided: boolean
	bDisableDepthTest: boolean
	OpacityMaskClipValue: number
end

declare CEdNodeGraph: { }
declare class CEdNodeGraph
	GraphSaveData: string
end

declare ECustomPrimitiveDataType: number
declare SCustomPrimitiveData: { }
declare class SCustomPrimitiveData
	Type: number
	function AsFloat(self): any
	function AsInt(self): any
	function AsBool(self): any
	function AsFloat4(self): any
	function SetAsFloat(self, X: number)
	function SetAsInt(self, X: number)
	function SetAsBool(self, X: boolean)
	function SetAsFloat4(self, Bytes: vec4)
end

declare SSkeletalMeshComponent: { }
declare class SSkeletalMeshComponent
	SkeletalMesh: CSkeletalMesh
	bCastShadow: boolean
	bReceiveShadow: boolean
	function GetAABB(self): any
end

declare CAudioStreamFactory: { }
declare class CAudioStreamFactory
end

declare CParticleGraphNode: { }
declare class CParticleGraphNode
end

declare SHealthComponent: { }
declare class SHealthComponent
	Health: number
	MaxHealth: number
	function ApplyDamage(self, Damage: number): number
	function GiveHealth(self, NewHealth: number)
end

declare CMaterialExpression: { }
declare class CMaterialExpression
	bDynamic: boolean
end
declare CMaterialExpression_Math: { }
declare class CMaterialExpression_Math
	ConstA: number
	ConstB: number
end
declare CMaterialExpression_Addition: { }
declare class CMaterialExpression_Addition
end
declare CMaterialExpression_Clamp: { }
declare class CMaterialExpression_Clamp
end
declare CMaterialExpression_Saturate: { }
declare class CMaterialExpression_Saturate
end
declare CMaterialExpression_Normalize: { }
declare class CMaterialExpression_Normalize
end
declare CMaterialExpression_Distance: { }
declare class CMaterialExpression_Distance
end
declare CMaterialExpression_Abs: { }
declare class CMaterialExpression_Abs
end
declare CMaterialExpression_SmoothStep: { }
declare class CMaterialExpression_SmoothStep
	X: number
end
declare CMaterialExpression_Subtraction: { }
declare class CMaterialExpression_Subtraction
end
declare CMaterialExpression_Multiplication: { }
declare class CMaterialExpression_Multiplication
end
declare CMaterialExpression_Sin: { }
declare class CMaterialExpression_Sin
end
declare CMaterialExpression_Cosin: { }
declare class CMaterialExpression_Cosin
end
declare CMaterialExpression_Floor: { }
declare class CMaterialExpression_Floor
end
declare CMaterialExpression_Fract: { }
declare class CMaterialExpression_Fract
end
declare CMaterialExpression_Ceil: { }
declare class CMaterialExpression_Ceil
end
declare CMaterialExpression_Power: { }
declare class CMaterialExpression_Power
end
declare CMaterialExpression_Mod: { }
declare class CMaterialExpression_Mod
end
declare CMaterialExpression_Min: { }
declare class CMaterialExpression_Min
end
declare CMaterialExpression_Max: { }
declare class CMaterialExpression_Max
end
declare CMaterialExpression_Step: { }
declare class CMaterialExpression_Step
end
declare CMaterialExpression_Lerp: { }
declare class CMaterialExpression_Lerp
	Alpha: number
end
declare CMaterialExpression_Division: { }
declare class CMaterialExpression_Division
end
declare CMaterialExpression_ComponentMask: { }
declare class CMaterialExpression_ComponentMask
	R: boolean
	G: boolean
	B: boolean
	A: boolean
end
declare CMaterialExpression_Append: { }
declare class CMaterialExpression_Append
end
declare CMaterialExpression_MakeFloat2: { }
declare class CMaterialExpression_MakeFloat2
end
declare CMaterialExpression_MakeFloat3: { }
declare class CMaterialExpression_MakeFloat3
end
declare CMaterialExpression_MakeFloat4: { }
declare class CMaterialExpression_MakeFloat4
end
declare CMaterialExpression_BreakFloat2: { }
declare class CMaterialExpression_BreakFloat2
end
declare CMaterialExpression_BreakFloat3: { }
declare class CMaterialExpression_BreakFloat3
end
declare CMaterialExpression_BreakFloat4: { }
declare class CMaterialExpression_BreakFloat4
end
declare CMaterialExpression_WorldPos: { }
declare class CMaterialExpression_WorldPos
end
declare CMaterialExpression_CameraPos: { }
declare class CMaterialExpression_CameraPos
end
declare CMaterialExpression_EntityID: { }
declare class CMaterialExpression_EntityID
end
declare CMaterialExpression_VertexNormal: { }
declare class CMaterialExpression_VertexNormal
end
declare CMaterialExpression_Panner: { }
declare class CMaterialExpression_Panner
	SpeedX: number
	SpeedY: number
end
declare CMaterialExpression_TexCoords: { }
declare class CMaterialExpression_TexCoords
	TextureIndex: number
	UTiling: number
	VTiling: number
end
declare CMaterialExpression_TerrainLayerWeight: { }
declare class CMaterialExpression_TerrainLayerWeight
	LayerIndex: number
end
declare CMaterialExpression_TerrainLayerWeights: { }
declare class CMaterialExpression_TerrainLayerWeights
end
declare CMaterialExpression_TerrainLayerBlend: { }
declare class CMaterialExpression_TerrainLayerBlend
end
declare CMaterialExpression_Constant: { }
declare class CMaterialExpression_Constant
	ParameterName: string
	Value: vec4
end
declare CMaterialExpression_ConstantFloat: { }
declare class CMaterialExpression_ConstantFloat
end
declare CMaterialExpression_ConstantFloat2: { }
declare class CMaterialExpression_ConstantFloat2
end
declare CMaterialExpression_ConstantFloat3: { }
declare class CMaterialExpression_ConstantFloat3
end
declare CMaterialExpression_ConstantFloat4: { }
declare class CMaterialExpression_ConstantFloat4
end

declare SSpringArmComponent: { }
declare class SSpringArmComponent
	TargetArmLength: number
	SocketOffset: vec3
	ProbeSize: number
	bDoCollisionTest: boolean
	bUseControlRotation: boolean
end

declare CMaterialInput: { }
declare class CMaterialInput
end

declare CAudioStream: { }
declare class CAudioStream
end

declare ESineWaveType: number

declare SDisabledTag: { }
declare class SDisabledTag
end

declare CEdNode_Reroute: { }
declare class CEdNode_Reroute
end

declare FScriptPath: { }
declare class FScriptPath
	Path: string
end

declare FTransform: { }
declare class FTransform
	Location: vec3
	Rotation: quat
	Scale: vec3
end

declare CThumbnailManager: { }
declare class CThumbnailManager
	CubeMesh: CStaticMesh
	SphereMesh: CStaticMesh
	PlaneMesh: CStaticMesh
end

declare vec2: { }
declare class vec2
	x: number
	y: number
end
declare vec3: { }
declare class vec3
	x: number
	y: number
	z: number
end
declare vec4: { }
declare class vec4
	x: number
	y: number
	z: number
	w: number
end
declare quat: { }
declare class quat
	x: number
	y: number
	z: number
	w: number
end

declare CPrefabFactory: { }
declare class CPrefabFactory
end

declare SInterpolatingMovementComponent: { }
declare class SInterpolatingMovementComponent
	Start: FTransform
	End: FTransform
	Speed: number
end
declare SSineWaveMovementComponent: { }
declare class SSineWaveMovementComponent
	Axis: vec3
	Amplitude: number
	Frequency: number
	PhaseOffset: number
	bEnableSecondaryAxis: boolean
	SecondaryAxis: vec3
	SecondaryAmplitude: number
	SecondaryFrequency: number
	SecondaryPhaseOffset: number
	WaveType: number
	bDampingEnabled: boolean
	DampingFactor: number
	bAmplitudeModulation: boolean
	AmplitudeModulationFrequency: number
	bUseRelativeMovement: boolean
	bAutoStart: boolean
	StartDelay: number
end

declare EMaterialParameterType: number
declare FMaterialParameter: { }
declare class FMaterialParameter
	ParameterName: string
	Type: number
	Index: number
end

declare CTexture: { }
declare class CTexture
end

declare SEnvironmentComponent: { }
declare class SEnvironmentComponent
	AmbientColor: vec3
	Intensity: number
end

declare CEdNodeGraphPin: { }
declare class CEdNodeGraphPin
end

declare SMeshComponent: { }
declare class SMeshComponent
	CustomPrimitiveData: SCustomPrimitiveData
	MaxDrawDistance: number
	BoundsScale: number
	bUseAsOccluder: boolean
	bIgnoreOcclusionCulling: boolean
	bCastShadow: boolean
	bReceiveShadow: boolean
end

declare CMaterialOutput: { }
declare class CMaterialOutput
end

declare STagComponent: { }
declare class STagComponent
	Tag: string
end

declare SCharacterControllerComponent: { }
declare class SCharacterControllerComponent
	MoveInput: vec3
	LookInput: vec2
	PitchClamp: vec2
	bJumpPressed: boolean
	function AddMovementInput(self, Move: vec3)
	function AddLookInput(self, Look: vec2)
	function AddPitch(self, Degrees: number)
	function AddYaw(self, Degrees: number)
	function GetLookForward(self): any
	function GetLookRight(self): any
	function Jump(self): any
end

declare SUpdateTransformEntitySystem: { }
declare class SUpdateTransformEntitySystem
end

declare CParticleExpression_ConstantFloat: { }
declare class CParticleExpression_ConstantFloat
	Value: number
end
declare CParticleExpression_ConstantFloat3: { }
declare class CParticleExpression_ConstantFloat3
	Value: vec3
end
declare CParticleExpression_ConstantFloat4: { }
declare class CParticleExpression_ConstantFloat4
	Value: vec4
end
declare CParticleExpression_Time: { }
declare class CParticleExpression_Time
end
declare CParticleExpression_ParticleAge: { }
declare class CParticleExpression_ParticleAge
end
declare CParticleExpression_LifeRatio: { }
declare class CParticleExpression_LifeRatio
end
declare CParticleExpression_BinaryOp: { }
declare class CParticleExpression_BinaryOp
end
declare CParticleExpression_Add: { }
declare class CParticleExpression_Add
end
declare CParticleExpression_Subtract: { }
declare class CParticleExpression_Subtract
end
declare CParticleExpression_Multiply: { }
declare class CParticleExpression_Multiply
end
declare CParticleExpression_Divide: { }
declare class CParticleExpression_Divide
end
declare CParticleExpression_Lerp: { }
declare class CParticleExpression_Lerp
end
declare CParticleExpression_Saturate: { }
declare class CParticleExpression_Saturate
end
declare CParticleExpression_Sin: { }
declare class CParticleExpression_Sin
end
declare CParticleExpression_Cos: { }
declare class CParticleExpression_Cos
end
declare CParticleExpression_Normalize: { }
declare class CParticleExpression_Normalize
end
declare CParticleExpression_MakeFloat3: { }
declare class CParticleExpression_MakeFloat3
end
declare CParticleExpression_MakeFloat4: { }
declare class CParticleExpression_MakeFloat4
end

declare STerrainLayer: { }
declare class STerrainLayer
	UVScale: number
	Name: string
end
declare STerrainComponent: { }
declare class STerrainComponent
	Material: CMaterialInterface
	Resolution: number
	ChunkResolution: number
	TileWorldSize: number
	MaxHeight: number
	bCastShadow: boolean
	bReceiveShadow: boolean
end

declare CParticleOutputNode: { }
declare class CParticleOutputNode
end

declare EUpdateStage: number

declare CGameInstance: { }
declare class CGameInstance
end

declare CComponentVisualizerRegistry: { }
declare class CComponentVisualizerRegistry
end
declare CComponentVisualizer: { }
declare class CComponentVisualizer
end
declare CComponentVisualizer_PointLight: { }
declare class CComponentVisualizer_PointLight
end
declare CComponentVisualizer_SpotLight: { }
declare class CComponentVisualizer_SpotLight
end
declare CComponentVisualizer_SphereCollider: { }
declare class CComponentVisualizer_SphereCollider
end
declare CComponentVisualizer_BoxCollider: { }
declare class CComponentVisualizer_BoxCollider
end
declare CComponentVisualizer_CharacterPhysics: { }
declare class CComponentVisualizer_CharacterPhysics
end
declare CComponentVisualizer_Camera: { }
declare class CComponentVisualizer_Camera
end

declare EKey: number

declare SLifetimeSystem: { }
declare class SLifetimeSystem
end

declare FAABB: { }
declare class FAABB
	Min: vec3
	Max: vec3
	function MaxScale(self): any
	function GetSize(self): any
	function GetCenter(self): any
end

declare CWorldFactory: { }
declare class CWorldFactory
end

declare SNameComponent: { }
declare class SNameComponent
	Name: string
end

declare CStaticMesh: { }
declare class CStaticMesh
end

declare SRigidBodyComponent: { }
declare class SRigidBodyComponent
	BodyID: number
	Mass: number
	CollisionProfile: FCollisionProfile
	NumVelocityStepsOverride: number
	NumPositionStepsOverride: number
	MaxLinearVelocity: number
	MaxAngularVelocity: number
	RestitutionOverride: number
	FrictionOverride: number
	LinearDamping: number
	AngularDamping: number
	MotionQualityLevel: number
	BodyType: number
	bIsSensor: boolean
	bUseManifoldReduction: boolean
	bApplyGyroscopicForce: boolean
	bAllowSleeping: boolean
	bUseGravity: boolean
end
declare SBoxColliderComponent: { }
declare class SBoxColliderComponent
	HalfExtent: vec3
	TranslationOffset: vec3
	RotationOffset: vec3
end
declare SSphereColliderComponent: { }
declare class SSphereColliderComponent
	Radius: number
	TranslationOffset: vec3
end

declare SInputComponent: { }
declare class SInputComponent
end

declare CMaterialOutputNode: { }
declare class CMaterialOutputNode
end

declare CSkeletalMesh: { }
declare class CSkeletalMesh
	Skeleton: CSkeleton
end

declare CMeshFactory: { }
declare class CMeshFactory
end

declare SStaticMeshComponent: { }
declare class SStaticMeshComponent
	StaticMesh: CStaticMesh
	function GetAABB(self): any
	function SetStaticMesh(self, InMesh: CStaticMesh)
	function GetStaticMesh(self): any
end

declare CMaterialExpression_TextureSample: { }
declare class CMaterialExpression_TextureSample
	Texture: CTexture
end

declare CMaterialExpression_CustomPrimitiveData: { }
declare class CMaterialExpression_CustomPrimitiveData
	Type: number
end

declare SLifetimeComponent: { }
declare class SLifetimeComponent
	Lifetime: number
end

declare CParticleInput: { }
declare class CParticleInput
end
declare CParticleOutput: { }
declare class CParticleOutput
end

declare ETerrainBrushMode: number
declare STerrainBrushComponent: { }
declare class STerrainBrushComponent
	Mode: number
	Radius: number
	Strength: number
	Falloff: number
	FlattenHeight: number
	ActiveLayer: number
end

declare SSimpleAnimationSystem: { }
declare class SSimpleAnimationSystem
end

declare SRayResult: { }
declare class SRayResult
	BodyID: number
	Entity: number
	Start: vec3
	End: vec3
	Location: vec3
	Normal: vec3
	Fraction: number
end
declare SRayCastSettings: { }
declare class SRayCastSettings
	Start: vec3
	End: vec3
	bDrawDebug: boolean
	DebugDuration: number
	DebugHitColor: vec3
	DebugMissColor: vec3
	LayerMask: number
end
declare SSphereCastSettings: { }
declare class SSphereCastSettings
	Start: vec3
	End: vec3
	Radius: number
	bDrawDebug: boolean
	DebugDuration: number
	DebugHitColor: vec3
	DebugMissColor: vec3
	LayerMask: number
end

declare CFactoryRegistry: { }
declare class CFactoryRegistry
end
declare CFactory: { }
declare class CFactory
end

declare SCameraComponent: { }
declare class SCameraComponent
	FOV: number
	bAutoActivate: boolean
	function GetPosition(self): any
	function GetForwardVector(self): any
	function GetRightVector(self): any
end

declare CTextureFactory: { }
declare class CTextureFactory
end

declare SCharacterPhysicsComponent: { }
declare class SCharacterPhysicsComponent
	CollisionProfile: FCollisionProfile
	HalfHeight: number
	Radius: number
	Mass: number
	Padding: number
	HitReductionCosMaxAngle: number
	PenetrationRecoverySpeed: number
	PredictiveContactDistance: number
	MaxStrength: number
	MaxSlopeAngle: number
	StepHeight: number
	MaxCollisionIterations: number
	MaxConstraintIterations: number
	MinTimeRemaining: number
	CollisionTolerance: number
	MaxNumHits: number
	function GetBodyID(self): any
end
declare SCharacterMovementComponent: { }
declare class SCharacterMovementComponent
	MoveSpeed: number
	Acceleration: number
	Deceleration: number
	AirControl: number
	GroundFriction: number
	JumpSpeed: number
	RotationRate: number
	MaxJumpCount: number
	Gravity: number
	Velocity: vec3
	bUseControllerRotation: boolean
	bOrientRotationToMovement: boolean
	bGrounded: boolean
	JumpCount: number
end

declare SAudioSourceComponent: { }
declare class SAudioSourceComponent
	SoundFile: string
	Volume: number
	Pitch: number
	MinDistance: number
	MaxDistance: number
	bLooping: boolean
	bPlayOnReady: boolean
end
declare SAudioListenerComponent: { }
declare class SAudioListenerComponent
end

declare CWorld: { }
declare class CWorld
	function ConstructEntity(self, Name: string, Transform: FTransform): number
	function GetEntityRegistry(self): any
	function GetPhysicsScene(self): any
	function GetEntityTransform(self, Entity: number): STransformComponent
	function GetEntityLocation(self, Entity: number): vec3
	function SetEntityLocation(self, Entity: number, Location: vec3)
	function SetEntityRotation(self, Entity: number, Rotation: quat)
	function TranslateEntity(self, Entity: number, Translation: vec3): vec3
	function GetNumEntities(self): any
	function GetDefaultWorldSettings(self): any
	function EntityHasTag(self, Entity: number, Tag: string): boolean
	function GetEntityByTag(self, Tag: string): number
	function GetEntityByName(self, Name: string): number
	function CastRay(self, Settings: SRayCastSettings): any
	function CastSphere(self, Settings: SSphereCastSettings): any
	function GetUpdateStage(self): any
end

declare EMaterialType: number
declare EBlendMode: number
declare EMaterialShadingModel: number
declare CMaterialInterface: { }
declare class CMaterialInterface
end

declare STransformComponent: { }
declare class STransformComponent
	LocalTransform: FTransform
	function GetLocalLocation(self): any
	function GetLocalRotation(self): any
	function GetLocalScale(self): any
	function GetLocalRotationAsEuler(self): any
	function SetLocalLocation(self, InLocation: vec3): vec3
	function Translate(self, Delta: vec3): vec3
	function SetLocalRotation(self, InRotation: quat): quat
	function SetLocalRotationFromEuler(self, EulerDegrees: vec3): vec3
	function AddLocalRotationFromEuler(self, EulerDegrees: vec3): vec3
	function AddYaw(self, Degrees: number)
	function AddPitch(self, Degrees: number, ClampMin: number, ClampMax: number)
	function AddRoll(self, Degrees: number)
	function SetLocalScale(self, InScale: vec3): vec3
	function GetWorldLocation(self): any
	function GetWorldRotation(self): any
	function GetWorldScale(self): any
	function GetWorldRotationAsEuler(self): any
	function GetWorldMatrix(self): any
	function GetWorldTransform(self): any
	function SetWorldTransform(self, InTransform: FTransform)
	function SetLocalTransform(self, InTransform: FTransform)
	function GetForward(self): any
	function GetRight(self): any
	function GetUp(self): any
	function MaxScale(self): any
	function GetLocation(self): any
	function GetPosition(self): any
	function GetRotation(self): any
	function GetScale(self): any
	function SetLocation(self, L: vec3): vec3
	function SetRotation(self, R: quat): quat
	function SetScale(self, S: vec3): vec3
	function SetRotationFromEuler(self, E: vec3): vec3
	function AddRotationFromEuler(self, E: vec3): vec3
	function GetRotationAsEuler(self): any
end

declare CPrefab: { }
declare class CPrefab
end

declare CMaterialNodeGetTime: { }
declare class CMaterialNodeGetTime
end

declare CSkeleton: { }
declare class CSkeleton
	PreviewMesh: CSkeletalMesh
end

declare SSimpleAnimationComponent: { }
declare class SSimpleAnimationComponent
	Animation: CAnimation
	CurrentTime: number
	PlaybackSpeed: number
	bLooping: boolean
	bPlaying: boolean
end

declare SImpulseEvent: { }
declare class SImpulseEvent
	BodyID: number
	Impulse: vec3
end
declare SForceEvent: { }
declare class SForceEvent
	BodyID: number
	Force: vec3
end
declare STorqueEvent: { }
declare class STorqueEvent
	BodyID: number
	Torque: vec3
end
declare SAngularImpulseEvent: { }
declare class SAngularImpulseEvent
	BodyID: number
	AngularImpulse: vec3
end
declare SSetVelocityEvent: { }
declare class SSetVelocityEvent
	BodyID: number
	Velocity: vec3
end
declare SSetAngularVelocityEvent: { }
declare class SSetAngularVelocityEvent
	BodyID: number
	AngularVelocity: vec3
end
declare SAddImpulseAtPositionEvent: { }
declare class SAddImpulseAtPositionEvent
	BodyID: number
	Impulse: vec3
	Position: vec3
end
declare SAddForceAtPositionEvent: { }
declare class SAddForceAtPositionEvent
	BodyID: number
	Force: vec3
	Position: vec3
end
declare SSetGravityFactorEvent: { }
declare class SSetGravityFactorEvent
	BodyID: number
	GravityFactor: number
end

declare SEditorEntityMovementSystem: { }
declare class SEditorEntityMovementSystem
end

declare SScriptSystem: { }
declare class SScriptSystem
end

declare CParticleSystemFactory: { }
declare class CParticleSystemFactory
end

declare EMouseMode: number
declare EMouseKey: number

declare CMaterialInstanceFactory: { }
declare class CMaterialInstanceFactory
end

declare SParticleSystemComponent: { }
declare class SParticleSystemComponent
	ParticleSystem: CParticleSystem
	EmitterOffset: vec3
	SpawnRateMultiplier: number
	TimeScale: number
	bEmit: boolean
	bBurstOnSpawn: boolean
	function Activate(self, bReset: boolean)
	function Deactivate(self): any
	function IsActive(self): any
end

declare CParticleNodeGraph: { }
declare class CParticleNodeGraph
end

declare SVelocityComponent: { }
declare class SVelocityComponent
	Velocity: vec3
	Speed: number
	Scale: number
end

declare CMaterialFactory: { }
declare class CMaterialFactory
end

declare SPointLightComponent: { }
declare class SPointLightComponent
	LightColor: vec3
	Intensity: number
	Attenuation: number
	Falloff: number
	bCastShadows: boolean
end
declare SSpotLightComponent: { }
declare class SSpotLightComponent
	LightColor: vec3
	Intensity: number
	InnerConeAngle: number
	OuterConeAngle: number
	Attenuation: number
	Falloff: number
	bCastShadows: boolean
	ShadowBias: number
	ShadowRadius: number
	bVolumetric: boolean
	VolumetricIntensity: number
end
declare SDirectionalLightComponent: { }
declare class SDirectionalLightComponent
	Color: vec3
	Direction: vec3
	Intensity: number
	bCastShadows: boolean
end

declare CMaterialInstance: { }
declare class CMaterialInstance
	Material: CMaterial
end

declare SCameraSystem: { }
declare class SCameraSystem
end

declare EParticleEmitterShape: number
declare EParticleVelocityMode: number
declare EParticleBlendMode: number
declare EParticleShaderMode: number
declare CParticleSystem: { }
declare class CParticleSystem
	MaxParticles: number
	SpawnRate: number
	BurstCount: number
	Duration: number
	bLooping: boolean
	ShaderMode: number
	Shape: number
	ShapeSize: vec3
	ShapeAngle: number
	VelocityMode: number
	VelocityMin: vec3
	VelocityMax: vec3
	SpeedRange: vec2
	LifetimeRange: vec2
	Gravity: vec3
	Drag: number
	InheritEmitterVelocity: number
	StartColor: vec4
	EndColor: vec4
	StartSizeRange: vec2
	EndSizeRange: vec2
	RotationRange: vec2
	RotationSpeedRange: vec2
	NoiseStrength: vec3
	NoiseScale: number
	NoiseSpeed: number
	BlendMode: number
	Texture: CTexture
	bBillboardToCamera: boolean
	bWriteDepth: boolean
end

declare CEdGraphNode: { }
declare class CEdGraphNode
	GridX: number
	GridY: number
	NodeID: number
end
