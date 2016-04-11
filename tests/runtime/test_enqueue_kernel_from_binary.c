/* Tests a kernel create with binary.

   Copyright (c) 2016 pocl developers

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <CL/opencl.h>

#define BUFFER_SIZE 1024

// OpenCL kernel. Each work item takes care of one element of c
const char *kernelSource =                                               "\n"
"__kernel void vecAdd( __constant int *a,                                 \n"
"                      __constant int *b,                                 \n"
"                      __global int *c)                                   \n"
"{                                                                        \n"
"unsigned int i = get_global_id(0) * get_global_size(1)                   \n"
"    + get_global_id(1);                                                  \n"
"c[i] = a[i] + b[i] + get_local_id(0) * get_local_size(1)                 \n"
"    + get_local_id(1);                                                   \n"
"}                                                                        \n";

const char *barrier_kernelSource =
"__kernel void barrier_kernel (global int *buffer) \n"
"{ \n"
"  private int a = buffer[get_global_id(0)-1] \n"
"    + buffer[get_global_id(0)] + buffer[get_global_id(0)+1]; \n"
"  barrier(CLK_LOCAL_MEM_FENCE); \n"
"  buffer[get_global_id(0)] = a/3; \n"
"} \n";

int main(void)
{
  int *h_a;
  int *h_b;
  int *h_c1;
  int *h_c2;
  int *h_c3;
  int *bb1;
  int *bb2;

  cl_mem d_a;
  cl_mem d_b;
  cl_mem d_c1;
  cl_mem d_c2;
  cl_mem d_c3;
  cl_mem barrier_buffer1;
  cl_mem barrier_buffer2;

  cl_platform_id cpPlatform;  // OpenCL platform
  cl_device_id device_id;   // device ID
  cl_context context;   // context
  cl_command_queue queue;   // command queue
  cl_program program1, program2;    // program
  cl_program b_program1, b_program2;
  cl_kernel kernel1, kernel2, kernel3, barrier_kernel1, barrier_kernel2;

  size_t globalSize[2], localSize[2];
  cl_int err;

//###################################################################
//###################################################################
// Initialize variables

  unsigned bytes = BUFFER_SIZE * sizeof(int);

  h_a = (int*)malloc(bytes);
  h_b = (int*)malloc(bytes);
  h_c1 = (int*)malloc(bytes);
  h_c2 = (int*)malloc(bytes);
  h_c3 = (int*)malloc(bytes);
  bb1 = (int*)malloc(bytes);
  bb2 = (int*)malloc(bytes);

  unsigned int i;
  for( i = 0; i < BUFFER_SIZE; i++ )
  {
    h_a[i] = 2*i-1;
    h_b[i] = -i;
  }
  
  for (i = 0; i < 8; ++i)
    {
      bb1[i] = bb2[i] = i*3;
    }
  memset(h_c1, 0, bytes);
  memset(h_c2, 0, bytes);
  memset(h_c3, 0, bytes);

//###################################################################
//###################################################################
// Initialize cl_programS

  err = clGetPlatformIDs(1, &cpPlatform, NULL);
  assert(!err);

  err = clGetDeviceIDs(cpPlatform, CL_DEVICE_TYPE_ALL, 1, &device_id, NULL);
  assert(!err);

  context = clCreateContext(0, 1, &device_id, NULL, NULL, &err);
  assert(context);

  queue = clCreateCommandQueue(context, device_id, 0, &err);
  assert(queue);

  program1 = clCreateProgramWithSource(context, 1,
        (const char **) & kernelSource, NULL, &err);
  assert(program1);

  clBuildProgram(program1, 0, NULL, NULL, NULL, NULL);

  size_t binary_sizes;
  unsigned char *binary;
  err = clGetProgramInfo(program1, CL_PROGRAM_BINARY_SIZES,
                         sizeof(size_t), &binary_sizes, NULL);
  assert(!err);

  binary = malloc(sizeof(unsigned char)*binary_sizes);
  assert(binary);

  err = clGetProgramInfo(program1, CL_PROGRAM_BINARIES, sizeof(unsigned char*), &binary, NULL);
  assert(!err);

  program2 = clCreateProgramWithBinary(
    context, 1, &device_id, &binary_sizes, (const unsigned char **)(&binary), NULL, &err);
  assert(!err);

  clBuildProgram(program2, 0, NULL, NULL, NULL, NULL);

  /* Barrier programs */
  b_program1 = clCreateProgramWithSource(context, 1,
                                         (const char **)
                                         &barrier_kernelSource,
                                         NULL, &err);
  assert(b_program1);

  clBuildProgram(b_program1, 0, NULL, NULL, NULL, NULL);

  size_t barrier_binary_sizes;
  unsigned char *barrier_binary;
  err = clGetProgramInfo(b_program1, CL_PROGRAM_BINARY_SIZES,
                         sizeof(size_t), &barrier_binary_sizes, NULL);
  assert(!err);

  barrier_binary = malloc(sizeof(unsigned char)*barrier_binary_sizes);
  assert(barrier_binary);

  err = clGetProgramInfo(b_program1, CL_PROGRAM_BINARIES,
                         sizeof(unsigned char*), &barrier_binary, NULL);
  assert(!err);

  b_program2 =
    clCreateProgramWithBinary(context, 1, &device_id,
                              &barrier_binary_sizes,
                              (const unsigned char **)(&barrier_binary),
                              NULL, &err);
  assert(!err);

  clBuildProgram(b_program2, 0, NULL, NULL, NULL, NULL);
//###################################################################
//###################################################################
// Set up buffers in memory

  d_a = clCreateBuffer(context, CL_MEM_READ_ONLY, bytes, NULL, NULL);
  assert(d_a);
  d_b = clCreateBuffer(context, CL_MEM_READ_ONLY, bytes, NULL, NULL);
  assert(d_b);
  d_c1 = clCreateBuffer(context, CL_MEM_WRITE_ONLY, bytes, NULL, NULL);
  assert(d_c1);
  d_c2 = clCreateBuffer(context, CL_MEM_WRITE_ONLY, bytes, NULL, NULL);
  assert(d_c2);
  d_c3 = clCreateBuffer(context, CL_MEM_WRITE_ONLY, bytes, NULL, NULL);
  assert(d_c3);

  barrier_buffer1 = clCreateBuffer(context, CL_MEM_COPY_HOST_PTR, bytes, bb1,
                                   NULL);
  
  barrier_buffer2 = clCreateBuffer(context, CL_MEM_COPY_HOST_PTR, bytes, bb2,
                                   NULL);

  err = clEnqueueWriteBuffer(queue, d_a, CL_TRUE, 0,
                             bytes, h_a, 0, NULL, NULL);
  assert(!err);
  err |= clEnqueueWriteBuffer(queue, d_b, CL_TRUE, 0,
                              bytes, h_b, 0, NULL, NULL);
  assert(!err);

//###################################################################
//###################################################################
// Create kernels

  kernel1 = clCreateKernel(program1, "vecAdd", &err);
  assert(kernel1);

  err = clSetKernelArg(kernel1, 0, sizeof(cl_mem), &d_a);
  err |= clSetKernelArg(kernel1, 1, sizeof(cl_mem), &d_b);
  err |= clSetKernelArg(kernel1, 2, sizeof(cl_mem), &d_c1);
  assert(!err);

  barrier_kernel1 = clCreateKernel(b_program1, "barrier_kernel", &err);
  assert(barrier_kernel1);

  err = clSetKernelArg(barrier_kernel1, 0, sizeof(cl_mem), &barrier_buffer1);
  assert(!err);

  kernel2 = clCreateKernel(program2, "vecAdd", &err);
  assert(kernel2);

  err = clSetKernelArg(kernel2, 0, sizeof(cl_mem), &d_a);
  err |= clSetKernelArg(kernel2, 1, sizeof(cl_mem), &d_b);
  err |= clSetKernelArg(kernel2, 2, sizeof(cl_mem), &d_c2);
  assert(!err);

  barrier_kernel2 = clCreateKernel(b_program2, "barrier_kernel", &err);
  assert(barrier_kernel2);

  err = clSetKernelArg(barrier_kernel2, 0, sizeof(cl_mem), &barrier_buffer2);
  assert(!err);

  kernel3 = clCreateKernel(program2, "vecAdd", &err);
  assert(kernel3);

  err = clSetKernelArg(kernel3, 0, sizeof(cl_mem), &d_a);
  err |= clSetKernelArg(kernel3, 1, sizeof(cl_mem), &d_b);
  err |= clSetKernelArg(kernel3, 2, sizeof(cl_mem), &d_c3);
  assert(!err);

//###################################################################
//###################################################################
// Enqueue kernels

  localSize[0] = 4;
  localSize[1] = 8;
  globalSize[0] = 16;
  globalSize[1] = 64;
  err = clEnqueueNDRangeKernel(queue, kernel1, 2, NULL, globalSize, localSize,
                               0, NULL, NULL);
  assert(!err);

  err = clEnqueueNDRangeKernel(queue, kernel2, 2, NULL, globalSize, localSize,
                               0, NULL, NULL);
  assert(!err);

  localSize[0] = 2;
  localSize[1] = 8;
  globalSize[0] = 16;
  globalSize[1] = 64;
  err = clEnqueueNDRangeKernel(queue, kernel3, 2, NULL, globalSize, localSize,
                               0, NULL, NULL);
  assert(!err);
  
  localSize[0] = 6;
  globalSize[0] = 6;
  size_t global_offset = 1;
  err = clEnqueueNDRangeKernel(queue, barrier_kernel1, 1, &global_offset,
                               globalSize,
                               localSize,
                               0, NULL, NULL);
  assert(!err);

  err = clEnqueueNDRangeKernel(queue, barrier_kernel2, 1, &global_offset,
                               globalSize,
                               localSize,
                               0, NULL, NULL);
  assert(!err);
  
//###################################################################
//###################################################################
// Read output buffers

  clFinish(queue);

  clEnqueueReadBuffer(queue, d_c1, CL_TRUE, 0,
                      bytes, h_c1, 0, NULL, NULL);
  clEnqueueReadBuffer(queue, d_c2, CL_TRUE, 0,
                      bytes, h_c2, 0, NULL, NULL);
  clEnqueueReadBuffer(queue, d_c3, CL_TRUE, 0,
                      bytes, h_c3, 0, NULL, NULL);
  clEnqueueReadBuffer(queue, barrier_buffer1, CL_TRUE, 0,
                      bytes, bb1, 0, NULL, NULL);
  clEnqueueReadBuffer(queue, barrier_buffer2, CL_TRUE, 0,
                      bytes, bb2, 0, NULL, NULL);

//###################################################################
//###################################################################
// Check output of each kernels

  for(i = 0; i < BUFFER_SIZE; i++)
  {
    if(h_c1[i] != h_c2[i])
    {
      printf("Check failed at offset %d, %i instead of %i\n", i, h_c2[i], h_c1[i]);
      exit(1);
    }
    if ((((i/128)%2) && (h_c1[i]-16 != h_c3[i])))
    {
      printf("Check failed at offset %d, %i instead of %i\n", i, h_c3[i], h_c1[i]-16);
      exit(1);
    }
    if (!((i/128)%2) && (h_c1[i] != h_c3[i]))
    {
      printf("Check failed at offset %d, %i instead of %i\n", i, h_c3[i], h_c1[i]);
      exit(1);
    }
  }

  for (i = 0; i < 8; ++i)
    {
      if (bb1[i] != bb2[i])
        {
          printf("barrier kernel Failed at index %d\n", i);
          return EXIT_FAILURE;
        }
    }
  

//###################################################################
//###################################################################
// Release everythings

  clReleaseMemObject(d_a);
  clReleaseMemObject(d_b);
  clReleaseMemObject(d_c1);
  clReleaseMemObject(d_c2);
  clReleaseMemObject(d_c3);
  clReleaseMemObject(barrier_buffer1);
  clReleaseMemObject(barrier_buffer2);
  clReleaseProgram(program1);
  clReleaseProgram(program2);
  clReleaseProgram(b_program1);
  clReleaseProgram(b_program2);
  clReleaseKernel(kernel1);
  clReleaseKernel(kernel2);
  clReleaseKernel(kernel3);
  clReleaseKernel(barrier_kernel1);
  clReleaseKernel(barrier_kernel2);
  clReleaseCommandQueue(queue);
  clReleaseContext(context);

  free(bb1);
  free(bb2);
  free(h_a);
  free(h_b);
  free(h_c1);
  free(h_c2);
  free(h_c3);
  free(binary);
  free(barrier_binary);

  return 0;
}