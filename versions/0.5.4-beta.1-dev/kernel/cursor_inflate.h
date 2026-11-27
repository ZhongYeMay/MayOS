#ifndef MAYOS_CURSOR_INFLATE_H
#define MAYOS_CURSOR_INFLATE_H

/* RFC 1950/1951. The output length is fixed by the validated PNG header. */
typedef struct { const u8 *data; u32 length, at, pending, bits; int failed; } CursorBits;
typedef struct { u16 count[16], symbols[288]; } CursorHuffman;

static u32 cursor_bits(CursorBits *source, u32 count) {
    while (source->bits < count) {
        if (source->at == source->length) {
            source->failed = 1;
            return 0;
        }
        source->pending |= (u32)source->data[source->at++] << source->bits;
        source->bits += 8;
    }
    u32 value = source->pending & ((1u << count) - 1);
    source->pending >>= count;
    source->bits -= count;
    return value;
}

static int cursor_huffman(CursorHuffman *h, const u8 *lengths, u32 size) {
    memset(h->count, 0, sizeof(h->count));
    for (u32 i = 0; i < size; i++) {
        if (lengths[i] > 15) return 0;
        h->count[lengths[i]]++;
    }
    u32 offsets[16], used = 0;
    int left = 1;
    for (int i = 1; i < 16; i++) {
        left = left * 2 - h->count[i];
        if (left < 0) return 0;
        offsets[i] = used;
        used += h->count[i];
    }
    for (u32 i = 0; i < size; i++)
        if (lengths[i]) h->symbols[offsets[lengths[i]]++] = (u16)i;
    return 1;
}

static int cursor_symbol(CursorBits *source, const CursorHuffman *h) {
    u32 code = 0, first = 0, offset = 0;
    for (int len = 1; len < 16; len++) {
        code = (code << 1) | cursor_bits(source, 1);
        if (source->failed) return -1;
        if (code >= first && code - first < h->count[len])
            return h->symbols[offset + code - first];
        first = (first + h->count[len]) << 1;
        offset += h->count[len];
    }
    return -1;
}

static int cursor_trees(CursorBits *source, u32 type, CursorHuffman *literal, CursorHuffman *distance) {
    u8 lengths[288 + 32];
    if (type == 1) {
        for (int i = 0; i < 288; i++) lengths[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
        memset(lengths + 288, 5, 32);
        return cursor_huffman(literal, lengths, 288) && cursor_huffman(distance, lengths + 288, 32);
    }
    static const u8 order[] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    u32 symbols = cursor_bits(source, 5) + 257, distances = cursor_bits(source, 5) + 1;
    u32 codes = cursor_bits(source, 4) + 4;
    if (symbols > 286 || source->failed) return 0;
    u8 codesizes[19] = {0};
    for (u32 i = 0; i < codes; i++) codesizes[order[i]] = (u8)cursor_bits(source, 3);
    CursorHuffman h;
    if (!cursor_huffman(&h, codesizes, 19)) return 0;
    for (u32 i = 0, limit = symbols + distances; i < limit;) {
        int code = cursor_symbol(source, &h);
        if (code < 0) return 0;
        if (code < 16) {
            lengths[i++] = (u8)code;
            continue;
        }
        if (code == 16 && !i) return 0;
        u32 count = code == 16 ? cursor_bits(source, 2) + 3 :
                    code == 17 ? cursor_bits(source, 3) + 3 : cursor_bits(source, 7) + 11;
        if (source->failed || count > limit - i) return 0;
        u8 value = code == 16 ? lengths[i - 1] : 0;
        for (u32 run = 0; run < count; run++) lengths[i++] = value;
    }
    return lengths[256] && cursor_huffman(literal, lengths, symbols) &&
           cursor_huffman(distance, lengths + symbols, distances);
}

static int cursor_inflate(const u8 *input, u32 size, u8 *output, u32 length) {
    if (size < 6 || (input[0] & 15) != 8 || (input[0] >> 4) > 7 ||
        ((u32)input[0] * 256 + input[1]) % 31 || (input[1] & 32)) return 0;
    CursorBits source = {input + 2, size - 6, 0, 0, 0, 0};
    static const u16 lengths[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    u32 written = 0, last;
    do {
        last = cursor_bits(&source, 1);
        u32 type = cursor_bits(&source, 2);
        if (source.failed || type == 3) return 0;
        if (!type) {
            cursor_bits(&source, source.bits % 8);
            u32 count = cursor_bits(&source, 16);
            u32 inverse = cursor_bits(&source, 16);
            if (source.failed || (count ^ inverse) != 65535 || count > length - written) return 0;
            for (u32 i = 0; i < count; i++) output[written++] = (u8)cursor_bits(&source, 8);
            if (source.failed) return 0;
            continue;
        }
        CursorHuffman literal, distance;
        if (!cursor_trees(&source, type, &literal, &distance)) return 0;
        for (;;) {
            int code = cursor_symbol(&source, &literal);
            if (code == 256) break;
            if (code < 0 || code > 285 || written == length) return 0;
            if (code < 256) {
                output[written++] = (u8)code;
                continue;
            }
            u32 li = (u32)code - 257;
            u32 extra = li < 8 || li == 28 ? 0 : li / 4 - 1;
            u32 run = lengths[li] + cursor_bits(&source, extra);
            int dist = cursor_symbol(&source, &distance);
            if (dist < 0 || dist > 29) return 0;
            extra = dist < 4 ? 0 : (u32)dist / 2 - 1;
            u32 back = dist < 4 ? (u32)dist + 1 : ((2u + (dist & 1)) << extra) + 1;
            back += cursor_bits(&source, extra);
            if (source.failed || back > written || run > length - written) return 0;
            for (u32 i = 0; i < run; i++, written++) output[written] = output[written - back];
        }
    } while (!last);
    if (written != length || source.at != source.length) return 0;
    u32 a = 1, b = 0;
    for (u32 i = 0; i < written; i++) {
        a = (a + output[i]) % 65521;
        b = (b + a) % 65521;
    }
    return input[size - 4] == (b >> 8) && input[size - 3] == (b & 255) &&
           input[size - 2] == (a >> 8) && input[size - 1] == (a & 255);
}

#endif
