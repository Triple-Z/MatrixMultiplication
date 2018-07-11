
#include <iostream>
#include <cstdlib>
#include <cstring>
#include "microtime.h"
#include <stdio.h>

#include <immintrin.h>

#include "tile.h"

#ifdef _MKL_
#include <mkl_cblas.h>
#include <mkl.h>
#elif _OPENBLAS_
#include <cblas.h>
#endif

#ifndef D
#define D 1024
#endif

#ifndef S 
#define S 4
#endif

void MY_MMult( int m, int n, int k, double *a, int lda, 
                                    double *b, int ldb,
                                    double *c, int ldc );

void MKL_MMult( int m, int n, int k, double *a, int lda,
                                     double *b, int ldb,
                                     double *c, int ldc);


void MKL_MMult( int m, int n, int k, double *a, int lda,
                                     double *b, int ldb,
                                     double *c, int ldc)
{

    double* tmpC = (double*) _mm_malloc ( sizeof(double) * D * D, 64);
    memcpy(tmpC,c, sizeof(double)*D*D);

#ifdef _MKL_
	const CBLAS_LAYOUT Order=CblasRowMajor;
	const CBLAS_TRANSPOSE TransA=CblasNoTrans;
	const CBLAS_TRANSPOSE TransB=CblasNoTrans;
#elif _OPENBLAS_
	const enum CBLAS_ORDER Order=CblasRowMajor;
	const enum CBLAS_TRANSPOSE TransA=CblasNoTrans;
	const enum CBLAS_TRANSPOSE TransB=CblasNoTrans;
#endif
	const int M= m;//numRows of A; numCols of C
	const int N= n;//numCols of B and C
	const int K= k;//numCols of A; numRows of B
	const float alpha=1;
	const float beta=1;

#ifdef _MKL_
    mkl_set_num_threads(1);
#endif
    
    
    double t1 = microtime();
    cblas_dgemm(CblasRowMajor, TransA, TransB, M, N, K, alpha, a, lda, b,
      ldb, beta, c, N);
    double t2 = microtime();
    std::cout<<" Standard: elapsed time when D="<< D<<" is "<< t2 - t1 << "s"<<std::endl;


    for(int i=0; i<10; i++)
    {
        double t1 = microtime();
        cblas_dgemm(CblasRowMajor, TransA, TransB, M, N, K, alpha, a, lda, b,
          ldb, beta, tmpC, N);
        
        double t2 = microtime();
        std::cout<<" c[0] = "<< tmpC[0]<<std::endl;

        std::cout<<" elapsed time when D="<< D<<" is "<< t2 - t1 << "s"<<std::endl;
    }
}

int main(int argc, char** argv)
{

    double* A = (double*) _mm_malloc ( sizeof(double) * D * (D+1), 64);
    double* B = (double*) _mm_malloc ( sizeof(double) * D * D, 64);
    double* C = (double*) _mm_malloc ( sizeof(double) * D * D, 64);
    double* C2 = (double*) _mm_malloc ( sizeof(double) * D * D, 64);
    double* refC = (double*) _mm_malloc ( sizeof(double) * D * D, 64);


//    srand(292);

    for(int i=0; i< D*D; i++)
    {
        
        A[i] = (double(i%100)/2);
        B[i] = (double(i%100)/2);
        C[i] = (double(i%100)/2);

    }
    
    memcpy(C2,C, sizeof(double)*D*D);
        
    double t1 = microtime();
    MY_MMult(D, D, D, A, D, B, D, C, D);
    double t2 = microtime();
    std::cout<<" Standard: elapsed time when D="<< D<<" is "<< t2 - t1 << "s"<<std::endl;


    for(int i=0; i<10; i++)
    {
        double t1 = microtime();
        MY_MMult(D, D, D, A, D, B, D, C2, D);
        
        double t2 = microtime();
        std::cout<<" C[0] = "<< C2[0]<<std::endl;

        std::cout<<" elapsed time when D="<< D<<" is "<< t2 - t1 << "s"<<std::endl;

    }

    MKL_MMult(D, D, D, A, D, B, D, refC, D);


    int err=0;
    for(int i=0; i < D*D; i++)
    {
       if(abs(refC[i] - C[i]) > 0.001*C[i])
       {
           err++;
           printf(" refC[%d] = %f; C[%d] = %f \n", i, refC[i], i, C[i]);
       }
//       std::cout<<" refC["<<i<<"] = "<<refC[i]<<"; C["<<i<<"] = "<<C[i]<<std::endl;
    }

    if(err==0)
        std::cout<<" Check Pass! "<<std::endl;
    else
        std::cout<<" "<<err<<" errors occurred"<<std::endl;

}

/* Block sizes */
#define kc 256
#define nc 1024
#define mcc 64
#define ncc 32
#define mb D

#define min( i, j ) ( (i)<(j) ? (i): (j) )

/* Routine for computing C = A * B + C */

void AddDot6x8( int, double *, int, double *, int, double *, int);
void PackB_and_AddDot6x8( int, double *, int, double *, int, double *, int, double *, int);
void PackMatrixA( int, double *, int, double *, int, int);
void PackMatrixB( int, double *, int, double * );
void InnerKernel( int, int, int, double *, int, double *, int, double *, int, int, double*, double*);
void InnerKernel12x4( int, int, int, double *, int, double *, int, double *, int, int, double*, double*);
void OutterKernel( int, int, int, double *, int, double *, int, double *, int, int, double*, double*);
void PackKernel( int, int, int, double *, int, double *, int, double *, int, int, double*, double*);

void MY_MMult( int m, int n, int k, double *a, int lda, 
                                    double *b, int ldb,
                                    double *c, int ldc )
{
  int i, p, pb, ib;

  double* packedA = (double*) _mm_malloc(sizeof(double) * kc * mb * 2, 64);
  double* packedB = (double*) _mm_malloc(sizeof(double) * kc * nc * 2, 64);
//  double* packedB = (double*) _mm_malloc(sizeof(double) * D * D * 2, 64);

  for ( p=0; p<k; p+=kc ){
    pb = min( k-p, kc );
    for ( i=0; i<n; i+=nc ){
      ib = min( n-i, nc );
//      InnerKernel( m, ib, pb, &A( 0,p ), lda, &B(p, i ), ldb, &C( 0,i ), ldc, i==0, packedA, packedB);
//      OutterKernel( m, ib, pb, &A( 0,p ), lda, &B(p, i ), ldb, &C( 0,i ), ldc, i==0, packedA, packedB + p*n+i*nc);
      OutterKernel( m, ib, pb, &A( 0,p ), lda, &B(p, i ), ldb, &C( 0,i ), ldc, i==0, packedA, packedB);
    }
  }

}

void OutterKernel( int m, int n, int k, double *a, int lda, 
                                       double *b, int ldb,
                                       double *c, int ldc, int first_time, double* packedA, double* packedB)
{
    int i,j,p;
    for(i=0; i<m; i+=mcc)
    {
        for(j=0; j<n; j+=ncc)
           InnerKernel( mcc, ncc, k, &A( i,0 ), lda, &B(0,j ), ldb, &C( i,j ), ldc, j==0, packedA, packedB);
    }
}



void InnerKernel( int m, int n, int k, double *a, int lda, 
                                       double *b, int ldb,
                                       double *c, int ldc, int first_time, double* packedA, double* packedB)
{
  int i, j;

  for ( j=0; j<m; j+=6 ){        /* Loop over the columns of C, unrolled by 4 */
    if ( first_time )
    {
      PackMatrixA( k, &A( j, 0 ), lda, &packedA[ j*k ], j, m);
    }
    for ( i=0; i<n; i+=8 ){        /* Loop over the rows of C */
      if ( j == 0 ) 
      {
//	      PackMatrixB( k, &B( 0, i ), ldb, &packedB[ i*k ]);
          PackB_and_AddDot6x8( k, &packedA[ j*k ], 6, &B( 0, i ), ldb, &packedB[ i*k ], 8, &C( j,i ), ldc);
//          AddDot6x8( k, &packedA[ j*k ], 6, &packedB[ i*k ], 8, &C( j,i ), ldc);
      }
      else if(j==60)
          AddDot4x8( k, &packedA[ j*k ], 6, &packedB[ i*k ], 8, &C( j,i ), ldc);
      else
          AddDot6x8( k, &packedA[ j*k ], 6, &packedB[ i*k ], 8, &C( j,i ), ldc);
    }
  }
}

void PackMatrixB( int k, double *b, int ldb, double *b_to)
{
  int j;

  __m256d reg1;

#pragma unroll(4)
//#pragma noprefetch
  for( j=0; j<k; j++){  /* loop over columns of A */
    double 
        *b_ij_pntr = b + j * ldb;

    reg1 = _mm256_load_pd(b_ij_pntr);
    _mm256_store_pd(b_to, reg1);
    b_to += 4;

    reg1 = _mm256_load_pd(b_ij_pntr + 4);
    _mm256_store_pd(b_to, reg1);
    b_to += 4;

  }
}


void PackMatrixA( int k, double *a, int lda, double *a_to, int j, int m)
{
  int i;

  if(m==64 && j==60)       // only when 6x8; mcc = 64; in the last pack iteration
  {
      double 
        *a_i0_pntr = &A( 0, 0 ), *a_i1_pntr = &A( 1, 0 ),
        *a_i2_pntr = &A( 2, 0 ), *a_i3_pntr = &A( 3, 0 );
      
      v4df_t vreg;
#pragma unroll(4)
      for( i=0; i<k; i++){  /* loop over rows of B */
        *a_to++ = *a_i0_pntr++;
        *a_to++ = *a_i1_pntr++;
        *a_to++ = *a_i2_pntr++;
        *a_to++ = *a_i3_pntr++;
      }

  }
  else
  {
      double 
        *a_i0_pntr = &A( 0, 0 ), *a_i1_pntr = &A( 1, 0 ),
        *a_i2_pntr = &A( 2, 0 ), *a_i3_pntr = &A( 3, 0 ),
        *a_i4_pntr = &A( 4, 0 ), *a_i5_pntr = &A( 5, 0 );

      v4df_t vreg;


#pragma unroll(4)
      for( i=0; i<k; i++){  /* loop over rows of B */
        *a_to++ = *a_i0_pntr++;
        *a_to++ = *a_i1_pntr++;
        *a_to++ = *a_i2_pntr++;
        *a_to++ = *a_i3_pntr++;
        *a_to++ = *a_i4_pntr++;
        *a_to++ = *a_i5_pntr++;
        
      }
  }

}

int print256(std::string msg, v4df_t x)
{

//    printf(" msg: %s \n", msg);
    std::cout<<" msg: "<< msg <<std::endl;
    for(int i=0; i< 4; i++)
        printf(" %f ", x.d[i]);
    printf(" \n");

    return 0;

}

void AddDot6x8( int k, double *a, int lda,  double *b, int ldb, double *c, int ldc)
{

  int p;
  v4df_t
      zero_vreg,

      c00_vreg, c04_vreg,
      c10_vreg, c14_vreg,
      c20_vreg, c24_vreg,
      c30_vreg, c34_vreg,
      c40_vreg, c44_vreg,
      c50_vreg, c54_vreg,

    b00_vreg,
    b04_vreg,

    a0_vreg;

  zero_vreg.v = _mm256_set1_pd(0);

  c00_vreg.v = zero_vreg.v; 
  c04_vreg.v = zero_vreg.v;
  c10_vreg.v = zero_vreg.v;
  c14_vreg.v = zero_vreg.v;
  c20_vreg.v = zero_vreg.v;
  c24_vreg.v = zero_vreg.v;
  c30_vreg.v = zero_vreg.v;
  c34_vreg.v = zero_vreg.v;
  c40_vreg.v = zero_vreg.v;
  c44_vreg.v = zero_vreg.v;
  c50_vreg.v = zero_vreg.v;
  c54_vreg.v = zero_vreg.v;


#pragma noprefetch 
#pragma unroll(8)
  for ( p=0; p<k; p++ ){
    
    b00_vreg.v = _mm256_load_pd( (double *) b );
    b04_vreg.v = _mm256_load_pd( (double *) (b+4) );


    /* First row and second rows */

    a0_vreg.v = _mm256_set1_pd( *(double *) a );       /* load and duplicate */
    c00_vreg.v = _mm256_fmadd_pd(b00_vreg.v, a0_vreg.v, c00_vreg.v);
    c04_vreg.v = _mm256_fmadd_pd(b04_vreg.v, a0_vreg.v, c04_vreg.v);

    a0_vreg.v = _mm256_set1_pd( *(double *) (a+1) );   /* load and duplicate */
    c10_vreg.v = _mm256_fmadd_pd(b00_vreg.v, a0_vreg.v, c10_vreg.v);
    c14_vreg.v = _mm256_fmadd_pd(b04_vreg.v, a0_vreg.v, c14_vreg.v);

    a0_vreg.v = _mm256_set1_pd( *(double *) (a+2) );   /* load and duplicate */
    c20_vreg.v = _mm256_fmadd_pd(b00_vreg.v, a0_vreg.v, c20_vreg.v);
    c24_vreg.v = _mm256_fmadd_pd(b04_vreg.v, a0_vreg.v, c24_vreg.v);

    a0_vreg.v = _mm256_set1_pd( *(double *) (a+3) );   /* load and duplicate */
    c30_vreg.v = _mm256_fmadd_pd(b00_vreg.v, a0_vreg.v, c30_vreg.v);
    c34_vreg.v = _mm256_fmadd_pd(b04_vreg.v, a0_vreg.v, c34_vreg.v);

    a0_vreg.v = _mm256_set1_pd( *(double *) (a+4) );   /* load and duplicate */
    c40_vreg.v = _mm256_fmadd_pd(b00_vreg.v, a0_vreg.v, c40_vreg.v);
    c44_vreg.v = _mm256_fmadd_pd(b04_vreg.v, a0_vreg.v, c44_vreg.v);

    a0_vreg.v = _mm256_set1_pd( *(double *) (a+5) );   /* load and duplicate */
    c50_vreg.v = _mm256_fmadd_pd(b00_vreg.v, a0_vreg.v, c50_vreg.v);
    c54_vreg.v = _mm256_fmadd_pd(b04_vreg.v, a0_vreg.v, c54_vreg.v);

    a += 6;
    b += 8;

  }
 
  c00_vreg.v = _mm256_add_pd(c00_vreg.v, _mm256_load_pd(&C(0,0)));   
  c04_vreg.v = _mm256_add_pd(c04_vreg.v, _mm256_load_pd(&C(0,4)));
  c10_vreg.v = _mm256_add_pd(c10_vreg.v, _mm256_load_pd(&C(1,0))); 
  c14_vreg.v = _mm256_add_pd(c14_vreg.v, _mm256_load_pd(&C(1,4)));   
  c20_vreg.v = _mm256_add_pd(c20_vreg.v, _mm256_load_pd(&C(2,0))); 
  c24_vreg.v = _mm256_add_pd(c24_vreg.v, _mm256_load_pd(&C(2,4))); 
  c30_vreg.v = _mm256_add_pd(c30_vreg.v, _mm256_load_pd(&C(3,0)));
  c34_vreg.v = _mm256_add_pd(c34_vreg.v, _mm256_load_pd(&C(3,4))); 
  c40_vreg.v = _mm256_add_pd(c40_vreg.v, _mm256_load_pd(&C(4,0)));
  c44_vreg.v = _mm256_add_pd(c44_vreg.v, _mm256_load_pd(&C(4,4))); 
  c50_vreg.v = _mm256_add_pd(c50_vreg.v, _mm256_load_pd(&C(5,0)));
  c54_vreg.v = _mm256_add_pd(c54_vreg.v, _mm256_load_pd(&C(5,4)));


  _mm256_store_pd(&C(0, 0), c00_vreg.v);
  _mm256_store_pd(&C(0, 4), c04_vreg.v);
  _mm256_store_pd(&C(1, 0), c10_vreg.v);
  _mm256_store_pd(&C(1, 4), c14_vreg.v);
  _mm256_store_pd(&C(2, 0), c20_vreg.v);
  _mm256_store_pd(&C(2, 4), c24_vreg.v);
  _mm256_store_pd(&C(3, 0), c30_vreg.v);
  _mm256_store_pd(&C(3, 4), c34_vreg.v);
  _mm256_store_pd(&C(4, 0), c40_vreg.v);
  _mm256_store_pd(&C(4, 4), c44_vreg.v);
  _mm256_store_pd(&C(5, 0), c50_vreg.v);
  _mm256_store_pd(&C(5, 4), c54_vreg.v);

}


void AddDot4x8( int k, double *a, int lda,  double *b, int ldb, double *c, int ldc)
{

  int p;
  v4df_t
      zero_vreg,

      c00_vreg, c04_vreg,
      c10_vreg, c14_vreg,
      c20_vreg, c24_vreg,
      c30_vreg, c34_vreg,

    b00_vreg,
    b04_vreg,

    a0_vreg;

  zero_vreg.v = _mm256_set1_pd(0);

  c00_vreg.v = zero_vreg.v; 
  c04_vreg.v = zero_vreg.v;
  c10_vreg.v = zero_vreg.v;
  c14_vreg.v = zero_vreg.v;
  c20_vreg.v = zero_vreg.v;
  c24_vreg.v = zero_vreg.v;
  c30_vreg.v = zero_vreg.v;
  c34_vreg.v = zero_vreg.v;


#pragma noprefetch 
#pragma unroll(8)
  for ( p=0; p<k; p++ ){
    
    b00_vreg.v = _mm256_load_pd( (double *) b );
    b04_vreg.v = _mm256_load_pd( (double *) (b+4) );
    b += 8;


    /* First row and second rows */

    a0_vreg.v = _mm256_set1_pd( *(double *) a );       /* load and duplicate */
    c00_vreg.v = _mm256_fmadd_pd(b00_vreg.v, a0_vreg.v, c00_vreg.v);
    c04_vreg.v = _mm256_fmadd_pd(b04_vreg.v, a0_vreg.v, c04_vreg.v);

    a0_vreg.v = _mm256_set1_pd( *(double *) (a+1) );   /* load and duplicate */
    c10_vreg.v = _mm256_fmadd_pd(b00_vreg.v, a0_vreg.v, c10_vreg.v);
    c14_vreg.v = _mm256_fmadd_pd(b04_vreg.v, a0_vreg.v, c14_vreg.v);

    a0_vreg.v = _mm256_set1_pd( *(double *) (a+2) );   /* load and duplicate */
    c20_vreg.v = _mm256_fmadd_pd(b00_vreg.v, a0_vreg.v, c20_vreg.v);
    c24_vreg.v = _mm256_fmadd_pd(b04_vreg.v, a0_vreg.v, c24_vreg.v);

    a0_vreg.v = _mm256_set1_pd( *(double *) (a+3) );   /* load and duplicate */
    c30_vreg.v = _mm256_fmadd_pd(b00_vreg.v, a0_vreg.v, c30_vreg.v);
    c34_vreg.v = _mm256_fmadd_pd(b04_vreg.v, a0_vreg.v, c34_vreg.v);

    a += 4;

/*
  if(profiling)
  {
     print256(" in computing a ", a_0p_a_1p_a_2p_a_3p_vreg);
     print256(" in computing b ", b_p0_vreg);
     print256(" in computing c ", c_00_c_10_c_20_c_30_vreg);
  }
*/

  }
 
//    _mm_prefetch((const char *)b+k*8, _MM_HINT_T1);
  c00_vreg.v = _mm256_add_pd(c00_vreg.v, _mm256_load_pd(&C(0,0)));   
  c04_vreg.v = _mm256_add_pd(c04_vreg.v, _mm256_load_pd(&C(0,4)));
  c10_vreg.v = _mm256_add_pd(c10_vreg.v, _mm256_load_pd(&C(1,0))); 
  c14_vreg.v = _mm256_add_pd(c14_vreg.v, _mm256_load_pd(&C(1,4)));   
  c20_vreg.v = _mm256_add_pd(c20_vreg.v, _mm256_load_pd(&C(2,0))); 
  c24_vreg.v = _mm256_add_pd(c24_vreg.v, _mm256_load_pd(&C(2,4))); 
  c30_vreg.v = _mm256_add_pd(c30_vreg.v, _mm256_load_pd(&C(3,0)));
  c34_vreg.v = _mm256_add_pd(c34_vreg.v, _mm256_load_pd(&C(3,4))); 


  _mm256_store_pd(&C(0, 0), c00_vreg.v);
  _mm256_store_pd(&C(0, 4), c04_vreg.v);
  _mm256_store_pd(&C(1, 0), c10_vreg.v);
  _mm256_store_pd(&C(1, 4), c14_vreg.v);
  _mm256_store_pd(&C(2, 0), c20_vreg.v);
  _mm256_store_pd(&C(2, 4), c24_vreg.v);
  _mm256_store_pd(&C(3, 0), c30_vreg.v);
  _mm256_store_pd(&C(3, 4), c34_vreg.v);


}

void PackB_and_AddDot6x8( int k, double *a, int lda, double *ob, int ldb,  double *b, int ldb2, double *c, int ldc)
{
  int j;

  double* b_to = b;
  int p;
  v4df_t
      zero_vreg,

      c00_vreg, c04_vreg,
      c10_vreg, c14_vreg,
      c20_vreg, c24_vreg,
      c30_vreg, c34_vreg,
      c40_vreg, c44_vreg,
      c50_vreg, c54_vreg,

    b00_vreg,
    b04_vreg,

    a0_vreg;

  zero_vreg.v = _mm256_set1_pd(0);
  zero_vreg.v = _mm256_set_pd(1.0,2.0,3.0,4.0);

  c00_vreg.v = zero_vreg.v; 
  c04_vreg.v = zero_vreg.v;
  c10_vreg.v = zero_vreg.v;
  c14_vreg.v = zero_vreg.v;
  c20_vreg.v = zero_vreg.v;
  c24_vreg.v = zero_vreg.v;
  c30_vreg.v = zero_vreg.v;
  c34_vreg.v = zero_vreg.v;
  c40_vreg.v = zero_vreg.v;
  c44_vreg.v = zero_vreg.v;
  c50_vreg.v = zero_vreg.v;
  c54_vreg.v = zero_vreg.v;


#pragma noprefetch 
#pragma unroll(8)
  for ( p=0; p<k; p++ ){
    _mm_prefetch((const char *)(ob + (p+8)*ldb), _MM_HINT_T0);
    _mm_prefetch((const char *)(ob + (p+8)*ldb + 4), _MM_HINT_T0);
    _mm_prefetch((const char *)a+48, _MM_HINT_T0);

    double  *b_ij_pntr = ob + p * ldb;
    
    b00_vreg.v = _mm256_load_pd(b_ij_pntr);
    b04_vreg.v = _mm256_load_pd(b_ij_pntr + 4);
    
    //b00_vreg.v = (__m256d)_mm256_stream_load_si256((__m256i*)b_ij_pntr);
    //b04_vreg.v = (__m256d)_mm256_stream_load_si256((__m256i*)(b_ij_pntr+4));


    a0_vreg.v = _mm256_set1_pd( *(double *) a );       /* load and duplicate */
    c00_vreg.v = _mm256_fmadd_pd(b00_vreg.v, a0_vreg.v, c00_vreg.v);
    c04_vreg.v = _mm256_fmadd_pd(b04_vreg.v, a0_vreg.v, c04_vreg.v);

    a0_vreg.v = _mm256_set1_pd( *(double *) (a+1) );   /* load and duplicate */
    c10_vreg.v = _mm256_fmadd_pd(b00_vreg.v, a0_vreg.v, c10_vreg.v);
    c14_vreg.v = _mm256_fmadd_pd(b04_vreg.v, a0_vreg.v, c14_vreg.v);

    _mm256_store_pd(b_to, b00_vreg.v);

    a0_vreg.v = _mm256_set1_pd( *(double *) (a+2) );   /* load and duplicate */
    c20_vreg.v = _mm256_fmadd_pd(b00_vreg.v, a0_vreg.v, c20_vreg.v);
    c24_vreg.v = _mm256_fmadd_pd(b04_vreg.v, a0_vreg.v, c24_vreg.v);

    a0_vreg.v = _mm256_set1_pd( *(double *) (a+3) );   /* load and duplicate */
    c30_vreg.v = _mm256_fmadd_pd(b00_vreg.v, a0_vreg.v, c30_vreg.v);
    c34_vreg.v = _mm256_fmadd_pd(b04_vreg.v, a0_vreg.v, c34_vreg.v);

    _mm256_store_pd(b_to+4, b04_vreg.v);

    a0_vreg.v = _mm256_set1_pd( *(double *) (a+4) );   /* load and duplicate */
    c40_vreg.v = _mm256_fmadd_pd(b00_vreg.v, a0_vreg.v, c40_vreg.v);
    c44_vreg.v = _mm256_fmadd_pd(b04_vreg.v, a0_vreg.v, c44_vreg.v);

    a0_vreg.v = _mm256_set1_pd( *(double *) (a+5) );   /* load and duplicate */
    c50_vreg.v = _mm256_fmadd_pd(b00_vreg.v, a0_vreg.v, c50_vreg.v);
    c54_vreg.v = _mm256_fmadd_pd(b04_vreg.v, a0_vreg.v, c54_vreg.v);

    a += 6;
    b_to += 8;

/*
  if(profiling)
  {
     print256(" in computing a ", a_0p_a_1p_a_2p_a_3p_vreg);
     print256(" in computing b ", b_p0_vreg);
     print256(" in computing c ", c_00_c_10_c_20_c_30_vreg);
  }
*/

  }
 
//    _mm_prefetch((const char *)b+k*8, _MM_HINT_T1);
  c00_vreg.v = _mm256_add_pd(c00_vreg.v, _mm256_load_pd(&C(0,0)));   
  c04_vreg.v = _mm256_add_pd(c04_vreg.v, _mm256_load_pd(&C(0,4)));
  c10_vreg.v = _mm256_add_pd(c10_vreg.v, _mm256_load_pd(&C(1,0))); 
  c14_vreg.v = _mm256_add_pd(c14_vreg.v, _mm256_load_pd(&C(1,4)));   
  c20_vreg.v = _mm256_add_pd(c20_vreg.v, _mm256_load_pd(&C(2,0))); 
  c24_vreg.v = _mm256_add_pd(c24_vreg.v, _mm256_load_pd(&C(2,4))); 
  c30_vreg.v = _mm256_add_pd(c30_vreg.v, _mm256_load_pd(&C(3,0)));
  c34_vreg.v = _mm256_add_pd(c34_vreg.v, _mm256_load_pd(&C(3,4))); 
  c40_vreg.v = _mm256_add_pd(c40_vreg.v, _mm256_load_pd(&C(4,0)));
  c44_vreg.v = _mm256_add_pd(c44_vreg.v, _mm256_load_pd(&C(4,4))); 
  c50_vreg.v = _mm256_add_pd(c50_vreg.v, _mm256_load_pd(&C(5,0)));
  c54_vreg.v = _mm256_add_pd(c54_vreg.v, _mm256_load_pd(&C(5,4)));


  _mm256_store_pd(&C(0, 0), c00_vreg.v);
  _mm256_store_pd(&C(0, 4), c04_vreg.v);
  _mm256_store_pd(&C(1, 0), c10_vreg.v);
  _mm256_store_pd(&C(1, 4), c14_vreg.v);
  _mm256_store_pd(&C(2, 0), c20_vreg.v);
  _mm256_store_pd(&C(2, 4), c24_vreg.v);
  _mm256_store_pd(&C(3, 0), c30_vreg.v);
  _mm256_store_pd(&C(3, 4), c34_vreg.v);
  _mm256_store_pd(&C(4, 0), c40_vreg.v);
  _mm256_store_pd(&C(4, 4), c44_vreg.v);
  _mm256_store_pd(&C(5, 0), c50_vreg.v);
  _mm256_store_pd(&C(5, 4), c54_vreg.v);

}




