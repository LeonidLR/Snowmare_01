/**
 * Автономный чистый TypeScript энкодер анимированных GIF (GIF89a)
 * Не требует внешних зависимостей, компилируется в браузере и генерирует валидные .gif файлы
 */

class LZWEncoder {
  private pixels: Uint8Array;
  private initCodeSize: number;
  private accum = new Uint8Array(256);
  private htab = new Int32Array(5003);
  private codetab = new Int32Array(5003);
  private cur_accum = 0;
  private cur_bits = 0;
  private free_ent = 0;
  private maxcode = 0;
  private clear_flg = false;
  private g_init_bits = 0;
  private ClearCode = 0;
  private EOFCode = 0;
  private output: number[] = [];

  constructor(pixels: Uint8Array, colorDepth: number) {
    this.pixels = pixels;
    this.initCodeSize = Math.max(2, colorDepth);
  }

  private char_out(c: number) {
    this.accum[this.cur_accum++] = c;
    if (this.cur_accum >= 254) this.flush_char();
  }

  private flush_char() {
    if (this.cur_accum > 0) {
      this.output.push(this.cur_accum);
      for (let i = 0; i < this.cur_accum; ++i) this.output.push(this.accum[i]);
      this.cur_accum = 0;
    }
  }

  private output_code(code: number) {
    this.cur_accum |= (code << this.cur_bits);
    this.cur_bits += this.g_init_bits;
    while (this.cur_bits >= 8) {
      this.char_out(this.cur_accum & 0xff);
      this.cur_accum >>= 8;
      this.cur_bits -= 8;
    }
    if (this.free_ent > this.maxcode || this.clear_flg) {
      if (this.clear_flg) {
        this.maxcode = (1 << (this.g_init_bits = this.initCodeSize)) - 1;
        this.clear_flg = false;
      } else {
        ++this.g_init_bits;
        this.maxcode = this.g_init_bits === 12 ? (1 << 12) : ((1 << this.g_init_bits) - 1);
      }
    }
    if (code === this.EOFCode) {
      while (this.cur_bits > 0) {
        this.char_out(this.cur_accum & 0xff);
        this.cur_accum >>= 8;
        this.cur_bits -= 8;
      }
      this.flush_char();
    }
  }

  public encode(): number[] {
    this.output = [];
    this.output.push(this.initCodeSize);

    this.g_init_bits = this.initCodeSize + 1;
    this.ClearCode = 1 << this.initCodeSize;
    this.EOFCode = this.ClearCode + 1;
    this.free_ent = this.ClearCode + 2;
    this.maxcode = (1 << this.g_init_bits) - 1;
    this.clear_flg = false;
    this.cur_accum = 0;
    this.cur_bits = 0;

    for (let i = 0; i < 5003; ++i) this.htab[i] = -1;

    this.output_code(this.ClearCode);

    let ent = this.pixels[0];
    const n_pixels = this.pixels.length;

    for (let i = 1; i < n_pixels; ++i) {
      const c = this.pixels[i];
      const fcode = (c << 12) + ent;
      let idx = (c << 4) ^ ent;

      if (this.htab[idx] === fcode) {
        ent = this.codetab[idx];
        continue;
      } else if (this.htab[idx] >= 0) {
        let disp = 5003 - idx;
        if (idx === 0) disp = 1;
        let found = false;
        do {
          idx -= disp;
          if (idx < 0) idx += 5003;
          if (this.htab[idx] === fcode) {
            ent = this.codetab[idx];
            found = true;
            break;
          }
        } while (this.htab[idx] >= 0);
        if (found) continue;
      }

      this.output_code(ent);
      ent = c;
      if (this.free_ent < (1 << 12)) {
        this.codetab[idx] = this.free_ent++;
        this.htab[idx] = fcode;
      } else {
        for (let j = 0; j < 5003; ++j) this.htab[j] = -1;
        this.free_ent = this.ClearCode + 2;
        this.clear_flg = true;
        this.output_code(this.ClearCode);
      }
    }

    this.output_code(ent);
    this.output_code(this.EOFCode);
    this.output.push(0); // block terminator
    return this.output;
  }
}

export class SimpleGifWriter {
  private width: number;
  private height: number;
  private bytes: number[] = [];

  constructor(width: number, height: number) {
    this.width = width;
    this.height = height;

    // GIF89a Header
    this.writeString('GIF89a');
    // Logical Screen Descriptor
    this.writeShort(width);
    this.writeShort(height);
    this.bytes.push(0x70); // GCT Flag (0), Color Res (7 = 8 bits), Sort (0), GCT Size (0)
    this.bytes.push(0);    // BG Color index
    this.bytes.push(0);    // Pixel Aspect Ratio

    // Application Extension for looping (NETSCAPE2.0)
    this.bytes.push(0x21, 0xFF, 0x0B);
    this.writeString('NETSCAPE2.0');
    this.bytes.push(0x03, 0x01);
    this.writeShort(0); // Loop count 0 = infinite
    this.bytes.push(0x00);
  }

  private writeString(str: string) {
    for (let i = 0; i < str.length; ++i) this.bytes.push(str.charCodeAt(i));
  }

  private writeShort(val: number) {
    this.bytes.push(val & 0xFF, (val >> 8) & 0xFF);
  }

  public addFrame(ctx: CanvasRenderingContext2D, delayMs: number) {
    const imgData = ctx.getImageData(0, 0, this.width, this.height).data;
    const { indexedPixels, palette } = this.quantize(imgData);

    // Graphic Control Extension
    const delayTime = Math.round(delayMs / 10); // in 1/100 sec
    this.bytes.push(0x21, 0xF9, 0x04);
    this.bytes.push(0x00); // disposal method (no special handling)
    this.writeShort(delayTime);
    this.bytes.push(0);    // transparent index
    this.bytes.push(0);    // block terminator

    // Image Descriptor
    this.bytes.push(0x2C);
    this.writeShort(0); // left
    this.writeShort(0); // top
    this.writeShort(this.width);
    this.writeShort(this.height);
    // Local Color Table Flag (1), Interlace (0), Sort (0), Size (7 = 256 colors)
    this.bytes.push(0x87);

    // Write Local Color Table (256 * 3 bytes)
    for (let i = 0; i < 256; ++i) {
      if (i < palette.length) {
        this.bytes.push(palette[i][0], palette[i][1], palette[i][2]);
      } else {
        this.bytes.push(0, 0, 0);
      }
    }

    // LZW Encode image data
    const encoder = new LZWEncoder(indexedPixels, 8);
    const lzwBytes = encoder.encode();
    for (let i = 0; i < lzwBytes.length; ++i) this.bytes.push(lzwBytes[i]);
  }

  private quantize(rgba: Uint8ClampedArray): { indexedPixels: Uint8Array, palette: number[][] } {
    const n = this.width * this.height;
    const indexed = new Uint8Array(n);
    const paletteMap = new Map<number, number>();
    const palette: number[][] = [];

    // Быстрое 6-6-6 цветовое квантование для плавного отображения тактического канваса
    for (let i = 0; i < n; ++i) {
      const off = i * 4;
      // Округляем до 6 уровней (216 цветов)
      const r = Math.floor(rgba[off] / 43);
      const g = Math.floor(rgba[off + 1] / 43);
      const b = Math.floor(rgba[off + 2] / 43);
      const key = (r << 16) | (g << 8) | b;

      let idx = paletteMap.get(key);
      if (idx === undefined) {
        idx = palette.length;
        if (palette.length < 255) {
          paletteMap.set(key, idx);
          palette.push([r * 51, g * 51, b * 51]);
        } else {
          idx = 0; // Запасной fallback
        }
      }
      indexed[i] = idx;
    }

    return { indexedPixels: indexed, palette };
  }

  public getBlob(): Blob {
    this.bytes.push(0x3B); // GIF Trailer
    const u8 = new Uint8Array(this.bytes);
    return new Blob([u8], { type: 'image/gif' });
  }
}
