#include <stdio.h>
#include <math.h>
#include <mpi.h>
#include <hip/hip_runtime.h>

/* ---------------------------------------------------------------------------------
Macro for checking errors in HIP API calls
----------------------------------------------------------------------------------*/
#define hipErrorCheck(call)                                                                 \
do{                                                                                         \
    hipError_t hipErr = call;                                                               \
    if(hipSuccess != hipErr){                                                               \
        printf("HIP Error - %s:%d: '%s'\n", __FILE__, __LINE__, hipGetErrorString(hipErr)); \
        exit(0);                                                                            \
    }                                                                                       \
}while(0)

/* ---------------------------------------------------------------------------------
Vector addition kernel
----------------------------------------------------------------------------------*/
__global__ void add_vectors(double *a, double *b, double *c, int n){
    
    int id = blockDim.x * blockIdx.x + threadIdx.x;
    if(id < n) c[id] = a[id] + b[id];
}

/* ---------------------------------------------------------------------------------
Main program
----------------------------------------------------------------------------------*/
int main(int argc, char *argv[]){
    MPI_Init(&argc, &argv);
  
    int size;
    MPI_Comm_size(MPI_COMM_WORLD, &size);
  
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  
    char name[MPI_MAX_PROCESSOR_NAME];
    int resultlength;
    MPI_Get_processor_name(name, &resultlength);
    
    if(rank == 0) {
      printf("number of ranks: %d\n", size);
    }
    // Start Total Runtime Timer
    double start_time, end_time, elapsed_time;
    start_time = MPI_Wtime();

    // Array length
    long long int N = 256*1024*1024;

    double tolerance = 1.0e-14;

    size_t buffer_size = N * sizeof(double);

    double *A = (double*)malloc(buffer_size);
    double *B = (double*)malloc(buffer_size);
    double *C = (double*)malloc(buffer_size);

    for(int i=0; i<N; i++){
        double random_value = (double)rand()/(double)RAND_MAX;
        A[i] = sin(random_value) * sin(random_value);
        B[i] = cos(random_value) * cos(random_value);
        C[i] = 0.0;
    }

    double *d_A, *d_B, *d_C;
    hipErrorCheck( hipMalloc(&d_A, buffer_size) );
    hipErrorCheck( hipMalloc(&d_B, buffer_size) );
    hipErrorCheck( hipMalloc(&d_C, buffer_size) );

    hipErrorCheck( hipMemcpy(d_A, A, buffer_size, hipMemcpyHostToDevice) );
    hipErrorCheck( hipMemcpy(d_B, B, buffer_size, hipMemcpyHostToDevice) );

    hipEvent_t start, end;
    hipErrorCheck( hipEventCreate(&start) );
    hipErrorCheck( hipEventCreate(&end) );

    // Set execution configuration parameters
    int thr_per_blk = 256;
    int blk_in_grid = ceil( float(N) / thr_per_blk );

    hipErrorCheck( hipEventRecord(start, NULL) );
    hipLaunchKernelGGL((add_vectors), dim3(blk_in_grid), dim3(thr_per_blk), 0, 0, d_A, d_B, d_C, N);

    hipErrorCheck( hipEventRecord(end, NULL) );
    hipErrorCheck( hipEventSynchronize(end) );
    float milliseconds = 0.0;
    hipErrorCheck( hipEventElapsedTime(&milliseconds, start, end) ); 

    hipErrorCheck( hipMemcpy(C, d_C, buffer_size, hipMemcpyDeviceToHost) );

    float max_gpu_time;
    MPI_Reduce(&milliseconds, &max_gpu_time, 1, MPI_FLOAT, MPI_MAX, 0, MPI_COMM_WORLD);

    double sum = 0.0;
    for(int i=0; i<N; i++){
        sum = sum + C[i];
    }

    double result = sum / (double)N;
    double relative_difference = fabs( (result - 1.0) / 1.0 );

    if(relative_difference > tolerance){
        printf("In rank %d: Test failed!\n", rank);
        exit(1);
    }


    hipErrorCheck( hipFree(d_A) );
    hipErrorCheck( hipFree(d_B) );
    hipErrorCheck( hipFree(d_C) );

    free(A);
    free(B);
    free(C);

    end_time = MPI_Wtime();
    elapsed_time = end_time - start_time;
    double total_time_max;
    MPI_Reduce(&elapsed_time, &total_time_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if(rank == 0) {
      printf("Result              = %.16f\n", result);
      printf("Relative difference = %.16f\n", relative_difference);
      printf("Tolerance           = %.16f\n", tolerance);
      printf("Array buffer size   = %zu\n", buffer_size);
      printf("Max GPU time (s)    = %.6f\n", max_gpu_time / 1000.0);
      printf("Max MPI time (s)    = %.6f\n", total_time_max / 1000.0);
    }
    MPI_Finalize();
    return 0;
}
