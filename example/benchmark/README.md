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

## Local OneLake Reads

The `onelake` mode uses AnyBlob's download engine with the Azure Blob REST
endpoint, not an Azure SDK client. It supports read-only byte ranges on an exact,
URL-encoded object path. SharedKey Azure mode is unchanged.

Acquire a short-lived token for `https://storage.azure.com/` using the explicitly
selected tenant/subscription, and supply it as `AZURE_STORAGE_BEARER_TOKEN` in the
benchmark process environment. Never put the token in command arguments or logs.
The token must remain valid for the run; this mode does not refresh it. TLS peer
and hostname verification are required. `SSL_CERT_FILE` may point to the system
CA bundle (for example `/etc/pki/tls/certs/ca-bundle.crt` on Azure Linux).

```sh
./build/Release/AnyBlobBenchmark onelake bandwidth \
	-b <workspace> \
	--object-path <lakehouse>.Lakehouse/Files/<object> \
	--read-offset 0 --read-bytes 1048576 \
	--request-timeout-ms 5000 -l 8 -c 2 -t 1 -i 1 -o onelake.csv
```

Only `-a uring` is supported in this mode. Each response must be HTTP 206 and
contain the requested byte count. Any failed or short read makes the run fail;
do not treat an output file from a failed run as a performance result. A repeated
single-range smoke test checks connectivity, not cold-storage throughput.

## Database Experiments

All our DBMS related experiments and our binary of our proprietary system are shared in https://gitlab.db.in.tum.de/durner/cloud-storage-analytics.
