#include "disb.h"

#include <memory>
#include <string>
#include <iostream>
#include <fstream>
#include <jsoncpp/json/json.h>
#include <cuda_runtime.h>
#include <NvInfer.h>
#include <vector>

std::shared_ptr<DISB::Client> ModelClientFactory(const Json::Value &config, const Json::Value &config_extra = Json::Value());

// 1. TensorRT 日志输出类
class Logger : public nvinfer1::ILogger
{
public:
    void log(Severity severity, const char* msg) noexcept override {
        if ((severity == Severity::kERROR) || (severity == Severity::kINTERNAL_ERROR)) {
            std::cout << "[TRT Error] " << msg << "\n";
        }
    }
	static Logger& getInstance() {
        static Logger instance;
        return instance;
    }	
};

// 2. 现代 TensorRT 对象智能指针释放器
struct TRTDeleter
{
    template< class T >
    void operator()(T* obj) const
    {
        if (obj) {
            delete obj; // TensorRT 8.5+ 推荐使用 delete 替代 destroy()
        }
    }
};

template< class T >
using TRTUniquePtr = std::unique_ptr< T, TRTDeleter >;

struct TensorBuffer {
    const char* name = nullptr;
    nvinfer1::Dims dims;
    void* ptr = nullptr; // 或 float*
};

class ModelClient: public DISB::DependentClient
{
private:
	std::string engine_path;
	int priority;
	cudaStream_t stream = nullptr;

	TRTUniquePtr<nvinfer1::IRuntime> runtime;
	TRTUniquePtr<nvinfer1::ICudaEngine> engine;
	TRTUniquePtr<nvinfer1::IExecutionContext> context;

	std::vector<TensorBuffer> in_buffer;
	std::vector<TensorBuffer> out_buffer;
    std::vector<std::vector<float>> cpu_input;
    std::vector<std::vector<float>> cpu_output;
	std::shared_ptr<DISB::InferResult> res_data;
	/* serial number */
	int64_t s_num = 0;

public:
    ModelClient(const std::string &name,std::string &_engine_path, int _priority)
    {
        setName(name);
		engine_path = _engine_path;
		priority = _priority;
    }
    
    virtual ~ModelClient() {
		cudaStreamDestroy(stream);
        // runtime, engine, context 会由智能指针自动按正确顺序析构，无需手动 delete
		/* 释放显存 */
		int32_t in_size = in_buffer.size();
		for(int32_t i=0; i<in_size; i++){
			cudaFree(in_buffer[i].ptr);
		}

		int32_t out_size = out_buffer.size();
		for(int32_t i=0; i<out_size; i++){
			cudaFree(out_buffer[i].ptr);
		}
	}

	bool init_act();
	bool infer_act();
	// 辅助函数：计算 Tensor 元素总数
	size_t getSizeByDim(const nvinfer1::Dims& dims);
	void generateRandomInput(float* gpu_input, size_t size);
	TRTUniquePtr<nvinfer1::ICudaEngine> loadEngine(const std::string& engine_path, nvinfer1::IRuntime* runtime);
	void postprocessResults();

	virtual int64_t get_s_num() override;
	virtual void set_s_num(int64_t num) override;

    virtual std::shared_ptr<DISB::InferResult> produceResult() override;
    virtual void consumePrevResults(const std::map<std::string, std::shared_ptr<DISB::InferResult>> &prevResults) override;

	virtual void init() override;

	/* 到达一个请求执行一遍以下API */
    virtual void preprocess() override;
    virtual void infer() override;
    virtual void postprocess() override;
};


class Yolov8sClient : public ModelClient{
public:
	Yolov8sClient(const std::string &name,std::string &_engine_path, int _priority) : ModelClient(name, _engine_path, _priority)
    {
        // 子类特有的初始化逻辑（如果有的话）
    }

};

class BiSeNetV2Client : public ModelClient{
public:
	BiSeNetV2Client(const std::string &name,std::string &_engine_path, int _priority) : ModelClient(name, _engine_path, _priority)
    {
        // 子类特有的初始化逻辑（如果有的话）
    }
};

class SuperPointClient : public ModelClient{
public:
	SuperPointClient(const std::string &name,std::string &_engine_path, int _priority) : ModelClient(name, _engine_path, _priority)
    {
        // 子类特有的初始化逻辑（如果有的话）
    }
};

class DeiT_Tiny_Client : public ModelClient{
public:
	DeiT_Tiny_Client(const std::string &name,std::string &_engine_path, int _priority) : ModelClient(name, _engine_path, _priority)
    {
        // 子类特有的初始化逻辑（如果有的话）
    }
};


