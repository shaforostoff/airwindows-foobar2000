/* ========================================
 *  Declick - DeclickProc.cpp
 *  MIT license, as the rest of the tree
 * ======================================== */

#ifndef __Declick_H
#include "Declick.h"
#endif

/*  There is no per-sample DSP written out here, and that is deliberate: the
 *  algorithm is a block-based autoregressive fit, detect and least-squares
 *  interpolate, and it lives in declick_core.cpp so that the foobar2000 DSP,
 *  the offline CLI and this plug-in are all running the identical code. What
 *  belongs in this file is only what a VST has to do that the others do not:
 *
 *    - notice sample rate and slider changes, since a VST is told about
 *      neither (updateConfig(), in Declick.cpp),
 *    - hand the host n samples out for every n in, which the core manages
 *      through the pre-roll that prime() installs,
 *    - dither on the way down to 32 bit float.
 *
 *  The core is push/pull rather than in-place because it holds cfg.latency
 *  samples of lookahead. prime() has already fed it that many zeros, which
 *  makes available() >= n hold after any push of n, so the pull below can
 *  never come up short and the loop stays a plain one-in-one-out. Those zeros
 *  are also what setInitialDelay() declares: the emitted stream is aligned
 *  with the input, delayed by cfg.latency.
 */

void Declick::processReplacing(float **inputs, float **outputs, VstInt32 sampleFrames)
{
    float* in1  =  inputs[0];
    float* in2  =  inputs[1];
    float* out1 = outputs[0];
    float* out2 = outputs[1];

	//FTZ for the duration of the buffer. The AR fit and the Cholesky solve both
	//run recursions down towards zero, so this is not just a speed guard: it is
	//part of the numerical contract, and the ports only match each other while
	//they all agree about it. foo_dsp_declick holds the same guard.
	declick::scoped_flush_denormals ftz;
	updateConfig();

    while (--sampleFrames >= 0)
    {
		double inputSampleL = *in1;
		double inputSampleR = *in2;

		chanL.push(&inputSampleL, 1, 1);
		chanR.push(&inputSampleR, 1, 1);
		chanL.pull(&inputSampleL, 1, 1);
		chanR.pull(&inputSampleR, 1, 1);
		//non-finite input is silenced inside the core, so nothing can lodge
		//itself in the model or the noise estimate

		//begin 32 bit stereo floating point dither
		int expon; frexpf((float)inputSampleL, &expon);
		fpdL ^= fpdL << 13; fpdL ^= fpdL >> 17; fpdL ^= fpdL << 5;
		inputSampleL += ((double(fpdL)-uint32_t(0x7fffffff)) * 5.5e-36l * pow(2,expon+62));
		frexpf((float)inputSampleR, &expon);
		fpdR ^= fpdR << 13; fpdR ^= fpdR >> 17; fpdR ^= fpdR << 5;
		inputSampleR += ((double(fpdR)-uint32_t(0x7fffffff)) * 5.5e-36l * pow(2,expon+62));
		//end 32 bit stereo floating point dither

		*out1 = inputSampleL;
		*out2 = inputSampleR;

		in1++;
		in2++;
		out1++;
		out2++;
    }
}

void Declick::processDoubleReplacing(double **inputs, double **outputs, VstInt32 sampleFrames)
{
    double* in1  =  inputs[0];
    double* in2  =  inputs[1];
    double* out1 = outputs[0];
    double* out2 = outputs[1];

	declick::scoped_flush_denormals ftz;
	updateConfig();

    while (--sampleFrames >= 0)
    {
		double inputSampleL = *in1;
		double inputSampleR = *in2;

		chanL.push(&inputSampleL, 1, 1);
		chanR.push(&inputSampleR, 1, 1);
		chanL.pull(&inputSampleL, 1, 1);
		chanR.pull(&inputSampleR, 1, 1);

		//begin 64 bit stereo floating point dither
		//int expon; frexp((double)inputSampleL, &expon);
		fpdL ^= fpdL << 13; fpdL ^= fpdL >> 17; fpdL ^= fpdL << 5;
		//inputSampleL += ((double(fpdL)-uint32_t(0x7fffffff)) * 1.1e-44l * pow(2,expon+62));
		//frexp((double)inputSampleR, &expon);
		fpdR ^= fpdR << 13; fpdR ^= fpdR >> 17; fpdR ^= fpdR << 5;
		//inputSampleR += ((double(fpdR)-uint32_t(0x7fffffff)) * 1.1e-44l * pow(2,expon+62));
		//end 64 bit stereo floating point dither
		//this path is left undithered, which also makes it the one to compare
		//against foo_dsp_declick sample for sample

		*out1 = inputSampleL;
		*out2 = inputSampleR;

		in1++;
		in2++;
		out1++;
		out2++;
    }
}
