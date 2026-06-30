#ifndef VML_FT_H
#define VML_FT_H
#ifdef __cplusplus
extern "C" {
#endif
#ifndef complex
#define complex _Complex
#endif
#ifndef _Complex_I
#define _Complex_I (__extension__ 1.0iF)
#endif
#undef I
#define I _Complex_I

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @Brief computes sine of vector elements.
 * @param[in]           len             Number of elements in the vector
 * @param[in]           src         Pointers to the source vectors.
 * @param[out]          dst         Pointer to the destination vector.
 * */
void vsin_f(const int len, const float *src, float *dst);
void vsin_d(const int len, const double *src, double *dst);

/**
 * @Brief computes cos of vector elements.
 * @param[in]           len             Number of elements in the vector
 * @param[in]           src         Pointers to the source vectors.
 * @param[out]          dst         Pointer to the destination vector.
 * */
void vcos_f(const int len, const float *src, float *dst);
void vcos_d(const int len, const double *src, double *dst);

/**
 * @Computes tangent of vector elements.
 * @param[in]           len                     Number of elements in the vector
 * @param[in]           src             Pointers to the source vectors.
 * @param[out]          dst             Pointer to the destination vector.
 * */
void vtan_f(const int len, const float *src, float *dst);
void vtan_d(const int len, const double *src, double *dst);

/**
 * @Brief computes asine of vector elements.
 * @param[in]           len             Number of elements in the vector
 * @param[in]           src         Pointers to the source vectors.
 * @param[out]          dst         Pointer to the destination vector.
 * */
void vasin_f(const int len, const float *src, float *dst);
void vasin_d(const int len, const double *src, double *dst);

/**
 * @Brief computes acos of vector elements.
 * @param[in]           len             Number of elements in the vector
 * @param[in]           src         Pointers to the source vectors.
 * @param[out]          dst         Pointer to the destination vector.
 * */
void vacos_f(const int len, const float *src, float *dst);
void vacos_d(const int len, const double *src, double *dst);

/**
 * @Brief computes atan of vector elements.
 * @param[in]           len             Number of elements in the vector
 * @param[in]           src         Pointers to the source vectors.
 * @param[out]          dst         Pointer to the destination vector.
 * */
void vatan_f(const int len, const float *src, float *dst);
void vatan_d(const int len, const double *src, double *dst);

/**
 * @Computes inverse tangent of vector elements.
 * @param[in]           len                     Number of elements in the vector
 * @param[in]           src             Pointers to the source vectors.
 * @param[out]          dst             Pointer to the destination vector.
 * */
void vatan2_f(const int len, const float *src1, const float *src2, float *dst);
void vatan2_d(const int len, const double *src1, const double *src2, double *dst);

/**
 * @Brief computes cos sin of vector elements.
 * @param[in]           len             Number of elements in the vector
 * @param[in]           src             Pointer to the source vector.
 * @param[out]          sindst          Pointer to the destination vector.
 * @param[out]          cosdst          Pointer to the destination vector.
 * */
void vsincos_f(const int len, const float *src, float *sindst, float *cosdst);
void vsincos_d(const int len, const double *src, double *sindst, double *cosdst);

void vcot_f(const int len, const float *src, float *dst);
void vcot_d(const int len, const double *src, double *dst);
void vcsc_f(const int len, const float *src, float *dst);
void vcsc_d(const int len, const double *src, double *dst);
void vsec_f(const int len, const float *src, float *dst);
void vsec_d(const int len, const double *src, double *dst);

/**
 * @Brief computes sinh of vector elements.
 * @param[in]           len             Number of elements in the vector
 * @param[in]           src         Pointers to the source vectors.
 * @param[out]          dst         Pointer to the destination vector.
 * */
void vsinh_f(const int len, const float *src, float *dst);
void vsinh_d(const int len, const double *src, double *dst);

/**
 * @Brief computes cos of vector elements.
 * @param[in]           len             Number of elements in the vector
 * @param[in]           src         Pointers to the source vectors.
 * @param[out]          dst         Pointer to the destination vector.
 * */
void vcosh_f(const int len, const float *src, float *dst);
void vcosh_d(const int len, const double *src, double *dst);

/**
 * @Brief computes tanh of vector elements.
 * @param[in]           len             Number of elements in the vector
 * @param[in]           src         Pointers to the source vectors.
 * @param[out]          dst         Pointer to the destination vector.
 * */
void vtanh_f(const int len, const float *src, float *dst);
void vtanh_d(const int len, const double *src, double *dst);

/**
 * @Brief computes sinh of vector elements.
 * @param[in]           len             Number of elements in the vector
 * @param[in]           src         Pointers to the source vectors.
 * @param[out]          dst         Pointer to the destination vector.
 * */
void vasinh_f(const int len, const float *src, float *dst);
void vasinh_d(const int len, const double *src, double *dst);

/**
 * @Brief computes cos of vector elements.
 * @param[in]           len             Number of elements in the vector
 * @param[in]           src         Pointers to the source vectors.
 * @param[out]          dst         Pointer to the destination vector.
 * */
void vacosh_f(const int len, const float *src, float *dst);
void vacosh_d(const int len, const double *src, double *dst);

/**
 * @Brief computes atanh of vector elements.
 * @param[in]           len             Number of elements in the vector
 * @param[in]           src         Pointers to the source vectors.
 * @param[out]          dst         Pointer to the destination vector.
 * */
void vatanh_f(const int len, const float *src, float *dst);
void vatanh_d(const int len, const double *src, double *dst);

void cvsinh_d(const int len, const double _Complex *src, double _Complex *dst);
void cvsinh_f(const int len, const float _Complex *src,  float _Complex *dst);
void cvcosh_d(const int len, const double _Complex *src, double _Complex *dst);
void cvcosh_f(const int len, const float _Complex *src,  float _Complex *dst);
void cvtanh_d(const int len, const double _Complex *src, double _Complex *dst);
void cvtanh_f(const int len, const float _Complex *src,  float _Complex *dst);

/**
 * @Brief computes an exponential of vector elements.
 * @param[in]           len                     Number of elements in the vector
 * @param[in]           src             Pointers to the source vectors.
 * @param[out]          dst             Pointer to the destination vector.
 * */
void vexp_f(const int len, const float *src, float *dst);
void vexp_d(const int len, const double *src, double *dst);
void cvexp_f(const int len, const float complex *src, float complex *dst);
void cvexp_d(const int len, const double complex *src, double complex *dst);

/**
 * @Brief computes the base 2 exponential of vector elements
 * @param[in]           len                     Number of elements in the vector
 * @param[in]           src             Pointers to the source vectors.
 * @param[out]          dst             Pointer to the destination vector.
 * */
void vexp2_f(const int len, const float *src, float *dst);
void vexp2_d(const int len, const double *src, double *dst);

/**
 * @Brief computes the base 10 exponential of vector elements
 * @param[in]           len                     Number of elements in the vector
 * @param[in]           src             Pointers to the source vectors.
 * @param[out]          dst             Pointer to the destination vector.
 * */
void vexp10_f(const int len, const float *src, float *dst);
void vexp10_d(const int len, const double *src, double *dst);

/**
 * @Brief computes the base e exponential of vector elements decreased by 1
 * @param[in]           len                     Number of elements in the vector
 * @param[in]           src             Pointers to the source vectors.
 * @param[out]          dst             Pointer to the destination vector.
 * */
void vexpm1_f(const int len, const float *src, float *dst);
void vexpm1_d(const int len, const double *src, double *dst);

/**
 * @Brief computes ln of vector elements.
 * @param[in]           len                     Number of elements in the vector
 * @param[in]           src             Pointers to the source vectors.
 * @param[out]          dst             Pointer to the destination vector.
 * */
void vln_f(const int len, const float *src, float *dst);
void vln_d(const int len, const double *src, double *dst);
void cvln_f(const int len, const float complex *src, float complex *dst);
void cvln_d(const int len, const double complex *src, double complex *dst);

/**
 * @Brief computes log2 of vector elements.
 * @param[in]           len                     Number of elements in the vector
 * @param[in]           src             Pointers to the source vectors.
 * @param[out]          dst             Pointer to the destination vector.
 * */
void vlog2_f(const int len, const float *src, float *dst);
void vlog2_d(const int len, const double *src, double *dst);

/**
 * @Brief computes log10 of vector elements.
 * @param[in]           len                     Number of elements in the vector
 * @param[in]           src             Pointers to the source vectors.
 * @param[out]          dst             Pointer to the destination vector.
 * */
void vlog10_f(const int len, const float *src, float *dst);
void vlog10_d(const int len, const double *src, double *dst);

/**
 * @Brief computes the natural logarithm of vector elements that are increased by 1
 * @param[in]           len                     Number of elements in the vector
 * @param[in]           src             Pointers to the source vectors.
 * @param[out]          dst             Pointer to the destination vector.
 * */
void vlog1p_f(const int len, const float *src, float *dst);
void vlog1p_d(const int len, const double *src, double *dst);

/**
 * @Brief computes pow of vector elements.
 * @param[in]       len         Number of elements in the vector
 * @param[in]       src         Pointers to the source vectors.
 * @param[out]      dst         Pointer to the destination vector.
 * */
void vpow_f(const int len, const float *src1, const float *src2, float *dst);
void vpow_d(const int len, const double *src1, const double *src2, double *dst);

/**
 * @Brief computes the exponents of the elements of input vector
 * @param[in]           len                     Number of elements in the vector
 * @param[in]           src             Pointers to the source vectors.
 * @param[out]          dst             Pointer to the destination vector.
 * */
void vlogb_f(const int len, const float *src, float *dst);
void vlogb_d(const int len, const double *src, double *dst);
void vilogb_f(const int len, const float *src, int *dst);
void vilogb_d(const int len, const double *src, int *dst);

void svpow_d(const int len, const double src1, const double *src2, double *dst);
void svpow_f(const int len, const float src1, const float *src2,  float *dst);

/**
 * @Brief performs element by element squaring of the vector.
 * @param[in]           len                     Number of elements in the vector
 * @param[in]           src             Pointers to the source vectors.
 * @param[out]          dst             Pointer to the destination vector.
 * */
void vsqr_f(const int len, const float *src, float *dst);
void vsqr_d(const int len, const double *src, double *dst);

/**
 * @BriefComputes a square root of vector elements.
 * @param[in]       len         Number of elements in the vector
 * @param[in]       src         Pointers to the source vectors.
 * @param[out]      dst         Pointer to the destination vector.
 **/
void vsqrt_f(const int len, const float *src, float *dst);
void vsqrt_d(const int len, const double *src, double *dst);
void cvsqrt_f(const int len, const float complex *src, float complex *dst);
void cvsqrt_d(const int len, const double complex *src, double complex *dst);

void vrsqrt_d(const int len, const double *src, double *dst);
void vrsqrt_f(const int len, const float *src,  float *dst);

/**
 * @BriefComputes a cube root of vector elements.
 * @param[in]       len         Number of elements in the vector 
 * @param[in]       src         Pointers to the source vectors.
 * @param[out]      dst         Pointer to the destination vector.
 **/ 
void vcbrt_f(const int len, const float *src, float *dst);
void vcbrt_d(const int len, const double *src, double *dst);

/**
 * @Brief computes (src1^2 + src2^2)^0.5 of vector elements.
 * @param[in]           len                     Number of elements in the vector
 * @param[in]           src1            Pointer to the first source vector.
 * @param[in]           src2            Pointer to the second source vector.
 * @param[out]          dst             Pointer to the destination vector.
 * */
void vhypot_f(const int len, const float *src1, const float *src2, float *dst);
void vhypot_d(const int len, const double *src1, const double *src2, double *dst);

void verf_f(const int len, const float *src, float *dst);
void verf_d(const int len, const double *src, double *dst);
void verfc_f(const int len, const float *src, float *dst);
void verfc_d(const int len, const double *src, double *dst);

void vlgamma_f(const int len, const float *src, float *dst);
void vlgamma_d(const int len, const double *src, double *dst);
void vtgamma_f(const int len, const float *src, float *dst);
void vtgamma_d(const int len, const double *src, double *dst);

struct vml_randobject;
typedef struct vml_randobject vml_randstate;
/* random number generator flags */
typedef enum {
    VML_PRNG = 0,
    VML_NPRNG = 1
} vml_rng;
vml_randstate *randcreate(unsigned long int seed, unsigned long int numseqs, unsigned long int, vml_rng portable);
int  randdestroy(vml_randstate *state);
void vrandu_f(const int len ,vml_randstate *state, float *dst);
void cvrandu_f(const int len,vml_randstate *state, float _Complex *dst);
void vrandn_f(const int len, vml_randstate *state, float *dst);
void cvrandn_f(const int len, vml_randstate *state, float _Complex *dst);
void vrandu_d(const int len, vml_randstate *state, double *dst);
void cvrandu_d(const int len, vml_randstate *state, double _Complex *dst);
void vrandn_d(const int len, vml_randstate *state, double *dst);
void cvrandn_d(const int len, vml_randstate *state, double _Complex *dst);

void vllt_d(const int len, const double *src1, const double *src2, unsigned int *dst);
void vllt_f(const int len, const float *src1, const float *src2, unsigned int *dst);
void vlle_d(const int len, const double *src1, const double *src2, unsigned int *dst);
void vlle_f(const int len, const float *src1, const float *src2, unsigned int *dst);
void vlgt_d(const int len, const double *src1, const double *src2, unsigned int *dst);
void vlgt_f(const int len, const float *src1, const float *src2, unsigned int *dst);
void vlge_d(const int len, const double *src1, const double *src2, unsigned int *dst);
void vlge_f(const int len, const float *src1, const float *src2, unsigned int *dst);
void vleq_d(const int len, const double *src1, const double *src2, unsigned int *dst);
void vleq_f(const int len, const float *src1, const float *src2, unsigned int *dst);
void vlne_d(const int len, const double *src1, const double *src2, unsigned int *dst);
void vlne_f(const int len, const float *src1, const float *src2, unsigned int *dst);
void vllt_i(const int len, const int *src1, const int *src2, unsigned int *dst);
void vlle_i(const int len, const int *src1, const int *src2, unsigned int *dst);
void vlgt_i(const int len, const int *src1, const int *src2, unsigned int *dst);
void vlge_i(const int len, const int *src1, const int *src2, unsigned int *dst);
void vleq_i(const int len, const int *src1, const int *src2, unsigned int *dst);
void vlne_i(const int len, const int *src1, const int *src2, unsigned int *dst);
void vmin_d(const int len, const double *src1, const double *src2, double *dst);
void vmin_f(const int len, const float *src1, const float *src2, float *dst);
void vmax_d(const int len, const double *src1, const double *src2, double *dst);
void vmax_f(const int len, const float *src1, const float *src2, float *dst);
void vminmg_d(const int len, const double *src1, const double *src2, double *dst);
void vminmg_f(const int len, const float *src1, const float *src2, float *dst);
void vmaxmg_d(const int len, const double *src1, const double *src2, double *dst);
void vmaxmg_f(const int len, const float *src1, const float *src2, float *dst);
double vmaxval_d(const int len, const double *src, unsigned int *indx);
float vmaxval_f(const int len, const float *src, unsigned int *indx);
double vminval_d(const int len, const double *src, unsigned int *indx);
float vminval_f(const int len, const float *src, unsigned int *indx);
double vmaxmgval_d(const int len, const double *src, unsigned int *indx);
float vmaxmgval_f(const int len, const float *src, unsigned int *indx);
double vminmgval_d(const int len, const double *src, unsigned int *indx);
float vminmgval_f(const int len, const float *src, unsigned int *indx);
void vcminmgsq_d(const int len, const double _Complex *src1, const double _Complex *src2, double *dst);
void vcminmgsq_f(const int len, const float _Complex *src1, const float _Complex *src2, float *dst);
void vcmaxmgsq_d(const int len, const double _Complex *src1, const double _Complex *src2, double *dst);
void vcmaxmgsq_f(const int len, const float _Complex *src1, const float _Complex *src2, float *dst);

void vfill_d(const int len, const double src, double *dst);
void vfill_f(const int len, const float src, float *dst);
void cvfill_d(const int len, const double _Complex src, double _Complex *dst);
void cvfill_f(const int len, const float _Complex src, float _Complex *dst);
void cvreal_d(const int len, const double _Complex *src,  double *dst); 
void cvreal_f(const int len, const float _Complex *src, float *dst);
void cvimag_d(const int len, const double _Complex *src, double *dst);
void cvimag_f(const int len, const float _Complex *src, float *dst);
void vswap_d(const int len, const double *src1,const double *src2);
void vswap_f(const int len, const float *src1, const float *src2);
void cvswap_d(const int len, const double _Complex *src1, const double _Complex *src2);
void cvswap_f(const int len, const float _Complex *src1, const float _Complex *src2);
void vcmplex_d(const int len, const double *src1, const double *src2, double _Complex *dst);
void vcmplex_f(const int len, const float *src1, const float *src2, float _Complex *dst);

/**
 * @Brief Adds the elements of two vectors.
 * @param[in]           len                     Number of elements in the vector
 * @param[in]           src1, src2      Pointers to the source vectors.
 * @param[out]          dst             Pointer to the destination vector.
 * */
void vadd_f(const int len, const float *src1, const float *src2, float *dst);
void vadd_d(const int len, const double *src1, const double *src2, double *dst);
void cvadd_f(const int len, const float complex *src1, const float complex *src2, float complex *dst);
void cvadd_d(const int len, const double complex *src1, const double complex *src2, double complex *dst);

void svadd_d(const int len, const double src1, const double *src2, double *dst);
void svadd_f(const int len, const float src1, const float *src2, float *dst);
void csvadd_d(const int len, const double _Complex src1, const double _Complex *src2,  double _Complex *dst);
void csvadd_f(const int len, const float _Complex src1, const float _Complex *src2,  float _Complex *dst);
void rcvadd_d(const int len, const double *src1, const double _Complex *src2,  double _Complex *dst);
void rcvadd_f(const int len, const float *src1, const float _Complex *src2,  float _Complex *dst);
void rscvadd_d(const int len, const double src1, const double _Complex *src2,  double _Complex *dst);
void rscvadd_f(const int len, const float src1, const float _Complex *src2,  float _Complex *dst);
void svadd_i(const int len, const int src1,const int *src2, int *dst);
void vadd_i(const int len, const int *src1,const int *src2, int *dst);

/**
 * @Brief Subs the elements of two vectors.
 * @param[in]           len                     Number of elements in the vector
 * @param[in]           src1, src2      Pointers to the source vectors.
 * @param[out]          dst             Pointer to the destination vector.
 * */
void vsub_f(const int len, const float *src1, const float *src2, float *dst);
void vsub_d(const int len, const double *src1, const double *src2, double *dst);
void cvsub_f(const int len, const float complex *src1, const float complex *src2, float complex *dst);
void cvsub_d(const int len, const double complex *src1, const double complex *src2, double complex *dst);

void svsub_d(const int len, const double src1, const double *src2, double *dst);
void svsub_f(const int len, const float src1, const float *src2, float *dst);
void vssub_d(const int len, const double *src1, const double src2, double *dst);
void vssub_f(const int len, const float *src1, const float src2,  float *dst);
void csvsub_d(const int len, const double _Complex src1, const double _Complex *src2,  double _Complex *dst);
void csvsub_f(const int len, const float _Complex src1, const float _Complex *src2,  float _Complex *dst);
void rcvsub_d(const int len, const double *src1, const double _Complex *src2,  double _Complex *dst);
void rcvsub_f(const int len, const float *src1, const float _Complex *src2,  float _Complex *dst);
void rscvsub_d(const int len, const double src1, const double _Complex *src2,  double _Complex *dst);
void rscvsub_f(const int len, const float src1, const float _Complex *src2,  float _Complex *dst);
void crvsub_f(const int len, const float _Complex *src1, const float *src2,  float _Complex *dst);
void crvsub_d(const int len, const double _Complex *src1, const double *src2,  double _Complex *dst);
void svsub_i(const int len, const int src1,const int *src2, int *dst);
void vsub_i(const int len, const int *src1,const int *src2, int *dst);

/**
 * @Brief Performs element by element multiplication of vector.
 * @param[in]       len         Number of elements in the vector
 * @param[in]       src1, src2  Pointers to the source vectors.
 * @param[out]      p _dst      Pointer to the destination vector.
 **/
void vmul_f(const int len, const float *src1, const float *src2, float *dst);
void vmul_d(const int len, const double *src1, const double *src2, double *dst);
void cvmul_f(const int len, const float complex *src1, const float complex *src2, float complex *dst);
void cvmul_d(const int len, const double complex *src1, const double complex *src2, double complex *dst);

void csvmul_d(const int len, const double _Complex src1, const double _Complex *src2,  double _Complex *dst);
void csvmul_f(const int len, const float _Complex src1, const float _Complex *src2,  float _Complex *dst);
void rcvmul_d(const int len, const double *src1, const double _Complex *src2,  double _Complex *dst);
void rcvmul_f(const int len, const float *src1, const float _Complex *src2,  float _Complex *dst);
void rscvmul_d(const int len, const double src1, const double _Complex *src2,  double _Complex *dst);
void rscvmul_f(const int len, const float src1, const float _Complex *src2,  float _Complex *dst);
void svmul_d(const int len, const double src1, const double *src2, double *dst);
void svmul_f(const int len, const float src1, const float *src2,  float *dst);
void cvjmul_d(const int len, const double _Complex *src1, const double _Complex *src2,  double _Complex *dst);
void cvjmul_f(const int len, const float _Complex *src1, const float _Complex *src2,  float _Complex *dst);
void vmul_i(const int len, const int *src1,const int *src2, int *dst);
void svmul_i(const int len, const int src1,const int *src2, int *dst);

/**
 * @Brief performs element by element division of vector. 
 * @param[in]       len         Number of elements in the vector
 * @param[in]       src1, src2  Pointers to the source vectors.
 * @param[out]      p _dst      Pointer to the destination vector.
 **/
void vdiv_f(const int len, const float *src1, const float *src2, float *dst);
void vdiv_d(const int len, const double *src1, const double *src2, double *dst);
void cvdiv_f(const int len, const float _Complex *src1, const float _Complex *src2, float _Complex *dst);
void cvdiv_d(const int len, const double _Complex *src1, const double _Complex *src2, double _Complex *dst);

void crvdiv_f(const int len, const float _Complex *src1, const float *src2, float _Complex *dst);
void crvdiv_d(const int len, const double _Complex *src1, const double *src2, double _Complex *dst);
void csvdiv_f(const int len, const float _Complex src1, const float _Complex *src2, float _Complex *dst);
void csvdiv_d(const int len, const double _Complex src1, const double _Complex *src2, double _Complex *dst );
void cvrsdiv_d(const int len, const double _Complex *src1, const double src2, double _Complex *dst );
void cvrsdiv_f(const int len, const float _Complex *src1, const float src2, float _Complex *dst );
void rcvdiv_d(const int len, const double *src1, const double _Complex *src2, double _Complex *dst);
void rcvdiv_f(const int len, const float *src1, const float _Complex *src2, float _Complex *dst);
void svdiv_d(const int len, const double src1, const double *src2, double *dst);
void svdiv_f(const int len, const float src1, const float *src2, float *dst);
void rscvdiv_d(const int len, const double src1, const double _Complex *src2, double _Complex *dst);
void rscvdiv_f(const int len, const float src1, const float _Complex *src2, float _Complex *dst);
void vsdiv_d(const int len, const double *src1, const double src2, double *dst);
void vsdiv_f(const int len, const float *src1, const float src2, float *dst);

void vam_d(const int len, const double *src1, const double *src2, const double *src3,double *dst);
void vam_f(const int len, const float *src1, const float *src2, const float *src3, float *dst);
void vma_d(const int len, const double *src1, const double *src2, const double *src3,double *dst);
void vma_f(const int len, const float *src1, const float *src2, const float *src3, float *dst);
void vsbm_d(const int len, const double *src1, const double *src2, const double *src3,double *dst);
void vsbm_f(const int len, const float *src1, const float *src2, const float *src3, float *dst);
void vmsb_d(const int len, const double *src1, const double *src2, const double *src3,double *dst);
void vmsb_f(const int len, const float *src1, const float *src2, const float *src3, float *dst);
void vsam_d(const int len, const double *src1, const double src2, const double *src3, double *dst);
void vsam_f(const int len, const float *src1, const float src2, const float *src3,  float *dst);
void vsma_d(const int len, const double *src1, const double src2, const double *src3, double *dst);
void vsma_f(const int len, const float *src1, const float src2, const float *src3,  float *dst);
void vmsa_d(const int len, const double *src1, const double *src2, const double src3, double *dst);
void vmsa_f(const int len, const float *src1, const float *src2, const float src3,  float *dst);
void vsmsa_d(const int len, const double *src1, const double src2, const double src3, double *dst);
void vsmsa_f(const int len, const float *src1, const float src2, const float src3,  float *dst);

void cvam_d(const int len, const double _Complex *src1, const double _Complex *src2, const double _Complex *src3,  double _Complex *dst);
void cvam_f(const int len, const float _Complex *src1, const float _Complex *src2, const float _Complex *src3,  float _Complex *dst);
void cvma_d(const int len, const double _Complex *src1, const double _Complex *src2, const double _Complex *src3,  double _Complex *dst);
void cvma_f(const int len, const float _Complex *src1, const float _Complex *src2, const float _Complex *src3,  float _Complex *dst);
void cvsbm_d(const int len, const double _Complex *src1, const double _Complex *src2, const double _Complex *src3,  double _Complex *dst);
void cvsbm_f(const int len, const float _Complex *src1, const float _Complex *src2, const float _Complex *src3,  float _Complex *dst);
void cvmsb_d(const int len, const double _Complex *src1, const double _Complex *src2, const double _Complex *src3,  double _Complex *dst);
void cvmsb_f(const int len, const float _Complex *src1, const float _Complex *src2, const float _Complex *src3,  float _Complex *dst);
void cvsam_d(const int len, const double _Complex *src1, const double _Complex src2, const double _Complex *src3,  double _Complex *dst);
void cvsam_f(const int len, const float _Complex *src1, const float _Complex src2, const float _Complex *src3,  float _Complex *dst);
void cvsma_d(const int len, const double _Complex *src1, const double _Complex src2, const double _Complex *src3,  double _Complex *dst);
void cvsma_f(const int len, const float _Complex *src1, const float _Complex src2, const float _Complex *src3,  float _Complex *dst);
void cvmsa_d(const int len, const double _Complex *src1, const double _Complex *src2, const double _Complex src3,  double _Complex *dst);
void cvmsa_f(const int len, const float _Complex *src1, const float _Complex *src2, const float _Complex src3,  float _Complex *dst);
void cvsmsa_d(const int len, const double _Complex *src1, const double _Complex src2, const double _Complex src3,  double _Complex *dst);
void cvsmsa_f(const int len, const float _Complex *src1, const float _Complex src2, const float _Complex src3,  float _Complex *dst);

/**
 * @Brief computes abs of vector elements.
 * @param[in]           len             Number of elements in the vector
 * @param[in]           src         Pointer to the source vector.
 * @param[out]          dst         Pointer to the destination vector.
 * */
void vmag_f(const int len, const float *src, float *dst);
void vmag_d(const int len, const double *src, double *dst);
void cvmag_f(const int len, const float complex *src, float *dst);
void cvmag_d(const int len, const double complex *src, double *dst);

void vcmagsq_d(const int len, const double _Complex *src, double *dst);
void vcmagsq_f(const int len, const float _Complex *src,  float *dst);
/**
 * @Brief computes the argument of vector elements.
 * @param[in]           len             Number of elements in the vector
 * @param[in]           src         Pointer to the source vector.
 * @param[out]          dst         Pointer to the destination vector.
 * */
void varg_f(const int len, const float complex *src, float *dst);
void varg_d(const int len, const double complex *src, double *dst);

/**
 * @Brief performs element by element division of vector.
 * @param[in]       len         Number of elements in the vector
 * @param[in]       src         Pointer to the source vector.
 * @param[out]      dst         Pointer to the destination vector.
 **/
void cvconj_f(const int len, const float complex *src, float complex *dst);
void cvconj_d(const int len, const double complex *src, double complex *dst);

/**
 * @Brief computes modulus of vector elements.
 * @param[in]       len         Number of elements in the vector
 * @param[in]       src         Pointer to the source vector.
 * @param[out]      dst         Pointer to the destination vector.
 * */
void vfmod_f(const int len, const float *src1, const float *src2, float *dst);
void vfmod_d(const int len, const double *src1, const double *src2, double *dst);

/**
 * @Brief computes inv of vector elements.
 * @param[in]           len         Number of elements in the vector
 * @param[in]           src         Pointers to the source vectors.
 * @param[out]          dst         Pointer to the destination vector.
 * */
void vinv_f(const int len, const float *src, float *dst);
void vinv_d(const int len, const double *src, double *dst);

void vrint_f(const int len, const float *src, float *dst);
void vrint_d(const int len, const double *src, double *dst);

void vtrunc_f(const int len, const float *src, float *dst);
void vtrunc_d(const int len, const double *src, double *dst);

void vfloor_f(const int len, const float *src, float *dst);
void vfloor_d(const int len, const double *src, double *dst);
void vround_f(const int len, const float *src, float *dst);
void vround_d(const int len, const double *src, double *dst);
void vceil_f(const int len, const float *src, float *dst);
void vceil_d(const int len, const double *src, double *dst);

void vfdim_f(const int len, const float *src1, const float *src2, float *dst);
void vfdim_d(const int len, const double *src1, const double *src2, double *dst);

void cvneg_d(const int len, const double _Complex *src,  double _Complex *dst);
void cvneg_f(const int len, const float _Complex *src,  float _Complex *dst);
void vneg_d(const int len, const double *src, double *dst);
void vneg_f(const int len, const float *src,  float *dst);

void vnot_i(const int len,const int *src, int *dst);
void vand_i(const int len,const int *src1,const  int *src2, int *dst);
void vxor_i(const int len,const  int *src1,const  int *src2, int *dst);
void vor_i(const int len, const int *src1, const int *src2, int *dst);
void vmag_i(const int len, const int *src, int *dst);
void vneg_i(const int len, const int *src, int *dst);

void vfrexp_f(const int len, const float *src, int *dst1,  float *dst2);
void vfrexp_d(const int len, const double *src, int *dst1, double *dst2);
void vldexp_f(const int len, const float *src1, const int *src2, float *dst);
void vldexp_d(const int len, const double *src1, const int *src2,double *dst);
void vmodf_f(const int len, const float *src, float *dst1, float *dst2);
void vmodf_d(const int len, const double *src, double *dst1, double *dst2);
void vscalbn_f(const int len, const float *src1, const int *src2, float *dst);
void vscalbn_d(const int len, const double *src1, const int *src2, double *dst);
/**
 * @Brief computes remainder of vector elements of a and b.
 * @param[in]       len         Number of elements in the vector
 * @param[in]       src1        Pointer to the source vector a.
 * @param[in]       src2        Pointer to the source vector b.
 * @param[out]      dst         Pointer to the destination vector.
 * */
void vremainder_f(const int len, const float *src1, const float *src2, float *dst);
void vremainder_d(const int len, const double *src1, const double *src2, double *dst);

void vnearbyint_f(const int len, const float *src, float *dst);
void vnearbyint_d(const int len, const double *src, double *dst);

void vremquo_f(const int len, const float *src1, const float *src2, const int *dst1, float *dst2);
void vremquo_d(const int len, const double *src1, const double *src2, const int *dst1, double *dst2);

void vcopysign_f(const int len, const float *src1, const float *src2, float *dst);
void vcopysign_d(const int len, const double *src1, const double *src2, double *dst);

void vnextafter_d(const int len, const double *src1, const double *src2, double *dst);
void vnextafter_f(const int len, const float *src1, const float *src2, float *dst);
void vnexttoward_d(const int len, const double *src1, const long double *src2, double *dst);
void vnexttoward_f(const int len, const float *src1, const long double *src2, float *dst);

void cvpolar_f(const int len, const float _Complex *src, float *dst1, float *dst2);
void cvpolar_d(const int len, const double _Complex *src, double *dst1, double *dst2);
void vrect_f(const int len, const float *src1, const float *src2, float _Complex *dst);
void vrect_d(const int len, const double *src1, const double *src2, double _Complex *dst);

double vsumval_d(const int len, const double *src);
float vsumval_f(const int len, const float *src);
double vsumsqval_d(const int len, const double *src);
float vsumsqval_f(const int len, const float *src);
double vmeanval_d(const int len, const double *src);
float vmeanval_f(const int len, const float *src);
double vmeansqval_d(const int len, const double *src);
float vmeansqval_f(const int len, const float *src);

double _Complex  cvsumval_d(const int len, const double _Complex *src);
float _Complex cvsumval_f(const int len, const float _Complex *src);
double _Complex  cvmeanval_d(const int len, const double _Complex *src);
float _Complex cvmeanval_f(const int len, const float _Complex *src);
double  cvmeansqval_d(const int len, const double _Complex *src);
float cvmeansqval_f(const int len, const float _Complex *src);

int  vsumval_i(const int len,const int *src);

double cvmodulate_d(const int len, const double _Complex *src, const double nu, const double phi, double _Complex *dst);
float cvmodulate_f(const int len, const float _Complex *src, const float nu, const float phi, float _Complex *dst);
double vmodulate_d(const int len, const double *src, const double nu, const double phi, double _Complex *dst);
float vmodulate_f(const int len, const float *src, const float nu, const float phi, float _Complex *dst);
void vcumsum_d(const int len, const double *src, double *dst);
void vcumsum_f(const int len, const float *src, float *dst);
double vlagr_d(const int len, const double x,const double *src1, const double *src2, const unsigned int low,const  unsigned int high);
void vexpoavg_d(const int len, const double alpha, const double *src, double *dst);
void vexpoavg_f(const int len, const float alpha, const float *src, float *dst);
void cvexpoavg_d(const int len, const double alpha, const double _Complex *src, double _Complex *dst);
void cvexpoavg_f(const int len, const float alpha, const float _Complex *src, float _Complex *dst);
void vramp_d(const int len, const double x, const double y, double *dst);
void vramp_f(const int len, const float x, const float y, float *dst );
double vdot_d(const int len, const double *src1, const double *src2);
float vdot_f(const int len, const float *src1, const float *src2);
double _Complex cvdot_d(const int len, const double _Complex *src1, const double _Complex *src2);
float _Complex cvdot_f(const int len, const float _Complex *src1, const float _Complex *src2);
double _Complex cvjdot_d(const int len, const double _Complex *src1, const double _Complex *src2);
float _Complex cvjdot_f(const int len, const float _Complex *src1, const float _Complex *src2);
double _Complex rcvdot_d(const int len, const double *src1, const double _Complex *src2);
float _Complex rcvdot_f(const int len, const float *src1, const float _Complex *src2);
void veuler_d(const int len, const double *src, double _Complex *dst);
void veuler_f(const int len, const float *src, float _Complex *dst);
void vcumsum_i(const int len, const int *src, int *dst);
void vramp_i(const int len, const int x, const int y, int *dst);

/* const int len           const float _Complex            const double _Complex */
#ifdef __cplusplus
}
#endif

#endif
