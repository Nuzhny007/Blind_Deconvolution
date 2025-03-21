/**
    prida
    main.cpp
    @author Tianyi Shan
    @version 1.0 May/17/2018
*/
#include <iostream>
#include <ctime>

#include <opencv2/opencv.hpp>
#include <opencv2/core/ocl.hpp>

int channel;
double LAMBDA;
int KERNEL_SIZE;
char* PATH;

struct ctf_params_t{
    double lambdaMultiplier;
    double maxLambda;
    double finalLambda, kernelSizeMultiplier;
};

struct uk_t{
    cv::Mat u;
    cv::Mat k;
};

struct params_t{
    double MK;
    double NK;
    double niters;
};

struct input {
    cv::Mat f;
    double MK ;
    double NK ;
    double lambda ;
    double lambdaMultiplier ;
    double scaleMultiplier ;
    double largestLambda ;
};

struct output{
    std::vector<cv::Mat> fp;
    std::vector<double> Mp, Np, MKp, NKp, lambdas;
    int  scales;
} ;

enum ConvolutionType {
    CONVOLUTION_FULL,
    CONVOLUTION_VALID
};

///
/// \brief The FuncTimer class
///
class FuncTimer
{
public:
    FuncTimer(const std::string& name)
        : m_name(name)
    {
        m_startTime = std::chrono::high_resolution_clock::now();
        m_lastPointTime = m_startTime;
    }

    ~FuncTimer()
    {
        auto stopTime = std::chrono::high_resolution_clock::now();
        auto workTime = std::chrono::duration_cast<std::chrono::milliseconds>(stopTime - m_startTime);
        std::cout << "[-" << m_name << "-] work time = " << workTime.count() << " ms\n";
    }

    void SetPoint()
    {
        ++m_pointsCount;
        auto pointTime = std::chrono::high_resolution_clock::now();
        auto workTime = std::chrono::duration_cast<std::chrono::milliseconds>(pointTime - m_lastPointTime);
        std::cout << "[-" << m_name << "-] point " << m_pointsCount << " time = " << workTime.count() << " ms\n";

        m_lastPointTime = pointTime;
    }

private:
    std::chrono::high_resolution_clock::time_point m_startTime;
    std::chrono::high_resolution_clock::time_point m_lastPointTime;
    std::string m_name;
    size_t m_pointsCount = 0;
};

/**********************************************************************************************
 * @param img The input img, Kernel The input kernel, type FULL or VALID, dest The output result
 * Compute conv2 by calling filter2D.
 ************************************************************************************************/
void conv2(  const cv::Mat &img, const cv::Mat& kernel, ConvolutionType type, cv::Mat& dest)
{
    //FuncTimer funcTimer("conv2");

    cv::Mat flipped_kernel;
    cv::flip( kernel, flipped_kernel, -1 );

    cv::Point2i pad;
    cv::Mat padded;

    switch( type ) {
    case CONVOLUTION_VALID:
        padded = img;
        pad = cv::Point2i( kernel.cols - 1, kernel.rows - 1);
        break;

    case CONVOLUTION_FULL:
        pad = cv::Point2i( kernel.cols - 1, kernel.rows - 1);
        padded.create(img.rows + 2*(kernel.rows - 1), img.cols + 2*(kernel.cols - 1), img.type());
        padded.setTo(cv::Scalar::all(0));
        img.copyTo(padded(cv::Rect((kernel.rows - 1), (kernel.cols - 1), img.cols, img.rows)));
        break;

    default:
        throw std::runtime_error("Unsupported convolutional shape");
    }
    cv::Rect region( pad.x / 2, pad.y / 2, padded.cols - pad.x, padded.rows - pad.y);
    cv::filter2D( padded, dest , -1, flipped_kernel, cv::Point(-1, -1), 0, cv::BORDER_CONSTANT );

    dest = dest( region );
}

/***********************************************************************************
 * @param f The scaled input image. dest The result image.
 * Calc total variation for image f
 *
 **********************************************************************************/
void gradTVcc(const cv::Mat &f, cv::Mat &dest)
{
    //FuncTimer funcTimer("gradTVcc");

    cv::Mat fxforw(f.size(), f.type());
    f(cv::Range(1,f.rows),cv::Range(0,f.cols)).copyTo(fxforw);
    cv::copyMakeBorder(fxforw, fxforw,0, 1, 0, 0, cv::BORDER_REPLICATE  );
    fxforw = fxforw - f;

    cv::Mat  fyforw(f.size(), f.type());
    f(cv::Range(0,f.rows),cv::Range(1,f.cols)).copyTo(fyforw);
    cv::copyMakeBorder(fyforw, fyforw,0, 0, 0, 1, cv::BORDER_REPLICATE  );
    fyforw = fyforw - f;

    cv::Mat fxback(f.size(), f.type());
    f(cv::Range(0,f.rows-1),cv::Range(0,f.cols)).copyTo(fxback);
    cv::copyMakeBorder(fxback, fxback,1, 0, 0, 0, cv::BORDER_REPLICATE  );

    cv::Mat fyback(f.size(), f.type());
    f(cv::Range(0,f.rows),cv::Range(0,f.cols-1)).copyTo(fyback);
    cv::copyMakeBorder(fyback, fyback,0, 0, 1, 0, cv::BORDER_REPLICATE  );

    cv::Mat fxmixd(f.size(), f.type());
    f(cv::Range(1,f.rows),cv::Range(0,f.cols-1)).copyTo(fxmixd);
    cv::copyMakeBorder(fxmixd, fxmixd,0, 1, 1, 0, cv::BORDER_REPLICATE  );
    fxmixd = fxmixd - fyback;

    cv::Mat fymixd(f.size(), f.type());
    f(cv::Range(0,f.rows-1),cv::Range(1,f.cols)).copyTo(fymixd);
    cv::copyMakeBorder(fymixd, fymixd,1, 0, 0, 1, cv::BORDER_REPLICATE  );
    fymixd = fymixd - fxback;
    fyback = f - fyback ;
    fxback = f - fxback;

    dest = cv::Mat::zeros(f.size(), CV_64FC3);
    std::vector<cv::Mat> pdest;
    std::vector<cv::Mat> pfxforw, pfyforw, pfxback, pfyback, pfxmixd, pfymixd;
    cv::split(fxforw, pfxforw);
    cv::split(fyforw, pfyforw);
    cv::split(fxback, pfxback);
    cv::split(fyback, pfyback);
    cv::split(fxmixd, pfxmixd);
    cv::split(fymixd, pfymixd);
    cv::split(dest, pdest);

    for (int c = 0; c < channel; ++c)
    {
        cv:: Mat powfx;
        cv::pow(pfxforw[c],2,powfx);
        cv:: Mat powfy;
        cv::pow(pfyforw[c],2,powfy);
        cv:: Mat sqtforw;

        cv::sqrt(powfx + powfy  ,sqtforw);

        cv:: Mat powfxback;
        cv::pow(pfxback[c],2,powfxback);
        cv:: Mat powfymixd;
        cv::pow(pfymixd[c],2,powfymixd);

        cv:: Mat sqtmixed;
        cv::sqrt(powfymixd  + powfxback  ,sqtmixed);

        cv:: Mat powfxmixd;
        cv::pow(pfxmixd[c],2,powfxmixd);
        cv:: Mat powfyback;
        cv::pow(pfyback[c],2,powfyback);
        cv:: Mat sqtback;

        cv::sqrt(powfxmixd  + powfyback,sqtback);

        cv:: Mat max1;
        cv::max( sqtforw,1e-3, max1);
        cv:: Mat max2;
        cv::max( sqtmixed,1e-3, max2);
        cv:: Mat max3;
        cv::max( sqtback,1e-3, max3);

        pdest[c] = (pfxforw[c] + pfyforw[c]) / max1;
        pdest[c] = pdest[c] - pfxback[c]  /  max2;
        pdest[c] = pdest[c] - pfyback[c] / max3;
    }
    if (channel == 1){
        cv::Mat t[] = {pdest[0], pdest[0], pdest[0]};
        cv::merge(t, 3, dest);
    }else {
        cv::merge(pdest, dest);
    }
}

/***********************************************************************************
 * @param f The scaled input image. u The scaled result image.
 *        k The scaled result kernel. lambda The input lambda. params Parameters.
 *        uk The output image and kernel.
 * Initialize gradu and gradk
 * Loop for niters times, call conv2 and gradTVCC to get the result of u and k.
 **********************************************************************************/
void prida(cv::Mat &f, cv::Mat &u, cv::Mat &k, const double lambda, struct params_t params )
{
    FuncTimer funcTimer("prida");

    cv::Mat gradu = cv::Mat::zeros(cv::Size(f.cols + (int) params.NK - 1,
                                            f.rows + (int) params.MK - 1), CV_64FC3);
    cv::Mat gradk = cv::Mat::zeros(k.size(), CV_64F);

    std::vector<cv::Mat> pGradu, pf, pu;
    cv::Mat tmp;
    cv::Mat u_new;
    cv::Mat MDS;
    cv::Mat expTmp;
    std::vector<cv::Mat> pff, puu;
    cv::Mat subconv2;
    cv::Mat rotu;
    cv::Mat majconv2;
    cv::Mat gradTV;
    cv::Mat rotk = cv::Mat::zeros(k.size(), CV_64F);

    for (int i = 0; i < params.niters; i++)
    {
        gradu = cv::Mat::zeros(cv::Size(f.cols + (int) params.NK - 1,
                                        f.rows + (int) params.MK - 1), CV_64FC3);

        cv::split(gradu, pGradu);
        cv::split(f, pf);
        cv::split(u, pu);

        for (int c = 0; c < channel; ++c)
        {
            conv2(pu[c], k, CONVOLUTION_VALID,tmp);
            tmp = tmp - pf[c];
            cv::rotate(k,rotk, cv::ROTATE_180);
            conv2(tmp , rotk, CONVOLUTION_FULL,pGradu[c]);
        }

        if (channel == 1)
        {
            cv::Mat t[] = {pGradu[0], pGradu[0], pGradu[0]};
            cv::merge(t, 3, gradu);
        }
        else if(channel == 3)
        {
            cv::merge(pGradu, gradu);
        }

        gradTVcc(u, gradTV);
        gradu = (gradu - lambda*gradTV);

        double maxValu;
        cv::minMaxLoc(u, nullptr, &maxValu);

        double maxValgu;
        cv::minMaxLoc(cv::abs(gradu), nullptr, &maxValgu);

        double sf = 1e-3 * maxValu / std::max(1e-31, maxValgu);
        u_new = u - sf * gradu;
        gradk.setTo(0);

        cv::split(f, pff);
        cv::split(u, puu);

        for (int c = 0; c < channel; ++c)
        {
            conv2(puu[c], k, CONVOLUTION_VALID, subconv2);
            subconv2 = subconv2 - pff[c];

            cv::rotate(puu[c],rotu, cv::ROTATE_180);

            conv2(rotu, subconv2, CONVOLUTION_VALID, majconv2);
            gradk = gradk + majconv2;
        }

        double maxValk;
        cv::minMaxLoc(k, nullptr, &maxValk);
        double maxValgk;
        cv::minMaxLoc(cv::abs(gradk), nullptr, &maxValgk);

        double sh = 1e-3 * maxValk / std::max(1e-31, maxValgk);
        double eps = DBL_EPSILON;
        cv::Mat etai = sh / (k + eps);

        int bigM = 1000;
        cv::exp((-etai).mul(gradk), expTmp);

        cv::Mat tmp2 = cv::min(expTmp, bigM);
        MDS = k.mul(tmp2);

        k = MDS/cv::sum(MDS)[0];
        u = u_new;
    }
}

/*****************************************************************************************************************
 * @param data Including input image, kernel size, lambda, lambdaMultiplier, scaleMultiplier and largestLambda
 *        answer The dest struct stores all the result
 ****************************************************************************************************************/
struct output buildPyramid(struct input &data, struct output &answer)
{
    FuncTimer funcTimer("buildPyramid");

    double smallestScale = 3;
    int scales = 1;
    double mkpnext = data.MK;
    double nkpnext = data.NK;
    double lamnext = data.lambda;


    cv::Size s = data.f.size();
    int M = s.height;
    int N = s.width;

    while (mkpnext > smallestScale
           && nkpnext > smallestScale
           && lamnext * data.lambdaMultiplier < data.largestLambda)
    {
        scales = scales + 1;
        double lamprev = lamnext;
        double mkpprev = mkpnext;
        double nkpprev = nkpnext;

        // Compute lambda value for the current scale
        lamnext = lamprev * data.lambdaMultiplier;
        mkpnext = round(mkpprev / data.scaleMultiplier);
        nkpnext = round(nkpprev / data.scaleMultiplier);

        // Makes kernel dimension odd
        if (fmod(mkpnext, 2) == 0)
            mkpnext = mkpnext - 1;

        if (fmod(nkpnext,2) == 0)
            nkpnext = nkpnext - 1;

        if (nkpnext == nkpprev)
            nkpnext = nkpnext - 2;

        if (mkpnext == mkpprev)
            mkpnext = mkpnext - 2;

        if (nkpnext < smallestScale)
            nkpnext = smallestScale;

        if (mkpnext < smallestScale)
            mkpnext = smallestScale;
    }

    answer.fp.resize(scales);
    answer.Mp.resize(scales);
    answer.Np.resize(scales);
    answer.MKp.resize(scales);
    answer.NKp.resize(scales);
    answer.lambdas.resize(scales);

    //set the first (finest level) of pyramid to original data
    answer.fp[0] = data.f;
    answer.Mp[0] = M;
    answer.Np[0] = N;
    answer.MKp[0] = data.MK;
    answer.NKp[0] = data.NK;
    answer.lambdas[0] = data.lambda;

    //loop and fill the rest of the pyramid
    for (int s = 1 ; s <scales; s++)
    {
        answer.lambdas[s] = answer.lambdas[s - 1] * data.lambdaMultiplier;

        answer.MKp[s] = round(answer.MKp[s - 1] / data.scaleMultiplier);
        answer.NKp[s] = round(answer.NKp[s - 1] / data.scaleMultiplier);

        // Makes kernel dimension odd
        if (fmod(answer.MKp[s],2) == 0)
            answer.MKp[s] = answer.MKp[s] - 1;

        if (fmod(answer.NKp[s],2) == 0)
            answer.NKp[s] -= 1;

        if (answer.NKp[s] == answer.NKp[s-1])
            answer.NKp[s] -= 2;

        if (answer.MKp[s] == answer.MKp[s-1])
            answer.MKp[s] -= 2;

        if (answer.NKp[s] < smallestScale)
            answer.NKp[s] = smallestScale;

        if (answer.MKp[s] < smallestScale)
            answer.MKp[s] = smallestScale;

        //Correct scaleFactor for kernel dimension correction
        double factorM = answer.MKp[s-1]/answer.MKp[s];
        double factorN = answer.NKp[s-1]/answer.NKp[s];

        answer.Mp[s] = round(answer.Mp[s-1] / factorM);
        answer.Np[s] = round(answer.Np[s-1] / factorN);

        // Makes image dimension odd
        if (fmod(answer.Mp[s],2) == 0)
            answer.Mp[s] -= 1;

        if (fmod(answer.Np[s],2) == 0)
            answer.Np[s] -= 1;

        cv:: Mat dst;
        cv::resize(data.f, dst, cv::Size((int) (answer.Np[s]), (int)(answer.Mp[s])) , 0, 0, cv::INTER_LINEAR);
        answer.fp[s] = dst;
    }
    answer.scales = scales;
    return answer;
}

/***********************************************************************************************
 * @param f The input image. blind_params The kernel size and niters size. params Parameters
 *        uk The output image and kernel
 * Call buildPyrmaid
 * For each layer in the pyrmaid, call Prida
 **********************************************************************************************/
void coarseToFine(cv::Mat f, struct params_t blind_params, struct ctf_params_t params, struct uk_t &uk )
{
    FuncTimer funcTimer("coarseToFine");

    double MK = blind_params.MK;
    double NK = blind_params.NK;

    cv:: Mat u;
    int top = (int)floor(MK/2);
    int left = (int )floor(NK/2);
    cv::copyMakeBorder(f, u, top, top, left, left, cv::BORDER_REPLICATE);

    funcTimer.SetPoint();

    cv::Mat k = cv::Mat::ones(cv::Size((int)MK,(int)NK),CV_64F);
    k = k / MK / NK;

    funcTimer.SetPoint();

    struct input data;
    struct output answer;
    data.MK = MK;
    data.NK = NK;
    data.lambda = params.finalLambda;
    data.lambdaMultiplier = params.lambdaMultiplier;
    data.scaleMultiplier = params.kernelSizeMultiplier;
    data.largestLambda = params.maxLambda;
    data.f = f;
    buildPyramid(data, answer);

    funcTimer.SetPoint();

    for (int i = answer.scales-1; i >=0; i--)
    {
        double Ms = answer.Mp[i];
        double Ns = answer.Np[i];

        double MKs = answer.MKp[i];
        double NKs = answer.NKp[i];
        cv::Mat fs = answer.fp[i];

        double lambda = answer.lambdas[i];

        cv::resize(u, u, cv::Size((int) (Ns + NKs - 1), (int)(Ms + MKs - 1)) , 0, 0, cv::INTER_LINEAR);
        cv::resize(k, k, cv::Size((int) NKs, (int)MKs) , 0, 0, cv::INTER_LINEAR);

        k = k / cv::sum(k)[0];
        blind_params.MK = MKs;
        blind_params.NK = NKs;

        prida(fs, u, k, lambda, blind_params);
        std::cout<< "Working on Scale: " << i+1 <<  " with lambda = "<< data.lambda <<" with pyramid_lambda = " << lambda << " and Kernel size " << MKs << std::endl;
        funcTimer.SetPoint();
    }
    uk.u = u;
    uk.k = k;
}

/********************************************************************************************
 * @param f The input image. lambda The input lambda. params The kernel size and niter size
 *        uk The output image and kernel
 * Convert the image to double precision
 * Adjust the photo size
 * Initialize parameters
 * Call coarseToFine
 *********************************************************************************************/
void blind_deconv(cv::Mat &f, double &lambda,struct params_t &params, struct uk_t &uk)
{
    FuncTimer funcTimer("blind_deconv");

    f.convertTo( f, CV_64F, 1./255. );
    funcTimer.SetPoint();
    cv:: Mat f3;
    int rpad = 0;
    int cpad = 0;
    if (fmod(f.rows,2) == 0)
        rpad = 1;

    if (fmod(f.cols,2) == 0)
        cpad = 1;

    funcTimer.SetPoint();

    f(cv::Range(0,f.rows-rpad),cv::Range(0,f.cols-cpad) ).copyTo(f3);

    funcTimer.SetPoint();

    struct ctf_params_t ctf_params;
    ctf_params.lambdaMultiplier = 1.9;
    ctf_params.maxLambda = 1.1e-1;
    ctf_params.finalLambda = lambda;
    ctf_params.kernelSizeMultiplier = 1.1;

    funcTimer.SetPoint();

    coarseToFine(f3, params, ctf_params, uk);
}

/***********************************************
 * @param img The input image
 *Check the color channel.
 *
 ***********************************************/
bool isGrayImage(cv::Mat img)
{
    FuncTimer funcTimer("isGrayImage");

    cv::Mat dst;
    cv::Mat bgr[3];
    cv::split( img, bgr );
    cv::absdiff( bgr[0], bgr[1], dst );
    if(cv::countNonZero( dst ))
        return false;
    cv::absdiff( bgr[0], bgr[2], dst );
    return !cv::countNonZero( dst );
}

/***********************************************
 * @param image The input image
 * Create structs uk and params
 * Set the niter number to 1000
 * Call blind_deconv
 * Write out the results to the folder
 *
 ***********************************************/
void helper(cv::Mat image)
{
    FuncTimer funcTimer("helper");

    struct uk_t uk;
    struct params_t params;

    params.MK = KERNEL_SIZE; // row
    params.NK = KERNEL_SIZE; // col
    params.niters = 1000;

    funcTimer.SetPoint();

    blind_deconv(image, LAMBDA, params,uk);
    std::ostringstream name;

    funcTimer.SetPoint();

    PATH[strlen(PATH) - 4] = '\0';
    name << PATH << "recov.png";
    std::ostringstream kname;
    kname << PATH << "recovkernel.png";

    funcTimer.SetPoint();

    cv::Mat tmpk, tmpu;
    uk.u.convertTo(tmpu,CV_8U, 1.*255.);
    double ksml, klag;
    cv::minMaxLoc(uk.k,&ksml, &klag);

    funcTimer.SetPoint();

    tmpk = uk.k / klag;
    uk.k.convertTo(tmpk,CV_8U, 1.*255.);
    cv::applyColorMap( tmpk, tmpk, cv::COLORMAP_BONE);
    cv::imwrite( name.str(), tmpu);
    cv::imwrite( kname.str(),  tmpk );

    funcTimer.SetPoint();
}

/***********************************************
 *  Load the Image
 *  Convert the param to double numbers
 *  Determine channel number
 *  Call threadhelp
 ***********************************************/
int main(int argc, char* argv[])
{
    //bool useOCL = true;
    //cv::ocl::setUseOpenCL(useOCL);
    //std::cout << (cv::ocl::useOpenCL() ? "OpenCL is enabled" : "OpenCL not used") << std::endl;

    if(argc < 4)
    {
        printf("usage: deblur IMAGE_PATH LAMBDA KERNEL_SIZE");
        exit(1);
    }

    PATH = argv[1];
    cv::Mat image = cv::imread(PATH);
    if (image.empty())
    {
        std::cerr << "Image " << PATH << " is empty" << std::endl;
        return 1;
    }

    LAMBDA = atof(argv[2]);
    KERNEL_SIZE = atof(argv[3]);

    if(isGrayImage(image))
    {
        channel = 1;
        std::cout<<"The image is gray" <<std::endl;
    }
    else
    {
        channel = 3;
        std::cout <<"The image is color " <<std::endl;
    }
    helper(image);

    return 0;
}
