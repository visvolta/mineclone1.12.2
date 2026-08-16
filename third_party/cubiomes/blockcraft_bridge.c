#include "blockcraft_bridge.h"

#include <stdlib.h>

#include "biomenoise.h"
#include "generator.h"

typedef struct CbColorNoise {
    PerlinNoise temperature;
    PerlinNoise grass;
} CbColorNoise;

CbGeneratorHandle cbCreateGenerator(int64_t seed, int largeBiomes, int default11, int fixedBiome)
{
    Generator* generator = (Generator*)calloc(1, sizeof(Generator));
    if (generator == NULL) return NULL;
    setupGenerator(generator, MC_1_12_2, largeBiomes ? LARGE_BIOMES : 0);
    if (fixedBiome >= 0)
        generator->ls.layers[L_BIOME_256].data = (void*)(intptr_t)(fixedBiome + 2);
    else if (default11)
        generator->ls.layers[L_BIOME_256].data = (void*)(intptr_t)1;
    applySeed(generator, DIM_OVERWORLD, (uint64_t)seed);
    return generator;
}

void cbDestroyGenerator(CbGeneratorHandle handle)
{
    free(handle);
}

int cbGenerateBiomes(CbGeneratorHandle handle, int scale, int x, int z,
                     int width, int height, int y, int* output)
{
    Generator* generator = (Generator*)handle;
    Range range = {scale, x, z, width, height, y, 1};
    const size_t cacheSize = getMinCacheSize(generator, scale, width, 1, height);
    int* cache = (int*)malloc(cacheSize * sizeof(int));
    if (cache == NULL) return 1;
    const int result = genBiomes(generator, cache, range);
    if (result == 0) {
        for (int i = 0; i < width * height; ++i) output[i] = cache[i];
    }
    free(cache);
    return result;
}

CbSurfaceNoiseHandle cbCreateSurfaceNoise(int64_t seed)
{
    SurfaceNoise* noise = (SurfaceNoise*)calloc(1, sizeof(SurfaceNoise));
    if (noise == NULL) return NULL;
    initSurfaceNoise(noise, DIM_OVERWORLD, (uint64_t)seed);
    return noise;
}

void cbDestroySurfaceNoise(CbSurfaceNoiseHandle handle)
{
    free(handle);
}

double cbSampleSurfaceNoise(CbSurfaceNoiseHandle handle, int x, int y, int z)
{
    return sampleSurfaceNoise((SurfaceNoise*)handle, x, y, z);
}

double cbSampleTerrainNoise(CbSurfaceNoiseHandle handle, int x, int y, int z,
                            double coordinateScale, double heightScale,
                            double lowerLimitScale, double upperLimitScale,
                            double mainNoiseScaleX, double mainNoiseScaleY,
                            double mainNoiseScaleZ)
{
    SurfaceNoise* noise = (SurfaceNoise*)handle;
    double minimum = 0.0;
    double maximum = 0.0;
    double main = 0.0;
    double persistence = 1.0;
    double contribution = 1.0;
    int octave;

    for (octave = 0; octave < 16; ++octave) {
        double dx = maintainPrecision(x * coordinateScale * persistence);
        double dy = maintainPrecision(y * heightScale * persistence);
        double dz = maintainPrecision(z * coordinateScale * persistence);
        double sy = heightScale * persistence;
        double ty = y * sy;
        minimum += samplePerlin(&noise->octmin.octaves[octave], dx, dy, dz, sy, ty) * contribution;
        maximum += samplePerlin(&noise->octmax.octaves[octave], dx, dy, dz, sy, ty) * contribution;

        if (octave < 8) {
            dx = maintainPrecision(x * coordinateScale / mainNoiseScaleX * persistence);
            dy = maintainPrecision(y * heightScale / mainNoiseScaleY * persistence);
            dz = maintainPrecision(z * coordinateScale / mainNoiseScaleZ * persistence);
            sy = heightScale / mainNoiseScaleY * persistence;
            ty = y * sy;
            main += samplePerlin(&noise->octmain.octaves[octave], dx, dy, dz, sy, ty) * contribution;
        }
        persistence *= 0.5;
        contribution *= 2.0;
    }

    return clampedLerp((main / 10.0 + 1.0) / 2.0,
                       minimum / lowerLimitScale,
                       maximum / upperLimitScale);
}

double cbSampleDepthNoise(CbSurfaceNoiseHandle handle, double x, double z)
{
    SurfaceNoise* noise = (SurfaceNoise*)handle;
    return sampleOctaveAmp(&noise->octdepth, x, 10.0, z, 1.0, 0.0, 1);
}

double cbSampleSurfaceOctaves(CbSurfaceNoiseHandle handle, double x, double z)
{
    SurfaceNoise* noise = (SurfaceNoise*)handle;
    double value = 0.0;
    double frequency = 1.0;
    double amplitude = 0.55;
    for (int index = 0; index < noise->octsurf.octcnt; ++index) {
        const PerlinNoise* octave = noise->octsurf.octaves + index;
        value += sampleSimplex2D(octave,
            x * frequency + octave->a,
            z * frequency + octave->b) * amplitude;
        frequency *= 0.5;
        amplitude *= 2.0;
    }
    return value;
}

CbColorNoiseHandle cbCreateColorNoise(void)
{
    CbColorNoise* noise = (CbColorNoise*)calloc(1, sizeof(CbColorNoise));
    if (noise == NULL) return NULL;
    uint64_t seed;
    setSeed(&seed, 1234ULL);
    perlinInit(&noise->temperature, &seed);
    setSeed(&seed, 2345ULL);
    perlinInit(&noise->grass, &seed);
    return noise;
}

void cbDestroyColorNoise(CbColorNoiseHandle handle)
{
    free(handle);
}

double cbSampleTemperatureNoise(CbColorNoiseHandle handle, double x, double z)
{
    return sampleSimplex2D(&((CbColorNoise*)handle)->temperature, x, z);
}

double cbSampleGrassColorNoise(CbColorNoiseHandle handle, double x, double z)
{
    return sampleSimplex2D(&((CbColorNoise*)handle)->grass, x, z);
}
