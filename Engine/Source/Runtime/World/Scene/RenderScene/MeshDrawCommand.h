#pragma once
#include "Containers/Array.h"
#include "Platform/GenericPlatform.h"
#include <Core/Math/Hash/Hash.h>
#include <Renderer/RHIFwd.h>


namespace Lumina
{
	class FRHIInputLayout;
	class FRHIBuffer;
	class CMaterialInterface;


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

	/**
	 * A mesh draw command fully encapsulates all the data needed to draw a mesh draw call.
	 * Mesh draw commands are cached in the scene.
	 */
	struct FMeshDrawCommand
	{
		TFixedHashMap<FDrawKey, uint32, 16>	DrawArgumentIndexMap;
		FRHIVertexShader*					VertexShader = nullptr;
		FRHIPixelShader*					PixelShader = nullptr;
		uint32                      		IndirectDrawOffset = 0;
		uint32                      		DrawCount = 0;
		uint32                      		bDrawInDepthPass : 1;
		uint32                      		bTranslucent : 1;
		uint32                      		bMasked : 1;
		uint32                      		bAdditive : 1;
	};
}
