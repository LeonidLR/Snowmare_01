#pragma once

#include "CoreMinimal.h"

/**
 * Deterministic 32-bit RNG + string hash of the props placement (docs/design/levelgen/PROPS_LAYERS.md sections 0 and 2.2, task C3).
 * Pure, no world, no floats. The TS port (Tools/TrainingGround/src/levelgen/props, task T2) must produce byte-identical streams.
 * Godot reference: none (UE-only level authoring). Tests: CodexTactics.LevelGen.Props.Rng.*.
 *
 * JS reference (the contract - do not "improve" it):
 *
 *   function mulberry32(a) {                       // a: uint32 seed
 *     return function () {                         // returns the RAW uint32 (never divided by 2^32)
 *       a = (a + 0x6D2B79F5) | 0;
 *       let t = Math.imul(a ^ (a >>> 15), 1 | a);
 *       t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
 *       return (t ^ (t >>> 14)) >>> 0;
 *     };
 *   }
 *   function fnv1a(str) {                          // 32-bit FNV-1a over the UTF-8 bytes
 *     let h = 0x811C9DC5;
 *     for (const b of new TextEncoder().encode(str)) { h ^= b; h = Math.imul(h, 0x01000193) >>> 0; }
 *     return h;
 *   }
 *   // layer stream: mulberry32((seed ^ fnv1a(layer.id)) >>> 0)
 *   // bounded draw:  nextBelow(n) = n === 0 ? 0 : Number((BigInt(next()) * BigInt(n)) >> 32n)   (exact, no modulo bias)
 *   // shuffle:       for (let i = a.length - 1; i > 0; --i) { const j = nextBelow(i + 1); [a[i], a[j]] = [a[j], a[i]]; }
 *
 * Reference vectors (node, checked by the tests):
 *   mulberry32(0): 0x4434b462 0x00159c37 0x39285b08 0x256d8104 0x77a2cbd4
 *   mulberry32(1): 0xa087eaf3 0x00b349c9 0x8706c4eb 0xfb2627fd 0xf7e79d2b
 *   mulberry32(7): 0x02ff152c 0x0fdc7f12 0xfa169e5e 0xb2f38b96 0x857d6fe7
 *   mulberry32(0xffffffff): 0xe57bf3d3 0x3081a5a4 0xb7350390 0xf1ade904 0xd8616a2f
 *   fnv1a: "" 0x811c9dc5, "a" 0xe40c292c, "foobar" 0xbf9cf968, "crates" 0xfcecbe49, "barrels" 0x575f8e06, "yashchik (U+044F U+0449 U+0438 U+043A)" 0xdb9bb9b9
 *   stream("crates", seed 7) = mulberry32(0xfcecbe4e): 0x9bb9e7c5 0x694c7fa9 0x82a1553f
 *   mulberry32(1) nextBelow(10) x8: 6 0 5 9 9 2 6 7
 *   mulberry32(42) shuffle [0..9]: 0 7 3 5 2 1 8 9 4 6
 *   mulberry32(5) next() % 4 x6: 0 3 2 1 3 0
 */
class CODEXTACTICS_API FPropsRng
{
public:
	explicit FPropsRng(uint32 InSeed) : State(InSeed) {}

	/** The next raw mulberry32 output. */
	uint32 NextU32();

	/** Uniform integer in [0, Bound) as (NextU32 * Bound) >> 32 (no modulo bias, no floats). Bound 0 returns 0 and draws nothing. */
	uint32 NextBelow(uint32 Bound);

	/** Fisher-Yates, i from Num-1 down to 1, j = NextBelow(i + 1). Fewer than 2 items draw nothing. */
	void Shuffle(TArray<int32>& Items);

private:
	uint32 State;
};

namespace PropsRng
{
	/** mulberry32 state increment. */
	constexpr uint32 Mulberry32Increment = 0x6D2B79F5u;
	/** FNV-1a 32-bit offset basis and prime. */
	constexpr uint32 Fnv1aOffsetBasis = 0x811C9DC5u;
	constexpr uint32 Fnv1aPrime = 0x01000193u;

	/** FNV-1a 32 over the UTF-8 bytes of Text. */
	CODEXTACTICS_API uint32 Fnv1a(const FString& Text);

	/** Seed of a layer's stream: Seed ^ Fnv1a(LayerId). */
	CODEXTACTICS_API uint32 StreamSeed(uint32 Seed, const FString& LayerId);
}
