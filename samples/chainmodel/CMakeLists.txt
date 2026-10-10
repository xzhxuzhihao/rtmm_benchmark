# 1. 查找 CUDA 环境变量/系统库 (现代 CMake 推荐 CUDAToolkit)
find_package(CUDAToolkit REQUIRED)

# 2. 查找 TensorRT 库 (如果 TensorRT 安装在默认系统路径，直接找 nvinfer 即可)
find_library(NVINFER_LIB nvinfer REQUIRED)

add_executable(chainmodeldisb 
    "main.cpp"
    "chain_model_client.cpp"
)

target_link_libraries(chainmodeldisb PRIVATE
    disb
    jsoncpp
    CUDA::cudart      # 对应 <cuda_runtime.h>
    ${NVINFER_LIB}    # 对应 <NvInfer.h>
)

install(TARGETS chainmodeldisb RUNTIME DESTINATION bin)
