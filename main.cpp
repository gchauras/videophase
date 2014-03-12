#include "stdafx.h"

using namespace std;
using namespace Halide;
using namespace cv;

Var x("x"), y("y"), c("c"), w("w");

ImageParam ip(Float(32), 3);
ImageParam ip_uint8(UInt(8), 3);

template<typename F0>
double timing(F0 f, int iterations = 1)
{
	auto start = chrono::high_resolution_clock::now();
	for (int i = 0; i < iterations; ++i)
		f();
	auto end = chrono::high_resolution_clock::now();
	double p = (double)chrono::high_resolution_clock::period::num / chrono::high_resolution_clock::period::den;
	return (end - start).count() * p / iterations;
}

template<typename F0>
void printTiming(F0 f, string message = "", int iterations = 1)
{
	if (!message.empty())
		cout << message << flush;
	double t = timing(f, iterations);
	cout << setprecision(15) << t << " s" << endl;
}

template<typename T>
Image<T> load2(string fileName = "images/in.png")
{
	Image<T> im;
	printTiming([&] { im = load<T>(fileName); }, "Loading " + fileName + "... ");
	return im;
}

template<typename T>
void save2(const Image<T>& im, string fileName = "images/out.png")
{
	printTiming([&] { save(im, fileName); }, "Saving to " + fileName + "... ");
}

template<typename T>
Func clipToEdges(const Image<T>& im)
{
	Func f;
	f(x, y, _) = im(clamp(x, 0, im.width() - 1), clamp(y, 0, im.height() - 1), _);
	return f;
}

Func clipToEdges(const ImageParam& im)
{
	Func f;
	f(x, y, _) = im(clamp(x, 0, im.width() - 1), clamp(y, 0, im.height() - 1), _);
	return f;
}

// Downsample with a 1 3 3 1 filter
Func downsample(Func f)
{
    Func downx, downy;

    downx(x, y, _) = (f(2*x-1, y, _) + 3.0f * (f(2*x, y, _) + f(2*x+1, y, _)) + f(2*x+2, y, _)) / 8.0f;
    downy(x, y, _) = (downx(x, 2*y-1, _) + 3.0f * (downx(x, 2*y, _) + downx(x, 2*y+1, _)) + downx(x, 2*y+2, _)) / 8.0f;

    return downy;
}

// Upsample using bilinear interpolation
Func upsample(Func f)
{
    Func upx, upy;

    upx(x, y, _) = 0.25f * f((x/2) - 1 + 2*(x % 2), y, _) + 0.75f * f(x/2, y, _);
    upy(x, y, _) = 0.25f * upx(x, (y/2) - 1 + 2*(y % 2), _) + 0.75f * upx(x, y/2, _);

    return upy;
}

Image<float> toImage(const Mat& mat)
{
    static Func convertFromMat;
    if (!convertFromMat.defined())
    {
        convertFromMat(x, y, c) = ip_uint8(2 - c, x, y) / 255.0f;
        convertFromMat.compile_jit();
    }
    Image<uint8_t> im = Image<uint8_t>(Buffer(UInt(8), mat.channels(), mat.cols, mat.rows, 0, mat.data));
    ip_uint8.set(im);
    return convertFromMat.realize(im.height(), im.channels(), im.width());
}

Image<uint16_t> toImage16(const Mat& mat)
{
    static Func convertFromMat16;
    if (!convertFromMat16.defined())
    {
        convertFromMat16(x, y, c) = cast<uint16_t>(ip_uint8(2 - c, x, y)) * 256;
        convertFromMat16.compile_jit();
    }
    Image<uint8_t> im = Image<uint8_t>(Buffer(UInt(8), mat.channels(), mat.cols, mat.rows, 0, mat.data));
    ip_uint8.set(im);
    return convertFromMat16.realize(im.height(), im.channels(), im.width());
}

Mat toMat(const Image<float>& im)
{
    static Func convertToMat;
    if (!convertToMat.defined())
    {
        convertToMat(c, x, y) = cast<uint8_t>(round(ip(x, y, 2 - c) * 255));
        convertToMat.compile_jit();
    }
    Mat out(im.height(), im.width(), CV_8UC3);
    Image<uint8_t> matIm(Buffer(UInt(8), im.channels(), im.width(), im.height(), 0, out.data));
    ip.set(im);
    convertToMat.realize(matIm);
    return out;
}

// Reconstructs image from Laplacian pyramid
template<int J>
Func reconstruct(ImageParam (&lPyramid)[J])
{
    Func clamped[J];
    for (int i = 0; i < J; i++)
        clamped[i] = clipToEdges(lPyramid[i]);
    Func output[J];
    output[J-1](x, y, _) = clamped[J-1](x, y, _);
    for (int j = J-2; j >= 0; j--)
        output[j](x, y, _) = upsample(output[j+1])(x, y, _) + clamped[j](x, y, _);
    for (int i = 0; i < J; i++)
        output[i].compute_root();
    return output[0];
}

template<int J>
void setImages(ImageParam (&ipArray)[J], Image<float> (&images)[J])
{
    for (int i = 0; i < J; i++)
        ipArray[i].set(images[i]);
}

int main()
{
    // Size of circular buffer
    const int P = 5;
    // Number of pyramid levels
    const int J = 8;
    // The multipliers for each pyramid level.
    const float alpha[J] = { 0, 0, 5, 5, 5, 5, 5, 5 };

    // Circular buffer to hold temporally processed pyramid.
    Image<float> processed[P][J];
    // Circular buffer to hold pyramid after processing.
    Image<float> pyramidBuffer[P][J];
    Image<float> outBuffer[P][J];

    ImageParam ipArray[J];
    for (int i = 0; i < J; i++)
        ipArray[i] = ImageParam(Float(32), 2);
    Func lReconstruct = reconstruct(ipArray);
    // Takes a 16-bit input
    ImageParam input(Float(32), 3);

    cv::VideoCapture capture(0);
    if (!capture.isOpened())
    {
        cerr << "Error when reading video file" << endl;
        return -1;
    }
    int width = capture.get(CV_CAP_PROP_FRAME_WIDTH);
    int height = capture.get(CV_CAP_PROP_FRAME_HEIGHT);
    int channels = 3;

    // Set a boundary condition
    Func clamped;
    clamped(x, y, c) = input(clamp(x, 0, width-1), clamp(y, 0, height-1), c);

    // Get the luminance channel
    Func gray;
    gray(x, y) = 0.299f * clamped(x, y, 0) + 0.587f * clamped(x, y, 1) + 0.114f * clamped(x, y, 2);

    // Make the Gaussian pyramid.
    Func gPyramid[J];
    gPyramid[0](x, y) = gray(x, y);
    for (int j = 1; j < J; j++) {
        gPyramid[j](x, y) = downsample(gPyramid[j-1])(x, y);
    }

    // Get its laplacian pyramid
    Func lPyramid[J];
    lPyramid[J-1](x, y) = gPyramid[J-1](x, y);
    for (int j = J-2; j >= 0; j--) {
        lPyramid[j](x, y) = gPyramid[j](x, y) - upsample(gPyramid[j+1])(x, y);
    }

    Func reconstruction;
    reconstruction(x, y, c) = clamp(lReconstruct(x, y) * clamped(x, y, c) / (0.01f + gray(x, y)), 0.0f, 1.0f);

    // Scheduling
    Var xo, yo, xi, yi;
    gray.compute_root().split(y, y, yi, 4).parallel(y).vectorize(x, 4);
    for (int j = 0; j < 4; j++)
    {
        if (j > 0) gPyramid[j].compute_root().split(y, y, yi, 4).parallel(y).vectorize(x, 4);
    }
    for (int j = 4; j < J; j++)
    {
        gPyramid[j].compute_root();
    }

    Mat frame;
    Mat outMat;
    cv::namedWindow("Out");
    for (int i = 0; i < 10000; i++)
    {
        capture >> frame;
        Image<float> inputIm;
        Image<float> output;
        printTiming([&]
        {
            inputIm = toImage(frame);
            input.set(inputIm);
            // Realize Laplacian pyramid
            for (int j = 0, w = width, h = height; j < J; j++, w /= 2, h /= 2)
            {
                pyramidBuffer[i % P][j] = lPyramid[j].realize(w, h);
                outBuffer[i % P][j] = Image<float>(w, h);
                if (i <= 4)
                    processed[i % P][j] = Image<float>(w, h);
                if (i >= 4)
                {
                    for (int y = 0; y < h; y++)
                        for (int x = 0; x < w; x++)
                        {
                            float result = 1.1430f * processed[(i-2) % P][j](x, y) - 0.4128 * processed[(i-4) % P][j](x, y)
                                            + 0.6389 * pyramidBuffer[i % P][j](x, y) - 1.2779 * pyramidBuffer[(i-2) % P][j](x, y)
                                             + 0.6389 * pyramidBuffer[(i-4) % P][j](x, y);
                            processed[i % P][j](x, y) = result;
                            outBuffer[i % P][j](x, y) = pyramidBuffer[i % P][j](x, y) + alpha[j] * result;
                        }
                }
            }
            setImages(ipArray, outBuffer[i % P]);
            // Reconstruct image from Laplacian pyramid
            output = reconstruction.realize(width, height, channels);
            outMat = toMat(output);
        }, "Processing frame... ");
        cv::imshow("Out", outMat);
        cv::waitKey(30);
    }
}