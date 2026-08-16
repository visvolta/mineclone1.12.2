#ifndef BLOCKCRAFT_CUBIOMES_BRIDGE_H
#define BLOCKCRAFT_CUBIOMES_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* CbGeneratorHandle;
typedef void* CbSurfaceNoiseHandle;
typedef void* CbColorNoiseHandle;

CbGeneratorHandle cbCreateGenerator(int64_t seed, int largeBiomes, int default11, int fixedBiome);
void cbDestroyGenerator(CbGeneratorHandle handle);
int cbGenerateBiomes(CbGeneratorHandle handle, int scale, int x, int z,
                     int width, int height, int y, int* output);

CbSurfaceNoiseHandle cbCreateSurfaceNoise(int64_t seed);
void cbDestroySurfaceNoise(CbSurfaceNoiseHandle handle);
double cbSampleSurfaceNoise(CbSurfaceNoiseHandle handle, int x, int y, int z);
double cbSampleDepthNoise(CbSurfaceNoiseHandle handle, double x, double y, double z);
double cbSampleSurfaceOctaves(CbSurfaceNoiseHandle handle, double x, double z);

CbColorNoiseHandle cbCreateColorNoise(void);
void cbDestroyColorNoise(CbColorNoiseHandle handle);
double cbSampleTemperatureNoise(CbColorNoiseHandle handle, double x, double z);
double cbSampleGrassColorNoise(CbColorNoiseHandle handle, double x, double z);

#ifdef __cplusplus
}
#endif

#endif
