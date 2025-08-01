#include "Noise.h"

#include <cmath>

namespace model {

WhiteNoise::WhiteNoise()
{
}

void WhiteNoise::setSeed(int64 seed)
{
    random.setSeed(seed);
}

float WhiteNoise::tick()
{
    return random.nextFloat();
}

//==============================================================================

SimplexNoise::SimplexNoise()
{
    setSeed(Time::currentTimeMillis());
}

void SimplexNoise::setSeed(int64 seed)
{
    static const SimplexNoise::Grad grad2[] = {
        { 1.0f,  1.0f }, {-1.0f,  1.0f, }, { 1.0f, -1.0f, }, {-1.0f, -1.0f, },
        { 1.0f,  0.0f }, {-1.0f,  0.0f, }, { 1.0f,  0.0f, }, {-1.0f,  0.0f, },
        { 0.0f,  1.0f }, { 0.0f, -1.0f, }, { 0.0f,  1.0f, }, { 0.0f, -1.0f, }
    };

    static const short p[] = {151,160,137,91,90,15,
        131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
        190, 6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,
        88,237,149,56,87,174,20,125,136,171,168, 68,175,74,165,71,134,139,48,27,166,
        77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,55,46,245,40,244,
        102,143,54, 65,25,63,161, 1,216,80,73,209,76,132,187,208, 89,18,169,200,196,
        135,130,116,188,159,86,164,100,109,198,173,186, 3,64,52,217,226,250,124,123,
        5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,
        223,183,170,213,119,248,152, 2,44,154,163, 70,221,153,101,155,167, 43,172,9,
        129,22,39,253, 19,98,108,110,79,113,224,232,178,185, 112,104,218,246,97,228,
        251,34,242,193,238,210,144,12,191,179,162,241, 81,51,145,235,249,14,239,107,
        49,192,214, 31,181,199,106,157,184, 84,204,176,115,121,50,45,127, 4,150,254,
        138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
    };

    short s = (short)(seed & 0xFFFF);

    if (s < 256)
        s |= seed << 8;

    for (int i = 0; i < 256; i++) {
        short v = (i & 1) ? p[i] ^ (s & 255)
                          : p[i] ^ ((s >> 8) & 255);

        perm[i] = perm[i + 256] = v;
        gradP[i] = gradP[i + 256] = grad2[v % 12];
    }
}

float SimplexNoise::sample2d(float x, float y)
{
    constexpr float F2{ 0.3660254037844386f };  // 0.5 * (sqrt(3.0) - 1.0);
    constexpr float G2{ 0.21132486540518713f }; // (3.0 - sqrt(3.0)) / 6.0;
    constexpr float F3{ 1.0f / 3.0f };
    constexpr float G3{ 1.0f / 6.0f };

    const float s{ (x + y) * F2 };

    int i = (int)floor(x + s);
    int j = (int)floor(y + s);

	float t = float(i + j) * G2;
	float x0 = x - i + t;
	float y0 = y - j + t;

    int i1 = x0 > y0 ? 1 : 0;
    int j1 = x0 > y0 ? 0 : 1;

	float x1 = x0 - i1 + G2;
	float y1 = y0 - j1 + G2;
	float x2 = x0 - 1 + 2 * G2;
	float y2 = y0 - 1 + 2 * G2;

	i &= 255;
	j &= 255;

	const Grad gi0{ gradP[i + perm[j]] };
	const Grad gi1{ gradP[i + i1 + perm[j + j1]] };
	const Grad gi2{ gradP[i + 1 + perm[j + 1]] };

	float t0 = 0.5f - x0 * x0 - y0 * y0;
	float n0{};
    float n1{};
    float n2{};

	if (t0 < 0) {
		n0 = 0;
	} else {
		t0 *= t0;
		n0 = t0 * t0 * gi0.dot(x0, y0);
	}

	float t1 = 0.5f - x1 * x1 - y1 * y1;

	if (t1 < 0) {
		n1 = 0;
	} else {
		t1 *= t1;
		n1 = t1 * t1 * gi1.dot(x1, y1);
	}

	float t2 = 0.5f - x2 * x2 - y2 * y2;

	if (t2 < 0) {
		n2 = 0;
	} else {
		t2 *= t2;
		n2 = t2 * t2 * gi2.dot(x2, y2);
	}

	return 70.0f * (n0 + n1 + n2);
}

float SimplexNoise::sample1d(float x)
{
	return sample2d(x * 1.2f, -x * 0.7f);
}


} // namespace model
