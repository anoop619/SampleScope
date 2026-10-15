#include "Resampler.h"

#include <algorithm>
#include <cmath>

namespace ss
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0, q = x * x * 0.25;
        for (int k = 1; k < 50; ++k)
        {
            term *= q / (double (k) * double (k));
            sum += term;
            if (term < 1.0e-12 * sum)
                break;
        }
        return sum;
    }

    /** Tabulated h(t) = sinc(t) * kaiser(t / zc) for t in [0, zc], `oversample` points per unit. */
    struct KernelTable
    {
        KernelTable (int zc, int oversample) : zeroCrossings (zc), os (oversample)
        {
            const double beta = zc >= 24 ? 9.0 : 6.5;
            const double i0b  = besselI0 (beta);
            const int n = zc * os + 2;
            table.resize ((size_t) n);

            for (int i = 0; i < n; ++i)
            {
                double t = double (i) / os;
                double sinc = t < 1.0e-9 ? 1.0 : std::sin (kPi * t) / (kPi * t);
                double r = t / zc;
                double w = r >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - r * r)) / i0b;
                table[(size_t) i] = (float) (sinc * w);
            }
        }

        inline float at (double t) const noexcept   // t >= 0
        {
            double idx = t * os;
            int i = (int) idx;
            if (i >= (int) table.size() - 1)
                return 0.0f;
            float f = (float) (idx - i);
            return table[(size_t) i] + f * (table[(size_t) i + 1] - table[(size_t) i]);
        }

        int zeroCrossings, os;
        std::vector<float> table;
    };
}

void Resampler::process (const float* in, int inLength, float* out, int outLength,
                         double inPerOut, int zeroCrossings)
{
    if (outLength <= 0)
        return;

    if (inLength <= 0)
    {
        std::fill (out, out + outLength, 0.0f);
        return;
    }

    // Identity: plain copy.
    if (std::abs (inPerOut - 1.0) < 1.0e-9)
    {
        int n = std::min (inLength, outLength);
        std::copy (in, in + n, out);
        std::fill (out + n, out + outLength, 0.0f);
        return;
    }

    const KernelTable kernel (zeroCrossings, 256);

    // Lower the cutoff when downsampling (or pitching up) to stay alias-free.
    const double cutoff = std::min (1.0, 1.0 / inPerOut) * 0.96;
    const double halfWidth = zeroCrossings / cutoff;    // in input samples

    for (int i = 0; i < outLength; ++i)
    {
        const double pos = i * inPerOut;
        const int first = (int) std::ceil (pos - halfWidth);
        const int last  = (int) std::floor (pos + halfWidth);

        double acc = 0.0, norm = 0.0;
        for (int j = first; j <= last; ++j)
        {
            const float w = kernel.at (std::abs (j - pos) * cutoff);
            norm += w;
            if (j >= 0 && j < inLength)
                acc += (double) w * in[j];
        }

        out[i] = norm > 1.0e-9 ? (float) (acc / norm) : 0.0f;
    }
}

std::vector<float> Resampler::resample (const float* in, int inLength, double inPerOut, int zeroCrossings)
{
    const int outLength = std::max (0, (int) std::floor (inLength / inPerOut));
    std::vector<float> out ((size_t) outLength);
    process (in, inLength, out.data(), outLength, inPerOut, zeroCrossings);
    return out;
}

} // namespace ss
