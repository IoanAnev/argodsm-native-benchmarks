/********************************************************************
  An ArgoDSM@OpenMP implementation of Himeno.

  This benchmark test program is measuring a cpu performance
  of floating point operation by a Poisson equation solver.

  If you have any question, please ask me via email.
  written by Ryutaro HIMENO, November 26, 2001.
  Version 3.0
  ----------------------------------------------
  Ryutaro Himeno, Dr. of Eng.
  Head of Computer Information Division,
  RIKEN (The Institute of Pysical and Chemical Research)
  Email : himeno@postman.riken.go.jp
  ---------------------------------------------------------------
  You can adjust the size of this benchmark code to fit your target
  computer. In that case, please chose following sets of
  [mimax][mjmax][mkmax]:
  small : 33,33,65
  small : 65,65,129
  midium: 129,129,257
  large : 257,257,513
  ext.large: 513,513,1025
  This program is to measure a computer performance in MFLOPS
  by using a kernel which appears in a linear solver of pressure
  Poisson eq. which appears in an incompressible Navier-Stokes solver.
  A point-Jacobi method is employed in this solver as this method can 
  be easily vectorized and be parallelized.
  ------------------
  Finite-difference method, curvilinear coodinate system
  Vectorizable and parallelizable on each grid point
  No. of grid points : imax x jmax x kmax including boundaries
  ------------------
  A,B,C:coefficient matrix, wrk1: source term of Poisson equation
  wrk2 : working area, OMEGA : relaxation parameter
  BND:control variable for boundaries and objects ( = 0 or 1)
  P: pressure
********************************************************************/

#include "argo.hpp"

#include <omp.h>
#include <iostream>
#include <string.h>
#include <sys/time.h>

#define MR(mt,n,r,c,d)  mt->m[(n) * mt->mrows * mt->mcols * mt->mdeps + (r) * mt->mcols* mt->mdeps + (c) * mt->mdeps + (d)]

struct Mat {
	float* m;
	int mnums;
	int mrows;
	int mcols;
	int mdeps;
};

/* prototypes */
typedef struct Mat Matrix;

int newMat(Matrix* Mat, int mnums, int mrows, int mcols, int mdeps);
void clearMat(Matrix* Mat);
void set_param(int i[],char *size);
void mat_set(Matrix* Mat,int l,float z);
void mat_set_init(Matrix* Mat);
void distribute(int& beg, int& end, const int& loop_size, 
		const int& beg_offset, const int& less_equal);
float jacobi(int n,Matrix* M1,Matrix* M2,Matrix* M3,
		Matrix* M4,Matrix* M5,Matrix* M6,Matrix* M7);
double second();

float  omega=0.8;
float  gosa1=0.0;
Matrix a,b,c,p,bnd,wrk1,wrk2;

int workrank;
int numtasks;
int nthreads;

int   *nn;
float *ggosa;

bool *lock_flag;
argo::globallock::global_tas_lock *lock;

int
main(int argc, char *argv[])
{
	argo::init(10*1024*1024*1024UL);

	int    imax,jmax,kmax,mimax,mjmax,mkmax,msize[3];
	float  gosa,target;
	double cpu0,cpu1,cpu,xmflops2,score,flop;
	char   size[10];

	if(argc == 2){
		strcpy(size,argv[1]);
	} else {
		if (workrank == 0) {
			printf("For example: \n");
			printf(" Grid-size= XS (32x32x64)\n");
			printf("\t    S  (64x64x128)\n");
			printf("\t    M  (128x128x256)\n");
			printf("\t    L  (256x256x512)\n");
			printf("\t    XL (512x512x1024)\n\n");
		}
		exit(2);
	}

	set_param(msize,size);

	mimax= msize[0];
	mjmax= msize[1];
	mkmax= msize[2];
	imax= mimax-1;
	jmax= mjmax-1;
	kmax= mkmax-1;

	target = 60.0;

	workrank = argo::node_id();
	numtasks = argo::number_of_nodes();

	#pragma omp parallel
	{
		#if defined(_OPENMP)
			#pragma omp master
			nthreads = omp_get_num_threads();
		#endif /* _OPENMP */
	}

	nn = argo::conew_<int>(3);
	ggosa = argo::conew_<float>(0.0);

	lock_flag = argo::conew_<bool>(false);
	lock = new argo::globallock::global_tas_lock(lock_flag);

	if (workrank == 0) {
		printf("mimax = %d mjmax = %d mkmax = %d\n",mimax,mjmax,mkmax);
		printf("imax = %d jmax = %d kmax =%d\n",imax,jmax,kmax);
	}

	/*
	 *    Initializing matrixes
	 */
	newMat(&p,1,mimax,mjmax,mkmax);
	newMat(&bnd,1,mimax,mjmax,mkmax);
	newMat(&wrk1,1,mimax,mjmax,mkmax);
	newMat(&wrk2,1,mimax,mjmax,mkmax);
	newMat(&a,4,mimax,mjmax,mkmax);
	newMat(&b,3,mimax,mjmax,mkmax);
	newMat(&c,3,mimax,mjmax,mkmax);

	mat_set_init(&p);
	mat_set(&bnd,0,1.0);
	mat_set(&wrk1,0,0.0);
	mat_set(&wrk2,0,0.0);
	mat_set(&a,0,1.0);
	mat_set(&a,1,1.0);
	mat_set(&a,2,1.0);
	mat_set(&a,3,1.0/6.0);
	mat_set(&b,0,0.0);
	mat_set(&b,1,0.0);
	mat_set(&b,2,0.0);
	mat_set(&c,0,1.0);
	mat_set(&c,1,1.0);
	mat_set(&c,2,1.0);

	#pragma omp parallel
	{
		/*
		 *    Start measuring
		 */
		#pragma omp master
		if (workrank == 0) {
			printf(" Start rehearsal measurement process.\n");
			printf(" Measure the performance in %d times.\n\n",*nn);
		}

		argo::barrier(nthreads);

		#pragma omp master
		if (workrank == 0)
			cpu0= second();

		gosa= jacobi(*nn,&a,&b,&c,&p,&bnd,&wrk1,&wrk2);

		#pragma omp master
		if (workrank == 0) {
			cpu1= second();
			cpu= cpu1 - cpu0;
			flop = (double)(kmax-1)*(double)(jmax-1)*(double)(imax-1)*34.0;

			if(cpu != 0.0)
				xmflops2= flop/cpu*1.e-6*(*nn);

			printf(" MFLOPS: %f time(s): %f %e\n\n",xmflops2,cpu,gosa);

			*nn= (int)(target/(cpu/3.0));

			printf(" Now, start the actual measurement process.\n");
			printf(" The loop will be excuted in %d times\n",*nn);
			printf(" This will take about one minute.\n");
			printf(" Wait for a while\n\n");
		}

		argo::barrier(nthreads);

		#pragma omp master
		if (workrank == 0)
			cpu0 = second();

		gosa = jacobi(*nn,&a,&b,&c,&p,&bnd,&wrk1,&wrk2);

		#pragma omp master
		if (workrank == 0) {
			cpu1 = second();
			cpu = cpu1 - cpu0;

			if(cpu != 0.0)
				xmflops2 = (double)flop/cpu*1.0e-6*(*nn);

			printf("cpu : %f sec.\n", cpu);
			printf("Loop executed for %d times\n",*nn);
			printf("Gosa : %e \n",gosa);
			printf("MFLOPS measured : %f\n",xmflops2);
			score = xmflops2/82.84;
			printf("Score based on Pentium III 600MHz using Fortran 77: %f\n",score);
		}
	}

	/*
	 * Generate output file for verification
	 */
	// MPI_File fh;
	// MPI_Status status;

	// if (workrank == 0) {
	// 	float *check = new float[mimax*mjmax*mkmax];
	// 	for (unsigned long i = 0; i < mimax*mjmax*mkmax; i++)
	// 		check[i] = p.m[i];

	// 	MPI_File_open(MPI_COMM_SELF, "outt",MPI_MODE_CREATE | MPI_MODE_WRONLY,MPI_INFO_NULL,&fh);
	// 	MPI_File_write(fh,check,mimax*mjmax*mkmax, MPI_FLOAT,&status);

	// 	delete [] check;
	// 	MPI_File_close(&fh);
  	// }

	/*
	 *   Matrix free
	 */ 
	clearMat(&p);
	clearMat(&bnd);
	clearMat(&wrk1);
	clearMat(&wrk2);
	clearMat(&a);
	clearMat(&b);
	clearMat(&c);

	argo::codelete_(nn);
	argo::codelete_(ggosa);

	delete lock;
	argo::codelete_(lock_flag);

	argo::finalize();

	return 0;
}

void
set_param(int is[],char *size)
{
	if(!strcmp(size,"XS") || !strcmp(size,"xs")){
		is[0]= 32;
		is[1]= 32;
		is[2]= 64;
		return;
	}
	if(!strcmp(size,"S") || !strcmp(size,"s")){
		is[0]= 64;
		is[1]= 64;
		is[2]= 128;
		return;
	}
	if(!strcmp(size,"M") || !strcmp(size,"m")){
		is[0]= 128;
		is[1]= 128;
		is[2]= 256;
		return;
	}
	if(!strcmp(size,"L") || !strcmp(size,"l")){
		is[0]= 256;
		is[1]= 256;
		is[2]= 512;
		return;
	}
	if(!strcmp(size,"Xl") || !strcmp(size,"xl")){
		is[0]= 512;
		is[1]= 512;
		is[2]= 1024;
		return;
	}
}

int
newMat(Matrix* Mat, int mnums,int mrows, int mcols, int mdeps)
{
	Mat->mnums= mnums;
	Mat->mrows= mrows;
	Mat->mcols= mcols;
	Mat->mdeps= mdeps;
	Mat->m= NULL;
	Mat->m= argo::conew_array<float>(mnums * mrows * mcols * mdeps);

	return(Mat->m != NULL) ? 1:0;
}

void
clearMat(Matrix* Mat)
{
	if(Mat->m)
		argo::codelete_array(Mat->m);
	Mat->m= NULL;
	Mat->mnums= 0;
	Mat->mcols= 0;
	Mat->mrows= 0;
	Mat->mdeps= 0;
}

void
mat_set(Matrix* Mat, int l, float val)
{
	int    beg, end;
	int    i,j,k;

	distribute(beg, end, Mat->mrows, 0, 0);

	for(i=beg; i<end; i++)
		for(j=0; j<Mat->mcols; j++)
			for(k=0; k<Mat->mdeps; k++)
				MR(Mat,l,i,j,k)= val;
}

void
mat_set_init(Matrix* Mat)
{
	int    beg, end;
	int    i,j,k,l;

	distribute(beg, end, Mat->mrows, 0, 0);

	for(i=beg; i<end; i++)
		for(j=0; j<Mat->mcols; j++)
			for(k=0; k<Mat->mdeps; k++)
				MR(Mat,0,i,j,k)= (float)(i*i)
					/(float)((Mat->mrows - 1)*(Mat->mrows - 1));
}

float
jacobi(int nn, Matrix* a,Matrix* b,Matrix* c,
		Matrix* p,Matrix* bnd,Matrix* wrk1,Matrix* wrk2)
{
	int    beg, end;
	int    i,j,k,n,imax,jmax,kmax;
	float  s0,ss;

	imax= p->mrows-1;
	jmax= p->mcols-1;
	kmax= p->mdeps-1;

	distribute(beg, end, imax, 1, 0);

	#pragma omp master
	if (workrank == 0)
		*ggosa = 0.0;

	for(n=0 ; n<nn ; n++){
		#pragma omp master
		gosa1 = 0.0;
		argo::barrier(nthreads);
		
		#pragma omp for schedule(static)
		for(i=beg; i<end; i++)
			for(j=1; j<jmax; j++)
				for(k=1; k<kmax; k++){
					s0= MR(a,0,i,j,k)*MR(p,0,i+1,j,  k)
						+ MR(a,1,i,j,k)*MR(p,0,i,  j+1,k)
						+ MR(a,2,i,j,k)*MR(p,0,i,  j,  k+1)
						+ MR(b,0,i,j,k)
						*( MR(p,0,i+1,j+1,k) - MR(p,0,i+1,j-1,k)
						- MR(p,0,i-1,j+1,k) + MR(p,0,i-1,j-1,k) )
						+ MR(b,1,i,j,k)
						*( MR(p,0,i,j+1,k+1) - MR(p,0,i,j-1,k+1)
						- MR(p,0,i,j+1,k-1) + MR(p,0,i,j-1,k-1) )
						+ MR(b,2,i,j,k)
						*( MR(p,0,i+1,j,k+1) - MR(p,0,i-1,j,k+1)
						- MR(p,0,i+1,j,k-1) + MR(p,0,i-1,j,k-1) )
						+ MR(c,0,i,j,k) * MR(p,0,i-1,j,  k)
						+ MR(c,1,i,j,k) * MR(p,0,i,  j-1,k)
						+ MR(c,2,i,j,k) * MR(p,0,i,  j,  k-1)
						+ MR(wrk1,0,i,j,k);

					ss= (s0*MR(a,3,i,j,k) - MR(p,0,i,j,k))*MR(bnd,0,i,j,k);

					#pragma omp critical
					gosa1+= ss*ss;

					MR(wrk2,0,i,j,k)= MR(p,0,i,j,k) + omega*ss;
				}
		argo::barrier(nthreads);
		
		#pragma omp for schedule(static)
		for(i=beg; i<end; i++)
			for(j=1; j<jmax; j++)
				for(k=1; k<kmax; k++)
					MR(p,0,i,j,k)= MR(wrk2,0,i,j,k);
	} /* end n loop */

	#pragma omp master
	{
		lock->lock();
		*ggosa += gosa1;
		lock->unlock();
	}

	argo::barrier(nthreads);

	return(*ggosa);
}

double
second()
{
	struct timeval tm;
	double t;

	static int base_sec = 0, base_usec = 0;

	gettimeofday(&tm, NULL);

	if(base_sec == 0 && base_usec == 0)
	{
		base_sec = tm.tv_sec;
		base_usec = tm.tv_usec;
		t = 0.0;
	} else {
		t = (double) (tm.tv_sec-base_sec) + 
			((double) (tm.tv_usec-base_usec))/1.0e6;
	}

	return t;
}

void
distribute(int& beg, int& end, const int& loop_size, 
		const int& beg_offset, const int& less_equal)
{
	int chunk = loop_size / numtasks;
	beg = workrank * chunk + ((workrank == 0) ? beg_offset : less_equal);
	end = (workrank != numtasks - 1) ? workrank * chunk + chunk : loop_size;
}
