
 MKL_FLAG= -mkl -D_MKL_ -O3 -march=core-avx2 -DD=2048

 OPT_FLAG= -lopenblas -D_OPENBLAS_ -march=core-avx2 -O3 -DD=2048 -xCORE-AVX2 -Wall
 DEBUG_FLAG= -lopenblas -D_OPENBLAS_ -march=core-avx2 -O0 -DD=2048

 ASM_FLAG = -masm=intel

 CFLAGS = -mavx -mavx2 -mfma -msse -msse2 -msse3 -lopenblas -Wall -O

all: gemm.cpp
	icpc $(OPT_FLAG) gemm.cpp -o gemm.out
mkl: gemm.cpp
	icpc $(MKL_FLAG) gemm.cpp -o gemm.out
thread: gemm.cpp
	icpc $(OPT_FLAG) gemm.cpp -o gemm.out -fopenmp
debug:
	icpc $(DEBUG_FLAG) gemm.cpp -g -o gemm.out
assemble:
	icpc $(OPT_FLAG) $(ASM_FLAG) gemm.cpp -o gemm.s -S
assemble-test:
	icpc $(OPT_FLAG) $(ASM_FLAG) gemm.cpp -o gemm-test.s -S
clean:
	rm gemm.out


all-g++: gemm.cpp
	g++ $(CFLAGS) gemm.cpp -o gemm-gcc.out

assemble-g++: gemm.cpp
	g++ $(CFLAGS) $(ASM_FLAG) gemm.cpp -S -o gemm-g++.s
