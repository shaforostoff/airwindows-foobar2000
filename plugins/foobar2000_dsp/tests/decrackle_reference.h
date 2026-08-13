/* ========================================
 *  Literal transcription of the Airwindows DeCrackle VST for use as a test
 *  oracle. Copied from plugins/WinVST/DeCrackle/{DeCrackle.cpp,DeCrackleProc.cpp}
 *  with only these changes:
 *    - getSampleRate() becomes a member,
 *    - the audio buffers are interleaved stereo instead of two planes,
 *    - the fpd seeds are the fixed ones the port uses, so both start level.
 *  Nothing else is touched, including the parts that turn out to be dead code.
 * ======================================== */

#ifndef DECRACKLE_REFERENCE_H
#define DECRACKLE_REFERENCE_H

#include <math.h>
#include <stdint.h>
#include <stddef.h>

namespace reference {

const int kshort = 1600;

class DeCrackleReference {
public:
    float A, B, C, D, E;

    DeCrackleReference() {
        A = 0.5f; B = 0.5f; C = 0.5f; D = 0.5f; E = 1.0f;
        reset();
    }

    void reset() {
        for (int x = 0; x < kshort + 2; x++) { aAL[x] = 0.0; aAR[x] = 0.0; }
        for (int x = 0; x < kshort + 2; x++) { aBL[x] = 0.0; aBR[x] = 0.0; }
        for (int x = 0; x < kshort + 2; x++) { aCL[x] = 0.0; }
        count = 1;
        for (int x = 0; x < 17; x++) {
            iirLSample[x] = 0.0; iirRSample[x] = 0.0; iirCSample[x] = 0.0;
            iirLAngle[x] = 0.0;  iirRAngle[x] = 0.0;
        }
        iirTargetL = 0.0; iirTargetR = 0.0;
        iirClickL = 0.0;  iirClickR = 0.0;
        prevSampleL = 0.0; prevSampleR = 0.0;
        prevSurfaceL = 0.0; prevSurfaceR = 0.0;
        prevOutL = 0.0; prevOutR = 0.0;
        fpdL = 0x2C6A1F3Bu;
        fpdR = 0x5D8E2A97u;
    }

    void processReplacing(float * interleaved, int sampleFrames, double sampleRate) {
        float * in1  = interleaved;
        float * in2  = interleaved + 1;
        float * out1 = interleaved;
        float * out2 = interleaved + 1;

        double overallscale = 1.0;
        overallscale /= 44100.0;
        overallscale *= sampleRate;
        int offset = (int)(overallscale * 1.1);
        double maxHeight = 1.0 * overallscale;
        double filterOut = pow((A * 0.618) + 0.1, 2.0) / overallscale;
        double filterRef = pow(((1.0 - B) * 0.618) + 0.1, 2.0) / overallscale;
        double iirCut = (pow(1.0 - B, 2.0) * 0.2) / overallscale;
        int adjDelay = (int)(B * ((double)(kshort / 8) * overallscale)) - 2.0;
        if (adjDelay > kshort - 1) adjDelay = kshort - 1;
        int halfTrig = fmin(0.5 + pow(B, 3.0), 0.999) * adjDelay;
        int halfRaw = 0.5 * adjDelay;
        int halfBez = 0.5 * adjDelay;
        double threshold = pow(C * 0.618, 2.0) - 0.1;
        double surface = (1.0 - pow(1.0 - D, 3.0)) * 0.9;
        double wet = E;

        while (--sampleFrames >= 0) {
            double inputSampleL = *in1;
            double inputSampleR = *in2;
            if (fabs(inputSampleL) < 1.18e-23) inputSampleL = fpdL * 1.18e-17;
            if (fabs(inputSampleR) < 1.18e-23) inputSampleR = fpdR * 1.18e-17;
            double drySampleL = inputSampleL;
            double drySampleR = inputSampleR;

            inputSampleL -= prevSampleL; prevSampleL = drySampleL;
            inputSampleR -= prevSampleR; prevSampleR = drySampleR;
            inputSampleL *= 16.0; inputSampleR *= 16.0;

            double bezL = drySampleL;
            for (int x = 0; x < 6; x++) {
                iirLAngle[x] = (iirLAngle[x] * (1.0 - filterOut)) + ((bezL - iirLSample[x]) * filterOut);
                bezL = ((iirLSample[x] + (iirLAngle[x] * filterOut)) * (1.0 - filterOut)) + (bezL * filterOut);
                iirLSample[x] = ((iirLSample[x] + (iirLAngle[x] * filterOut)) * (1.0 - filterOut)) + (bezL * filterOut);
            }
            double bezR = drySampleR;
            for (int x = 0; x < 6; x++) {
                iirRAngle[x] = (iirRAngle[x] * (1.0 - filterOut)) + ((bezR - iirRSample[x]) * filterOut);
                bezR = ((iirRSample[x] + (iirRAngle[x] * filterOut)) * (1.0 - filterOut)) + (bezR * filterOut);
                iirRSample[x] = ((iirRSample[x] + (iirRAngle[x] * filterOut)) * (1.0 - filterOut)) + (bezR * filterOut);
            }

            double rect = fabs(drySampleL * drySampleR * 64.0);
            for (int x = 0; x < 6; x++) {
                rect = fabs((iirCSample[x] * (1.0 - filterRef)) + (rect * filterRef));
                iirCSample[x] = (iirCSample[x] * (1.0 - filterRef)) + (rect * filterRef);
            }

            aAL[count] = drySampleL;
            aAR[count] = drySampleR;
            aBL[count] = bezL;
            aBR[count] = bezR;
            aCL[count] = rect;

            count++; if (count < 0 || count > adjDelay) count = 0;
            double nearV = rect;
            double farV = aCL[count - ((count > adjDelay) ? adjDelay + 1 : 0)];

            double prevL = aAL[(count + halfRaw + offset) - (((count + halfRaw + offset) > adjDelay) ? adjDelay + 1 : 0)];
            double prevR = aAR[(count + halfRaw + offset) - (((count + halfRaw + offset) > adjDelay) ? adjDelay + 1 : 0)];
            double outL = aAL[(count + halfRaw) - (((count + halfRaw) > adjDelay) ? adjDelay + 1 : 0)];
            double outR = aAR[(count + halfRaw) - (((count + halfRaw) > adjDelay) ? adjDelay + 1 : 0)];
            double outBezL = aBL[(count + halfBez) - (((count + halfBez) > adjDelay) ? adjDelay + 1 : 0)];
            double outBezR = aBR[(count + halfBez) - (((count + halfBez) > adjDelay) ? adjDelay + 1 : 0)];
            double trigL = aAL[(count + halfTrig) - (((count + halfTrig) > adjDelay) ? adjDelay + 1 : 0)];
            double trigR = aAR[(count + halfTrig) - (((count + halfTrig) > adjDelay) ? adjDelay + 1 : 0)];

            double deClickL = pow(fmax((fabs(trigL) - threshold) - fmax(nearV, farV), 0.0) * 16.0, 3.0);
            double deClickR = pow(fmax((fabs(trigR) - threshold) - fmax(nearV, farV), 0.0) * 16.0, 3.0);
            iirTargetL = fmax(iirTargetL - iirCut, 0.0);
            iirTargetR = fmax(iirTargetR - iirCut, 0.0);
            if (deClickL > iirTargetL) iirTargetL = fmin(deClickL, maxHeight);
            if (deClickR > iirTargetR) iirTargetR = fmin(deClickR, maxHeight);
            if (deClickR * 0.618 > iirTargetL) iirTargetL = fmin(deClickR * 0.618, maxHeight);
            if (deClickL * 0.618 > iirTargetR) iirTargetR = fmin(deClickL * 0.618, maxHeight);
            iirClickL = fmin(iirClickL + iirCut, iirTargetL);
            iirClickR = fmin(iirClickR + iirCut, iirTargetR);
            inputSampleL = (outBezL * fmin(iirClickL, 1.0)) + (outL * (1.0 - fmin(iirClickL, 1.0)));
            inputSampleR = (outBezR * fmin(iirClickR, 1.0)) + (outR * (1.0 - fmin(iirClickR, 1.0)));

            if (wet < 1.0 && wet > 0.0) {
                inputSampleL = (inputSampleL * wet) + (outL * (1.0 - wet));
                inputSampleR = (inputSampleR * wet) + (outR * (1.0 - wet));
            }

            if (wet == 0.0) {
                inputSampleL = outL - inputSampleL;
                inputSampleR = outR - inputSampleR;
            }

            double recordVolume = fmax(fmax(nearV, farV), fmax(prevL, prevR)) + 0.001;
            double surfaceL = sin(fmin((fabs(outL - prevL) / recordVolume) * surface, 3.14159265358979)) * 0.5;
            double surfaceR = sin(fmin((fabs(outR - prevR) / recordVolume) * surface, 3.14159265358979)) * 0.5;
            double gateOnAudio = fmax(surface - (recordVolume * surface * 4.0), 0.0);
            if (surface > 0.0 && wet > 0.0) {
                inputSampleL = (prevOutL * surfaceL) + (inputSampleL * (1.0 - surfaceL));
                inputSampleR = (prevOutR * surfaceR) + (inputSampleR * (1.0 - surfaceR));
                inputSampleL = (prevOutL * gateOnAudio) + (inputSampleL * (1.0 - gateOnAudio));
                inputSampleR = (prevOutR * gateOnAudio) + (inputSampleR * (1.0 - gateOnAudio));
                prevOutL = (prevOutL * gateOnAudio) + (inputSampleL * (1.0 - gateOnAudio));
                prevOutR = (prevOutR * gateOnAudio) + (inputSampleR * (1.0 - gateOnAudio));
            }

            //begin 32 bit stereo floating point dither
            int expon; frexpf((float)inputSampleL, &expon);
            fpdL ^= fpdL << 13; fpdL ^= fpdL >> 17; fpdL ^= fpdL << 5;
            inputSampleL += ((double(fpdL) - uint32_t(0x7fffffff)) * 5.5e-36l * pow(2, expon + 62));
            frexpf((float)inputSampleR, &expon);
            fpdR ^= fpdR << 13; fpdR ^= fpdR >> 17; fpdR ^= fpdR << 5;
            inputSampleR += ((double(fpdR) - uint32_t(0x7fffffff)) * 5.5e-36l * pow(2, expon + 62));
            //end 32 bit stereo floating point dither

            *out1 = (float)inputSampleL;
            *out2 = (float)inputSampleR;

            in1 += 2; in2 += 2; out1 += 2; out2 += 2;
        }
    }

private:
    double aAL[kshort + 5];
    double aBL[kshort + 5];
    double aAR[kshort + 5];
    double aBR[kshort + 5];
    double aCL[kshort + 5];
    int count;
    double iirLSample[18];
    double iirRSample[18];
    double iirLAngle[18];
    double iirRAngle[18];
    double iirCSample[18];
    double iirTargetL, iirTargetR;
    double iirClickL, iirClickR;
    double prevSampleL, prevSampleR;
    double prevSurfaceL, prevSurfaceR;
    double prevOutL, prevOutR;
    uint32_t fpdL, fpdR;
};

} // namespace reference

#endif // DECRACKLE_REFERENCE_H
