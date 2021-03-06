#pragma once

class RieszMagnifier
{
public:
	RieszMagnifier(int channels, Halide::Type type, int pyramidLevels = 5);

    void compileJIT(bool tile);
    void bindJIT(
            float a1,
            float a2,
            float b0,
            float b1,
            float b2,
            float alpha,
            int stabilize,
            std::vector<Halide::Image<float>> historyBuffer,
            std::vector<Halide::Image<float>> amplitudeBuffer);

    void process(Halide::Buffer frame, Halide::Buffer out);
	void computeBandSigmas();

    void compute_ref_amplitude(Halide::Buffer frame, std::vector<Halide::Image<float>> amplitudeBuff);
	int getPyramidLevels();

private:
	void schedule(bool tile);

	static const int CIRCBUFFER_SIZE = 2;
	static const int NUM_BUFFER_TYPES = 7;

	// Spatial regularization
	std::vector<float> bandSigma;

	int channels;

	int pyramidLevels;

	// Input params
	Halide::ImageParam input;

    // Filter coefficients
	Halide::Param<float> a1, a2, b0, b1, b2;

    // Amplification coefficients
	Halide::Param<float> alpha;

    // 4D buffer: For each pyramid level, an image of size width x height x type x circular buffer index.
	// Types:
	// ------
	// 0: pyramidBuffer
	// 1: phaseCBuffer
	// 2: phaseSBuffer
	// 3: lowpass1CBuffer
	// 4: lowpass2CBuffer
	// 5: lowpass1SBuffer
	// 6: lowpass2SBuffer
	std::vector<Halide::ImageParam> historyBuffer;

    // reference amplitude
    std::vector<Halide::ImageParam>   amplitudeBuffer;
    std::vector<Halide::Image<float>> amplitudeBuffer_img;

    // Current frame modulo 2. (For circular buffer).
	Halide::Param<int> pParam;

    // Use amplitude stabilization or not
	Halide::Param<int> stabilize;

	// Funcs
	std::vector<Halide::Func>
		gPyramidDownX,
		gPyramid,
		lPyramidUpX,
		lPyramid_orig,
		lPyramid,
		lPyramidCopy,
		clampedPyramidBuffer,
		r1Pyramid,
		r1Prev,
		r2Pyramid,
		r2Prev,
        phi_diff,
        qPhaseDiffC,
		qPhaseDiffS,
		phaseC,
		phaseS,
		phaseCCopy,
		phaseSCopy,
		changeC,
		lowpass1C,
		lowpass2C,
		changeS,
		lowpass1S,
		lowpass2S,
		lowpass1CCopy,
		lowpass2CCopy,
		lowpass1SCopy,
		lowpass2SCopy,
		changeCTuple,
		changeSTuple,
		changeC2,
		changeS2,
		amp,
		ampPrev,
		amp_orig,
		changeCAmp,
		changeCRegX,
		changeCReg,
		changeSAmp,
		changeSRegX,
		changeSReg,
		ampRegX,
		ampReg,
		magC,
		pair,
		outLPyramid,
		outGPyramidUpX,
		outGPyramid;

	Halide::Func floatOutput;
    Halide::Func output;

	int frameCounter;
};
