// Training Ground / level generator: deterministic 32-bit RNG + string hash for props placement.
// Docs: docs/design/levelgen/PROPS_LAYERS.md sections 0, 2.2, and 7.
// C++ reference: Source/CodexTactics/Public/LevelGen/PropsRng.h

export class PropsRng {
  private state: number;
  public calls = 0;

  constructor(seed: number) {
    this.state = seed >>> 0;
  }

  /** The next raw mulberry32 output (uint32). */
  nextU32(): number {
    this.calls++;
    this.state = (this.state + 0x6d2b79f5) | 0;
    let t = Math.imul(this.state ^ (this.state >>> 15), 1 | this.state);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return (t ^ (t >>> 14)) >>> 0;
  }

  /** Uniform integer in [0, bound) as (nextU32 * bound) >> 32 (no modulo bias). Bound 0 returns 0 and draws nothing. */
  nextBelow(bound: number): number {
    if (bound <= 0) return 0;
    const n = BigInt(bound >>> 0);
    const u = BigInt(this.nextU32());
    return Number((u * n) >> 32n);
  }

  /** Fisher-Yates shuffle: i from num - 1 down to 1, j = nextBelow(i + 1). */
  shuffle<T>(items: T[]): void {
    for (let i = items.length - 1; i > 0; --i) {
      const j = this.nextBelow(i + 1);
      const tmp = items[i];
      items[i] = items[j];
      items[j] = tmp;
    }
  }
}

/** 32-bit FNV-1a hash over the UTF-8 bytes of str. */
export function fnv1a(str: string): number {
  let h = 0x811c9dc5;
  const bytes = new TextEncoder().encode(str);
  for (let i = 0; i < bytes.length; i++) {
    h ^= bytes[i];
    h = Math.imul(h, 0x01000193) >>> 0;
  }
  return h >>> 0;
}

/** Stream seed for a layer: (seed ^ fnv1a(layerId)) >>> 0. */
export function streamSeed(seed: number, layerId: string): number {
  return ((seed >>> 0) ^ fnv1a(layerId)) >>> 0;
}
