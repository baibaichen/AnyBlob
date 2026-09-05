# AnyBlobBenchmark
This directory contains the AnyBlob benchmarking tools, and also includes all third-parties (e.g., AWS SDK) to run our experiments of the AnyBlob paper.

# Validate Experiments

## AnyBlob Experiments

To validate our findings you need to set-up an AWS storage account to create randomly generated data objects.
Further, set up an instance with IAM Storage permissions and copy the files to this node.
For throughput experiments, we recommend the c5n.18xlarge, but a cheaper instance such as the c5n.xlarge can be used for latency experiments.

With the create.sh script from the scripts subdirectory, you can create random objects with different sizes.
All .sh files within the script folder need some small adaptations to include your random object bucket and also the result bucket to write back the experimental results.

The raw data of our Figures were created from multiple scripts. This list contains the mapping between individual scripts and Section / Figures.

- sizes_latency.sh for Figure 2 (Section 2.3)
- latency.sh for Figure 3 (Section 2.3)
- latency_sparse.sh for Figure 4 (Section 2.3)
- throughput_sparse.sh for Figures 5, 6 & 7 (Section 2.4)
- sizes.sh for Figure 8 (Section 2.5)
- https.sh for Figure 9 (Section 2.6)
- model.sh for Figure 10 (Section 2.8) and Figure 12 (Section 3.4)

In the following, we show the steps to compile our benchmarking binary. CMake
downloads and builds the pinned dependencies under the repository's `.deps`
directory. AnyBlob and both AWS SDK backends use the same AWS-LC build.

	git clone --recursive https://github.com/durner/AnyBlob
	cd AnyBlob/example/benchmark
	cmake -S . -B build/Release \
	  -DCMAKE_BUILD_TYPE=Release
	cmake --build build/Release --target AnyBlobBenchmark --parallel 16

The dependency layout is:

	.deps/src       # downloaded sources
	.deps/build     # dependency build trees and stamps
	.deps/prefix    # headers and libraries used by every backend

The third-party versions are pinned in `thirdparty/dependencies.cmake`. Override
`ANYBLOB_DEPS_ROOT`, `ANYBLOB_GIT_TAG`, `AWS_SDK_GIT_TAG`, or
`ANYBLOB_JOBS` at CMake configure time when needed.
AnyBlob itself uses the current checkout by default. When changing dependency
commits or build options, remove `.deps` first to avoid mixing installed files
from different dependency generations.

The binary keeps all three comparison backends. Select one per run with
`-a uring`, `-a s3`, or `-a s3crt`, using identical data and concurrency
settings for a fair comparison.

## Database Experiments

All our DBMS related experiments and our binary of our proprietary system are shared in https://gitlab.db.in.tum.de/durner/cloud-storage-analytics.
