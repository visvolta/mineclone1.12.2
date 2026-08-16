#!/usr/bin/env python3
"""Print canonical 1.12.2 block-state and biome hashes from an Anvil chunk.

This is a fixture-authoring utility, not part of the game runtime. Generate a
world with an unmodified 1.12.2 server, stop it cleanly, then pass its world
directory and a chunk coordinate. The C++ parity tests use the same FNV-1a byte
order over all 65,536 packed legacy states and the 256 biome bytes.
"""

from __future__ import annotations

import argparse
import gzip
import io
import pathlib
import struct
import zlib


class NbtReader:
    def __init__(self, payload: bytes) -> None:
        self.stream = io.BytesIO(payload)

    def read(self, size: int) -> bytes:
        value = self.stream.read(size)
        if len(value) != size:
            raise ValueError("truncated NBT payload")
        return value

    def unpack(self, pattern: str):
        return struct.unpack(">" + pattern, self.read(struct.calcsize(">" + pattern)))

    def string(self) -> str:
        (size,) = self.unpack("H")
        return self.read(size).decode("utf-8")

    def payload(self, tag: int):
        if tag == 1:
            return self.unpack("b")[0]
        if tag == 2:
            return self.unpack("h")[0]
        if tag == 3:
            return self.unpack("i")[0]
        if tag == 4:
            return self.unpack("q")[0]
        if tag == 5:
            return self.unpack("f")[0]
        if tag == 6:
            return self.unpack("d")[0]
        if tag == 7:
            (size,) = self.unpack("i")
            return self.read(size)
        if tag == 8:
            return self.string()
        if tag == 9:
            child, size = self.unpack("Bi")
            return [self.payload(child) for _ in range(size)]
        if tag == 10:
            result = {}
            while True:
                (child,) = self.unpack("B")
                if child == 0:
                    return result
                name = self.string()
                result[name] = self.payload(child)
        if tag == 11:
            (size,) = self.unpack("i")
            return list(self.unpack(f"{size}i"))
        if tag == 12:
            (size,) = self.unpack("i")
            return list(self.unpack(f"{size}q"))
        raise ValueError(f"unsupported NBT tag {tag}")

    def root(self):
        (tag,) = self.unpack("B")
        if tag != 10:
            raise ValueError("NBT root is not a compound")
        self.string()
        return self.payload(tag)


def floor_div(value: int, divisor: int) -> int:
    return value // divisor


def read_chunk(world: pathlib.Path, chunk_x: int, chunk_z: int):
    region_x = floor_div(chunk_x, 32)
    region_z = floor_div(chunk_z, 32)
    region = world / "region" / f"r.{region_x}.{region_z}.mca"
    local_x = chunk_x - region_x * 32
    local_z = chunk_z - region_z * 32
    with region.open("rb") as stream:
        stream.seek((local_x + local_z * 32) * 4)
        location = stream.read(4)
        if len(location) != 4:
            raise ValueError("truncated Anvil location table")
        sector = int.from_bytes(location[:3], "big")
        if sector == 0:
            raise ValueError(f"chunk {chunk_x},{chunk_z} is not present")
        stream.seek(sector * 4096)
        length = int.from_bytes(stream.read(4), "big")
        compression = stream.read(1)[0]
        payload = stream.read(length - 1)
    if compression == 1:
        payload = gzip.decompress(payload)
    elif compression == 2:
        payload = zlib.decompress(payload)
    else:
        raise ValueError(f"unsupported Anvil compression {compression}")
    return NbtReader(payload).root()["Level"]


def fnv_update(value: int, byte: int) -> int:
    return ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF


def hashes(level) -> tuple[int, int]:
    states = [0] * (16 * 256 * 16)
    for section in level["Sections"]:
        section_y = section["Y"] & 0xFF
        blocks = section["Blocks"]
        metadata = section["Data"]
        additions = section.get("Add")
        for index, low_id in enumerate(blocks):
            high_id = 0
            if additions is not None:
                packed = additions[index >> 1]
                high_id = (packed >> (4 if index & 1 else 0)) & 15
            packed_meta = metadata[index >> 1]
            meta = (packed_meta >> (4 if index & 1 else 0)) & 15
            block_id = low_id | (high_id << 8)
            local_y = index >> 8
            local_z = (index >> 4) & 15
            local_x = index & 15
            y = section_y * 16 + local_y
            states[(y * 16 + local_z) * 16 + local_x] = block_id | (meta << 12)

    block_hash = 14695981039346656037
    for state in states:
        block_hash = fnv_update(block_hash, state & 0xFF)
        block_hash = fnv_update(block_hash, (state >> 8) & 0xFF)
    biome_hash = 14695981039346656037
    for biome in level["Biomes"]:
        biome_hash = fnv_update(biome_hash, biome)
    return block_hash, biome_hash


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("world", type=pathlib.Path)
    parser.add_argument("chunk_x", type=int)
    parser.add_argument("chunk_z", type=int)
    args = parser.parse_args()
    block_hash, biome_hash = hashes(read_chunk(args.world, args.chunk_x, args.chunk_z))
    print(f"chunk=({args.chunk_x},{args.chunk_z})")
    print(f"blocks=0x{block_hash:016x}")
    print(f"biomes=0x{biome_hash:016x}")


if __name__ == "__main__":
    main()
