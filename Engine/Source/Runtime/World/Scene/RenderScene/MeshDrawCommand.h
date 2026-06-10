#pragma once
#include "Containers/Array.h"
#include "Platform/GenericPlatform.h"
#include <Core/Math/Hash/Hash.h>
#include <Renderer/RHIFwd.h>

namespace Lumina
{
	class CMaterialInterface;

	// Shader entries resolved from a material on the GAME thread. Entries live in the shader
	// library for the process lifetime, so these stay valid even after the owning material
	// asset is destroyed mid-frame.
	struct FRenderMaterialShaders
	{
		const FShaderEntry* VertexShader = nullptr;
		const FShaderEntry* PixelShader  = nullptr;
	};


	struct FDrawKey
	{
		uint32 StartIndex;
		uint32 IndexCount;

		bool operator == (const FDrawKey& Key) const
		{
			return StartIndex == Key.StartIndex && IndexCount == Key.IndexCount;
		}
	};

	static uint64 GetTypeHash(const FDrawKey& K)
	{
		size_t Seed = 0;
		Hash::HashCombine(Seed, K.StartIndex);
		Hash::HashCombine(Seed, K.IndexCount);
		return Seed;
	}


	struct FDrawBatchKey
	{
		uint64 MaterialID;

		uint32 bDrawInDepthPass : 1;
		uint32 bTranslucent : 1;
		uint32 bMasked : 1;
		uint32 bAdditive : 1;

		bool operator == (const FDrawBatchKey& Key) const
		{
			return MaterialID == Key.MaterialID
				&& bDrawInDepthPass == Key.bDrawInDepthPass
				&& bTranslucent == Key.bTranslucent
				&& bMasked == Key.bMasked
				&& bAdditive == Key.bAdditive;
		}
	};

	static uint64 GetTypeHash(const FDrawBatchKey& K)
	{
		size_t Seed = 0;
		Hash::HashCombine(Seed, K.MaterialID);
		Hash::HashCombine(Seed, K.bDrawInDepthPass);
		Hash::HashCombine(Seed, K.bTranslucent);
		Hash::HashCombine(Seed, K.bMasked);
		Hash::HashCombine(Seed, K.bAdditive);
		return Seed;
	}

	// All data needed for one mesh draw call; cached in the scene. Shader entries are
	// library-owned (immortal), so a deleted material asset can't dangle the render thread.
	struct FMeshDrawCommand
	{
		const FShaderEntry*					VertexShader = nullptr;
		const FShaderEntry*					PixelShader  = nullptr;
		// Per-material depth-prepass / shadow VS, populated only for WPO materials so prepass
		// depth matches the base pass. Null means fall back to the global library shader.
		const FShaderEntry*					DepthVertexShader  = nullptr;
		const FShaderEntry*					ShadowVertexShader = nullptr;
		uint32                      		IndirectDrawOffset = 0;
		uint32                      		DrawCount = 0;
		uint32                      		bDrawInDepthPass : 1;
		uint32                      		bTranslucent : 1;
		uint32                      		bMasked : 1;
		uint32                      		bAdditive : 1;
	};
}
